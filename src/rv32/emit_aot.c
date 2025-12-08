#include <assert.h>
#include <string.h>
#include "rv32i.h"
#include "../libqbe.h"
#include "../all.h"
#include "../aot.h"

#define ALIGN_PTR(p, align) ((void *)((((uintptr_t)p) + (align-1)) & ~(align-1)))

#define TEMPLATE_WRITE(val, p, p_end, type, align) \
    do { \
        p = ALIGN_PTR(p, align); \
        if ((long unsigned int)(p_end - p) < sizeof(type)) { \
            goto ERROR; \
        } \
        *((type *)p) = (val); \
        p += sizeof(type); \
    } while (0)

#define WRITE_UINT64(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, uint64_t, sizeof(uint32_t))

#define WRITE_UINT32(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, uint32_t, sizeof(uint32_t))

#define WRITE_UINT16(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, uint16_t, sizeof(uint16_t))

#define WRITE_UINT8(val, p, p_end) \
    TEMPLATE_WRITE(val, p, p_end, uint8_t, sizeof(uint8_t))

#define WRITE_BYTE_ARRAY(p, p_end, addr, len) \
    do { \
        if ((long unsigned int)(p_end - p) < len) { \
            goto ERROR; \
        } \
        memcpy(p, addr, len); \
        p += len; \
    } while (0)


/* All the '*_ins_fmt' struct inside the union 'as' are 32-bit long */
#define EMIT(INS, P, P_END) WRITE_UINT32((INS).as.u32, P, P_END)

static void emit_li(uint32_t rd, int32_t imm, uint8_t **p, uint8_t *p_end);
static bool is_imm(Ref *r, int imm_min, int imm_max);
static void fix_arg(Ref *arg, rv32_reg r, uint8_t **p, uint8_t *p_end);
static void fix_ins_args(Ins *i, uint8_t **p, uint8_t *p_end);
static void emitins(AOTModule *aotm, Ins *i);

typedef struct instr_info_elem {
    /* TRUE if the instr can be a register-immediate instruction,
     * FALSE otherwise. A register-immediate instruction can have
     * one operand (narg == 1) or two operands (narg == 2),
     * the operand that hold the immediate value is the first operand
     * if narg == 1 and is the second operand if narg == 2. */
    bool can_be_reg_imm;
    /* The immediate value v must be imm_min <= v <= imm_max */
    int imm_min;
    int imm_max;
    /* TRUE if the instr is commutative, FALSE otherwise */
    bool is_commutative;
    /* argn can be only 1 or 2 */
    unsigned int argn;
} instr_info_elem;

static const instr_info_elem instr_info[] = {
	[ADD_INSTR] = {
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2,
    },
    [SUB_INSTR] = {
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = false,
        .argn = 2,
    },
    [MUL_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = true,
        .argn = 2,
    },
    [DIV_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [UDIV_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [REM_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [UREM_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [AND_INSTR] = {
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2
    },
    [OR_INSTR] = {
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2,
    },
    [SHL_INSTR] = {
        .can_be_reg_imm = true,
        .is_commutative = false,
        .imm_min = -16,
        .imm_max =  15,
        .argn = 2,
    },
    [SAR_INSTR] = {
        .can_be_reg_imm = true,
        .is_commutative = false,
        .imm_min = -16,
        .imm_max =  15,
        .argn = 2,
    },
    [EQZW_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [XOR_INSTR] = {
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2,
    },
    [CSLTW_INSTR] = {
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = false,
        .argn = 2,
    },
    [CULTW_INSTR] = {
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = false,
        .argn = 2,
    },
    [STOREW_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [LOADW_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [LOADUB_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [STOREB_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [SHR_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [CNEW_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [CALL_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [PAR_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [ARG_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [EXTSW_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [COPY_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [PUSH_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [POP_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
};

/* binary operation tempalte */
#define BINOP(OP, OPI)                                \
    {                                                 \
        fix_ins_args(i, &p, p_end);                   \
        rv32_reg rd = i->to.as.loc.as.reg;            \
        rv32_reg rs1 = i->arg[0].as.loc.as.reg;       \
        if (i->arg[1].type == REF_TYPE_INT32_CONST) { \
            uint32_t imm = i->arg[1].as.int32_const;  \
            EMIT(OPI(rd, rs1, imm), p, p_end);        \
        } else {                                      \
            rv32_reg rs2 = i->arg[1].as.loc.as.reg;   \
            EMIT(OP(rd, rs1, rs2), p, p_end);         \
        }                                             \
    } break

static void emitins(AOTModule *aotm, Ins *i) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    switch (i->op) {
        case ADD_INSTR: BINOP(RV32_ADD, RV32_ADDI);
        case SUB_INSTR: BINOP(RV32_SUB, RV32_SUBI);
        case EQZW_INSTR: {
            fix_ins_args(i, &p, p_end);
            rv32_reg rd = i->to.as.loc.as.reg;
            rv32_reg rs1 = i->arg[0].as.loc.as.reg;
            EMIT(RV32_SEQZ(rd, rs1), p, p_end);
        } break;
        case XOR_INSTR:   BINOP(RV32_XOR, RV32_XORI);
        case CSLTW_INSTR: BINOP(RV32_SLT, RV32_SLTI);
        case CULTW_INSTR: BINOP(RV32_SLTU, RV32_SLTIU);
        case COPY_INSTR: {
            assert(i->to.type == REF_TYPE_LOCATION);
            if (i->to.as.loc.type == REGISTER) {
                /* the copy destination is a register */
                location *loc = &i->arg[0].as.loc;
                if (i->arg[0].type == REF_TYPE_LOCATION && loc->type == REGISTER) {
                    /* the copy source is a register */
                    if (i->to.as.loc.as.reg != loc->as.reg) {
                        /* the copy destination is not equal to the copy source */
                        EMIT(RV32_MV(i->to.as.loc.as.reg, loc->as.reg), p, p_end);
                    }
                }
                else if (i->arg[0].type == REF_TYPE_LOCATION && loc->type == STACK_SLOT) {
                    /* the copy source is a stack slot */
                    /* STACK_SLOT as a copy destination is not implemented */
                    assert(0);
                }
                else if (i->arg[0].type == REF_TYPE_INT32_CONST) {
                    /* the copy source is a constant */
                    emit_li(i->to.as.loc.as.reg, i->arg[0].as.int32_const, &p, p_end);
                }
                else assert(0);
            }
            else if (i->to.as.loc.type == STACK_SLOT) {
                /* the copy destination is a stack slot */
                /* STACK_SLOT as a copy destination is not implemented */
                assert(0); 
            }
            else assert(0);
        } break;
        case CALL_INSTR: {
            assert(i->arg[0].type == REF_TYPE_NAME);

            AOTRelocation *reloc = xmalloc(sizeof(struct AOTRelocation));
            reloc->offset = p - aotm->text_start;
            reloc->addend = 0;
            reloc->type = R_RISCV_CALL;
            /* symbol_name points in the array func_decls in the wasm_module struct */
            reloc->symbol_name = i->arg[0].as.name;
            reloc->next = aotm->reloc_list;
            aotm->reloc_list = reloc;
            aotm->reloc_count++;

            EMIT(RV32_AUIPC(RA, 0), p, p_end);
            EMIT(RV32_JARL(RA, RA, 0), p, p_end);
        } break;
        case STOREW_INSTR: {
            fix_arg(&i->to, rv32_reserved_reg[0], &p, p_end);
            fix_arg(&i->arg[1], rv32_reserved_reg[1], &p, p_end);
            assert(i->arg[0].type == REF_TYPE_INT32_CONST);
            rv32_reg rs2 = i->to.as.loc.as.reg;
            uint32_t imm = i->arg[0].as.int32_const;
            rv32_reg rs1 = i->arg[1].as.loc.as.reg;
            EMIT(RV32_SW(rs2, imm, rs1), p, p_end);
        } break;
        case LOADW_INSTR: {
            fix_arg(&i->to, rv32_reserved_reg[0], &p, p_end);
            fix_arg(&i->arg[1], rv32_reserved_reg[1], &p, p_end);
            assert(i->arg[0].type == REF_TYPE_INT32_CONST);
            rv32_reg rs2 = i->to.as.loc.as.reg;
            uint32_t imm = i->arg[0].as.int32_const;
            rv32_reg rs1 = i->arg[1].as.loc.as.reg;
            EMIT(RV32_LW(rs2, imm, rs1), p, p_end);
        } break;
        default:
            fprintf(stderr, "Error: opcode %d not implemented!\n", i->op);
            assert(0);
    }

    aotm->p = p;
    return;

ERROR:
    panic();
}

static void fix_arg(Ref *arg, rv32_reg r, uint8_t **p, uint8_t *p_end) {
    switch (arg->type) {
        case REF_TYPE_INT32_CONST: {
            emit_li(r, arg->as.int32_const, p, p_end);
            arg->type = REF_TYPE_LOCATION;
            arg->as.loc.type = REGISTER;
            arg->as.loc.as.reg = r;
        } break;
        case REF_TYPE_LOCATION: {
            switch (arg->as.loc.type) {
                case REGISTER:
                    break;
                case STACK_SLOT: {
                    //TODO: not implemented
                    assert(0);
                } break;
                default:
                    assert(0);
            }
        } break;
        default:
            assert(0);
    }
}

static bool is_imm(Ref *r, int imm_min, int imm_max) {
    if (r->type == REF_TYPE_INT32_CONST) {
        if (imm_min <= r->as.int32_const && r->as.int32_const <= imm_max) {
            return true;
        }
    }
    return false;
}

static void fix_ins_args(Ins *i, uint8_t **p, uint8_t *p_end) {
    const instr_info_elem *e = &instr_info[i->op];
    if (e->can_be_reg_imm) {
        if (e->argn == 2) {
            bool imm = is_imm(&i->arg[1], e->imm_min, e->imm_max);
            if (imm) {
                fix_arg(&i->arg[0], rv32_reserved_reg[0], p, p_end);
                return;
            } else if (e->is_commutative) {
                bool imm = is_imm(&i->arg[0], e->imm_min, e->imm_max);
                if (imm) {
                    Ref tmp = i->arg[0];
                    i->arg[0] = i->arg[1];
                    i->arg[1] = tmp;
                    fix_arg(&i->arg[0], rv32_reserved_reg[0], p, p_end);
                    return;
                }
            }
        } else if (e->argn == 1) {
            bool imm = is_imm(&i->arg[0], e->imm_min, e->imm_max);
            if (imm) return;
        }
    }

    for (unsigned int j = 0; j < e->argn; j++) {
        fix_arg(&i->arg[j], rv32_reserved_reg[j], p, p_end);
    }
}

/* Instr: li
 * Description: load immediate (psuedoinstruction)
 * Use: li rd, imm
 * Result: rd = imm */
//https://stackoverflow.com/questions/50742420/risc-v-build-32-bit-constants-with-lui-and-addi
static void emit_li(uint32_t rd, int32_t imm,
                    uint8_t **p, uint8_t *p_end) {

    if (-2048 <= imm && imm <= 2047) {
        EMIT(RV32_ADDI(rd, ZERO, imm), *p, p_end);
    } else {
        uint32_t lui_imm = imm >> 12;
        uint32_t addi_imm = imm & 0xfff;
        if (addi_imm & 0x800) {
            lui_imm += 1;
        }
        EMIT(RV32_LUI(rd, lui_imm), *p, p_end);
        EMIT(RV32_ADDI(rd, ZERO, addi_imm), *p, p_end);
    }
    return;

ERROR:
    panic();
}

/*
  Stack-frame layout:

  +=============+
  |  saved ra   |
  |  saved fp   |
  +-------------+ <- fp
  |    ...      |
  | spill slots |
  |    ...      |
  +-------------+
  |   padding   |
  +=============+

*/

typedef struct RV32JumpPatch {
    struct RV32JumpPatch *next;
    Blk *jump_target;
    enum {
        J_PATCH_TYPE,
        BEQZ_PATCH_TYPE,
        BNEZ_PATCH_TYPE,
    } type;
    uint8_t *jump_instr;
} RV32JumpPatch;

static void backpatch(list *patch_list, Blk *b) {
    listNode *patch_node;
    listNode *patch_iter = listFirst(patch_list);
    while ((patch_node = listNext(&patch_iter)) != NULL) {
        RV32JumpPatch *p = listNodeValue(patch_node);
        if (b != p->jump_target) {
            continue;
        }
        int32_t imm = b->text_start - p->jump_instr;
        switch (p->type) {
            case J_PATCH_TYPE: {
                *(uint32_t *)p->jump_instr = RV32_J(imm).as.u32;
            } break;
            case BEQZ_PATCH_TYPE: {
                rv32_reg rs1 = ((b_ins_fmt *)p->jump_instr)->rs1;
                *(uint32_t *)p->jump_instr = RV32_BEQZ(rs1, imm).as.u32;
            } break;
            case BNEZ_PATCH_TYPE: {
                rv32_reg rs1 = ((b_ins_fmt *)p->jump_instr)->rs1;
                *(uint32_t *)p->jump_instr = RV32_BNEZ(rs1, imm).as.u32;
            } break;
            default:
                /* Unexpected patch type */
                assert(0);
        }
        listDelNode(patch_list, patch_node);
    }
}

void rv32_emit_fn_text(AOTModule *aotm, Fn *fn, uint32_t type_index) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;
    list *patch_list = listCreate();
    listSetFreeMethod(patch_list, free);

    uint32_t next_func = aotm->next_func;
    assert(next_func < aotm->func_count);
    aotm->funcs[next_func].text_offset = p - aotm->text_start;
    aotm->funcs[next_func].type_index = type_index;
    aotm->next_func++;

    /* store old fp on the current stack frame */
    EMIT(RV32_SW(FP, -8, SP), p, p_end);
    /* store old ra on the current stack frame */
    EMIT(RV32_SW(RA, -4, SP), p, p_end);
    /* update fp, now it point to the saved fp on current the stack */
    EMIT(RV32_ADDI(FP, SP, -8), p, p_end);

    /* allocates the current stack frame */
    assert(fn->num_stack_slots == 0);
    // TODO: why 16 aligned?
    unsigned int frame = (8 + 4 * fn->num_stack_slots + 15) & ~15;
    /* 2047 is the maximum positive number that can be represent
     * in the imm field of the addi instruction */
    if (frame <= 2047) {
        EMIT(RV32_ADDI(SP, SP, -1 * frame), p, p_end);
    } else {
        emit_li(T6, frame, &p, p_end);
        EMIT(RV32_SUB(SP, SP, T6), p, p_end);
    }

    listNode *blk_node;
    listNode *blk_iter = listFirst(fn->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        b->text_start = p;
        backpatch(patch_list, b);
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            aotm->p = p;
            emitins(aotm, i);
            p = aotm->p;
        }
        switch (b->jmp.type) {
            case HALT_JUMP_TYPE: {
                EMIT(RV32_EBREAK, p, p_end);
            } break;
            case RET0_JUMP_TYPE: {
                if (frame <= 2047) {
                    EMIT(RV32_ADDI(SP, SP, frame), p, p_end);
                } else {
                    emit_li(T6, frame, &p, p_end);
                    EMIT(RV32_SUB(SP, SP, T6), p, p_end);
                }
                /* restore the saved fp */
                EMIT(RV32_LW(RA, 4, FP), p, p_end);
                /* restore the saved ra */
                EMIT(RV32_LW(FP, 0, FP), p, p_end);
                EMIT(RV32_RET, p, p_end);
            } break;
            case JMP_JUMP_TYPE: {
                Blk *blk_next = NULL;
                if (listNextNode(blk_node) != NULL) {
                    blk_next = listNodeValue(listNextNode(blk_node));
                }
                Blk *jump_target = b->succ[0];
                /* If the jump target is the next block to be processed,
                 * don't emit the j instruction */
                if (jump_target != blk_next) {
                    int32_t imm = 0;
                    if (jump_target->text_start != NULL) {
                        /* jump_target was already processed, so we know where the
                         * instructions of jump_target start in the text segment */
                         imm = jump_target->text_start - p;
                    } else {
                        /* jump_target hasn't already been processed,
                         * let's create a patch and fix it later */
                        RV32JumpPatch *patch = xmalloc(sizeof(struct RV32JumpPatch));
                        patch->jump_target = jump_target;
                        patch->type = J_PATCH_TYPE;
                        patch->jump_instr = p;
                        listAddNodeTail(patch_list, patch);
                    }
                    EMIT(RV32_J(imm), p, p_end);
                }
            } break;
            case JNZ_JUMP_TYPE: {
                fix_arg(&b->jmp.arg, rv32_reserved_reg[0], &p, p_end);
                Blk *blk_next = NULL;
                if (listNextNode(blk_node) != NULL) {
                    blk_next = listNodeValue(listNextNode(blk_node));
                }
                if (blk_next == b->succ[0]) {
                    /* The next block to be processed is the 'then' block, so jump
                     * to the 'else' block is the argument of jnz is equal to zero */
                    Blk *jump_target = b->succ[1];
                    rv32_reg rs1 = b->jmp.arg.as.loc.as.reg;
                    int32_t imm = 0;
                    if (jump_target->text_start != NULL) {
                        imm = jump_target->text_start - p;
                    } else {
                        RV32JumpPatch *patch = xmalloc(sizeof(struct RV32JumpPatch));
                        patch->jump_target = jump_target;
                        patch->type = BEQZ_PATCH_TYPE;
                        patch->jump_instr = p;
                        listAddNodeTail(patch_list, patch);
                    }
                    EMIT(RV32_BEQZ(rs1, imm), p, p_end);
                } else if (blk_next == b->succ[1]) {
                    /* The next block to be processed is the 'else' block, so jump
                     * to the 'then' block is the argument of jnz is not equal to zero */
                    Blk *jump_target = b->succ[0];
                    rv32_reg rs1 = b->jmp.arg.as.loc.as.reg;
                    int32_t imm = 0;
                    if (jump_target->text_start != NULL) {
                        imm = jump_target->text_start - p;
                    } else {
                        RV32JumpPatch *patch = xmalloc(sizeof(struct RV32JumpPatch));
                        patch->jump_target = jump_target;
                        patch->type = BNEZ_PATCH_TYPE;
                        patch->jump_instr = p;
                        listAddNodeTail(patch_list, patch);
                    }
                    EMIT(RV32_BNEZ(rs1, imm), p, p_end);
                } else {
                    /* The next block to be processed after a block ending with a jnz
                     * must be a the 'else' block or the 'then' block of jnz. */
                    assert(0);
                }
            } break;
            default:
                /* Unexpected jump type */
                assert(0);
        }
    }

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_init(AOTModule *aotm) {
    aotm->buf_len = 1024;
    aotm->buf = xcalloc(aotm->buf_len, sizeof(uint8_t));
    aotm->p = aotm->buf;
    aotm->p_end = aotm->buf + aotm->buf_len;
    aotm->text_size = NULL;
    aotm->text_start = NULL;
    aotm->func_count = 0;
    aotm->next_func = 0;
    aotm->funcs = NULL;
    aotm->reloc_count = 0;
    aotm->reloc_list = NULL;

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_MAGIC_NUMBER, p, p_end);
    WRITE_UINT32(AOT_CURRENT_VERSION, p, p_end);

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_emit_target_info(AOTModule *aotm) {

    static const AOTTargetInfo target_info = {
        .bin_type = BIN_TYPE_ELF32L,
        .abi_type = 0,
        .e_type = E_TYPE_REL,
        .e_machine = E_MACHINE_RISCV,
        .e_version = E_VERSION_CURRENT,
        .e_flags = 0,
        .feature_flags = 0,
        .reserved = 0,
        .arch = "riscv32",
    };

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_SECTION_TYPE_TARGET_INFO, p, p_end);
    WRITE_UINT32(sizeof(struct AOTTargetInfo), p, p_end);
    WRITE_UINT16(target_info.bin_type, p, p_end);
    WRITE_UINT16(target_info.abi_type, p, p_end);
    WRITE_UINT16(target_info.e_type, p, p_end);
    WRITE_UINT16(target_info.e_machine, p, p_end);
    WRITE_UINT32(target_info.e_version, p, p_end);
    WRITE_UINT32(target_info.e_flags, p, p_end);
    WRITE_UINT64(target_info.reserved, p, p_end);
    WRITE_UINT64(target_info.feature_flags, p, p_end);
    WRITE_BYTE_ARRAY(p, p_end, target_info.arch, sizeof(target_info.arch));

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_emit_init_data(AOTModule *aotm, const AOTInitData *init_data) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    const wasm_func_type *types = init_data->types;
    const uint32_t types_len = init_data->types_len;
    const uint32_t func_count = init_data->func_count;

    aotm->func_count = func_count;
    aotm->funcs = calloc(func_count, sizeof(struct AOTFuncSecEntry));
    for (uint32_t i = 0; i < func_count; i++) {
        aotm->funcs[i].text_offset = -1;
        aotm->funcs[i].type_index = -1;
    }

    WRITE_UINT32(AOT_SECTION_TYPE_INIT_DATA, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    //----- Emit Memory Info subsection -----
    const uint32_t import_memory_count = 0;
    const uint32_t memory_count = 1;
    const memory_info_t mem = {
        .min_page_num = 0,
        .max_page_num = 0,
    };
    const uint32_t mem_flags = 0;
    const uint32_t mem_num_bytes_per_page = 65536;

    const uint32_t mem_init_data_count = 0;

    WRITE_UINT32(import_memory_count, p, p_end);
    WRITE_UINT32(memory_count, p, p_end);

    WRITE_UINT32(mem_flags, p, p_end);
    WRITE_UINT32(mem_num_bytes_per_page, p, p_end);
    WRITE_UINT32(init_data->mem0->min_page_num, p, p_end);
    WRITE_UINT32(init_data->mem0->max_page_num, p, p_end);

    WRITE_UINT32(mem_init_data_count, p, p_end);

    //----- Emit Memory Info subsection -----
    const uint32_t import_table_count = 0;
    const uint32_t table_count = 0;
    const uint32_t table_init_data_count = 0;
    WRITE_UINT32(import_table_count, p, p_end);
    WRITE_UINT32(table_count, p, p_end);
    WRITE_UINT32(table_init_data_count, p, p_end);

    //----- Emit Type Info subsection -----
    WRITE_UINT32(types_len, p, p_end);
    for (uint32_t i = 0; i < types_len; i++) {
        p = ALIGN_PTR(p, 4);
        //type_flag field is not used
        uint16_t type_flag = 0;
        WRITE_UINT16(type_flag, p, p_end);
        WRITE_UINT16(types[i].num_params, p, p_end);
        const uint16_t num_return = 1;
        WRITE_UINT16(num_return, p, p_end);

        WRITE_BYTE_ARRAY(p, p_end, types[i].params_type, types[i].num_params);
        WRITE_UINT8(types[i].return_type, p, p_end);
    }

    //----- Emit Import Global Info subsection  -----
    const uint32_t import_global_count = 0;
    WRITE_UINT32(import_global_count, p, p_end);

    //----- Emit Global Info subsection  -----
    const uint32_t global_count = 0;
    WRITE_UINT32(global_count, p, p_end);

    //----- Emit Import Func Info subsection -----
    const uint32_t import_func_count = 0;
    WRITE_UINT32(import_func_count, p, p_end);

    //----- Emit function count and start function index -----
    WRITE_UINT32(func_count, p, p_end);
    const uint32_t start_func_index = -1;
    WRITE_UINT32(start_func_index, p, p_end);

    //----- Emit aux -----
    const uint32_t aux_data_end_global_index = -1;
    const uint64_t aux_data_end = 0;
    const uint32_t aux_heap_base_global_index = -1;
    const uint64_t aux_heap_base = 0;
    const uint32_t aux_stack_top_global_index = -1;
    const uint64_t aux_stack_bottom = 0;
    const uint32_t aux_stack_size = 0;

    WRITE_UINT32(aux_data_end_global_index, p, p_end);
    WRITE_UINT64(aux_data_end, p, p_end);
    WRITE_UINT32(aux_heap_base_global_index, p, p_end);
    WRITE_UINT64(aux_heap_base, p, p_end);
    WRITE_UINT32(aux_stack_top_global_index, p, p_end);
    WRITE_UINT64(aux_stack_bottom, p, p_end);
    WRITE_UINT32(aux_stack_size, p, p_end);

    //----- Emit data sections -----
    const uint32_t data_section_count = 0;
    WRITE_UINT32(data_section_count, p, p_end);

    //Fix section size
    *section_size = p - section_start;

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_init_text(AOTModule *aotm) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_SECTION_TYPE_TEXT, p, p_end);
    aotm->text_size = p;
    p += sizeof(uint32_t);
    const uint32_t literal_size = 0;
    WRITE_UINT32(literal_size, p, p_end);
    p = ALIGN_PTR(p, sizeof(uint32_t));
    aotm->text_start = p;
    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_finalize_text(AOTModule *aotm) {
    *(aotm->text_size) = aotm->p - (aotm->text_size + sizeof(uint32_t));
}

void rv32_emit_function(AOTModule *aotm) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_SECTION_TYPE_FUNCTION, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    for (uint32_t i = 0; i < aotm->func_count; i++) {
        WRITE_UINT32(aotm->funcs[i].text_offset, p, p_end);
    }

    for (uint32_t i = 0; i < aotm->func_count; i++) {
        WRITE_UINT32(aotm->funcs[i].type_index, p, p_end);
    }

    /* not used */
    uint32_t max_local_cell_num = 0;
    uint32_t max_stack_cell_num = 0;
    for (uint32_t i = 0; i < aotm->func_count; i++) {
        WRITE_UINT32(max_local_cell_num, p, p_end);
        WRITE_UINT32(max_stack_cell_num, p, p_end);
    }

    //Fix section size
    *section_size = p - section_start;

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_emit_export(AOTModule *aotm, uint32_t num_exports, WASMExport *exports) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_SECTION_TYPE_EXPORT, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    WRITE_UINT32(num_exports, p, p_end);
    for (uint32_t i = 0; i < num_exports; i++) {
        WRITE_UINT32(exports[i].index, p, p_end);
        WRITE_UINT8(exports[i].export_desc, p, p_end);
        uint32_t name_len = exports[i].name_len;
        if (exports[i].name[name_len-1] == '\0') {
            WRITE_UINT16(name_len, p, p_end);
            WRITE_BYTE_ARRAY(p, p_end, exports[i].name, name_len);
        } else {
            WRITE_UINT16(name_len + 1, p, p_end);
            WRITE_BYTE_ARRAY(p, p_end, exports[i].name, name_len);
            if ((long unsigned int)(p_end - p) < sizeof(char)) {
                goto ERROR;
            }
            *p = '\0';
            p += sizeof(char);
        }
    }

    //Fix section size
    *section_size = p - section_start;

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_emit_relocation(AOTModule *aotm) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_SECTION_TYPE_RELOCATION, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    uint32_t symbol_count = 0;
    for (AOTRelocation *i = aotm->reloc_list; i != NULL; i = i->next) {
        i->representative = NULL;
        for (AOTRelocation *j = aotm->reloc_list; j != i; j = j->next) {
            if (i->symbol_name == j->symbol_name) {
                i->representative = j;
                break;
            }
        }
        if (i->representative == NULL) {
            symbol_count++;
        }
    }
    if (aotm->reloc_list != NULL) {
        symbol_count++;
    }
    WRITE_UINT32(symbol_count, p, p_end);
    uint32_t *symbol_offset = (uint32_t *)p;
    uint32_t symbol_index = 0;
    p += symbol_count * sizeof(uint32_t);
    uint32_t *symbol_buf_size = (uint32_t *)p;
    p += sizeof(uint32_t);
    uint8_t *symbol_buf_start = p;

    if (aotm->reloc_list != NULL) {
        char *group_section_name = ".rela.text";
        *symbol_offset = p - symbol_buf_start;
        symbol_offset++;
        symbol_index++;
        uint16_t len = strlen(group_section_name) + 1;
        WRITE_UINT16(len, p, p_end);
        WRITE_BYTE_ARRAY(p, p_end, group_section_name, len);
        p = ALIGN_PTR(p, 2);
    }

    for (AOTRelocation *i = aotm->reloc_list; i != NULL; i = i->next) {
        if (i->representative != NULL) continue;
        *symbol_offset = p - symbol_buf_start;
        symbol_offset++;
        i->symbol_index = symbol_index++;
        uint16_t len = strlen(i->symbol_name) + 1;
        WRITE_UINT16(len, p, p_end);
        WRITE_BYTE_ARRAY(p, p_end, i->symbol_name, len);
        p = ALIGN_PTR(p, 2);
    }
    *symbol_buf_size = p - symbol_buf_start;

    if (aotm->reloc_list != NULL) {
        uint32_t group_count = 1;
        WRITE_UINT32(group_count, p, p_end);
        /* group name index is 4 bytes aligned. */
        p = ALIGN_PTR(p, sizeof(uint32_t));
        uint32_t group_name_index = 0;
        WRITE_UINT32(group_name_index, p, p_end);
        WRITE_UINT32(aotm->reloc_count, p, p_end);
        for (AOTRelocation *i = aotm->reloc_list; i != NULL; i = i->next) {
            WRITE_UINT32(i->offset, p, p_end);
            WRITE_UINT32(i->addend, p, p_end);
            WRITE_UINT32(i->type, p, p_end);
            if (i->representative != NULL) {
                WRITE_UINT32(i->representative->symbol_index, p, p_end);
            } else {
                WRITE_UINT32(i->symbol_index, p, p_end);
            }
        }
    } else {
        uint32_t group_count = 0;
        WRITE_UINT32(group_count, p, p_end);
    }

    //Fix section size
    *section_size = p - section_start;

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_finalize(AOTModule *aotm, uint8_t **buf, uint32_t *buf_len) {
    *buf = aotm->buf;
    *buf_len = aotm->p - aotm->buf;
}




