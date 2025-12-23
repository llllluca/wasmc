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
    IR_TYPE_VOID,
    IR_TYPE_I32,
    IR_TYPE_I64,
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

/* Integer Arithmetic Operations */
    IR_OPCODE_ADD,
    IR_OPCODE_SUB,
    IR_OPCODE_MUL,
    IR_OPCODE_SDIV,
    IR_OPCODE_UDIV,
    IR_OPCODE_SREM,
    IR_OPCODE_UREM,

/* Bitwise Operations */
    IR_OPCODE_AND,
    IR_OPCODE_OR,
    IR_OPCODE_XOR,
    IR_OPCODE_SHL,
    IR_OPCODE_LSHR,
    IR_OPCODE_ASHR,

/* Comparison Operations */
    IR_OPCODE_EQZ,
    IR_OPCODE_EQ,
    IR_OPCODE_NE,
    IR_OPCODE_SLT,
    IR_OPCODE_ULT,
    IR_OPCODE_SGT,
    IR_OPCODE_UGT,
    IR_OPCODE_SLE,
    IR_OPCODE_ULE,
    IR_OPCODE_SGE,
    IR_OPCODE_UGE,

/* Memory Operations */
/* store <type> <value>, <offset>, <ptr> */
    IR_OPCODE_STORE,
/* <result> = load <type> <offset>, <ptr> */
    IR_OPCODE_LOAD,
    IR_OPCODE_ULOAD8,
    IR_OPCODE_STORE8,

/* Function definitions and calls */
    IR_OPCODE_CALL,
    IR_OPCODE_PAR,
    IR_OPCODE_ARG,

/* Other Operations */
    IR_OPCODE_COPY,
    IR_OPCODE_PUSH,
    IR_OPCODE_POP,

    IR_OPCODE_COUNT,
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

#define APPEND_ADD(b, type, result, opd1, opd2) \
    ir_append_ins(b, IR_OPCODE_ADD, type, result, opd1, opd2)

#define APPEND_STORE(b, type, value, offset, ptr) \
    ir_append_ins(b, IR_OPCODE_STORE, type, value, offset, ptr)

#define APPEND_LOAD(b, type, result, offset, ptr) \
    ir_append_ins(b, IR_OPCODE_LOAD, type, result, offset, ptr)

#define APPEND_CALL(b, type, result, f) \
    ir_append_ins(b, IR_OPCODE_CALL, type, result, f, UNDEFINED_REF)

#define APPEND_VOID_CALL(b, f) \
    ir_append_ins(b, IR_OPCODE_CALL, IR_TYPE_VOID, UNDEFINED_REF, f, UNDEFINED_REF)





/* libqbe.c */
void printfn(Fn *fn, FILE *f);
Ref newMemoryAddr(Fn *f, Blk *b);
Fn *newFunc(IRType ret_type, char *name, Blk *start);
Ref newFuncParam(Fn *f, IRType type);
Ref newTemp(Fn *func);
Blk *newBlock(uint32_t nlocals);
void newFuncCallArg(Blk *b, IRType type, Ref r);
void ir_append_ins(Blk *b, IROpcode op, IRType type, Ref arg0, Ref arg1, Ref arg2);
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
