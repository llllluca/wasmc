#ifndef LIBQBE_H_INCLUDED
#define LIBQBE_H_INCLUDED

#include "qbe-1.2/all.h"

#define QBE_DEBUG 0

typedef enum func_return_type {
    FUNC_NO_RETURN_TYPE = 9, /* must match K0 in qbe-1.2/parse.c */
    FUNC_WORD_RETURN_TYPE = Kw,
    FUNC_LONG_RETURN_TYPE = Kl,
    FUNC_SINGLE_PRECISION_RETURN_TYPE = Ks,
    FUNC_DOUBLE_PRECISION_RETURN_TYPE = Kd,
} func_return_type;

typedef enum simple_type {
    NO_TYPE = Kx,
    WORD_TYPE = Kw,
    LONG_TYPE = Kl,
    SINGLE_PRECISION_TYPE = Ks,
    DOUBLE_PRECISION_TYPE = Kd,
} simple_type;

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

/* Arithmetic and Bits */
#define ADD(temp, kind, ref1, ref2) \
    instr((temp), (kind), ADD_INSTR, (ref1), (ref2))

#define SUB(temp, kind, ref1, ref2) \
    instr((temp), (kind), SUB_INSTR, (ref1), (ref2))

#define NEG(temp, kind, ref) \
    instr((temp), (kind), NEG_INSTR, (ref1), R)

#define DIV(temp, kind, ref1, ref2) \
    instr((temp), (kind), DIV_INSTR, (ref1), (ref2))

#define REM(temp, kind, ref1, ref2) \
    instr((temp), (kind), REM_INSTR, (ref1), (ref2))

#define UDIV(temp, kind, ref1, ref2) \
    instr((temp), (kind), UDIV_INSTR, (ref1), (ref2))

#define UREM(temp, kind, ref1, ref2) \
    instr((temp), (kind), UREM_INSTR, (ref1), (ref2))

#define MUL(temp, kind, ref1, ref2) \
    instr((temp), (kind), MUL_INSTR, (ref1), (ref2))

#define AND(temp, kind, ref1, ref2) \
    instr((temp), (kind), AND_INSTR, (ref1), (ref2))

#define OR(temp, kind, ref1, ref2) \
    instr((temp), (kind), OR_INSTR, (ref1), (ref2))

#define XOR(temp, kind, ref1, ref2) \
    instr((temp), (kind), XOR_INSTR, (ref1), (ref2))

#define SAR(temp, kind, ref1, ref2) \
    instr((temp), (kind), SAR_INSTR, (ref1), (ref2))

#define SHR(temp, kind, ref1, ref2) \
    instr((temp), (kind), SHR_INSTR, (ref1), (ref2))

#define SHL(temp, kind, ref1, ref2) \
    instr((temp), (kind), SHL_INSTR, (ref1), (ref2))

/* Comparisons */
#define CEQW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CEQW_INSTR, (ref1), (ref2))

#define CNEW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CNEW_INSTR, (ref1), (ref2))

#define CSLTW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CSLTW_INSTR, (ref1), (ref2))

#define CULTW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CULTW_INSTR, (ref1), (ref2))

#define CSGTW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CSGTW_INSTR, (ref1), (ref2))

#define CUGTW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CUGTW_INSTR, (ref1), (ref2))

#define CSLEW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CSLEW_INSTR, (ref1), (ref2))

#define CULEW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CULEW_INSTR, (ref1), (ref2))

#define CSGEW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CSGEW_INSTR, (ref1), (ref2))

#define CUGEW(temp, ref1, ref2) \
    instr((temp), WORD_TYPE, CUGEW_INSTR, (ref1), (ref2))

/* Memory */
#define ALLOC4(temp, kind, ref) \
    instr((temp), (kind), ALLOC4_INSTR, (ref), R)

#define STOREW(fromRef, toRef) \
    instr(R, WORD_TYPE, STOREW_INSTR, (fromRef), (toRef))

#define LOADW(temp, addr) \
    instr((temp), WORD_TYPE, LOADSW_INSTR, (addr), R)

/* Conversions */
#define EXTSW(temp, ref) \
    instr((temp), LONG_TYPE, EXTSW_INSTR, (ref), R)

/* Cast and Copy */
#define COPY(temp, kind, ref1) \
    instr((temp), (kind), COPY_INSTR, (ref1), R)

/* Call */
#define FUNC_CALL(temp, kind, addrRef) \
    instr((temp), kind, CALL_INSTR, (addrRef), R)

#define VOID_FUNC_CALL(addrRef) \
    instr(R, WORD_TYPE, CALL_INSTR, (addrRef), R)


/* Data */
#define ADD_INT32_DATA_FIELD(num) addNumDataField(DW, (num))
#define ADD_ZEROS_DATA_FIELD(num) addNumDataField(DZ, (num))
#define ADD_STR_DATA_FIELD(str) addStrDataField(DB, (str))

#if QBE_DEBUG != 0
void printfn(Fn *fn, FILE *f);
void printref(Ref r, Fn *fn, FILE *f);
#endif

Fn *newFunc(Lnk *link_info, func_return_type ret_type, char *name);
Ref newFuncParam(Fn *f, simple_type type);
Blk *newBlock(void);
Ref newTemp(Fn *func);
Ref newIntConst(Fn *f, int64_t value);
void newFuncCallArg(simple_type type, Ref r);
Ref newAddrConst(Fn *f, char *addrName);
void instr(Ref r, simple_type type, instr_opcode op, Ref arg1, Ref arg2);
void jmp(Fn *f, Blk *from, Blk *to);
void jnz(Fn *f, Blk *from, Ref r, Blk *b0, Blk *b1);
void ret(Blk *b);
void retRef(Blk *b, Ref r);
void halt(Blk *b);
void phi(Ref temp, simple_type type, Blk *b0, Ref r0, Blk *b1, Ref r1);
void optimizeFunc(Fn *fn);
void typecheck(Fn *fn);


void newData(char *name, Lnk *lnk);
void addNumDataField(int type, int64_t num);
void addStrDataField(int type, char *str);
void closeData(void);

#endif 
