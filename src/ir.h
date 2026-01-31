#ifndef LIBQBE_H_INCLUDED
#define LIBQBE_H_INCLUDED

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include "rv32/rv32im.h"

#include "listx.h"
#include "wasm.h"

typedef struct IRFunction IRFunction;
typedef struct IRBlock IRBlock;
typedef struct IRPhi IRPhi;
typedef struct Location Location;
typedef struct LiveInterval LiveInterval;

typedef struct IRReference {
    enum {
        IR_REF_TYPE_UNDEFINED,
        IR_REF_TYPE_TMP,
        IR_REF_TYPE_PHI,
        IR_REF_TYPE_I32,
        IR_REF_TYPE_FUNCTION,
        IR_REF_TYPE_LOCATION,
    } type;
    union {
        uint32_t tmp_id;
        IRPhi *phi;
        int32_t i32;
        WASMFunction *wasm_func;
        Location *location;
    } as;
} IRReference;

struct IRFunction {
    WASMFunction *wasm_func;
    struct list_head working_block_list;
    struct list_head block_list;
    IRBlock *start;
    IRReference WASMExecEnv;
    uint32_t next_tmp_id;
    uint32_t next_block_id;
    uint32_t ir_local_count;
    uint32_t tmp_count;
    uint32_t stack_slot_count;
    LiveInterval *live_intervals;
    bool regs_to_preserve[RV32_NUM_REG];
};

typedef struct IRPredecessor {
    struct list_head link;
    IRBlock *ptr;
} IRPredecessor;

typedef struct IRLoopEnd {
    struct list_head link;
    IRBlock *ptr;
} IRLoopEnd;

struct IRBlock {
    unsigned int id;
    struct list_head link;
    struct list_head phi_list;
    struct list_head instr_list;
    struct {
        enum {
            IR_JUMP_TYPE_NONE,
            IR_JUMP_TYPE_JMP,
            IR_JUMP_TYPE_JNZ,
            IR_JUMP_TYPE_RET0,
            IR_JUMP_TYPE_RET1,
            IR_JUMP_TYPE_HALT,
        } type;
        IRReference arg;
        unsigned int seqnum;
    } jump;

    // 0 is the 'then' branch, 1 is the 'else' branch
    IRBlock *succ[2];
    struct list_head pred_list;
    uint32_t pred_count;
    IRReference *locals;
    IRPhi **incomplete_phi;
    bool is_sealed;

/* bitmap of variable that are live at the beginning of the block */
    unsigned long *live_in;
    unsigned int seqnum;

    bool is_loop_header;
    struct list_head loop_end_block_list;

    uint8_t *text_start;
};

typedef enum __attribute__ ((__packed__)) IRType {
    IR_TYPE_VOID = 0,
    IR_TYPE_I32 = WASM_VALTYPE_I32,
    IR_TYPE_I64 = WASM_VALTYPE_I64,
    IR_TYPE_F32 = WASM_VALTYPE_F32,
    IR_TYPE_F64 = WASM_VALTYPE_F64,
} IRType;

typedef struct IRPhiArg {
    struct list_head link;
    IRReference value;
    IRBlock *predecessor;
} IRPhiArg;

struct IRPhi {
    struct list_head link;
    IRBlock *block;
    uint32_t id;
    IRType type;
    struct list_head phi_arg_list;
    struct list_head use_list;
};

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
    IR_OPCODE_STORE8,
/* <result> = load <type> <offset>, <ptr> */
    IR_OPCODE_LOAD,
    IR_OPCODE_ULOAD8,
    IR_OPCODE_SLOAD8,

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

typedef struct IRInstr {
    struct list_head link;
    IROpcode op;
    IRType type;
    IRReference to;
    IRReference arg[2];
    unsigned int seqnum;
} IRInstr;

struct Location {
    enum {
        LOCATION_TYPE_NONE,
        LOCATION_TYPE_REGISTER,
        LOCATION_TYPE_STACK_SLOT,
    } type;
    union {
        rv32_reg reg;
        unsigned int stack_slot;
    } as;
};

struct LiveInterval {
    struct list_head link;
    unsigned int start;
    unsigned int end;
    Location assign;
    int register_hint;
};

typedef struct IRUse {
    struct list_head link;
    IRReference *ref;
} IRUse;


#define IR_REF_UNDEFINED \
    (IRReference) { .type = IR_REF_TYPE_UNDEFINED }

#define IR_REF_TMP(ir_func) \
    (IRReference) { \
        .type = IR_REF_TYPE_TMP, \
        .as.tmp_id = ((ir_func)->tmp_count++, (ir_func)->next_tmp_id++) \
    }

#define IR_REF_PHI(_phi) \
    (IRReference) { .type = IR_REF_TYPE_PHI, .as.phi = _phi }

#define IR_REF_I32(value) \
    (IRReference) { .type = IR_REF_TYPE_I32, .as.i32 = value }

#define IR_REF_FUNC(wf) \
    (IRReference){ .type = IR_REF_TYPE_FUNCTION, .as.wasm_func = wf }

#define IR_REF_LOC(loc_ptr) \
    (IRReference){ .type = IR_REF_TYPE_LOCATION, .as.location = loc_ptr }



#define ir_append_func_call_arg(block, arg_type, arg_value) \
    ir_append_instr3(block, IR_OPCODE_ARG, arg_type, \
                     IR_REF_UNDEFINED, arg_value, IR_REF_UNDEFINED)

#define ir_append_void_func_call(block, func_name) \
    ir_append_instr3(block, IR_OPCODE_CALL, IR_TYPE_VOID, \
                     IR_REF_UNDEFINED, func_name, IR_REF_UNDEFINED)

#define ir_append_func_call(block, return_type, return_value, func_name) \
    ir_append_instr3(block, IR_OPCODE_CALL, return_type, \
                     return_value, func_name, IR_REF_UNDEFINED)

#define ir_append_load(block, type, destination, offset, pointer) \
    ir_append_instr3(block, IR_OPCODE_LOAD, type, destination, offset, pointer)

#define ir_append_store(block, type, source, offset, pointer) \
    ir_append_instr3(block, IR_OPCODE_STORE, type, source, offset, pointer)

#define ir_append_add(block, type, result, left_operand, right_opendard) \
    ir_append_instr3(block, IR_OPCODE_ADD, type, result, left_operand, right_opendard)

IRFunction *ir_create_function(WASMFunction *wasm_func);
IRBlock *ir_create_block(IRFunction *f);
IRBlock *ir_create_sealed_block(IRFunction *f);
IRBlock *ir_create_sealed_block_without_locals(IRFunction *f);
bool ir_append_instr1(IRBlock *b, IROpcode op, IRType type, IRReference to);
bool ir_append_instr2(IRBlock *b, IROpcode op, IRType type, IRReference to, IRReference arg0);
bool ir_append_instr3(IRBlock *b, IROpcode op, IRType type, IRReference to, IRReference arg0, IRReference arg1);
bool ir_jmp(IRFunction *f, IRBlock *departure, IRBlock *arrival);
bool ir_jnz(IRFunction *f, IRBlock *departure, IRReference arg,
            IRBlock *arg_not_zero_arrival, IRBlock *arg_is_zero_arrival);
void ir_ret0(IRFunction *f, IRBlock *b);
bool ir_ret1(IRFunction *f, IRBlock *b, IRReference return_value);
void ir_halt(IRFunction *f, IRBlock *b);
IRPhi *ir_create_phi(IRFunction *f, IRBlock *block, IRType type);
bool ir_append_phi_arg(IRPhi *phi, IRReference value, IRBlock *predecessor);
bool ir_add_predecessor(IRBlock *block, IRBlock *pred);
bool ir_add_loop_end(IRBlock *block, IRBlock *loop_end);
bool ir_add_usage(IRReference *ref);
void ir_rm_usage(IRReference *ref);

IRReference ir_get_default(WASMValtype t);
IRType ir_cast(WASMValtype t);
bool ir_reference_equal(IRReference *ref1, IRReference *ref2);
void ir_print_fn(IRFunction *fn, FILE *f);
void ir_print_location(Location *loc, FILE *f);
void ir_print_ref(IRReference r, FILE *f);

void ir_free_function(IRFunction *f);
void ir_free_block(IRBlock *b);
void ir_free_phi(IRPhi *phi);

bool ir_read_local(IRFunction *f, IRBlock *block, uint32_t localidx, IRReference *out);
bool ir_seal_block(IRFunction *f, IRBlock *block);
void ir_write_local(IRBlock *block, uint32_t localidx, IRReference value);

#endif 
