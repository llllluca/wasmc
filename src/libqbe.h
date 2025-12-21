#ifndef LIBQBE_H_INCLUDED
#define LIBQBE_H_INCLUDED

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include "adlist.h"
#include "rv32/rv32i.h"

typedef struct Phi Phi;
typedef struct Ins Ins;
typedef struct Use Use;
typedef struct Blk Blk;
typedef struct Tmp Tmp;

typedef enum __attribute__ ((__packed__)) IRType {
    IR_TYPE_NONE,
    IR_TYPE_INT32,
    IR_TYPE_INT64,
    IR_TYPE_F32,
    IR_TYPE_F64,
} IRType;

typedef struct location {
    location_type type;
    union {
        rv32_reg reg;
        unsigned int stack_slot;
    } as;
} location;

#define LOCATION_EQ(a, b) ( \
    ((a).type == LOCATION_NONE && (b).type == LOCATION_NONE) || \
    ((a).type == REGISTER && (b).type == REGISTER && \
     (a).as.reg == (b).as.reg) || \
    ((a).type == STACK_SLOT && (b).type == STACK_SLOT && \
     (a).as.stack_slot == (b).as.stack_slot))

typedef struct live_interval {
    unsigned int start;
    unsigned int end;
    location assign;
    rv32_reg register_hint;
} live_interval;

struct Tmp {
    list *use_list;
    live_interval *i;
};

typedef struct Ref {
    enum {
        REF_TYPE_UNDEFINED,
        REF_TYPE_TMP,
        REF_TYPE_INT32_CONST,
        REF_TYPE_NAME,
        REF_TYPE_LOCATION,
    } type;
    union {
        listNode *tmp_node;
        int32_t int32_const;
        char *name;
        location loc;
    } as;
} Ref;

#define UNDEFINED_REF \
    (Ref){ .type = REF_TYPE_UNDEFINED, }

#define INT32_CONST(val) \
    (Ref){ .type = REF_TYPE_INT32_CONST, .as.int32_const = (val) }

#define NEW_NAME(val) \
    (Ref){ .type = REF_TYPE_NAME, .as.name = (val) }

#define REF_EQ(a, b) \
    (((a).type == REF_TYPE_UNDEFINED && (b).type == REF_TYPE_UNDEFINED) || \
    ((a).type == REF_TYPE_TMP && (b).type == REF_TYPE_TMP && \
        (a).as.tmp_node == (b).as.tmp_node) || \
    ((a).type == REF_TYPE_INT32_CONST && (b).type == REF_TYPE_INT32_CONST && \
        (a).as.int32_const == (b).as.int32_const) || \
    ((a).type == REF_TYPE_NAME && (b).type == REF_TYPE_NAME && \
        (a).as.name == (b).as.name) || \
    ((a).type == REF_TYPE_LOCATION && (b).type == REF_TYPE_LOCATION && \
        LOCATION_EQ((a).as.loc, (b).as.loc)))

typedef enum Jump_type {
    NONE_JUMP_TYPE,
    JMP_JUMP_TYPE,
    JNZ_JUMP_TYPE,
    RET0_JUMP_TYPE,
    RET1_JUMP_TYPE,
    HALT_JUMP_TYPE
} Jump_type;

typedef struct Jump {
    Jump_type type;
    Ref arg;
    unsigned int id;
} Jump;

struct Blk {
    list *phi_list;
    list *ins_list;
    Jump jmp;
    Blk *succ[2];
    list *preds;
    Ref *locals;
    listNode **incomplete_phis;
    bool is_sealed;
/* list of variable that are live at the beginning of the block */
    list *live_in;
    unsigned int id;

    bool is_loop_header;
    list* loop_end_blk_list;

    uint8_t *text_start;
};

typedef struct Phi_arg {
    Ref r;
    Blk *b;
} Phi_arg;

struct Phi {
    Ref to;
    list *phi_arg_list;
    IRType type;
    Blk *block;
};

typedef struct Fn {
    list *tmp_list;
    list *blk_list;
    char name[16];
    Blk *start;
    IRType ret_type;
    unsigned int num_stack_slots;
} Fn;

typedef enum IROpcode {
    /* Arithmetic and Bits */
    IR_OPCODE_ADD,
    IR_OPCODE_AND,
    IR_OPCODE_DIV,
    IR_OPCODE_MUL,
    IR_OPCODE_OR,
    IR_OPCODE_REM,
    IR_OPCODE_SAR,
    IR_OPCODE_SHL,
    IR_OPCODE_SHR,
    IR_OPCODE_SUB,
    IR_OPCODE_UDIV,
    IR_OPCODE_UREM,
    IR_OPCODE_XOR,

    /* Comparisons */
    IR_OPCODE_CEQZI32,
    IR_OPCODE_CEQI32,
    IR_OPCODE_CNEI32,
    IR_OPCODE_CSLTI32,
    IR_OPCODE_CULTI32,
    IR_OPCODE_CSGTI32,
    IR_OPCODE_CUGTI32,
    IR_OPCODE_CSLEI32,
    IR_OPCODE_CULEI32,
    IR_OPCODE_CSGEI32,
    IR_OPCODE_CUGEI32,

    /* Memory */
    IR_OPCODE_STORE32,
    IR_OPCODE_LOAD32,
    IR_OPCODE_LOADU8,
    IR_OPCODE_STORE8,

    /* Conversions */
    EXTSW_INSTR,

    /* Other */
    CALL_INSTR,
    PAR_INSTR,
    ARG_INSTR,
    COPY_INSTR,
    PUSH_INSTR,
    POP_INSTR,

    } IROpcode;

struct Ins {
    IROpcode op;
    IRType type;
    Ref to;
    Ref arg[2];
    unsigned int id;
};

typedef enum Use_type {
    UPhi,
    UIns,
    UJmp,
    ULocal,
} Use_type;

typedef union Use_ptr {
    listNode *ins;
    listNode *phi;
    listNode *blk;
    Ref *local; 
} Use_ptr;

struct Use {
    Use_type type;
    Use_ptr u;
};

/* Arithmetic and Bits */
#define ADD(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), IR_OPCODE_ADD, (ref1), (ref2))

#define SUB(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), SUB_INSTR, (ref1), (ref2))

#define NEG(b, temp, kind, ref) \
    instr((b), (temp), (kind), NEG_INSTR, (ref1), UNDEFINED_REF)

#define DIV(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), DIV_INSTR, (ref1), (ref2))

#define REM(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), REM_INSTR, (ref1), (ref2))

#define UDIV(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), UDIV_INSTR, (ref1), (ref2))

#define UREM(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), UREM_INSTR, (ref1), (ref2))

#define MUL(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), MUL_INSTR, (ref1), (ref2))

#define AND(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), AND_INSTR, (ref1), (ref2))

#define OR(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), OR_INSTR, (ref1), (ref2))

#define XOR(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), XOR_INSTR, (ref1), (ref2))

#define SAR(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), SAR_INSTR, (ref1), (ref2))

#define SHR(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), SHR_INSTR, (ref1), (ref2))

#define SHL(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), SHL_INSTR, (ref1), (ref2))

/* Comparisons */
#define EQZW(b, temp, ref1) \
    instr((b), (temp), WORD_TYPE, EQZW_INSTR, (ref1), UNDEFINED_REF)

#define CNEW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CNEW_INSTR, (ref1), (ref2))

#define CSLTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CSLTW_INSTR, (ref1), (ref2))

#define CULTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CULTW_INSTR, (ref1), (ref2))

/* Memory */
#define STOREI32(b, fromRef, offset, toRef) \
    instr((b), (fromRef), IR_TYPE_INT32, IR_OPCODE_STORE32, (offset), (toRef))

#define LOADI32(b, toRef, offset, fromRef) \
    instr((b), (toRef), IR_TYPE_INT32, IR_OPCODE_LOAD32, (offset), (fromRef))

/* Conversions */
#define EXTSW(b, temp, ref) \
    instr((b), (temp), LONG_TYPE, EXTSW_INSTR, (ref), UNDEFINED_REF)

/* Cast and Copy */
#define COPY(b, temp, kind, ref1) \
    instr((b), (temp), (kind), COPY_INSTR, (ref1), UNDEFINED_REF)

/* Call */
#define FUNC_CALL(b, temp, kind, addrRef) \
    instr((b), (temp), kind, CALL_INSTR, (addrRef), UNDEFINED_REF)

#define VOID_FUNC_CALL(b, addrRef) \
    instr((b), UNDEFINED_REF, IR_TYPE_INT32, CALL_INSTR, (addrRef), UNDEFINED_REF)

/* libqbe.c */
void printfn(Fn *fn, FILE *f);
Ref newMemoryAddr(Fn *f, Blk *b);
Fn *newFunc(IRType ret_type, char *name, Blk *start);
Ref newFuncParam(Fn *f, IRType type);
Ref newTemp(Fn *func);
Blk *newBlock(uint32_t nlocals);
void newFuncCallArg(Blk *b, IRType type, Ref r);
void instr(Blk *b, Ref r, IRType type, IROpcode op, Ref arg1, Ref arg2);
void jmp(Fn *f, Blk *from, Blk *to);
void jnz(Fn *f, Blk *from, Ref r, Blk *b0, Blk *b1);
void ret(Fn *f, Blk *b);
void retRef(Fn *f, Blk *b, Ref r);
void halt(Fn *f, Blk *b);
void optimizeFunc(Fn *fn);
void typecheck(Fn *fn);
listNode *newPhi(Blk *b, Ref temp, IRType type);
void phiAppendOperand(listNode *phi_node, Blk *b, Ref arg);
void addUsage(Tmp *tmp, Use_type type, Use_ptr ptr);
void rmUsage(Tmp *tmp, Use_type type, Use_ptr ptr);
void freeFunc(Fn *f);
void freeTemp(Tmp *tmp);
void freePhi(Phi *phi);
void freeBlock(Blk *b);

#endif 
