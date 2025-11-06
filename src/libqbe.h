#ifndef LIBQBE_H_INCLUDED
#define LIBQBE_H_INCLUDED

#include <stdint.h>
#include <stdio.h>
#include "adlist.h"

#define NString 80

typedef struct Phi Phi;
typedef struct Ins Ins;
typedef struct Use Use;
typedef struct Blk Blk;

typedef enum __attribute__ ((__packed__)) simple_type {
    NO_TYPE,
    WORD_TYPE,
    LONG_TYPE,
    SINGLE_PRECISION_TYPE,
    DOUBLE_PRECISION_TYPE,
} simple_type;

typedef struct Lnk {
    char is_exported;
    char thread;
    char align;
    char *sec;
    char *secf;
} Lnk;

typedef struct Tmp {
    char name[NString];
    simple_type cls;
    list *use_list;
} Tmp;

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
} Ref_type;

typedef union Ref_ptr {
    listNode *tmp_node;
    Con *con;
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

typedef struct Jump {
    Jump_type type;
    Ref arg;
} Jump;

struct Blk {
    list *phi_list;
    list *ins_list;
    Jump jmp;
    Blk *s1;
    Blk *s2;
    list *preds;
    char name[NString];
    Ref *locals;
    listNode **incomplete_phis;
    unsigned char is_sealed;
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
    Lnk lnk;
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
	Lnk lnk;
    list *dataField_list;
} Data;

typedef enum instr_opcode {
    /* Arithmetic and Bits */
    ADD_INSTR = 1,
    SUB_INSTR = 2,
    NEG_INSTR = 3,
    DIV_INSTR = 4,
    REM_INSTR = 5,
    UDIV_INSTR = 6,
    UREM_INSTR = 7,
    MUL_INSTR = 8,
    AND_INSTR = 9,
    OR_INSTR = 10,
    XOR_INSTR = 11,
    SAR_INSTR = 12,
    SHR_INSTR = 13,
    SHL_INSTR = 14,
    /* Comparisons */
    CEQW_INSTR = 15,
    CNEW_INSTR = 16,
    CSGEW_INSTR = 17,
    CSGTW_INSTR = 18,
    CSLEW_INSTR = 19,
    CSLTW_INSTR = 20,
    CUGEW_INSTR = 21,
    CUGTW_INSTR = 22,
    CULEW_INSTR = 23,
    CULTW_INSTR = 24,
    CEQL_INSTR = 25,
    CNEL_INSTR = 26,
    CSGEL_INSTR = 27,
    CSGTL_INSTR = 28,
    CSLEL_INSTR = 29,
    CSLTL_INSTR = 30,
    CUGEL_INSTR = 31,
    CUGTL_INSTR = 32,
    CULEL_INSTR = 33,
    CULTL_INSTR = 34,
    CEQS_INSTR = 35,
    CGES_INSTR = 36,
    CGTS_INSTR = 37,
    CLES_INSTR = 38,
    CLTS_INSTR = 39,
    CNES_INSTR = 40,
    COS_INSTR = 41,
    CUOS_INSTR = 42,
    CEQD_INSTR = 43,
    CGED_INSTR = 44,
    CGTD_INSTR = 45,
    CLED_INSTR = 46,
    CLTD_INSTR = 47,
    CNED_INSTR = 48,
    COD_INSTR = 49,
    CUOD_INSTR = 50,
    /* Memory */
    STOREB_INSTR = 51,
    STOREH_INSTR = 52,
    STOREW_INSTR = 53,
    STOREL_INSTR = 54,
    STORES_INSTR = 55,
    STORED_INSTR = 56,
    LOADSB_INSTR = 57,
    LOADUB_INSTR = 58,
    LOADSH_INSTR = 59,
    LOADUH_INSTR = 60,
    LOADSW_INSTR = 61,
    LOADUW_INSTR = 62,
    LOAD_INSTR = 63,
    ALLOC4_INSTR = 81,
    ALLOC8_INSTR = 82,
    ALLOC16_INSTR = 83,
    /* Conversions */
    EXTSB_INSTR = 64,
    EXTUB_INSTR = 65,
    EXTSH_INSTR = 66,
    EXTUH_INSTR = 67,
    EXTSW_INSTR = 68,
    EXTUW_INSTR = 69,
    EXTS_INSTR = 70,
    TRUNCD_INSTR = 71,
    STOSI_INSTR = 72,
    STOUI_INSTR = 73,
    DTOSI_INSTR = 74,
    DTOUI_INSTR = 75,
    SWTOF_INSTR = 76,
    UWTOF_INSTR = 77,
    SLTOF_INSTR = 78,
    ULTOF_INSTR = 79,
    /* Cast and Copy */
    CAST_INSTR = 80,
    COPY_INSTR = 86,
    /* Variadic */
    VAARG_INSTR = 84,
    VASTART_INSTR = 85,
    /* Call */
    CALL_INSTR = 119,
    /* Other */
    DBGLOC_INSTR = 87,
    NOP_INSTR = 88,
    ADDR_INSTR = 89,
    BLIT0_INSTR = 90,
    BLIT1_INSTR = 91,
    SWAP_INSTR = 92,
    SIGN_INSTR = 93,
    SALLOC_INSTR = 94,
    XIDIV_INSTR = 95,
    XDIV_INSTR = 96,
    XCMP_INSTR = 97,
    XTEST_INSTR = 98,
    ACMP_INSTR = 99,
    ACMN_INSTR = 100,
    AFCMP_INSTR = 101,
    REQZ_INSTR = 102,
    RNEZ_INSTR = 103,
    PAR_INSTR = 104,
    PARSB_INSTR = 105,
    PARUB_INSTR = 106,
    PARSH_INSTR = 107,
    PARUH_INSTR = 108,
    PARC_INSTR = 109,
    PARE_INSTR = 110,
    ARG_INSTR = 111,
    ARGSB_INSTR = 112,
    ARGUB_INSTR = 113,
    ARGSH_INSTR = 114,
    ARGUH_INSTR = 115,
    ARGC_INSTR = 116,
    ARGE_INSTR = 117,
    ARGV_INSTR = 118,
    FLAGIEQ_INSTR = 120,
    FLAGINE_INSTR = 121,
    FLAGISGE_INSTR = 122,
    FLAGISGT_INSTR = 123,
    FLAGISLE_INSTR = 124,
    FLAGISLT_INSTR = 125,
    FLAGIUGE_INSTR = 126,
    FLAGIUGT_INSTR = 127,
    FLAGIULE_INSTR = 128,
    FLAGIULT_INSTR = 129,
    FLAGFEQ_INSTR = 130,
    FLAGFGE_INSTR = 131,
    FLAGFGT_INSTR = 132,
    FLAGFLE_INSTR = 133,
    FLAGFLT_INSTR = 134,
    FLAGFNE_INSTR = 135,
    FLAGFO_INSTR = 136,
    FLAGFUO_INSTR = 137,
} instr_opcode;

struct Ins {
    instr_opcode op;
    simple_type type;
    Ref to;
    Ref arg[2];
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
#define CEQW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CEQW_INSTR, (ref1), (ref2))

#define CNEW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CNEW_INSTR, (ref1), (ref2))

#define CSLTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CSLTW_INSTR, (ref1), (ref2))

#define CULTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CULTW_INSTR, (ref1), (ref2))

#define CSGTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CSGTW_INSTR, (ref1), (ref2))

#define CUGTW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CUGTW_INSTR, (ref1), (ref2))

#define CSLEW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CSLEW_INSTR, (ref1), (ref2))

#define CULEW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CULEW_INSTR, (ref1), (ref2))

#define CSGEW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CSGEW_INSTR, (ref1), (ref2))

#define CUGEW(b, temp, ref1, ref2) \
    instr((b), (temp), WORD_TYPE, CUGEW_INSTR, (ref1), (ref2))

/* Memory */
#define ALLOC4(b, temp, kind, ref) \
    instr((b), (temp), (kind), ALLOC4_INSTR, (ref), UNDEF_TMP_REF)

#define STOREW(b, fromRef, toRef) \
    instr((b), UNDEF_TMP_REF, WORD_TYPE, STOREW_INSTR, (fromRef), (toRef))

#define LOADW(b, temp, addr) \
    instr((b), (temp), WORD_TYPE, LOADSW_INSTR, (addr), UNDEF_TMP_REF)

#define LOADUB(b, temp, addr) \
    instr((b), (temp), WORD_TYPE, LOADUB_INSTR, (addr), UNDEF_TMP_REF)

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

void printfn(Fn *fn, FILE *f);
void printdata(Data *d, FILE *f);

Fn *newFunc(Lnk *link_info, simple_type ret_type, char *name, Blk *start);
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

Data *newData(Lnk *link_info, char *name);
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
void freePhi_arg(Phi_arg *arg);
void freeBlock(Blk *b);
void freeData(Data *d);
void freeDataField(DataField *df);

#endif 
