#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "rv32i.h"
#include "../libqbe.h"
#include "../aot.h"

static AOTErr_t emit(AOTModule *m, ins_fmt ins);
static AOTErr_t fix_arg(AOTModule *m, Ref *arg, rv32_reg r);
static bool imm(Ref r, int imm_min, int imm_max);
static AOTErr_t emit_li(AOTModule *m, uint32_t rd, int32_t imm);
static AOTErr_t emitins(AOTModule *aotm, Ins *i);
static AOTErr_t rv32_emit_target_info(AOTModule *m);
static AOTErr_t rv32_emitfn(AOTModule *m, Fn *fn, uint32_t typeidx);

#define ERR_CHECK(x) if (x) return AOT_ERR;

const bool rv32_is_callee_saved[RV32_NUM_REG] = {
    [ZERO] = false,
    [RA]   = false,
    [SP]   = true,
    [GP]   = false,
    [TP]   = false,
    [T0]   = false,
    [T1]   = false,
    [T2]   = false,
    [FP]   = true,
    [S1]   = true,
    [A0]   = false,
    [A1]   = false,
    [A2]   = false,
    [A3]   = false,
    [A4]   = false,
    [A5]   = false,
    [A6]   = false,
    [A7]   = false,
    [S2]   = true,
    [S3]   = true,
    [S4]   = true,
    [S5]   = true,
    [S6]   = true,
    [S7]   = true,
    [S8]   = true,
    [S9]   = true,
    [S10]  = true,
    [S11]  = true,
    [T3]   = false,
    [T4]   = false,
    [T5]   = false,
    [T6]   = false,
};

const rv32_reg rv32_gp_reg[RV32_GP_NUM_REG] = {
    T0, T1, T2, T3, T4,
    S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11,
};

const rv32_reg rv32_arg_reg[RV32_ARG_NUM_REG] = {
    A0, A1, A2, A3, A4, A5, A6, A7,
};

const rv32_reg rv32_priv_reg[RV32_RESERVED_NUM_REG] = {
    T5, T6
};

Target rv32 = {
    .name = "riscv32",
    .emit_target_info = &rv32_emit_target_info,
    .emitfn = &rv32_emitfn,
};

static AOTErr_t emit(AOTModule *m, ins_fmt ins) {
    unsigned long size = m->buf_end - m->offset;
    if (size < sizeof(uint32_t)) return AOT_ERR;
    *(uint32_t *)(m->offset) = ins.as.u32;
    m->offset += sizeof(uint32_t);
    return AOT_OK;
}

static AOTErr_t fix_arg(AOTModule *m, Ref *arg, rv32_reg r) {
    switch (arg->type) {
        case REF_TYPE_INT32_CONST: {
            ERR_CHECK(emit_li(m, r, arg->as.int32_const));
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
    return AOT_OK;
}

static inline bool imm(Ref r, int imm_min, int imm_max) {
    return r.type == REF_TYPE_INT32_CONST &&
        imm_min <= r.as.int32_const &&
        r.as.int32_const <= imm_max;
}

/* Instr: li
 * Description: load immediate (psuedoinstruction)
 * Use: li rd, imm
 * Result: rd = imm */
//https://stackoverflow.com/questions/50742420/risc-v-build-32-bit-constants-with-lui-and-addi
static AOTErr_t emit_li(AOTModule *m, uint32_t rd, int32_t imm) {
    if (-2048 <= imm && imm <= 2047) {
        ERR_CHECK(emit(m, RV32_ADDI(rd, ZERO, imm)));
    } else {
        uint32_t lui_imm = imm >> 12;
        uint32_t addi_imm = imm & 0xfff;
        if (imm & 0x800) {
            lui_imm += 1;
        }
        ERR_CHECK(emit(m, RV32_LUI(rd, lui_imm)));
        ERR_CHECK(emit(m, RV32_ADDI(rd, rd, addi_imm)));
    }
    return AOT_OK;
}

#define EMIT_BINOP(aotm, ins, OP)                                   \
    do {                                                            \
        ERR_CHECK(fix_arg(aotm, &(ins)->arg[0], rv32_priv_reg[0])); \
        ERR_CHECK(fix_arg(aotm, &(ins)->arg[1], rv32_priv_reg[1])); \
        rv32_reg rd  = (ins)->to.as.loc.as.reg;                     \
        rv32_reg rs1 = (ins)->arg[0].as.loc.as.reg;                 \
        rv32_reg rs2 = (ins)->arg[1].as.loc.as.reg;                 \
        ERR_CHECK(emit(aotm, OP(rd, rs1, rs2)));                    \
    } while(0)

#define EMIT_BINOP_IMM(aotm, ins, OP, OPI)                              \
    do {                                                                \
        ERR_CHECK(fix_arg(aotm, &(ins)->arg[0], rv32_priv_reg[0]));     \
        rv32_reg rd = (ins)->to.as.loc.as.reg;                          \
        rv32_reg rs1 = (ins)->arg[0].as.loc.as.reg;                     \
        if (imm((ins)->arg[1], -2048, 2047)) {                          \
            uint32_t imm = (ins)->arg[1].as.int32_const;                \
            ERR_CHECK(emit(aotm, OPI(rd, rs1, imm)));                   \
        } else {                                                        \
            ERR_CHECK(fix_arg(aotm, &(ins)->arg[1], rv32_priv_reg[1])); \
            rv32_reg rs2 = (ins)->arg[1].as.loc.as.reg;                 \
            ERR_CHECK(emit(aotm, OP(rd, rs1, rs2)));                    \
        }                                                               \
    } while (0)

#define EMIT_MEMOP(aotm, ins, OP)                                   \
    do {                                                            \
        ERR_CHECK(fix_arg(aotm, &(ins)->to, rv32_priv_reg[0]));     \
        ERR_CHECK(fix_arg(aotm, &(ins)->arg[1], rv32_priv_reg[1])); \
        assert((ins)->arg[0].type == REF_TYPE_INT32_CONST);         \
        rv32_reg rs2 = (ins)->to.as.loc.as.reg;                     \
        int32_t imm  = (ins)->arg[0].as.int32_const;                \
        rv32_reg rs1 = (ins)->arg[1].as.loc.as.reg;                 \
        while (imm > 2047) {                                        \
            ERR_CHECK(emit(aotm, RV32_ADDI(rs1, rs1, 2047)));       \
            imm -= 2047;                                            \
        }                                                           \
        while (imm < -2048) {                                       \
            ERR_CHECK(emit(aotm, RV32_ADDI(rs1, rs1, -2048)));      \
            imm += 2048;                                            \
        }                                                           \
        ERR_CHECK(emit(aotm, OP(rs2, imm, rs1)));                   \
    } while (0)


static AOTErr_t emitins(AOTModule *m, Ins *i) {

    switch (i->op) {

        /* Arithmetic and Bits */
        case IR_OPCODE_ADD:
            EMIT_BINOP_IMM(m, i, RV32_ADD, RV32_ADDI);
            break;
        case IR_OPCODE_AND:
            EMIT_BINOP_IMM(m, i, RV32_AND, RV32_ANDI);
            break;
        case IR_OPCODE_SDIV:
            EMIT_BINOP(m, i, RV32_DIV);
            break;
        case IR_OPCODE_MUL:
            EMIT_BINOP(m, i, RV32_MUL);
            break;
        case IR_OPCODE_OR:
            EMIT_BINOP_IMM(m, i, RV32_OR, RV32_ORI);
            break;
        case IR_OPCODE_SREM:
            EMIT_BINOP(m, i, RV32_REM);
            break;
        case IR_OPCODE_ASHR:
            EMIT_BINOP_IMM(m, i, RV32_SRA, RV32_SRAI);
            break;
        case IR_OPCODE_SHL:
            EMIT_BINOP_IMM(m, i, RV32_SLL, RV32_SLLI);
            break;
        case IR_OPCODE_SUB:
            EMIT_BINOP_IMM(m, i, RV32_SUB, RV32_SUBI);
            break;
        case IR_OPCODE_UDIV:
            EMIT_BINOP(m, i, RV32_DIVU);
            break;
        case IR_OPCODE_UREM:
            EMIT_BINOP(m, i, RV32_REMU);
            break;
        case IR_OPCODE_XOR:
            EMIT_BINOP_IMM(m, i, RV32_XOR, RV32_XORI);
            break;

        /* Comparisons */
        case IR_OPCODE_EQZ: {
            ERR_CHECK(fix_arg(m, &i->arg[0], rv32_priv_reg[0]));
            rv32_reg rd = i->to.as.loc.as.reg;
            rv32_reg rs1 = i->arg[0].as.loc.as.reg;
            ERR_CHECK(emit(m, RV32_SEQZ(rd, rs1)));
        } break;
        case IR_OPCODE_EQ: {
            ERR_CHECK(fix_arg(m, &i->arg[0], rv32_priv_reg[0]));
            ERR_CHECK(fix_arg(m, &i->arg[1], rv32_priv_reg[1]));
            rv32_reg rd = i->to.as.loc.as.reg;
            rv32_reg rs1 = i->arg[0].as.loc.as.reg;
            rv32_reg rs2 = i->arg[1].as.loc.as.reg;
            ERR_CHECK(emit(m, RV32_XOR(rs1, rs1, rs2)));
            ERR_CHECK(emit(m, RV32_SEQZ(rd, rs1)));
        } break;
        case IR_OPCODE_NE: {
            ERR_CHECK(fix_arg(m, &i->arg[0], rv32_priv_reg[0]));
            ERR_CHECK(fix_arg(m, &i->arg[1], rv32_priv_reg[1]));
            rv32_reg rd = i->to.as.loc.as.reg;
            rv32_reg rs1 = i->arg[0].as.loc.as.reg;
            rv32_reg rs2 = i->arg[1].as.loc.as.reg;
            ERR_CHECK(emit(m, RV32_XOR(rd, rs1, rs2)));
        } break;
        case IR_OPCODE_SLT:
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
            break;
        case IR_OPCODE_ULT:
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
            break;
        case IR_OPCODE_SGT: {
            Ref tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
        } break;
        case IR_OPCODE_UGT: {
            Ref tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
        } break;
        case IR_OPCODE_SLE: {
            Ref tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
            rv32_reg rd = i->to.as.loc.as.reg;
            ERR_CHECK(emit(m, RV32_XORI(rd, rd, 1)));
        } break;
        case IR_OPCODE_ULE: {
            Ref tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
            rv32_reg rd = i->to.as.loc.as.reg;
            ERR_CHECK(emit(m, RV32_XORI(rd, rd, 1)));
        } break;
        case IR_OPCODE_SGE: {
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
            rv32_reg rd = i->to.as.loc.as.reg;
            ERR_CHECK(emit(m, RV32_XORI(rd, rd, 1)));
        } break;
        case IR_OPCODE_UGE: {
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
            rv32_reg rd = i->to.as.loc.as.reg;
            ERR_CHECK(emit(m, RV32_XORI(rd, rd, 1)));
        } break;

        /* Memory */
        case IR_OPCODE_STORE:
            EMIT_MEMOP(m, i, RV32_SW);
            break;
        case IR_OPCODE_ULOAD8:
            EMIT_MEMOP(m, i, RV32_LBU);
            break;
        case IR_OPCODE_LOAD:
            EMIT_MEMOP(m, i, RV32_LW);
            break;
        case IR_OPCODE_STORE8:
            EMIT_MEMOP(m, i, RV32_SB);
            break;

        /* Other */
        case IR_OPCODE_COPY: {
            assert(i->to.type == REF_TYPE_LOCATION);
            if (i->to.as.loc.type == REGISTER) {
                /* the copy destination is a register */
                location *loc = &i->arg[0].as.loc;
                if (i->arg[0].type == REF_TYPE_LOCATION && loc->type == REGISTER) {
                    /* the copy source is a register */
                    if (i->to.as.loc.as.reg != loc->as.reg) {
                        /* the copy destination is not equal to the copy source */
                        ERR_CHECK(emit(m, RV32_MV(i->to.as.loc.as.reg, loc->as.reg)));
                    }
                }
                else if (i->arg[0].type == REF_TYPE_LOCATION && loc->type == STACK_SLOT) {
                    /* the copy source is a stack slot */
                    /* STACK_SLOT as a copy destination is not implemented */
                    assert(0);
                }
                else if (i->arg[0].type == REF_TYPE_INT32_CONST) {
                    /* the copy source is a constant */
                    ERR_CHECK(emit_li(m, i->to.as.loc.as.reg, i->arg[0].as.int32_const));
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
        case IR_OPCODE_PUSH: {
            assert(i->arg[0].type == REF_TYPE_LOCATION);
            assert(i->arg[0].as.loc.type == REGISTER);
            ERR_CHECK(emit(m, RV32_ADDI(SP, SP, -4)));
            ERR_CHECK(emit(m, RV32_SW(i->arg[0].as.loc.as.reg, 0, SP)));
        } break;
        case IR_OPCODE_POP: {
            assert(i->to.type == REF_TYPE_LOCATION);
            assert(i->to.as.loc.type == REGISTER);
            ERR_CHECK(emit(m, RV32_LW(i->to.as.loc.as.reg, 0, SP)));
            ERR_CHECK(emit(m, RV32_ADDI(SP, SP, 4)));
        } break;
        case IR_OPCODE_CALL: {
            assert(i->arg[0].type == REF_TYPE_NAME);

            AOTRelocation *reloc = malloc(sizeof(struct AOTRelocation));
            if (reloc == NULL) return AOT_ERR;
            reloc->offset = m->offset - m->text_start;
            reloc->addend = 0;
            reloc->type = R_RISCV_CALL;
            /* symbol_name points in the array func_decls in the wasm_module struct */
            reloc->symbol_name = i->arg[0].as.name;
            reloc->next = m->reloc_list;
            m->reloc_list = reloc;
            m->reloc_count++;

            ERR_CHECK(emit(m, RV32_AUIPC(RA, 0)));
            ERR_CHECK(emit(m, RV32_JARL(RA, RA, 0)));
        } break;
        default:
            fprintf(stderr, "Error: opcode %d not implemented!\n", i->op);
            assert(0);
    }
    return AOT_OK;
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
  +-------------+
  | callee-save |
  |  registers  |
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

static AOTErr_t rv32_emitfn(AOTModule *m, Fn *fn, uint32_t typeidx) {

    list *patch_list = listCreate();
    listSetFreeMethod(patch_list, free);

    uint32_t next_func = m->next_func;
    assert(next_func < m->wasm_mod->function_count);
    m->funcs[next_func].text_offset = m->offset - m->text_start;
    m->funcs[next_func].type_index = typeidx;
    m->next_func++;

    /* store old fp on the current stack frame */
    if (emit(m, RV32_SW(FP, -8, SP))) goto ERROR;
    /* store old ra on the current stack frame */
    if (emit(m, RV32_SW(RA, -4, SP))) goto ERROR;
    /* update fp, now it point to the saved fp on current the stack */
    if (emit(m, RV32_ADDI(FP, SP, -8))) goto ERROR;

    /* allocates the current stack frame */
    assert(fn->num_stack_slots == 0);
    // TODO: why 16 aligned?
    unsigned int frame = (8 + 4 * fn->num_stack_slots + 15) & ~15;
    for (unsigned int i = 0; i < RV32_NUM_REG; i++) {
        if (fn->regs_to_preserve[i]) {
            frame += 4;
        }
    }
    frame = (frame + 15) & ~15;
    /* 2047 is the maximum positive number that can be represent
     * in the imm field of the addi instruction */
    if (frame <= 2047) {
        if (emit(m, RV32_ADDI(SP, SP, -1 * frame))) goto ERROR;
    } else {
        if (emit_li(m, T6, frame)) goto ERROR;
        if (emit(m, RV32_SUB(SP, SP, T6))) goto ERROR;
    }

    for (unsigned int i = 0, off = 0; i < RV32_NUM_REG; i++) {
        if (fn->regs_to_preserve[i]) {
            if (emit(m, RV32_SW(i, off, SP))) goto ERROR;
            off += 4;
        }
    }

    listNode *blk_node;
    listNode *blk_iter = listFirst(fn->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        b->text_start = m->offset;
        backpatch(patch_list, b);
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            if (emitins(m, i)) goto ERROR;
        }
        switch (b->jmp.type) {
            case HALT_JUMP_TYPE: {
                if (emit(m, RV32_EBREAK)) goto ERROR;
            } break;
            case RET0_JUMP_TYPE: {
                for (unsigned int i = 0, off = 0; i < RV32_NUM_REG; i++) {
                    if (fn->regs_to_preserve[i]) {
                        if (emit(m, RV32_LW(i, off, SP))) goto ERROR;
                        off += 4;
                    }
                }

                if (frame <= 2047) {
                    if (emit(m, RV32_ADDI(SP, SP, frame))) goto ERROR;
                } else {
                    if (emit_li(m, T6, frame)) goto ERROR;
                    if (emit(m, RV32_SUB(SP, SP, T6))) goto ERROR;
                }

                /* restore the saved fp */
                if (emit(m, RV32_LW(RA, 4, FP))) goto ERROR;
                /* restore the saved ra */
                if (emit(m, RV32_LW(FP, 0, FP))) goto ERROR;
                if (emit(m, RV32_RET)) goto ERROR;
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
                         imm = jump_target->text_start - m->offset;
                    } else {
                        /* jump_target hasn't already been processed,
                         * let's create a patch and fix it later */
                        RV32JumpPatch *patch = malloc(sizeof(struct RV32JumpPatch));
                        if (patch == NULL) goto ERROR;
                        patch->jump_target = jump_target;
                        patch->type = J_PATCH_TYPE;
                        patch->jump_instr = m->offset;
                        listAddNodeTail(patch_list, patch);
                    }
                    if (emit(m, RV32_J(imm))) goto ERROR;
                }
            } break;
            case JNZ_JUMP_TYPE: {
                if (fix_arg(m, &b->jmp.arg, rv32_priv_reg[0])) goto ERROR;
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
                        imm = jump_target->text_start - m->offset;
                    } else {
                        RV32JumpPatch *patch = malloc(sizeof(struct RV32JumpPatch));
                        if (patch == NULL) goto ERROR;
                        patch->jump_target = jump_target;
                        patch->type = BEQZ_PATCH_TYPE;
                        patch->jump_instr = m->offset;
                        listAddNodeTail(patch_list, patch);
                    }
                    if (emit(m, RV32_BEQZ(rs1, imm))) goto ERROR;
                } else if (blk_next == b->succ[1]) {
                    /* The next block to be processed is the 'else' block, so jump
                     * to the 'then' block is the argument of jnz is not equal to zero */
                    Blk *jump_target = b->succ[0];
                    rv32_reg rs1 = b->jmp.arg.as.loc.as.reg;
                    int32_t imm = 0;
                    if (jump_target->text_start != NULL) {
                        imm = jump_target->text_start - m->offset;
                    } else {
                        RV32JumpPatch *patch = malloc(sizeof(struct RV32JumpPatch));
                        if (patch == NULL) goto ERROR;
                        patch->jump_target = jump_target;
                        patch->type = BNEZ_PATCH_TYPE;
                        patch->jump_instr = m->offset;
                        listAddNodeTail(patch_list, patch);
                    }
                    if (emit(m, RV32_BNEZ(rs1, imm))) goto ERROR;
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

    listRelease(patch_list);
    return AOT_OK;

ERROR:
    listRelease(patch_list);
    return AOT_ERR;
}


static AOTErr_t rv32_emit_target_info(AOTModule *m) {

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

    WRITE_UINT32(m, AOT_SECTION_TYPE_TARGET_INFO);
    WRITE_UINT32(m, sizeof(struct AOTTargetInfo));
    WRITE_UINT16(m, target_info.bin_type);
    WRITE_UINT16(m, target_info.abi_type);
    WRITE_UINT16(m, target_info.e_type);
    WRITE_UINT16(m, target_info.e_machine);
    WRITE_UINT32(m, target_info.e_version);
    WRITE_UINT32(m, target_info.e_flags);
    WRITE_UINT64(m, target_info.reserved);
    WRITE_UINT64(m, target_info.feature_flags);
    WRITE_BYTE_ARRAY(m, target_info.arch, sizeof(target_info.arch));
    return AOT_OK;
}

