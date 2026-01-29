#ifndef RV32I_H_INCLUDED
#define RV32I_H_INCLUDED

#include <inttypes.h>
#include <stdbool.h>

typedef enum {
    ZERO = 0,
    RA  = 1,
    SP  = 2,
    GP  = 3,
    TP  = 4,
    T0  = 5,
    T1  = 6,
    T2  = 7,
    FP  = 8,
    S1  = 9,
    A0  = 10,
    A1  = 11,
    A2  = 12,
    A3  = 13,
    A4  = 14,
    A5  = 15,
    A6  = 16,
    A7  = 17,
    S2  = 18,
    S3  = 19,
    S4  = 20,
    S5  = 21,
    S6  = 22,
    S7  = 23,
    S8  = 24,
    S9  = 25,
    S10 = 26,
    S11 = 27,
    T3  = 28,
    T4  = 29,
    T5  = 30,
    T6  = 31,
    RV32_NUM_REG
} rv32_reg;

extern const bool rv32_is_callee_saved[RV32_NUM_REG];

#define RV32_GP_NUM_REG 16
extern const rv32_reg rv32_gp_reg[RV32_GP_NUM_REG];

#define RV32_ARG_NUM_REG 8
extern const rv32_reg rv32_arg_reg[RV32_ARG_NUM_REG];

#define RV32_PRIVATE_REG0 T5
#define RV32_PRIVATE_REG1 T6
#define RV32_RESERVED_NUM_REG 2
extern const rv32_reg rv32_priv_reg[RV32_RESERVED_NUM_REG];

typedef struct r_ins_fmt {
    unsigned int op:7;
    unsigned int rd:5;
    unsigned int func3:3;
    unsigned int rs1:5;
    unsigned int rs2:5;
    unsigned int func7:7;
} r_ins_fmt;

typedef struct i_ins_fmt {
    unsigned int op:7;
    unsigned int rd:5;
    unsigned int func3:3;
    unsigned int rs1:5;
    unsigned int imm:12;
} i_ins_fmt;

typedef struct s_ins_fmt {
    unsigned int op:7;
    unsigned int imm1:5;
    unsigned int func3:3;
    unsigned int rs1:5;
    unsigned int rs2:5;
    unsigned int imm2:7;
} s_ins_fmt;

typedef struct b_ins_fmt {
    unsigned int op:7;
    unsigned int imm_11:1;
    unsigned int imm_4_1:4;
    unsigned int func3:3;
    unsigned int rs1:5;
    unsigned int rs2:5;
    unsigned int imm_10_5:6;
    unsigned int imm_12:1;
} b_ins_fmt;

typedef struct u_ins_fmt {
    unsigned int op:7;
    unsigned int rd:5;
    unsigned int imm:20;
} u_ins_fmt;

typedef struct j_ins_fmt {
    unsigned int op:7;
    unsigned int rd:5;
    unsigned int imm_19_12:8;
    unsigned int imm_11:1;
    unsigned int imm_10_1:10;
    unsigned int imm_20:1;
} j_ins_fmt;

typedef struct ins_fmt {
    enum {
        R_INS_FMT,
        I_INS_FMT,
        S_INS_FMT,
        B_INS_FMT,
        U_INS_FMT,
        J_INS_FMT,
    } kind;
    union {
        r_ins_fmt r;
        i_ins_fmt i;
        s_ins_fmt s;
        b_ins_fmt b;
        u_ins_fmt u;
        j_ins_fmt j;
        uint32_t u32;
    } as;

} ins_fmt;

#define R_TYPE_INS_TEMPLATE(OPCODE, FUNC3, FUNC7, RD, RS1, RS2)  \
    (ins_fmt) {                                                  \
        .kind = R_INS_FMT,                                       \
        .as.r = {                                                \
            .op = (OPCODE),                                      \
            .rd = (RD),                                          \
            .func3 = (FUNC3),                                    \
            .rs1 = (RS1),                                        \
            .rs2 = (RS2),                                        \
            .func7 = (FUNC7),                                    \
        }                                                        \
    }

#define I_TYPE_INS_TEMPLATE(OPCODE, FUNC3, RD, RS1, IMM)  \
    (ins_fmt) {                                           \
        .kind = I_INS_FMT,                                \
        .as.i = {                                         \
            .op = (OPCODE),                               \
            .rd = (RD),                                   \
            .func3 = (FUNC3),                             \
            .rs1 = (RS1),                                 \
            .imm = (IMM),                                 \
        }                                                 \
    }

#define S_TYPE_INS_TEMPLATE(OPCODE, FUNC3, RS2, IMM, RS1)   \
    (ins_fmt) {                                             \
        .kind = S_INS_FMT,                                  \
        .as.s = {                                           \
            .op = (OPCODE),                                 \
            .imm1 = ((IMM) & 0x1f),                         \
            .func3 = (FUNC3),                               \
            .rs1 = (RS1),                                   \
            .rs2 = (RS2),                                   \
            .imm2 = (((IMM) & 0xfe0) >> 5),                 \
        }                                                   \
    }

#define U_TYPE_INS_TEMPLATE(OPCODE, RD, IMM)  \
    (ins_fmt) {                               \
        .kind = U_INS_FMT,                    \
        .as.u = {                             \
            .op = (OPCODE),                   \
            .rd = (RD),                       \
            .imm = (IMM),                     \
        }                                     \
    }

#define B_TYPE_INS_TEMPLATE(OPCODE, IMM_11, IMM_4_1, FUNC3, RS1, RS2, IMM_10_5, IMM_12) \
    (ins_fmt) {                                                                         \
        .kind = B_INS_FMT,                                                              \
        .as.b = {                                                                       \
            .op = (OPCODE),                                                             \
            .imm_11 = (IMM_11),                                                         \
            .imm_4_1 = (IMM_4_1),                                                       \
            .func3 = (FUNC3),                                                           \
            .rs1 = (RS1),                                                               \
            .rs2 = (RS2),                                                               \
            .imm_10_5 = (IMM_10_5),                                                     \
            .imm_12 = (IMM_12),                                                         \
        }                                                                               \
    }

#define J_TYPE_INS_TEMPLATE(OPCODE, RD, IMM_19_12, IMM_11, IMM_10_1, IMM_20) \
    (ins_fmt) {                                                              \
        .kind = J_INS_FMT,                                                   \
        .as.j = {                                                            \
            .op = (OPCODE),                                                  \
            .rd = (RD),                                                      \
            .imm_19_12 = (IMM_19_12),                                        \
            .imm_11 = (IMM_11),                                              \
            .imm_10_1 = (IMM_10_1),                                          \
            .imm_20 = (IMM_20),                                              \
        }                                                                    \
    }



/* ----- Integer Register-Immediate Instructions ----- */

#define IMM_OPCODE 0x13

/* Instr: addi
 * Description: add immediate
 * Use: addi rd, rs1, imm
 * Result: rd = rs1 + imm */
#define ADDI_FUNC3  0x0
#define RV32_ADDI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, ADDI_FUNC3, RD, RS1, IMM)

/* Instr: nop
 * Description: no operation (pseudoinstruction)
 * Use: nop
 * Result: - */
#define RV32_NOP RV32_ADDI(ZERO, ZERO, 0)

/* Instr: mv
 * Description: copy register (pseudoinstruction)
 * Use: mv rd, rs1
 * Result rd = rs1 */
#define RV32_MV(RD, RS1) RV32_ADDI(RD, RS1, 0)

/* Instr: subi
 * Description: sub immediate (pseudoinstruction)
 * Use: subi rd, rs1, imm
 * Result rd = rs1 - imm */
#define RV32_SUBI(RD, RS1, IMM) RV32_ADDI(RD, RS1, -1 * IMM)

/* Instr: slli
 * Description: shift left logical immediate
 * Use: slli rd, rs1, imm
 * Result: rd = rs1 << imm */
#define SLLI_FUNC3 0x1
#define RV32_SLLI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, SLLI_FUNC3, RD, RS1, IMM & 0x1f)

/* Instr: slti
 * Description: set less than immediate
 * Use: slti rd, rs1, imm
 * Result: rd = (rs1 < imm) */
#define SLTI_FUNC3  0x2
#define RV32_SLTI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, SLTI_FUNC3, RD, RS1, IMM)

/* Instr: sltiu
 * Description: set less than immediate unsigned
 * Use: sltui rd, rs1, imm
 * Result: rd = (rs1 < imm) */
#define SLTIU_FUNC3 0x3
#define RV32_SLTIU(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, SLTIU_FUNC3, RD, RS1, IMM)

/* Instr: seqz
 * Description: set equal zero (pseudoinstruction)
 * Use: seqz rd, rs1
 * Result: rd = (rs1 == 0) */
#define RV32_SEQZ(RD, RS1) RV32_SLTIU(RD, RS1, 1)

/* Insr: xori
 * Description: xor immediate
 * Use: xori rd, rs1, imm
 * Result: rd = rs1 ^ imm */
#define XORI_FUNC3  0x4
#define RV32_XORI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, XORI_FUNC3, RD, RS1, IMM)

/* Instr: not
 * Description: not (pseudoinstruction)
 * Use: not rd, rs1
 * Result: rd = ~rs1 */
#define RV32_NOT(RD, RS1) RV32_XORI(RD, RS1, -1)

/* Instr: srli
 * Description: shift right logical immediate
 * Use: srli rd, rs1, imm
 * Result: rd = rs1 >> imm */
#define SRLI_FUNC3 0x5
#define RV32_SRLI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, SRLI_FUNC3, RD, RS1, IMM & 0x1f)

/* Instr: srai
 * Description: shift right arithmetic immediate
 * Use: srai rd, rs1, imm
 * Result: rd = rs1 >>> imm */
#define SRAI_FUNC3 0x5
#define RV32_SRAI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, SRLI_FUNC3, RD, RS1, (IMM & 0x1f) | 0x400)

/* Instr: ori
 * Description or immediate
 * Use: ori rd, rs1, imm
 * Result: rd = rs1 | imm */
#define ORI_FUNC3   0x6
#define RV32_ORI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, ORI_FUNC3, RD, RS1, IMM)

/* Instr: andi
 * Description: and immediate
 * Use: andi rd, rs1, imm
 * Result: rd = rs1 & imm */
#define ANDI_FUNC3  0x7
#define RV32_ANDI(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(IMM_OPCODE, ANDI_FUNC3, RD, RS1, IMM)

/* Instr: lui
 * Description: load upper immediate
 * Use: lui rd, imm
 * Result: rd = imm << 12 */
#define LUI_OPCODE 0x37
#define RV32_LUI(RD, IMM) \
    U_TYPE_INS_TEMPLATE(LUI_OPCODE, RD, IMM)

/* Instr: auipc
 * Description: add upper immediate to pc
 * Use: auipc rd, imm
 * Result: rd = pc + (imm << 12) */
#define AUIPC_OPCODE 0x17
#define RV32_AUIPC(RD, IMM) \
    U_TYPE_INS_TEMPLATE(AUIPC_OPCODE, RD, IMM)

/* ----- Integer Register-Register Instructions ----- */

#define REG_OPCODE 0x33
/* Instr: add
 * Description: add
 * Use: add rd, rs1, rs2
 * Result: rd = rs1 + rs2 */
#define ADD_FUNC3 0x0
#define ADD_FUNC7 0x0
#define RV32_ADD(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, ADD_FUNC3, ADD_FUNC7, RD, RS1, RS2)

/* Instr: sub
 * Description: subtract
 * Use: sub rd, rs1, rs2
 * Result: rd = rs1 - rs2 */
#define SUB_FUNC3 0x0
#define SUB_FUNC7 0x20
#define RV32_SUB(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, SUB_FUNC3, SUB_FUNC7, RD, RS1, RS2)

/* Instr: sll
 * Description: shift left logical
 * Use: sll rd, rs1, rs2
 * Result: rd = rs1 << rs2 */
#define SLL_FUNC3 0x1
#define SLL_FUNC7 0x0
#define RV32_SLL(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, SLL_FUNC3, SLL_FUNC7, RD, RS1, RS2)

/* Instr: slt
 * Description: set less than
 * Use: slt rd, rs1, rs2
 * Result: rd = (rs1 < rs2) */
#define SLT_FUNC3 0x2
#define SLT_FUNC7 0x0
#define RV32_SLT(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, SLT_FUNC3, SLT_FUNC7, RD, RS1, RS2)

/* Instr: sltu
 * Description: set less than unsigned
 * Use: sltu rd, rs1, rs2
 * Result: rd = (rs1 < rs2) */
#define SLTU_FUNC3 0x3
#define SLTU_FUNC7 0x0
#define RV32_SLTU(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, SLTU_FUNC3, SLTU_FUNC7, RD, RS1, RS2)

/* Instr: snez
 * Description: set not equal zero (pseudoinstruction)
 * Use: snez rd, rs1
 * Result: rd = (rs1 != 0) */
#define RV32_SNEZ(RD, RS1) RV32_SLTU(RD, ZERO, RS1)

/* Instr: ltz
 * Description: set less than zero (psuedoinstruction)
 * Use: sltz rd, rs1
 * Result: rd = (rs1 < 0) */
#define RV32_SLTZ(RD, RS1) RV32_SLTU(RD, RS1, ZERO)

/* Instr: sgtz
 * Description: set greater than zero (psuedoinstruction)
 * Use: sgtz rd, rs1
 * Result: rd = (rs1 > 0) */
#define RV32_SGTZ(RD, RS1) RV32_SLTU(RD, ZERO, RS1)

/* Instr: xor
 * Description: xor
 * Use: xor rd, rs1, rs2
 * Result: rd = rs1 ^ rs2 */
#define XOR_FUNC3 0x4
#define XOR_FUNC7 0x0
#define RV32_XOR(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, XOR_FUNC3, XOR_FUNC7, RD, RS1, RS2)

/* Instr: srl
 * Description: shift right logical
 * Use: srl rd, rs1, rs2
 * Result: rd = rs1 >> rs2 */
#define SRL_FUNC3 0x5
#define SRL_FUNC7 0x0
#define RV32_SRL(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, SRL_FUNC3, SRL_FUNC7, RD, RS1, RS2)

/* Instr: sra
 * Description: shift right arithmetic
 * Use: sra rd, rs1, rs2
 * Result: rd = rs1 >>> rs2 */
#define SRA_FUNC3 0x5
#define SRA_FUNC7 0x20
#define RV32_SRA(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, SRA_FUNC3, SRA_FUNC7, RD, RS1, RS2)

/* Instr: or
 * Description: or
 * Use: or rd, rs1, rs2
 * Result: rd = rs1 | rs2 */
#define OR_FUNC3 0x6
#define OR_FUNC7 0x0
#define RV32_OR(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, OR_FUNC3, OR_FUNC7, RD, RS1, RS2)

/* Instr: and
 * Description: and
 * Use: and rd, rs1, rs2
 * Result: rd = rs1 & rs2 */
#define AND_FUNC3 0x7
#define AND_FUNC7 0x0
#define RV32_AND(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, AND_FUNC3, AND_FUNC7, RD, RS1, RS2)

/* ----- Control Transfer Instructions, Unconditional Jumps ----- */

/* Instr: jal
 * Description: jump and link
 * Use: jal rd, imm
 * Result: rd = pc+4; pc += imm */
#define JAL_OPCODE 0x6F
#define RV32_JAL(RD, IMM) \
    J_TYPE_INS_TEMPLATE(JAL_OPCODE, RD, ((IMM) >> 12) & 0xFF, ((IMM) >> 11) & 0x1, ((IMM) >> 1) & 0x3FF, ((IMM) >> 20) & 0x1)

/* Instr: j
 * Description: Jump (pseudoinstruction)
 * Use: j imm
 * Result: pc += imm */
#define RV32_J(IMM) RV32_JAL(ZERO, IMM)

/* Instr: jalr
 * Description: jump and link register
 * Use: jalr rd, rs1, imm
 * Result: rd = pc+4; pc = rs1+imm */
#define JARL_OPCODE 0x67
#define JARL_FUNC3 0x0
#define RV32_JARL(RD, RS1, IMM) \
    I_TYPE_INS_TEMPLATE(JARL_OPCODE, JARL_FUNC3, RD, RS1, IMM)

/* Instr: ret
 * Description: return from function (pseudoinstruction)
 * Use: ret
 * Result: pc = ra */
#define RV32_RET RV32_JARL(ZERO, RA, 0)

/* ----- Control Transfer Instructions, Conditional Jumps ----- */

#define RV32_BRANCH_OPCODE 0x63

/* Instr: beq
 * Description: branch equal
 * Use: beq rs1, rs2, imm
 * Result: if(rs1 == rs2) pc += imm */
#define BEQ_FUNC3 0x0
#define RV32_BEQ(RS1, RS2, IMM) \
    B_TYPE_INS_TEMPLATE(RV32_BRANCH_OPCODE, ((IMM) >> 11) & 0x1, ((IMM) >> 1) & 0xF, BEQ_FUNC3, RS1, RS2, ((IMM) >> 5) & 0x3F, ((IMM) >> 12) & 0x1)

/* Instr: beqz
 * Description: branch equal zero (pseudoinstruction)
 * Use: beqz rs1, imm
 * Result: if(rs1 == 0) pc += imm */
#define RV32_BEQZ(RS1, IMM) RV32_BEQ(RS1, ZERO, IMM)

/* Instr: bne
 * Description: branch not equal
 * Use: bne rs1, rs2, imm
 * Result: if(rs1 != rs2) pc += imm */
#define BNE_FUNC3 0x1
#define RV32_BNE(RS1, RS2, IMM) \
    B_TYPE_INS_TEMPLATE(RV32_BRANCH_OPCODE, ((IMM) >> 11) & 0x1, ((IMM) >> 1) & 0xF, BNE_FUNC3, RS1, RS2, ((IMM) >> 5) & 0x3F, ((IMM) >> 12) & 0x1)

/* Instr: bnez
 * Description: branch not equal zero (psuedoinstruction)
 * Use: bnez rs1, rs2, imm
 * Result: if(rs1 != 0) pc += imm */
#define RV32_BNEZ(RS1, IMM) RV32_BNE(RS1, ZERO, IMM)

/* Instr: blt
 * Description: branch less than
 * Use: blt rs1, rs2, imm
 * Result: if(rs1 < rs2) pc += imm */
#define BLT_FUNC3 0x4
#define RV32_BLT(RS1, RS2, IMM) \
    B_TYPE_INS_TEMPLATE(RV32_BRANCH_OPCODE, ((IMM) >> 11) & 0x1, ((IMM) >> 1) & 0xF, BLT_FUNC3, RS1, RS2, ((IMM) >> 5) & 0x3F, ((IMM) >> 12) & 0x1)

/* Instr: bgt
 * Description: branch greater than (pseudoinstruction)
 * Use: bgt rs1, rs2, imm
 * Result: if(rs1 > rs2) pc += imm */
#define RV32_BGT(RS1, RS2, IMM) RV32_BLT(RS2, RS1, IMM)

/* Instr: bge
 * Description: branch greater or equal
 * Use: bge rs1, rs2, imm
 * Result: if(rs1 >= rs2) pc += imm */
#define BGE_FUNC3 0x5
#define RV32_BGE(RS1, RS2, IMM) \
    B_TYPE_INS_TEMPLATE(RV32_BRANCH_OPCODE, ((IMM) >> 11) & 0x1, ((IMM) >> 1) & 0xF, BGE_FUNC3, RS1, RS2, ((IMM) >> 5) & 0x3F, ((IMM) >> 12) & 0x1)

/* Instr: ble
 * Description: branch less or equal (psuedoinstruction)
 * Use: ble rs1, rs2, imm
 * Result: if(rs1 <= rs2) pc += imm */
#define RV32_BLE(RS1, RS2, IMM) RV32_BGE(RS2, RS1, IMM)

/* Instr: bltu
 * Description: branch less than unsigned
 * Use: bltu rs1, rs2, imm
 * Result: if(rs1 < rs2) pc += imm */
#define BLTU_FUNC3 0x6
#define RV32_BLTU(RS1, RS2, IMM) \
    B_TYPE_INS_TEMPLATE(RV32_BRANCH_OPCODE, ((IMM) >> 11) & 0x1, ((IMM) >> 1) & 0xF, BLTU_FUNC3, RS1, RS2, ((IMM) >> 5) & 0x3F, ((IMM) >> 12) & 0x1)

/* Instr: bltz
 * Description: branch less than zero (pseudoinstructoin)
 * Use: bltz rs1, imm
 * Result: if(rs1 < 0) pc += imm */
#define RV32_BLTZ(RS1, IMM) RV32_BLT(RS1, ZERO, IMM)

/* Instr: bgtu
 * Description: branch greater than unsigned (psuedoinstruction)
 * Use: bgtu rs1, rs2, imm
 * Result: if(rs1 > rs2) pc += imm */
#define RV32_BGTU(RS1, RS2, IMM) RV32_BLTU(RS2, RS1, IMM)

/* Instr: gtz
 * Description: branch greater than zero (pseudoinstruction)
 * Use: bgtz rs1, imm
 * Result: if(rs1 > 0) pc += imm */
#define RV32_GTZ(RS1, IMM) RV32_BGT(RS1, ZERO, IMM)

/* Instr: bgeu
 * Description: branch greater or equal unsigned
 * Use: bgeu rs1, rs2, imm
 * Result: if(rs1 >= rs2) pc += imm */
#define BGEU_FUNC3 0x7
#define RV32_BGEU(RS1, RS2, IMM) \
    B_TYPE_INS_TEMPLATE(RV32_BRANCH_OPCODE, ((IMM) >> 11) & 0x1, ((IMM) >> 1) & 0xF, BGEU_FUNC3, RS1, RS2, ((IMM) >> 5) & 0x3F, ((IMM) >> 12) & 0x1)

/* Instr: bleu
 * Description: branch less or equal unsigned (pseudoinstruction)
 * Use: bleu rs1, rs2, imm
 * Result: if(rs1 <= rs2) pc += imm */
#define RV32_BLEU(RS1, RS2, IMM) RV32_BGE(RS2, RS1, IMM)

/* Instr: blez
 * Description: branch less or equal zero (pseudoinstruction)
 * Use: blez rs1, imm
 * Result: if(rs1 <= 0) pc += imm */
#define RV32_BLEZ(RS1, IMM) RV32_BLE(RS1, ZERO, IMM)

/* Instr: bgez
 * Description: branch greater or equal zero (pseudoinstruction)
 * Use: bgez rs1, imm
 * Result: if(rs1 >= 0) pc += imm */
#define RV32_BGEZ(RS1, IMM) RV32_BGE(RS1, ZERO, IMM)

/* ----- Load and Store Instructions ----- */

#define LOAD_OPCODE 0x3
#define STORE_OPCODE 0x23

/* Instr: lb
 * Description: load byte
 * Use: lb rd, imm(rs1)
 * Result: rd = mem[rs1+imm][0:7] */
#define LB_FUNC3 0x0
#define RV32_LB(RD, IMM, RS1) \
    I_TYPE_INS_TEMPLATE(LOAD_OPCODE, LB_FUNC3, RD, RS1, IMM)

/* Instr: lw
 * Description: load word
 * Use: lw rd, imm(rs1)
 * Result: rd = mem[rs1+imm] */
#define LW_FUNC3 0x2
#define RV32_LW(RD, IMM, RS1) \
    I_TYPE_INS_TEMPLATE(LOAD_OPCODE, LW_FUNC3, RD, RS1, IMM)

/* Instr: lbu
 * Description: load byte unsigned
 * Use: lbu rd, imm(rs1)
 * Result: rd = mem[rs1+imm][0:7] */
#define LBU_FUNC3 0x4
#define RV32_LBU(RD, IMM, RS1) \
    I_TYPE_INS_TEMPLATE(LOAD_OPCODE, LW_FUNC3, RD, RS1, IMM)


/* Instr: sw
 * Description: store word
 * Use: sw rs2, imm(rs1)
 * Result: mem[rs1+imm] = rs2 */
#define SW_FUNC3 0x2
#define RV32_SW(RS2, IMM, RS1) \
    S_TYPE_INS_TEMPLATE(STORE_OPCODE, SW_FUNC3, RS2, IMM, RS1)

/* Instr: sb
 * Description: store byte
 * Use: sb rs2, imm(rs1)
 * Result: mem[rs1+imm][0:7] = rs2 */
#define SB_FUNC3 0x0
#define RV32_SB(RS2, IMM, RS1) \
    S_TYPE_INS_TEMPLATE(STORE_OPCODE, SB_FUNC3, RS2, IMM, RS1)

/* ----- Environment Call and Breakpoints ----- */

#define SYSTEM_OPCODE 0x73

/* Instr: ebreak
 * Description: environment break (debugger call)
 * Use: ebreak
 * Result: - */
#define EBREAK_IMM 0x1
#define EBREAK_FUNC3 0x0
#define RV32_EBREAK \
    I_TYPE_INS_TEMPLATE(SYSTEM_OPCODE, EBREAK_FUNC3, 0, 0, EBREAK_IMM)

/* ----- M Extension for Integer Multiplication and Division ----- */

#define MULDIV_FUNC7 0x1

/* Instr: div
 * Description: Divide
 * Use: div rd, rs1, rs2
 * Result: rd = rs1 / rs2 */
#define DIV_FUNC3 0x4
#define RV32_DIV(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, DIV_FUNC3, MULDIV_FUNC7, RD, RS1, RS2)

/* Instr: divu
 * Description: divide unsigned
 * Use: divu rd, rs1, rs2
 * Result: rd = rs1 / rs2 */
#define DIVU_FUNC3 0x5
#define RV32_DIVU(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, DIVU_FUNC3, MULDIV_FUNC7, RD, RS1, RS2)

/* Instr: rem
 * Description: remainder
 * Use: rem rd, rs1, rs2
 * Result: rd = rs1 % rs2 */
#define REM_FUNC3 0x6
#define RV32_REM(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, REM_FUNC3, MULDIV_FUNC7, RD, RS1, RS2)

/* Instr: remu
 * Description: remainder unsigned
 * Use: remu rd, rs1, rs2
 * Result: rd = rs1 % rs2 */
#define REMU_FUNC3 0x7
#define RV32_REMU(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, REMU_FUNC3, MULDIV_FUNC7, RD, RS1, RS2)

/* Instr: mul
 * Description: Multiply
 * Use: mul rd, rs1, rs2
 * Result: rd = (rs1 * rs2)[31:0] */
#define MUL_FUNC3 0x0
#define RV32_MUL(RD, RS1, RS2) \
    R_TYPE_INS_TEMPLATE(REG_OPCODE, MUL_FUNC3, MULDIV_FUNC7, RD, RS1, RS2)





#endif
