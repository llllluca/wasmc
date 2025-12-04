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

typedef enum __attribute__ ((__packed__)) simple_type {
    NO_TYPE,
    WORD_TYPE,
    LONG_TYPE,
    SINGLE_PRECISION_TYPE,
    DOUBLE_PRECISION_TYPE,
} simple_type;

typedef struct location {
    location_type type;
    union {
        rv32_reg reg;
        unsigned int stack_slot;
    } as;
} location;

typedef struct live_interval {
    unsigned int start;
    unsigned int end;
    location assign;
    rv32_reg register_hint;
} live_interval;

#define NString 80
struct Tmp {
    char name[NString];
    simple_type cls; // TODO: when cls is used?
    list *use_list;
    live_interval *i;
};

typedef enum Con_type {
    CInt64,
    CDouble,
    CSingle,
    CAddr,
} Con_type;

typedef struct Con {
    Con_type type;
    union {
        int64_t i;
        double d;
        float s;
        char *addr;
    } val;
} Con;

typedef enum Ref_type {
    RTmp,
    RCon,
    RLoc
} Ref_type;

typedef union Ref_ptr {
    listNode *tmp_node;
    Con *con;
    location loc;
} Ref_ptr;

typedef struct Ref {
    Ref_type type;
    Ref_ptr val;
} Ref;

#define UNDEF_TMP_REF (Ref){ .type = RTmp, .val.tmp_node = NULL }

typedef enum Jump_type {
    NONE_JUMP_TYPE,
    JMP_JUMP_TYPE,
    JNZ_JUMP_TYPE,
    RET0_JUMP_TYPE,
    RET1_JUMP_TYPE,
    HALT_JUMP_TYPE
} Jump_type;

// TODO: put Jump as normal Ins
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
    char name[NString];
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
    simple_type type;
    Blk *block;
};

typedef struct Fn {
    list *tmp_list;
    list *con_list;
    list *blk_list;
    char name[NString];
    Blk *start;
    simple_type ret_type;
    unsigned int num_stack_slots;
} Fn;

typedef enum DataField_type {
    DByte,
    DWord,
    DLong,
    DZeros,
    DString,
} DataField_type;

typedef struct DataField {
    DataField_type type;
    union {
        unsigned char b;
        int32_t w;
        int64_t l;
        uint64_t z;
        char *s;
    } val;
} DataField;

typedef struct Data {
    char name[NString];
    list *dataField_list;
} Data;

typedef enum instr_opcode {
    ADD_INSTR,
    SUB_INSTR,
    DIV_INSTR,
    REM_INSTR,
    UDIV_INSTR,
    UREM_INSTR,
    MUL_INSTR,
    AND_INSTR,
    OR_INSTR,
    XOR_INSTR,
    SAR_INSTR,
    SHR_INSTR,
    SHL_INSTR,
    EQZW_INSTR,
    CNEW_INSTR,
    CSLTW_INSTR,
    CULTW_INSTR,
    STOREW_INSTR,
    LOADW_INSTR,
    LOADUB_INSTR,
    STOREB_INSTR,
    CALL_INSTR,
    PAR_INSTR,
    ARG_INSTR,
    EXTSW_INSTR,
    COPY_INSTR,

    PUSH_INSTR,
    POP_INSTR,
} instr_opcode;

struct Ins {
    instr_opcode op;
    simple_type type;
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
    instr((b), (temp), (kind), ADD_INSTR, (ref1), (ref2))

#define SUB(b, temp, kind, ref1, ref2) \
    instr((b), (temp), (kind), SUB_INSTR, (ref1), (ref2))

#define NEG(b, temp, kind, ref) \
    instr((b), (temp), (kind), NEG_INSTR, (ref1), UNDEF_TMP_REF)

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
    instr((b), (temp), WORD_TYPE, EQZW_INSTR, (ref1), UNDEF_TMP_REF)

#define CNEW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CNEW_INSTR, (ref1), (ref2))

#define CSLTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CSLTW_INSTR, (ref1), (ref2))

#define CULTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CULTW_INSTR, (ref1), (ref2))

/* Memory */
#define STOREW(b, fromRef, toRef) \
    instr((b), UNDEF_TMP_REF, WORD_TYPE, STOREW_INSTR, (fromRef), (toRef))

#define LOADW(b, temp, addr) \
    instr((b), (temp), WORD_TYPE, LOADW_INSTR, (addr), UNDEF_TMP_REF)

/* Conversions */
#define EXTSW(b, temp, ref) \
    instr((b), (temp), LONG_TYPE, EXTSW_INSTR, (ref), UNDEF_TMP_REF)

/* Cast and Copy */
#define COPY(b, temp, kind, ref1) \
    instr((b), (temp), (kind), COPY_INSTR, (ref1), UNDEF_TMP_REF)

/* Call */
#define FUNC_CALL(b, temp, kind, addrRef) \
    instr((b), (temp), kind, CALL_INSTR, (addrRef), UNDEF_TMP_REF)

#define VOID_FUNC_CALL(b, addrRef) \
    instr((b), UNDEF_TMP_REF, WORD_TYPE, CALL_INSTR, (addrRef), UNDEF_TMP_REF)


/* Data */
#define ADD_INT32_DATA_FIELD(num) addNumDataField(DW, (num))
#define ADD_ZEROS_DATA_FIELD(num) addNumDataField(DZ, (num))
#define ADD_STR_DATA_FIELD(str) addStrDataField(DB, (str))


/* libqbe.c */
void printfn(Fn *fn, FILE *f);
void printdata(Data *d, FILE *f);
Fn *newFunc(simple_type ret_type, char *name, Blk *start);
Ref newFuncParam(Fn *f, simple_type type);
Ref newTemp(Fn *func);
Blk *newBlock(uint32_t nlocals);
Ref newIntConst(Fn *f, int64_t value);
// TODO: usare instr e macro per newFuncCallArg
void newFuncCallArg(Blk *b, simple_type type, Ref r);
Ref newAddrConst(Fn *f, char *addrName);
void instr(Blk *b, Ref r, simple_type type, instr_opcode op, Ref arg1, Ref arg2);
void jmp(Fn *f, Blk *from, Blk *to);
void jnz(Fn *f, Blk *from, Ref r, Blk *b0, Blk *b1);
void ret(Fn *f, Blk *b);
void retRef(Fn *f, Blk *b, Ref r);
void halt(Fn *f, Blk *b);
void optimizeFunc(Fn *fn);
void typecheck(Fn *fn);
Data *newData(char *name);
void dataAppendByteField(Data *d, unsigned char b);
void dataAppendWordField(Data *d, int32_t w);
void dataAppendLongField(Data *d, int64_t l);
void dataAppendZerosField(Data *d, uint64_t z);
void dataAppendStringField(Data *d, char *s, unsigned int len);
listNode *newPhi(Blk *b, Ref temp, simple_type type);
void phiAppendOperand(listNode *phi_node, Blk *b, Ref arg);
void addUsage(Tmp *tmp, Use_type type, Use_ptr ptr);
void rmUsage(Tmp *tmp, Use_type type, Use_ptr ptr);
int req(Ref a, Ref b);
void freeFunc(Fn *f);
void freeTemp(Tmp *tmp);
void freePhi(Phi *phi);
void freeBlock(Blk *b);
void freeData(Data *d);
void freeDataField(DataField *df);

#endif 
