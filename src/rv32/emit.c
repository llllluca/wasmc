#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "rv32im.h"
#include "ir.h"
#include "aot.h"
#include "wasmc.h"

#define WASMC_OK 0

typedef enum JumpTargetType {
    JUMP_TO_BLOCK,
    JUMP_TO_FUNCTION,
} JumpTargetType;

typedef struct RV32JumpPatch {
    struct list_head link;
    /* target_id is a function id or a block id
     * depending on the value of target_type */
    JumpTargetType target_type;
    uint32_t target_id;
    enum {
        J_PATCH_TYPE,
        JAL_PATCH_TYPE,
        BEQZ_PATCH_TYPE,
        BNEZ_PATCH_TYPE,
    } type;
    uint8_t *jump_instr;
} RV32JumpPatch;

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
    RV32_PRIVATE_REG0,
    RV32_PRIVATE_REG1,
};

const AOTTargetInfo target_info = {
    .bin_type = AOT_BIN_TYPE_ELF32L,
    .abi_type = 0,
    .e_type = AOT_E_TYPE_REL,
    .e_machine = AOT_E_MACHINE_RISCV,
    .e_version = AOT_E_VERSION_CURRENT,
    .e_flags = 4,
    .feature_flags = 0,
    .reserved = 0,
    .arch = "riscv32",
};

static Location rv32_private_reg0 = {
    .type = LOCATION_TYPE_REGISTER,
    .as.reg = RV32_PRIVATE_REG0,
};

static Location rv32_private_reg1 = {
    .type = LOCATION_TYPE_REGISTER,
    .as.reg = RV32_PRIVATE_REG1,
};

static int emit(AOTModule *m, ins_fmt ins) {
    unsigned long size = m->buf_end - m->offset;
    if (size < sizeof(uint32_t)) {
        return WASMC_ERR_AOT_BUFFER_TOO_SMALL;
    }
    *(uint32_t *)(m->offset) = ins.as.u32;
    m->offset += sizeof(uint32_t);
    return WASMC_OK;
}

/* Instr: li
 * Description: load immediate (psuedoinstruction)
 * Use: li rd, imm
 * Result: rd = imm */
//https://stackoverflow.com/questions/50742420/risc-v-build-32-bit-constants-with-lui-and-addi
static int emit_li(AOTModule *m, uint32_t rd, int32_t imm) {
    int err;
    if (-2048 <= imm && imm <= 2047) {
        err = emit(m, RV32_ADDI(rd, ZERO, imm));
        if (err) return err;
    } else {
        uint32_t lui_imm = imm >> 12;
        uint32_t addi_imm = imm & 0xfff;
        if (imm & 0x800) {
            lui_imm += 1;
        }
        err = emit(m, RV32_LUI(rd, lui_imm));
        if (err) return err;
        err = emit(m, RV32_ADDI(rd, rd, addi_imm));
        if (err) return err;
    }
    return WASMC_OK;
}

static int fix_arg(AOTModule *m, IRReference *arg, Location *loc) {
    int err;
    switch (arg->type) {
    case IR_REF_TYPE_I32: {
        err = emit_li(m, loc->as.reg, arg->as.i32);
        if (err) return err;
        arg->type = IR_REF_TYPE_LOCATION;
        arg->as.location = loc;
    } break;
    case IR_REF_TYPE_LOCATION: {
        switch (arg->as.location->type) {
            case LOCATION_TYPE_REGISTER:
                break;
            case LOCATION_TYPE_STACK_SLOT: {
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
    return WASMC_OK;
}

static inline bool imm(IRReference r, int imm_min, int imm_max) {
    return r.type == IR_REF_TYPE_I32 && imm_min <= r.as.i32 && r.as.i32 <= imm_max;
}

#define EMIT_BINOP(aotm, ins, OP)                                \
    do {                                                         \
        err = fix_arg(aotm, &(ins)->arg[0], &rv32_private_reg0); \
        if (err) return err;                                     \
        err = fix_arg(aotm, &(ins)->arg[1], &rv32_private_reg1); \
        if (err) return err;                                     \
        rv32_reg rd  = (ins)->to.as.location->as.reg;            \
        rv32_reg rs1 = (ins)->arg[0].as.location->as.reg;        \
        rv32_reg rs2 = (ins)->arg[1].as.location->as.reg;        \
        err = (emit(aotm, OP(rd, rs1, rs2)));                    \
        if (err) return err;                                     \
    } while(0)

#define EMIT_BINOP_IMM(aotm, ins, OP, OPI)                           \
    do {                                                             \
        err = fix_arg(aotm, &(ins)->arg[0], &rv32_private_reg0);     \
        if (err) return err;                                         \
        rv32_reg rd = (ins)->to.as.location->as.reg;                 \
        rv32_reg rs1 = (ins)->arg[0].as.location->as.reg;            \
        if (imm((ins)->arg[1], -2048, 2047)) {                       \
            uint32_t imm = (ins)->arg[1].as.i32;                     \
            err = emit(aotm, OPI(rd, rs1, imm));                     \
            if (err) return err;                                     \
        } else {                                                     \
            err = fix_arg(aotm, &(ins)->arg[1], &rv32_private_reg1); \
            if (err) return err;                                     \
            rv32_reg rs2 = (ins)->arg[1].as.location->as.reg;        \
            err = emit(aotm, OP(rd, rs1, rs2));                      \
            if (err) return err;                                     \
        }                                                            \
    } while (0)

#define EMIT_MEMOP(aotm, ins, OP)                                \
    do {                                                         \
        err = fix_arg(aotm, &(ins)->to, &rv32_private_reg0);     \
        if (err) return err;                                     \
        err = fix_arg(aotm, &(ins)->arg[1], &rv32_private_reg1); \
        if (err) return err;                                     \
        assert((ins)->arg[0].type == IR_REF_TYPE_I32);           \
        rv32_reg rs2 = (ins)->to.as.location->as.reg;            \
        int32_t imm  = (ins)->arg[0].as.i32;                     \
        rv32_reg rs1 = (ins)->arg[1].as.location->as.reg;        \
        if (-2048 <= imm && imm <=  2047) {                      \
            err = emit(aotm, OP(rs2, imm, rs1));                 \
            if (err) return err;                                 \
            break;                                               \
        }                                                        \
        rv32_reg tmp = rv32_private_reg0.as.reg;                 \
        while (imm > 2047) {                                     \
            err = emit(aotm, RV32_ADDI(tmp, rs1, 2047));         \
            if (err) return err;                                 \
            imm -= 2047;                                         \
            rs1 = tmp;                                           \
        }                                                        \
        while (imm < -2048) {                                    \
            err = emit(aotm, RV32_ADDI(tmp, rs1, -2048));        \
            if (err) return err;                                 \
            imm += 2048;                                         \
            rs1 = tmp;                                           \
        }                                                        \
        err = emit(aotm, OP(rs2, imm, rs1));                     \
        if (err) return err;                                     \
    } while (0)


static int emitins(AOTModule *m, IRInstr *i) {

    int err;
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
            err = fix_arg(m, &i->arg[0], &rv32_private_reg0);
            if (err) return err;
            rv32_reg rd = i->to.as.location->as.reg;
            rv32_reg rs1 = i->arg[0].as.location->as.reg;
            err = emit(m, RV32_SEQZ(rd, rs1));
            if (err) return err;
        } break;
        case IR_OPCODE_EQ: {
            err = fix_arg(m, &i->arg[0], &rv32_private_reg0);
            if (err) return err;
            err = fix_arg(m, &i->arg[1], &rv32_private_reg1);
            if (err) return err;
            rv32_reg rd = i->to.as.location->as.reg;
            rv32_reg rs1 = i->arg[0].as.location->as.reg;
            rv32_reg rs2 = i->arg[1].as.location->as.reg;
            err = emit(m, RV32_XOR(rd, rs1, rs2));
            if (err) return err;
            err = emit(m, RV32_SEQZ(rd, rd));
            if (err) return err;
        } break;
        case IR_OPCODE_NE: {
            err = fix_arg(m, &i->arg[0], &rv32_private_reg0);
            if (err) return err;
            err = fix_arg(m, &i->arg[1], &rv32_private_reg1);
            if (err) return err;
            rv32_reg rd = i->to.as.location->as.reg;
            rv32_reg rs1 = i->arg[0].as.location->as.reg;
            rv32_reg rs2 = i->arg[1].as.location->as.reg;
            err = emit(m, RV32_XOR(rd, rs1, rs2));
            if (err) return err;
            err = emit(m, RV32_SNEZ(rd, rd));
            if (err) return err;
        } break;
        case IR_OPCODE_SLT:
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
            break;
        case IR_OPCODE_ULT:
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
            break;
        case IR_OPCODE_SGT: {
            IRReference tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
        } break;
        case IR_OPCODE_UGT: {
            IRReference tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
        } break;
        case IR_OPCODE_SLE: {
            IRReference tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
            rv32_reg rd = i->to.as.location->as.reg;
            err = emit(m, RV32_XORI(rd, rd, 1));
            if (err) return err;
        } break;
        case IR_OPCODE_ULE: {
            IRReference tmp = i->arg[0];
            i->arg[0] = i->arg[1];
            i->arg[1] = tmp;
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
            rv32_reg rd = i->to.as.location->as.reg;
            err = emit(m, RV32_XORI(rd, rd, 1));
            if (err) return err;
        } break;
        case IR_OPCODE_SGE: {
            EMIT_BINOP_IMM(m, i, RV32_SLT, RV32_SLTI);
            rv32_reg rd = i->to.as.location->as.reg;
            err = emit(m, RV32_XORI(rd, rd, 1));
            if (err) return err;
        } break;
        case IR_OPCODE_UGE: {
            EMIT_BINOP_IMM(m, i, RV32_SLTU, RV32_SLTIU);
            rv32_reg rd = i->to.as.location->as.reg;
            err = emit(m, RV32_XORI(rd, rd, 1));
            if (err) return err;
        } break;

        /* Memory */
        case IR_OPCODE_STORE:
            EMIT_MEMOP(m, i, RV32_SW);
            break;
        case IR_OPCODE_ULOAD8:
            EMIT_MEMOP(m, i, RV32_LBU);
            break;
        case IR_OPCODE_SLOAD8:
            EMIT_MEMOP(m, i, RV32_LB);
            break;
        case IR_OPCODE_LOAD:
            EMIT_MEMOP(m, i, RV32_LW);
            break;
        case IR_OPCODE_STORE8:
            EMIT_MEMOP(m, i, RV32_SB);
            break;

        /* Other */
        case IR_OPCODE_COPY: {
            assert(i->to.type == IR_REF_TYPE_LOCATION);
            if (i->to.as.location->type == LOCATION_TYPE_REGISTER) {
                /* the copy destination is a register */
                Location *loc = i->arg[0].as.location;
                if (i->arg[0].type == IR_REF_TYPE_LOCATION &&
                    loc->type == LOCATION_TYPE_REGISTER) {
                    /* the copy source is a register */
                    if (i->to.as.location->as.reg != loc->as.reg) {
                        /* the copy destination is not equal to the copy source */
                        err = emit(m, RV32_MV(i->to.as.location->as.reg, loc->as.reg));
                        if (err) return err;
                    }
                }
                else if (i->arg[0].type == IR_REF_TYPE_LOCATION &&
                         loc->type == LOCATION_TYPE_STACK_SLOT) {
                    /* the copy source is a stack slot */
                    /* STACK_SLOT as a copy destination is not implemented */
                    assert(0);
                }
                else if (i->arg[0].type == IR_REF_TYPE_I32) {
                    /* the copy source is a constant */
                    err = emit_li(m, i->to.as.location->as.reg, i->arg[0].as.i32);
                    if (err) return err;
                }
                else assert(0);
            }
            else if (i->to.as.location->type == LOCATION_TYPE_STACK_SLOT) {
                /* the copy destination is a stack slot */
                /* STACK_SLOT as a copy destination is not implemented */
                assert(0);
            }
            else assert(0);
        } break;
        case IR_OPCODE_PUSH: {
            assert(i->arg[0].type == IR_REF_TYPE_LOCATION);
            assert(i->arg[0].as.location->type == LOCATION_TYPE_REGISTER);
            err = emit(m, RV32_ADDI(SP, SP, -4));
            if (err) return err;
            err = emit(m, RV32_SW(i->arg[0].as.location->as.reg, 0, SP));
            if (err) return err;
        } break;
        case IR_OPCODE_POP: {
            assert(i->to.type == IR_REF_TYPE_LOCATION);
            assert(i->to.as.location->type == LOCATION_TYPE_REGISTER);
            err = emit(m, RV32_LW(i->to.as.location->as.reg, 0, SP));
            if (err) return err;
            err = emit(m, RV32_ADDI(SP, SP, 4));
            if (err) return err;
        } break;
        case IR_OPCODE_CALL: {
            assert(i->arg[0].type == IR_REF_TYPE_FUNCTION);
            uint32_t id = i->arg[0].as.wasm_func->id;
            int32_t imm = 0;
            if (m->function_text_start[id] != NULL) {
                imm = m->function_text_start[id] - m->offset;
            } else {
                RV32JumpPatch *patch = malloc(sizeof(struct RV32JumpPatch));
                if (patch == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
                patch->target_type = JUMP_TO_FUNCTION;
                patch->target_id = id;
                patch->type = JAL_PATCH_TYPE;
                patch->jump_instr = m->offset;
                list_add_tail(&patch->link, &m->patch_list);
            }
            err = emit(m, RV32_JAL(RA, imm));
            if (err) return err;
        } break;
        default:
            fprintf(stderr, "Error: opcode %d not implemented!\n", i->op);
            assert(0);
    }
    return WASMC_OK;
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
  | callee-save |
  |  registers  |
  +=============+

*/

static void backpatch(struct list_head *patch_list, JumpTargetType target_type,
                      uint32_t target_id, uint8_t *address) {

    RV32JumpPatch *patch, *iter;
    list_for_each_entry_safe(patch, iter, patch_list, link) {
        if (target_type != patch->target_type || target_id != patch->target_id) {
            continue;
        }
        int32_t imm = address - patch->jump_instr;
        switch (patch->type) {
            case J_PATCH_TYPE: {
                *(uint32_t *)patch->jump_instr = RV32_J(imm).as.u32;
            } break;
            case JAL_PATCH_TYPE: {
                rv32_reg rd = ((j_ins_fmt *)patch->jump_instr)->rd;
                *(uint32_t *)patch->jump_instr = RV32_JAL(rd, imm).as.u32;
            } break;
            case BEQZ_PATCH_TYPE: {
                rv32_reg rs1 = ((b_ins_fmt *)patch->jump_instr)->rs1;
                *(uint32_t *)patch->jump_instr = RV32_BEQZ(rs1, imm).as.u32;
            } break;
            case BNEZ_PATCH_TYPE: {
                rv32_reg rs1 = ((b_ins_fmt *)patch->jump_instr)->rs1;
                *(uint32_t *)patch->jump_instr = RV32_BNEZ(rs1, imm).as.u32;
            } break;
            default:
                /* Unexpected patch type */
                assert(0);
        }
        list_del(&patch->link);
        free(patch);
    }
}

static unsigned int get_stack_frame_size(IRFunction *fn) {
    assert(fn->stack_slot_count == 0);
    #define SAVED_RA_SIZE 4
    #define SAVED_FP_SIZE 4
    #define STACK_SLOT_SIZE 4
    unsigned int frame;
    frame = SAVED_RA_SIZE + SAVED_FP_SIZE;
    frame += STACK_SLOT_SIZE * fn->stack_slot_count;
    for (unsigned int i = 0; i < RV32_NUM_REG; i++) {
        if (fn->regs_to_preserve[i]) {
            frame += 4;
        }
    }
    /* 16-bit aligned */
    frame = (frame + 15) & ~15;
    return frame;
}

static int emit_ret0(AOTModule *m, IRFunction *fn, unsigned int frame) {

    int err;
    /* Restore callee-saved registers */
    for (unsigned int i = 0, off = 0; i < RV32_NUM_REG; i++) {
        if (fn->regs_to_preserve[i]) {
            err = emit(m, RV32_LW(i, off, SP));
            if (err) return err;
            off += 4;
        }
    }

    /* Pop of the stack frame */
    if (frame <= 2047) {
        err = emit(m, RV32_ADDI(SP, SP, frame));
        if (err) return err;
    } else {
        err = emit_li(m, T6, frame);
        if (err) return err;
        err = emit(m, RV32_SUB(SP, SP, T6));
        if (err) return err;
    }

    /* Restore the saved FP */
    err = emit(m, RV32_LW(RA, 4, FP));
    if (err) return err;
    /* Restore the saved RA */
    err = emit(m, RV32_LW(FP, 0, FP));
    if (err) return err;
    /* Emit ret */
    err = emit(m, RV32_RET);
    if (err) return err;

    return WASMC_OK;
}

static int emit_jmp(AOTModule *m, IRBlock *block) {

    int err;
    IRBlock *block_next = NULL;
    struct list_head *next = list_next(&block->link);
    if (next != NULL) {
        block_next = container_of(next, IRBlock, link);
    }
    IRBlock *jump_target = block->succ[0];
    /* If the jump target is the next block to be processed,
     * don't emit the j instruction */
    if (jump_target != block_next) {
        int32_t imm = 0;
        if (jump_target->text_start != NULL) {
            /* jump_target was already processed, so we know where the
             * instructions of jump_target start in the text segment */
             imm = jump_target->text_start - m->offset;
        } else {
            /* jump_target hasn't already been processed,
             * let's create a patch and fix it later */
            RV32JumpPatch *patch = malloc(sizeof(struct RV32JumpPatch));
            if (patch == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
            patch->target_type = JUMP_TO_BLOCK;
            patch->target_id = jump_target->id;
            patch->type = J_PATCH_TYPE;
            patch->jump_instr = m->offset;
            list_add_tail(&patch->link, &m->patch_list);
        }
        err = emit(m, RV32_J(imm));
        if (err) return err;
    }
    return WASMC_OK;
}

static int emit_jnz(AOTModule *m, IRBlock *block) {
    int err;
    err = fix_arg(m, &block->jump.arg, &rv32_private_reg0);
    if (err) return err;
    IRBlock *block_next = NULL;
    struct list_head *next = list_next(&block->link);
    if (next != NULL) {
        block_next = container_of(next, IRBlock, link);
    }
    if (block_next == block->succ[0]) {
        /* The next block to be processed is the 'then' block, so jump
         * to the 'else' block is the argument of jnz is equal to zero */
        IRBlock *jump_target = block->succ[1];
        rv32_reg rs1 = block->jump.arg.as.location->as.reg;
        int32_t imm = 0;
        if (jump_target->text_start != NULL) {
            imm = jump_target->text_start - m->offset;
        } else {
            RV32JumpPatch *patch = malloc(sizeof(struct RV32JumpPatch));
            if (patch == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
            patch->target_type = JUMP_TO_BLOCK;
            patch->target_id = jump_target->id;
            patch->type = BEQZ_PATCH_TYPE;
            patch->jump_instr = m->offset;
            list_add_tail(&patch->link, &m->patch_list);
        }
        err = emit(m, RV32_BEQZ(rs1, imm));
        if (err) return err;
    } else if (block_next == block->succ[1]) {
        /* The next block to be processed is the 'else' block, so jump
         * to the 'then' block is the argument of jnz is not equal to zero */
        IRBlock *jump_target = block->succ[0];
        rv32_reg rs1 = block->jump.arg.as.location->as.reg;
        int32_t imm = 0;
        if (jump_target->text_start != NULL) {
            imm = jump_target->text_start - m->offset;
        } else {
            RV32JumpPatch *patch = malloc(sizeof(struct RV32JumpPatch));
            if (patch == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
            patch->target_type = JUMP_TO_BLOCK;
            patch->target_id = jump_target->id;
            patch->type = BNEZ_PATCH_TYPE;
            patch->jump_instr = m->offset;
            list_add_tail(&patch->link, &m->patch_list);
        }
        err = emit(m, RV32_BNEZ(rs1, imm));
        if (err) return err;
    } else {
        /* The next block to be processed after a block ending with a jnz
         * must be a the 'else' block or the 'then' block of jnz. */
        assert(0);
    }
    return WASMC_OK;
}

static void free_patch_list(struct list_head *patch_list) {
    RV32JumpPatch *patch, *iter;
    list_for_each_entry_safe(patch, iter, patch_list, link) {
        list_del(&patch->link);
        free(patch);
    }
}

int rv32_emit_text(AOTModule *m, IRFunction *fn) {

    int err;
    m->function_text_start[fn->wasm_func->id] = m->offset;
    backpatch(&m->patch_list, JUMP_TO_FUNCTION, fn->wasm_func->id, m->offset);

    /* Push FP on the stack (saved FP) */
    err = emit(m, RV32_SW(FP, -8, SP));
    if (err) goto ERROR;
    /* Push RA on the stack (saved RA) */
    err = emit(m, RV32_SW(RA, -4, SP));
    if (err) goto ERROR;
    /* Update FP, now it point to the saved FP */
    err = emit(m, RV32_ADDI(FP, SP, -8));
    if (err) goto ERROR;

    unsigned int frame = get_stack_frame_size(fn);
    /* Allocate the remaining portion of the stack frame */
    if (frame <= 2047) {
        err = emit(m, RV32_ADDI(SP, SP, -1 * frame));
        if (err) goto ERROR;
    } else {
        err = emit_li(m, T6, frame);
        if (err) goto ERROR;
        err = emit(m, RV32_SUB(SP, SP, T6));
        if (err) goto ERROR;
    }

    /* Store callee-saved registers used by this function on the stack frame */
    for (unsigned int i = 0, off = 0; i < RV32_NUM_REG; i++) {
        if (fn->regs_to_preserve[i]) {
            err = emit(m, RV32_SW(i, off, SP));
            if (err) goto ERROR;
            off += 4;
        }
    }

    IRBlock *block;
    list_for_each_entry(block, &fn->block_list, link) {
        block->text_start = m->offset;
        backpatch(&m->patch_list, JUMP_TO_BLOCK, block->id, block->text_start);
        IRInstr *ins;
        list_for_each_entry(ins, &block->instr_list, link) {
            err = emitins(m, ins);
            if (err) goto ERROR;
        }
        switch (block->jump.type) {
            case IR_JUMP_TYPE_HALT:
                err = emit(m, RV32_EBREAK);
                if (err) goto ERROR;
                break;
            case IR_JUMP_TYPE_RET0:
                emit_ret0(m, fn, frame);
                break;
            case IR_JUMP_TYPE_JMP:
                emit_jmp(m, block);
                break;
            case IR_JUMP_TYPE_JNZ:
                emit_jnz(m, block);
                break;
            default:
                assert(0);
        }
    }

    return WASMC_OK;

ERROR:
    free_patch_list(&m->patch_list);
    return err;
}
