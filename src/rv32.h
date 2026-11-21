/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * rv32.h - RV32IM instruction encoder (no compressed instructions).
 *
 * Every function returns the 32-bit encoding of one instruction; the
 * caller appends it with aotw_emit32(). Immediates are taken as signed
 * values and truncated to their field; callers check the ranges
 * (rv_fits12() etc.).
 *
 * The names follow the RISC-V unprivileged ISA manual, chapter "RV32I Base
 * Integer Instruction Set" (instruction formats and opcode map).
 */
#ifndef RV32_H_INCLUDED
#define RV32_H_INCLUDED

#include <stdint.h>

/* integer registers (ABI names) */
enum {
    RV_ZERO = 0,
    RV_RA = 1,
    RV_SP = 2,
    RV_GP = 3,
    RV_TP = 4,
    RV_T0 = 5,
    RV_T1 = 6,
    RV_T2 = 7,
    RV_S0 = 8,
    RV_S1 = 9,
    RV_A0 = 10,
    RV_A1 = 11,
    RV_A2 = 12,
    RV_A3 = 13,
    RV_A4 = 14,
    RV_A5 = 15,
    RV_A6 = 16,
    RV_A7 = 17,
    RV_S2 = 18,
    RV_S3 = 19,
    RV_S4 = 20,
    RV_S5 = 21,
    RV_S6 = 22,
    RV_S7 = 23,
    RV_S8 = 24,
    RV_S9 = 25,
    RV_S10 = 26,
    RV_S11 = 27,
    RV_T3 = 28,
    RV_T4 = 29,
    RV_T5 = 30,
    RV_T6 = 31,
};

/* major opcodes, bits 6:0 of every instruction */
enum {
    RV_OP_LOAD = 0x03,
    RV_OP_IMM = 0x13,
    RV_OP_AUIPC = 0x17,
    RV_OP_STORE = 0x23,
    RV_OP_REG = 0x33,
    RV_OP_LUI = 0x37,
    RV_OP_BRANCH = 0x63,
    RV_OP_JALR = 0x67,
    RV_OP_JAL = 0x6F,
};

/* funct3: selects the operation within a major opcode */
enum {
    /* OP and OP-IMM */
    RV_F3_ADD = 0,
    RV_F3_SLL = 1,
    RV_F3_SLT = 2,
    RV_F3_SLTU = 3,
    RV_F3_XOR = 4,
    RV_F3_SR = 5, /* srl/sra, told apart by funct7 */
    RV_F3_OR = 6,
    RV_F3_AND = 7,
    /* OP with funct7 = RV_F7_MULDIV (RV32M) */
    RV_F3_MUL = 0,
    RV_F3_MULH = 1,
    RV_F3_MULHU = 3,
    RV_F3_DIV = 4,
    RV_F3_DIVU = 5,
    RV_F3_REM = 6,
    RV_F3_REMU = 7,
    /* BRANCH */
    RV_F3_BEQ = 0,
    RV_F3_BNE = 1,
    RV_F3_BLT = 4,
    RV_F3_BGE = 5,
    RV_F3_BLTU = 6,
    RV_F3_BGEU = 7,
    /* LOAD and STORE */
    RV_F3_BYTE = 0,
    RV_F3_HALF = 1,
    RV_F3_WORD = 2,
    RV_F3_BYTE_U = 4,
    RV_F3_HALF_U = 5,
};

/* funct7 of the R-type instructions */
enum {
    RV_F7_BASE = 0x00,
    RV_F7_MULDIV = 0x01,
    RV_F7_ALT = 0x20, /* sub, sra */
};

/* bit positions of the instruction fields */
enum {
    RV_POS_RD = 7,
    RV_POS_FUNCT3 = 12,
    RV_POS_RS1 = 15,
    RV_POS_RS2 = 20,
    RV_POS_FUNCT7 = 25,
    RV_POS_IMM_I = 20, /* I-type imm[11:0] */
    RV_POS_IMM_U = 12, /* U-type imm[31:12] */
};

/* field masks and immediate widths, in bits */
enum {
    RV_OPCODE_MASK = 0x7F,
    RV_REG_MASK = 0x1F,
    RV_SHAMT_MASK = 0x1F,
    RV_IMM20_MASK = 0xFFFFF,
    RV_XLEN = 32,
    RV_I_IMM_BITS = 12,
    RV_B_IMM_BITS = 13,
    RV_J_IMM_BITS = 21,
};

/* instruction size and the I-type immediate range */
enum {
    RV_INSN_BYTES = 4,
    RV_IMM12_MIN = -(1 << (RV_I_IMM_BITS - 1)),
    RV_IMM12_MAX = (1 << (RV_I_IMM_BITS - 1)) - 1,
};

static inline int rv_fits12(int32_t val)
{
    return val >= RV_IMM12_MIN && val <= RV_IMM12_MAX;
}
static inline int rv_fits_jal(int32_t val)
{
    const int32_t limit = 1 << (RV_J_IMM_BITS - 1);
    return val >= -limit && val < limit && !(val & 1);
}
/* a branch reaches +-RV_BRANCH_REACH bytes */
enum { RV_BRANCH_REACH = 1 << (RV_B_IMM_BITS - 1) };

static inline int rv_fits_branch(int32_t val)
{
    return val >= -RV_BRANCH_REACH && val < RV_BRANCH_REACH && !(val & 1);
}

/*
 * The S, B and J formats split their immediate over several fields. Each
 * table lists the pieces: `width` bits from bit `from` of the immediate go
 * to bit `to` of the instruction.
 */
typedef struct {
    uint8_t from;
    uint8_t width;
    uint8_t to;
} rv_imm_piece;

typedef struct {
    const rv_imm_piece *pieces;
    unsigned count;
} rv_imm_layout;

/* imm[11:5] -> 31:25, imm[4:0] -> 11:7 */
static inline rv_imm_layout rv_s_layout(void)
{
    static const rv_imm_piece pieces[] = {{0, 5, 7}, {5, 7, 25}};
    return (rv_imm_layout){pieces, sizeof pieces / sizeof pieces[0]};
}
/* imm[12] -> 31, imm[10:5] -> 30:25, imm[4:1] -> 11:8, imm[11] -> 7 */
static inline rv_imm_layout rv_b_layout(void)
{
    static const rv_imm_piece pieces[] = {
        {11, 1, 7}, {1, 4, 8}, {5, 6, 25}, {12, 1, 31}};
    return (rv_imm_layout){pieces, sizeof pieces / sizeof pieces[0]};
}
/* imm[20] -> 31, imm[10:1] -> 30:21, imm[11] -> 20, imm[19:12] -> 19:12 */
static inline rv_imm_layout rv_j_layout(void)
{
    static const rv_imm_piece pieces[] = {
        {12, 8, 12}, {11, 1, 20}, {1, 10, 21}, {20, 1, 31}};
    return (rv_imm_layout){pieces, sizeof pieces / sizeof pieces[0]};
}

static inline uint32_t rv_low_bits(unsigned width)
{
    return (1U << width) - 1U;
}

/* offset of a branch or jump over the next `ninsns` instructions */
static inline int32_t rv_skip(unsigned ninsns)
{
    return (int32_t)((ninsns + 1U) * RV_INSN_BYTES);
}

/*
 * A 32-bit constant as lui + addi: the upper 20 bits are rounded so that
 * adding the sign-extended lower 12 bits gives the value back.
 */
static inline uint32_t rv_hi20(uint32_t val)
{
    return (val + (1U << (RV_I_IMM_BITS - 1))) >> RV_POS_IMM_U;
}
static inline int32_t rv_lo12(uint32_t val)
{
    return (int32_t)(val - (rv_hi20(val) << RV_POS_IMM_U));
}

/* the immediate's bits placed in the instruction fields */
static inline uint32_t rv_imm_scatter(rv_imm_layout layout, uint32_t imm)
{
    uint32_t insn = 0;
    for (unsigned i = 0; i < layout.count; i++) {
        const rv_imm_piece *piece = &layout.pieces[i];
        insn |= ((imm >> piece->from) & rv_low_bits(piece->width)) << piece->to;
    }
    return insn;
}

/* the immediate read back from the instruction fields (unsigned) */
static inline uint32_t rv_imm_gather(rv_imm_layout layout, uint32_t insn)
{
    uint32_t imm = 0;
    for (unsigned i = 0; i < layout.count; i++) {
        const rv_imm_piece *piece = &layout.pieces[i];
        imm |= ((insn >> piece->to) & rv_low_bits(piece->width)) << piece->from;
    }
    return imm;
}

/* sign-extends the low `width` bits */
static inline int32_t rv_sign_extend(uint32_t bits, unsigned width)
{
    return (int32_t)(bits << (RV_XLEN - width)) >> (RV_XLEN - width);
}

/* formats */
static inline uint32_t rv_r(uint32_t funct7, unsigned rs2, unsigned rs1,
                            uint32_t funct3, unsigned dst, uint32_t opcode)
{
    return (funct7 << RV_POS_FUNCT7) | ((uint32_t)rs2 << RV_POS_RS2) |
           ((uint32_t)rs1 << RV_POS_RS1) | (funct3 << RV_POS_FUNCT3) |
           ((uint32_t)dst << RV_POS_RD) | opcode;
}
static inline uint32_t rv_i(int32_t imm, unsigned rs1, uint32_t funct3,
                            unsigned dst, uint32_t opcode)
{
    return ((uint32_t)imm << RV_POS_IMM_I) | ((uint32_t)rs1 << RV_POS_RS1) |
           (funct3 << RV_POS_FUNCT3) | ((uint32_t)dst << RV_POS_RD) | opcode;
}
static inline uint32_t rv_s(int32_t imm, unsigned rs2, unsigned rs1,
                            uint32_t funct3, uint32_t opcode)
{
    return rv_imm_scatter(rv_s_layout(), (uint32_t)imm) |
           ((uint32_t)rs2 << RV_POS_RS2) | ((uint32_t)rs1 << RV_POS_RS1) |
           (funct3 << RV_POS_FUNCT3) | opcode;
}
static inline uint32_t rv_b(int32_t imm, unsigned rs2, unsigned rs1,
                            uint32_t funct3)
{
    return rv_imm_scatter(rv_b_layout(), (uint32_t)imm) |
           ((uint32_t)rs2 << RV_POS_RS2) | ((uint32_t)rs1 << RV_POS_RS1) |
           (funct3 << RV_POS_FUNCT3) | RV_OP_BRANCH;
}
static inline uint32_t rv_u(uint32_t imm20, unsigned dst, uint32_t opcode)
{
    return (imm20 << RV_POS_IMM_U) | ((uint32_t)dst << RV_POS_RD) | opcode;
}

/* J-type immediate of a jal, both ways (used by the fix-up chains) */
static inline uint32_t rv_jal(unsigned dst, int32_t off)
{
    return rv_imm_scatter(rv_j_layout(), (uint32_t)off) |
           ((uint32_t)dst << RV_POS_RD) | RV_OP_JAL;
}
static inline int32_t rv_jal_off(uint32_t insn)
{
    return rv_sign_extend(rv_imm_gather(rv_j_layout(), insn), RV_J_IMM_BITS);
}
static inline unsigned rv_jal_rd(uint32_t insn)
{
    return insn >> RV_POS_RD & RV_REG_MASK;
}

/* RV32I */
static inline uint32_t rv_lui(unsigned dst, uint32_t imm20)
{
    return rv_u(imm20 & RV_IMM20_MASK, dst, RV_OP_LUI);
}
static inline uint32_t rv_auipc(unsigned dst, uint32_t imm20)
{
    return rv_u(imm20 & RV_IMM20_MASK, dst, RV_OP_AUIPC);
}
static inline uint32_t rv_jalr(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, 0, dst, RV_OP_JALR);
}

static inline uint32_t rv_beq(unsigned src1, unsigned src2, int32_t off)
{
    return rv_b(off, src2, src1, RV_F3_BEQ);
}
static inline uint32_t rv_bne(unsigned src1, unsigned src2, int32_t off)
{
    return rv_b(off, src2, src1, RV_F3_BNE);
}
static inline uint32_t rv_blt(unsigned src1, unsigned src2, int32_t off)
{
    return rv_b(off, src2, src1, RV_F3_BLT);
}
static inline uint32_t rv_bge(unsigned src1, unsigned src2, int32_t off)
{
    return rv_b(off, src2, src1, RV_F3_BGE);
}
static inline uint32_t rv_bltu(unsigned src1, unsigned src2, int32_t off)
{
    return rv_b(off, src2, src1, RV_F3_BLTU);
}
static inline uint32_t rv_bgeu(unsigned src1, unsigned src2, int32_t off)
{
    return rv_b(off, src2, src1, RV_F3_BGEU);
}

static inline uint32_t rv_lb(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_BYTE, dst, RV_OP_LOAD);
}
static inline uint32_t rv_lh(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_HALF, dst, RV_OP_LOAD);
}
static inline uint32_t rv_lw(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_WORD, dst, RV_OP_LOAD);
}
static inline uint32_t rv_lbu(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_BYTE_U, dst, RV_OP_LOAD);
}
static inline uint32_t rv_lhu(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_HALF_U, dst, RV_OP_LOAD);
}
static inline uint32_t rv_sb(unsigned rs2, unsigned rs1, int32_t imm)
{
    return rv_s(imm, rs2, rs1, RV_F3_BYTE, RV_OP_STORE);
}
static inline uint32_t rv_sh(unsigned rs2, unsigned rs1, int32_t imm)
{
    return rv_s(imm, rs2, rs1, RV_F3_HALF, RV_OP_STORE);
}
static inline uint32_t rv_sw(unsigned rs2, unsigned rs1, int32_t imm)
{
    return rv_s(imm, rs2, rs1, RV_F3_WORD, RV_OP_STORE);
}

static inline uint32_t rv_addi(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_ADD, dst, RV_OP_IMM);
}
static inline uint32_t rv_slti(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_SLT, dst, RV_OP_IMM);
}
static inline uint32_t rv_sltiu(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_SLTU, dst, RV_OP_IMM);
}
static inline uint32_t rv_xori(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_XOR, dst, RV_OP_IMM);
}
static inline uint32_t rv_ori(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_OR, dst, RV_OP_IMM);
}
static inline uint32_t rv_andi(unsigned dst, unsigned rs1, int32_t imm)
{
    return rv_i(imm, rs1, RV_F3_AND, dst, RV_OP_IMM);
}
/* shifts by an immediate: imm[4:0] is the amount, imm[11:5] is funct7 */
static inline int32_t rv_shift_imm(uint32_t funct7, unsigned shamt)
{
    return (int32_t)((funct7 << (RV_POS_FUNCT7 - RV_POS_IMM_I)) |
                     (shamt & RV_SHAMT_MASK));
}
static inline uint32_t rv_slli(unsigned dst, unsigned rs1, unsigned shamt)
{
    return rv_i(rv_shift_imm(RV_F7_BASE, shamt), rs1, RV_F3_SLL, dst,
                RV_OP_IMM);
}
static inline uint32_t rv_srli(unsigned dst, unsigned rs1, unsigned shamt)
{
    return rv_i(rv_shift_imm(RV_F7_BASE, shamt), rs1, RV_F3_SR, dst, RV_OP_IMM);
}
static inline uint32_t rv_srai(unsigned dst, unsigned rs1, unsigned shamt)
{
    return rv_i(rv_shift_imm(RV_F7_ALT, shamt), rs1, RV_F3_SR, dst, RV_OP_IMM);
}

static inline uint32_t rv_add(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_ADD, dst, RV_OP_REG);
}
static inline uint32_t rv_sub(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_ALT, src2, src1, RV_F3_ADD, dst, RV_OP_REG);
}
static inline uint32_t rv_sll(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_SLL, dst, RV_OP_REG);
}
static inline uint32_t rv_slt(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_SLT, dst, RV_OP_REG);
}
static inline uint32_t rv_sltu(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_SLTU, dst, RV_OP_REG);
}
static inline uint32_t rv_xor(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_XOR, dst, RV_OP_REG);
}
static inline uint32_t rv_srl(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_SR, dst, RV_OP_REG);
}
static inline uint32_t rv_sra(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_ALT, src2, src1, RV_F3_SR, dst, RV_OP_REG);
}
static inline uint32_t rv_or(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_OR, dst, RV_OP_REG);
}
static inline uint32_t rv_and(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_BASE, src2, src1, RV_F3_AND, dst, RV_OP_REG);
}

/* RV32M */
static inline uint32_t rv_mul(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_MULDIV, src2, src1, RV_F3_MUL, dst, RV_OP_REG);
}
static inline uint32_t rv_mulh(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_MULDIV, src2, src1, RV_F3_MULH, dst, RV_OP_REG);
}
static inline uint32_t rv_mulhu(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_MULDIV, src2, src1, RV_F3_MULHU, dst, RV_OP_REG);
}
static inline uint32_t rv_div(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_MULDIV, src2, src1, RV_F3_DIV, dst, RV_OP_REG);
}
static inline uint32_t rv_divu(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_MULDIV, src2, src1, RV_F3_DIVU, dst, RV_OP_REG);
}
static inline uint32_t rv_rem(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_MULDIV, src2, src1, RV_F3_REM, dst, RV_OP_REG);
}
static inline uint32_t rv_remu(unsigned dst, unsigned src1, unsigned src2)
{
    return rv_r(RV_F7_MULDIV, src2, src1, RV_F3_REMU, dst, RV_OP_REG);
}

/* pseudo-instructions */
static inline uint32_t rv_mv(unsigned dst, unsigned src)
{
    return rv_addi(dst, src, 0);
}
static inline uint32_t rv_neg(unsigned dst, unsigned src)
{
    return rv_sub(dst, RV_ZERO, src);
}
static inline uint32_t rv_not(unsigned dst, unsigned src)
{
    return rv_xori(dst, src, -1);
}
static inline uint32_t rv_seqz(unsigned dst, unsigned src)
{
    return rv_sltiu(dst, src, 1);
}
static inline uint32_t rv_snez(unsigned dst, unsigned src)
{
    return rv_sltu(dst, RV_ZERO, src);
}
static inline uint32_t rv_ret(void)
{
    return rv_jalr(RV_ZERO, RV_RA, 0);
}

/* rewrites the offset of an already encoded B-type branch */
static inline uint32_t rv_b_set_off(uint32_t insn, int32_t off)
{
    const uint32_t imm_fields = rv_imm_scatter(rv_b_layout(), ~0U);
    return (insn & ~imm_fields) | rv_imm_scatter(rv_b_layout(), (uint32_t)off);
}

/* offset of an encoded B-type branch */
static inline int32_t rv_b_off(uint32_t insn)
{
    return rv_sign_extend(rv_imm_gather(rv_b_layout(), insn), RV_B_IMM_BITS);
}

static inline int rv_is_branch(uint32_t insn)
{
    return (insn & RV_OPCODE_MASK) == RV_OP_BRANCH;
}

/* inverse of a branch condition (bit 0 of funct3 flips beq/bne, blt/bge,
 * bltu/bgeu) */
static inline uint32_t rv_branch_invert(uint32_t insn)
{
    return insn ^ (1U << RV_POS_FUNCT3);
}

#endif /* RV32_H_INCLUDED */
