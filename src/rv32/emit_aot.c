#include <assert.h>
#include <string.h>
#include "rv32i.h"
#include "../libqbe.h"
#include "../all.h"

#define AOT_MAGIC_NUMBER 0x746f6100
#define AOT_CURRENT_VERSION 5

typedef enum AOTSectionType {
    /* These six sections are mandatory and must appear in the following order,
     * between each mandatory section there can be zero one or more custom sections. */
    AOT_SECTION_TYPE_TARGET_INFO = 0,
    AOT_SECTION_TYPE_INIT_DATA = 1,
    AOT_SECTION_TYPE_TEXT = 2,
    AOT_SECTION_TYPE_FUNCTION = 3,
    AOT_SECTION_TYPE_EXPORT = 4,
    AOT_SECTION_TYPE_RELOCATION = 5,
    /* Note: We haven't had anything to use AOT_SECTION_TYPE_SIGNATURE.
     * It's just reserved for possible module signing features. */
    AOT_SECTION_TYPE_SIGNATURE = 6,
    AOT_SECTION_TYPE_CUSTOM = 100,
} AOTSectionType;

/* Legal values for bin_type */
#define BIN_TYPE_ELF32L 0 /* 32-bit little endian */
#define BIN_TYPE_ELF32B 1 /* 32-bit big endian */
#define BIN_TYPE_ELF64L 2 /* 64-bit little endian */
#define BIN_TYPE_ELF64B 3 /* 64-bit big endian */

/* Legal values for e_type (object file type). */
#define E_TYPE_NONE 0  /* No file type */
#define E_TYPE_REL  1  /* Relocatable file */
#define E_TYPE_EXEC 2  /* Executable file */
#define E_TYPE_DYN  3  /* Shared object file */
/* eXecute In Place file, it is not a standard ELF e_type,
 * see wasm-micro-runtime/doc/xip.md */
#define E_TYPE_XIP  4

/* Legal values for e_machine (architecture).  */
#define E_MACHINE_RISCV 243         /* RISC-V 32/64 */

/* Legal values for e_version */
#define E_VERSION_CURRENT 1 /* Current version */

/* Legal values for e_flags */
#define EF_RISCV_FLOAT_ABI_DOUBLE  0x0004

/* Wasm feature supported, legal value for feature_flags */
#define WASM_FEATURE_SIMD_128BIT (1 << 0)
#define WASM_FEATURE_BULK_MEMORY (1 << 1)
#define WASM_FEATURE_MULTI_THREAD (1 << 2)
#define WASM_FEATURE_REF_TYPES (1 << 3)
#define WASM_FEATURE_GARBAGE_COLLECTION (1 << 4)
#define WASM_FEATURE_EXCEPTION_HANDLING (1 << 5)
#define WASM_FEATURE_TINY_STACK_FRAME (1 << 6)
#define WASM_FEATURE_MULTI_MEMORY (1 << 7)
#define WASM_FEATURE_DYNAMIC_LINKING (1 << 8)
#define WASM_FEATURE_COMPONENT_MODEL (1 << 9)
#define WASM_FEATURE_RELAXED_SIMD (1 << 10)
#define WASM_FEATURE_FLEXIBLE_VECTORS (1 << 11)
/* Stack frame is created at the beginning of the function,
 * and not at the beginning of each function call */
#define WASM_FEATURE_FRAME_PER_FUNCTION (1 << 12)
#define WASM_FEATURE_FRAME_NO_FUNC_IDX (1 << 13)


/* Target info, read from ELF header of object file */
typedef struct AOTTargetInfo {
    /* Binary type (elf32l/elf32b/elf64l/elf64b). The only two least significant bits
     * of bin_type has a meaning, the other bit are useless.

     * target_info.bin_type & 1 identifies the architecture for this binary.(This
     * bit has the same meaning as the EI_CLASS field of e_ident in the ELF header).
     * - If target_info.bin_type & 1 == 1 then this binary is for 64-bit machine.
     * - If target_info.bin_type & 1 == 1 then this binary is for 32-bit machine.

     * target_info.bin_type & 2 specifies the data encoding of the processor-specific
     * data in the file. (This bit has the same meaning as the EI_DATA field of
     * e_ident in the ELF header).
     * - If target_info.bin_type & 2 == 0 then the data encoding is two's complement,
     * little-endian.
     * - If target_info.bin_type & 2 == 1 then the data encoding is two's complement,
     * big-endian. */
    uint16_t bin_type;

    /* Not used by WAMR, can be any value */
    uint16_t abi_type;

    /* e_type identifies the object file type. This field has the same meaning as the
     * e_type field in the ELF header */
    uint16_t e_type;

    /* e_machine specifies the architecture for which this file is intended,
     * indicating the type of machine that the executable is designed to run on */
    uint16_t e_machine;

    /* Object file version */
    uint32_t e_version;

    /* e_flags holds processor-specific flags. See chapter 8 of riscv-abi.pdf */
    uint32_t e_flags;

    /* Specify wasm features supported */
    uint64_t feature_flags;

    /* Reserved, always zero */
    uint64_t reserved;

    /* Architecture string name, must be set consistence with e_machine.
     * Legal value for arch are:
     * - "x86_64"
     * - "i386"
     * - "arm64"
     * - "aarch64"
     * - "mips"
     * - "xtensa"
     * - "riscv32"
     * - "riscv64"
     * - "arc" */
    char arch[16];
} AOTTargetInfo;

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
static void emitins(Ins *i, uint8_t **p, uint8_t *p_end);

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
        .can_be_reg_imm = false,
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
    [NEG_INSTR] = {
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
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

static void emitins(Ins *i, uint8_t **p, uint8_t *p_end) {
    switch (i->op) {
        case ADD_INSTR: {
            fix_ins_args(i, p, p_end);
            rv32_reg rd = i->to.val.loc.as.reg;
            rv32_reg rs1 = i->arg[0].val.loc.as.reg;
            if (i->arg[1].type == RCon) {
                uint64_t imm = i->arg[1].val.con->val.i;
                EMIT(RV32_ADDI(rd, rs1, imm), *p, p_end);
            } else {
                rv32_reg rs2 = i->arg[1].val.loc.as.reg;
                EMIT(RV32_ADD(rd, rs1, rs2), *p, p_end);
            }
        } break;
        case SUB_INSTR: {
            const instr_info_elem *e = &instr_info[ADD_INSTR];
            rv32_reg rd = i->to.val.loc.as.reg;
            rv32_reg rs1 = i->arg[0].val.loc.as.reg;
            if (is_imm(&i->arg[1], e->imm_min, e->imm_max)) {
                uint64_t imm = -1 * i->arg[1].val.con->val.i;
                EMIT(RV32_ADDI(rd, rs1, imm), *p, p_end);
            } else {
                rv32_reg rs2 = i->arg[1].val.loc.as.reg;
                EMIT(RV32_SUB(rd, rs1, rs2), *p, p_end);
            }
        } break;
        default:
            fprintf(stderr, "Error: opcode %d not implemented!\n", i->op);
            assert(0);
    }
    return;

ERROR:
    panic();
}

static void fix_arg(Ref *arg, rv32_reg r, uint8_t **p, uint8_t *p_end) {
    switch (arg->type) {
        case RCon: {
            switch (arg->val.con->type) {
                case CInt64: {
                    emit_li(r, arg->val.con->val.i, p, p_end);
                    arg->type = RLoc;
                    arg->val.loc.type = REGISTER;
                    arg->val.loc.as.reg = r;
                } break;
                case CAddr: {
                    assert(0 && "fix_arg: CAddr not implemented!");
                    //fprintf(f, "\tla %s, %s\n",
                    //rname[r], arg->val.con->val.addr);
                    arg->type = RLoc;
                    arg->val.loc.type = REGISTER;
                    arg->val.loc.as.reg = r;
                } break;
                default:
                    assert(0);
            }
        } break;
        case RLoc: {
            switch (arg->val.loc.type) {
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
    if (r->type == RCon) {
        Con *c = r->val.con;
        if (c->type == CInt64) {
            if (imm_min <= c->val.i && c->val.i <= imm_max) {
                return true;
            }
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

/*
#define LEN (10 * 1024)
static uint8_t buf[LEN];
static uint8_t *p = buf;
static uint8_t *p_end = buf + LEN;
static uint8_t *text_section_size;
static uint8_t *text_section_start;
*/

void rv32_emit_fn_text(AOTModule *aotm, Fn *fn) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

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

    bool lbl = false;
    listNode *blk_node;
    listNode *blk_iter = listFirst(fn->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        if (lbl || listLength(b->preds) > 1) {
            assert(0 && "label unimplemented!");
        }
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            emitins(i, &p, p_end);
        }
        lbl = true;
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
                assert(0 && "JMP_JUMP_TYPE unimplemented!");
            } break;
            case JNZ_JUMP_TYPE: {
                assert(0 && "JNZ_JUMP_TYPE unimplemented!");
            } break;
            default:
                panic();
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
    WRITE_UINT32(mem.min_page_num, p, p_end);
    WRITE_UINT32(mem.max_page_num, p, p_end);

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
    aotm->text_start = p;
    const uint32_t literal_size = 0;
    WRITE_UINT32(literal_size, p, p_end);

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_finalize_text(AOTModule *aotm) {
    *(aotm->text_size) = aotm->p - aotm->text_start;
}

void rv32_emit_function(AOTModule *aotm) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_SECTION_TYPE_FUNCTION, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    //TODO: fixme
    uint32_t function_offset = 0;
    WRITE_UINT32(function_offset, p, p_end);

    //TODO: fixme
    uint32_t function_type_index = 0;
    WRITE_UINT32(function_type_index, p, p_end);
    
    /* not used */
    uint32_t max_local_cell_num = 0;
    uint32_t max_stack_cell_num = 0;
    WRITE_UINT32(max_local_cell_num, p, p_end);
    WRITE_UINT32(max_stack_cell_num, p, p_end);

    //Fix section size
    *section_size = p - section_start;

    aotm->p = p;
    return;

ERROR:
    panic();
}

void rv32_emit_export(AOTModule *aotm) {

    uint8_t *p = aotm->p;
    uint8_t *p_end = aotm->p_end;

    WRITE_UINT32(AOT_SECTION_TYPE_EXPORT, p, p_end);
    uint8_t *section_size = p;
    p += sizeof(uint32_t);
    uint8_t *section_start = p;

    //TODO: fixme
    uint32_t export_count = 1;
    WRITE_UINT32(export_count, p, p_end);

    //TODO: fixme
    uint32_t export_func_index = 0;
    #define EXPORT_FUNC_KIND 0x00
    WRITE_UINT32(export_func_index, p, p_end);
    WRITE_UINT8(EXPORT_FUNC_KIND, p, p_end);
    char *name = "add";
    uint32_t name_len = strlen(name)+1;
    WRITE_UINT16(name_len, p, p_end);
    WRITE_BYTE_ARRAY(p, p_end, "add", name_len);

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
    WRITE_UINT32(symbol_count, p, p_end);

    uint32_t total_string_len = 0;
    WRITE_UINT32(total_string_len, p, p_end);

    uint32_t group_count = 0;
    WRITE_UINT32(group_count, p, p_end);

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




