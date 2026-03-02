#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "ir.h"
#include "wasmc.h"

#define WASMC_OK 0

static const char *rname[] = {
    [ZERO] = "zero", [RA] = "ra",   [SP] = "sp",   [GP] = "gp", [TP] = "tp",
    [T0] = "t0",     [T1] = "t1",   [T2] = "t2",   [FP] = "fp", [S1] = "s1",
    [A0] = "a0",     [A1] = "a1",   [A2] = "a2",   [A3] = "a3", [A4] = "a4",
    [A5] = "a5",     [A6] = "a6",   [A7] = "a7",   [S2] = "s2", [S3] = "s3",
    [S4] = "s4",     [S5] = "s5",   [S6] = "s6",   [S7] = "s7", [S8] = "s8",
    [S9] = "s9",     [S10] = "s10", [S11] = "s11", [T3] = "t3", [T4] = "t4",
    [T5] = "t5",     [T6] = "t6",
};

static const char *optab[] = {
    /* Integer Arithmetic Operations */
    [IR_OPCODE_ADD]  = "add",
    [IR_OPCODE_SUB]  = "sub",
    [IR_OPCODE_MUL]  = "mul",
    [IR_OPCODE_SDIV] = "sdiv",
    [IR_OPCODE_UDIV] = "udiv",
    [IR_OPCODE_SREM] = "srem",
    [IR_OPCODE_UREM] = "urem",

    /* Bitwise Operations */
    [IR_OPCODE_AND]  = "and",
    [IR_OPCODE_OR]   = "or",
    [IR_OPCODE_XOR]  = "xor",
    [IR_OPCODE_SHL]  = "shl",
    [IR_OPCODE_LSHR] = "lshr",
    [IR_OPCODE_ASHR] = "ashr",

    /* Comparison Operations */
    [IR_OPCODE_EQZ] = "eqz",
    [IR_OPCODE_EQ]  = "eq",
    [IR_OPCODE_NE]  = "ne",
    [IR_OPCODE_SLT] = "slt",
    [IR_OPCODE_ULT] = "ult",
    [IR_OPCODE_SGT] = "sgt",
    [IR_OPCODE_UGT] = "ugt",
    [IR_OPCODE_SLE] = "sle",
    [IR_OPCODE_ULE] = "ule",
    [IR_OPCODE_SGE] = "sge",
    [IR_OPCODE_UGE] = "uge",

    /* Memory Operations */
    [IR_OPCODE_STORE]  = "store",
    [IR_OPCODE_LOAD]   = "load",
    [IR_OPCODE_STORE8] = "store8",
    [IR_OPCODE_ULOAD8] = "uload8",
    [IR_OPCODE_SLOAD8] = "sload8",

/* Function definitions and calls */
    [IR_OPCODE_CALL] = "call",
    [IR_OPCODE_PAR]  = "par",
    [IR_OPCODE_ARG]  = "arg",

    /* Other Operations */
    [IR_OPCODE_COPY] = "copy",
    [IR_OPCODE_PUSH] = "push",
    [IR_OPCODE_POP]  = "pop",
};

static char *type2str(IRType type) {
    switch (type) {
    case IR_TYPE_VOID: return "void";
    case IR_TYPE_I32: return "i32";
    case IR_TYPE_I64: return "i64";
    case IR_TYPE_F32: return "f32";
    case IR_TYPE_F64: return "f64";
    default:
        assert(0);
    }
}
void ir_print_location(Location *loc, FILE *f) {
    switch (loc->type) {
    case LOCATION_TYPE_NONE:
        fprintf(f, "LOCATION_NONE");
        break;
    case LOCATION_TYPE_REGISTER:
        fprintf(f, "%s", rname[loc->as.reg]);
        break;
    case LOCATION_TYPE_STACK_SLOT:
        fprintf(f, "STACK_SLOT(%u)", loc->as.stack_slot);
        break;
    }
}

void ir_print_ref(IRReference r, FILE *f) {
    switch (r.type) {
    case IR_REF_TYPE_UNDEFINED:
        fprintf(f, "UNDEFINED");
        break;
    case IR_REF_TYPE_TMP:
        fprintf(f, "%%t%"PRIu32, r.as.tmp_id);
        break;
    case IR_REF_TYPE_PHI:
        fprintf(f, "%%t%"PRIu32, r.as.phi->id);
        break;
    case IR_REF_TYPE_I32:
        printf("%"PRIi32, r.as.i32);
        break;
    case IR_REF_TYPE_FUNCTION:
        printf("%s", r.as.wasm_func->name);
        break;
    case IR_REF_TYPE_LOCATION:
        ir_print_location(r.as.location, f);
        break;
    default:
        assert(0);
    }
}

static void ir_print_phi(IRPhi *phi, FILE *f) {
    fprintf(f, "\t%%t%"PRIu32, phi->id);
    fprintf(f, " =%s phi ", type2str(phi->type));
    IRPhiArg *phi_arg;
    list_for_each_entry(phi_arg, &phi->phi_arg_list, link) {
        fprintf(f, "@%u ", phi_arg->predecessor->id);
        ir_print_ref(phi_arg->value, f);
        if (phi_arg->link.next != &phi->phi_arg_list) {
            fprintf(f, ", ");
        }
    }
    printf("\n");
}

static void ir_print_instr(IRInstr *i, FILE *f) {
    fprintf(f, "\t");
    switch (i->op) {
    case IR_OPCODE_LOAD:
    case IR_OPCODE_ULOAD8:
        ir_print_ref(i->to, f);
        fprintf(f, " =%s %s ", type2str(i->type), optab[i->op]);
        ir_print_ref(i->arg[0], f);
        fprintf(f, "(");
        ir_print_ref(i->arg[1], f);
        fprintf(f, ")");
        break;
    case IR_OPCODE_STORE:
    case IR_OPCODE_STORE8:
        fprintf(f, "%s ", optab[i->op]);
        ir_print_ref(i->to, f);
        fprintf(f, ", ");
        ir_print_ref(i->arg[0], f);
        fprintf(f, "(");
        ir_print_ref(i->arg[1], f);
        fprintf(f, ")");
        break;
    case IR_OPCODE_PUSH:
    case IR_OPCODE_ARG:
        fprintf(f, "%s %s ", optab[i->op], type2str(i->type));
        ir_print_ref(i->arg[0], f);
        break;
    case IR_OPCODE_CALL:
        if (i->type == IR_TYPE_VOID) {
            fprintf(f, "%s ", optab[i->op]);
            ir_print_ref(i->arg[0], f);
        } else {
            ir_print_ref(i->to, f);
            fprintf(f, " =%s %s ", type2str(i->type), optab[i->op]);
            ir_print_ref(i->arg[0], f);
        }
        break;
    default:
        ir_print_ref(i->to, f);
        fprintf(f, " =%s %s", type2str(i->type), optab[i->op]);
        if (i->arg[0].type != IR_REF_TYPE_UNDEFINED) {
            fprintf(f, " ");
            ir_print_ref(i->arg[0], f);
        }
        if (i->arg[1].type != IR_REF_TYPE_UNDEFINED) {
            fprintf(f, ", ");
            ir_print_ref(i->arg[1], f);
        }
        break;
    }
    fprintf(f, "\n");
}

static void ir_print_jump(IRBlock *b, FILE *f) {
    fprintf(f, "\t");
    switch (b->jump.type) {
        case IR_JUMP_TYPE_RET0:
            fprintf(f, "ret\n");
            break;
        case IR_JUMP_TYPE_RET1:
            fprintf(f, "ret ");
            ir_print_ref(b->jump.arg, f);
            fprintf(f, "\n");
            break;
        case IR_JUMP_TYPE_HALT:
            fprintf(f, "hlt\n");
            break;
        case IR_JUMP_TYPE_JMP:
            fprintf(f, "jmp @%u\n", b->succ[0]->id);
            break;
        case IR_JUMP_TYPE_JNZ:
            fprintf(f, "jnz ");
            ir_print_ref(b->jump.arg, f);
            fprintf(f, ", @%u, @%u\n", b->succ[0]->id, b->succ[1]->id);
            break;
        default:
            assert(0);
    }
}

void ir_print_fn(IRFunction *fn, FILE *f) {

    WASMFunction *wf = fn->wasm_func;
    WASMFuncType *t = wf->type;
    IRType return_type = t->result_count > 0 ? ir_cast(t->result_type) : IR_TYPE_VOID;
    fprintf(f, "function %s %s() {\n", type2str(return_type), wf->name);

    IRBlock *block;
    list_for_each_entry(block, &fn->block_list, link) {
        fprintf(f, "@%u\n", block->id);
        IRPhi *phi;
        list_for_each_entry(phi, &block->phi_list, link) {
            ir_print_phi(phi, f);
        }
        IRInstr *instr;
        list_for_each_entry(instr, &block->instr_list, link) {
            ir_print_instr(instr, f);
        }
        ir_print_jump(block, f);
    }
    fprintf(f, "}\n");
}

int ir_add_usage(IRReference *ref) {
    /* The list of uses of ref is only needed in the try_remove_trivial_phi() function.
     * In the try_remove_trivial_phi() function you need to know the uses of ref
     * defined in the left hand side of a phi statement. Hence we only record the uses
     * of ref defined in the left hand size of a phi statement. */
    if (ref->type == IR_REF_TYPE_PHI) {
        IRUse *use = malloc(sizeof(struct IRUse));
        if (use == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
        use->ref = ref;
        list_add(&use->link, &ref->as.phi->use_list);
    }
    return WASMC_OK;
}

void ir_rm_usage(IRReference *ref) {
    if (ref->type != IR_REF_TYPE_PHI) return;
    IRUse *use;
    list_for_each_entry(use, &ref->as.phi->use_list, link) {
        if (use->ref == ref) {
            list_del(&use->link);
            free(use);
            break;
        }
    }
}

IRFunction *ir_create_function(WASMFunction *wasm_func) {

    IRFunction *ir_func = malloc(sizeof(struct IRFunction));
    if (ir_func == NULL) return NULL;

    /* Initialize the IRFunction */
    ir_func->wasm_func = wasm_func;
    ir_func->next_tmp_id = 0;
    ir_func->tmp_count = 0;
    ir_func->next_block_id = 0;
    INIT_LIST_HEAD(&ir_func->block_list);
    INIT_LIST_HEAD(&ir_func->working_block_list);
    ir_func->stack_slot_count = 0;
    memset(ir_func->regs_to_preserve, 0, RV32_NUM_REG);
    ir_func->live_intervals = NULL;
    uint32_t param_count = wasm_func->type->param_count;
    ir_func->ir_local_count = wasm_func->local_count + param_count;
    IRBlock *start = ir_create_sealed_block(ir_func);
    if (start == NULL) {
        ir_free_function(ir_func);
        return NULL;
    };
    ir_func->start = start;

    /* Initialize function parameters, the first parameter
     * always hold a pointer to the struct WASMExecEnv (See WAMR code) */
    ir_func->WASMExecEnv = IR_REF_TMP(ir_func);
    bool err;
    err = ir_append_instr(start, IR_OPCODE_PAR, IR_TYPE_I32,
            ir_func->WASMExecEnv, IR_REF_UNDEFINED, IR_REF_UNDEFINED);
    if (err) {
        ir_free_function(ir_func);
        return NULL;
    }
    WASMFuncType *t = wasm_func->type;
    for (uint32_t i = 0; i < param_count; i++) {
        IRType type = (IRType)t->param_types[i];
        IRReference param = IR_REF_TMP(ir_func);
        err = ir_append_instr(start, IR_OPCODE_PAR, type,
                param, IR_REF_UNDEFINED, IR_REF_UNDEFINED);
        if (err) {
            ir_free_function(ir_func);
            return NULL;
        }
        /* Initialize locals that are function parameters */
        start->locals[i] = param;
    }

    /* Initialize other locals */
    for (uint32_t i = 0; i < wasm_func->local_count; i++) {
        uint32_t localidx = t->param_count + i;
        WASMValtype valtype = wasm_valtype_of(wasm_func, localidx);
        start->locals[localidx] = ir_get_default(valtype);
    }

    return ir_func;
}

static IRBlock *__ir_create_block(IRFunction *f) {
    IRBlock *block = malloc(sizeof(struct IRBlock));
    if (block == NULL) return NULL;
    memset(block, 0, sizeof(struct IRBlock));
    INIT_LIST_HEAD(&block->phi_list);
    INIT_LIST_HEAD(&block->instr_list);
    INIT_LIST_HEAD(&block->pred_list);
    INIT_LIST_HEAD(&block->loop_end_block_list);
    block->id = f->next_block_id++;
    list_add_tail(&block->link, &f->working_block_list);
    return block;
}


IRBlock *ir_create_sealed_block_without_locals(IRFunction *f) {

    /* Initialize a new block */
    IRBlock *block = __ir_create_block(f);
    if (block == NULL) return NULL;

    /* Seal the block */
    block->is_sealed = true;

    return block;
}

IRBlock *ir_create_sealed_block(IRFunction *f) {

    /* Initialize a new block */
    IRBlock *block = __ir_create_block(f);
    if (block == NULL) return NULL;

    /* Seal the block */
    block->is_sealed = true;

    uint32_t n = f->ir_local_count;
    if (n == 0) return block;

    /* Inizialize the locals of the block */
    block->locals = calloc(n, sizeof(struct IRReference));
    if (block->locals == NULL) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        block->locals[i] = IR_REF_UNDEFINED;
    }

    return block;
}

IRBlock *ir_create_block(IRFunction *f) {

    /* Initialize a new block */
    IRBlock *block = __ir_create_block(f);
    if (block == NULL) return NULL;

    uint32_t n = f->ir_local_count;
    if (n == 0) return block;

    /* Inizialize the locals of the block */
    block->locals = calloc(n, sizeof(struct IRReference));
    if (block->locals == NULL) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        block->locals[i] = IR_REF_UNDEFINED;
    }

    /* Inizialize the incomplete_phi of the block,
     * this step is only needed if the block is not sealed */
    block->incomplete_phi = calloc(n, sizeof(IRPhi *));
    if (block->incomplete_phi == NULL) return NULL;
    for (uint32_t i = 0; i < n; i++) {
        block->incomplete_phi[i] = NULL;
    }

    return block;
}


int ir_append_instr(IRBlock *b, IROpcode op, IRType type,
                    IRReference to, IRReference arg0, IRReference arg1) {

    IRInstr *ins = malloc(sizeof(struct IRInstr));
    if (ins == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    ins->op = op;
    ins->type = type;
    ins->to = to;
    ins->arg[0] = arg0;
    ins->arg[1] = arg1;
    ins->seqnum = 0;
    list_add_tail(&ins->link, &b->instr_list);
    //TODO: add a comment here
    int err;
    if (ins->op == IR_OPCODE_STORE || ins->op == IR_OPCODE_STORE8) {
        err = ir_add_usage(&ins->to);
        if (err) return err;
    }
    err = ir_add_usage(&ins->arg[0]);
    if (err) return err;
    err = ir_add_usage(&ins->arg[1]);
    if (err) return err;

    return WASMC_OK;
}

int ir_add_predecessor(IRBlock *block, IRBlock *pred) {
    if (block == NULL) return false;
    IRPredecessor *node = malloc(sizeof(struct IRPredecessor));
    if (node == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    node->ptr = pred;
    list_add_tail(&node->link, &block->pred_list);
    block->pred_count++;
    return WASMC_OK;
}

int ir_add_loop_end(IRBlock *block, IRBlock *loop_end) {
    IRLoopEnd *node = malloc(sizeof(struct IRLoopEnd));
    if (node == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    node->ptr = loop_end;
    list_add_tail(&node->link, &block->loop_end_block_list);
    return false;
}

int ir_jmp(IRFunction *f, IRBlock *departure, IRBlock *arrival) {

    departure->jump.type = IR_JUMP_TYPE_JMP;
    departure->succ[0] = arrival;

    /* remove the 'departure' block from the working block list */
    list_del(&departure->link);
    list_add_tail(&departure->link, &f->block_list);

    int err = ir_add_predecessor(arrival, departure);
    if (err) return err;

    return WASMC_OK;
}

int ir_jnz(IRFunction *f, IRBlock *departure, IRReference arg,
            IRBlock *arg_not_zero_arrival, IRBlock *arg_is_zero_arrival) {

    departure->jump.type = IR_JUMP_TYPE_JNZ;
    departure->jump.arg = arg;
    departure->succ[0] = arg_not_zero_arrival;
    departure->succ[1] = arg_is_zero_arrival;

    /* remove the 'departure' block from the working block list */
    list_del(&departure->link);
    list_add_tail(&departure->link, &f->block_list);

    int err = ir_add_usage(&departure->jump.arg);
    if (err) return err;

    err = ir_add_predecessor(arg_not_zero_arrival, departure);
    if (err) return err;

    err = ir_add_predecessor(arg_is_zero_arrival, departure);
    if (err) return err;

    return WASMC_OK;
}

void ir_ret0(IRFunction *f, IRBlock *block) {

    block->jump.type = IR_JUMP_TYPE_RET0;
    /* remove the block from the working block list */
    list_del(&block->link);
    list_add_tail(&block->link, &f->block_list);
}

int ir_ret1(IRFunction *f, IRBlock *block, IRReference return_value) {

    block->jump.type = IR_JUMP_TYPE_RET1;
    block->jump.arg = return_value;
    /* remove the block from the working block list */
    list_del(&block->link);
    list_add_tail(&block->link, &f->block_list);
    int err = ir_add_usage(&block->jump.arg);
    if (err) return err;

    return WASMC_OK;
}

void ir_halt(IRFunction *f, IRBlock *block) {

    block->jump.type = IR_JUMP_TYPE_HALT;
    /* remove the block from the working block list */
    list_del(&block->link);
    list_add_tail(&block->link, &f->block_list);
}

IRPhi *ir_create_phi(IRFunction *f, IRBlock *block, IRType type) {

    IRPhi *phi = malloc(sizeof(struct IRPhi));
    if (phi == NULL) return NULL;
    phi->id = f->next_tmp_id++;
    f->tmp_count++;
    phi->type = type;
    phi->block = block;
    INIT_LIST_HEAD(&phi->phi_arg_list);
    INIT_LIST_HEAD(&phi->use_list);
    list_add_tail(&phi->link, &block->phi_list);
    return phi;
}

int ir_append_phi_arg(IRPhi *phi, IRReference value, IRBlock *predecessor) {

    assert(predecessor != NULL);
    IRPhiArg *phi_arg = malloc(sizeof(struct IRPhiArg));
    if (phi_arg == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    phi_arg->value = value;
    phi_arg->predecessor = predecessor;
    list_add_tail(&phi_arg->link, &phi->phi_arg_list);

    if (ir_add_usage(&phi_arg->value)) return true;

    return false;
}

bool ir_reference_equal(IRReference *ref1, IRReference *ref2) {
    if (ref1->type != ref2->type) return false;

    switch(ref1->type) {
    case IR_REF_TYPE_UNDEFINED:
        return true;
    case IR_REF_TYPE_TMP:
        return ref1->as.tmp_id == ref2->as.tmp_id;
    case IR_REF_TYPE_PHI:
        return ref1->as.phi == ref2->as.phi;
    case IR_REF_TYPE_I32:
        return ref1->as.i32 == ref2->as.i32;
    case IR_REF_TYPE_FUNCTION:
        return ref1->as.wasm_func == ref2->as.wasm_func;
    default:
        assert(0);
        return false;
    }
}

IRReference ir_get_default(WASMValtype type) {
    IRReference result;
    switch(type) {
    case WASM_VALTYPE_I32:
        result = IR_REF_I32(0);
        break;
    case WASM_VALTYPE_I64:
    case WASM_VALTYPE_F32:
    case WASM_VALTYPE_F64:
        /* not implemented */
        assert(0);
    default:
        assert(0);
    }
    return result;
}

IRType ir_cast(WASMValtype t) {
    switch(t) {
        case WASM_VALTYPE_I32:
            return IR_TYPE_I32;
        case WASM_VALTYPE_I64:
            return IR_TYPE_I64;
        case WASM_VALTYPE_F32:
            return IR_TYPE_F32;
        case WASM_VALTYPE_F64:
            return IR_TYPE_F64;
        default:
            assert(0);
            return IR_TYPE_I32;
    }
}

void ir_free_function(IRFunction *f) {

    list_release(&f->block_list, ir_free_block, IRBlock, link);
    if (f->live_intervals != NULL) free(f->live_intervals);
    free(f);
}

void ir_free_block(IRBlock *b) {

    list_release(&b->phi_list, ir_free_phi, IRPhi, link);
    list_release(&b->instr_list, free, IRInstr, link);
    list_release(&b->pred_list, free, IRPredecessor, link);
    list_release(&b->loop_end_block_list, free, IRLoopEnd, link);
    if (b->locals != NULL) free(b->locals);
    if (b->incomplete_phi != NULL) free(b->incomplete_phi);
    free(b);
}

void ir_free_phi(IRPhi *phi) {

    list_release(&phi->phi_arg_list, free, IRPhiArg, link);
    list_release(&phi->use_list, free, IRUse, link);
    free(phi);
}

/* The following SSA construction algorithm is taken from this article:
 * Simple and Efficient Construction of Static Single Assignment Form.
 * Authors: Matthias Braun, Sebastian Buchwald, Sebastian Hack,
 * Roland Leißa, Christoph Mallon, and Andreas Zwinkau */

static int read_local_recursive(IRFunction *f, IRBlock *block,
                                 uint32_t localidx, IRReference *out);
static int add_phi_operands(IRFunction *f, IRPhi *phi,
                             uint32_t localidx, IRReference *out);
static int try_remove_trivial_phi(IRFunction *f, IRPhi *phi, IRReference *out);


void ir_write_local(IRBlock *block, uint32_t localidx, IRReference value) {

    block->locals[localidx] = value;
    ir_rm_usage(&block->locals[localidx]);
    ir_add_usage(&block->locals[localidx]);
}

int ir_read_local(IRFunction *f, IRBlock *block, uint32_t localidx, IRReference *out) {

    if (block->locals[localidx].type != IR_REF_TYPE_UNDEFINED) {
        if (out != NULL) *out = block->locals[localidx];
        return false;
    }

    /* If a block currently contains no definition for a variable,
    * we recursively look for a definition in its predecessors. */
    return read_local_recursive(f, block, localidx, out);
}

static int read_local_recursive(IRFunction *f, IRBlock *block,
                                uint32_t localidx, IRReference *out) {
    int err;
    IRReference value;
    if (!block->is_sealed) {
        /* Incomplete CFG */
        WASMValtype valtype = wasm_valtype_of(f->wasm_func, localidx);
        IRPhi *phi = ir_create_phi(f, block, (IRType)valtype);
        if (phi == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
        block->incomplete_phi[localidx] = phi;
        value = IR_REF_PHI(phi);
    } else if (block->pred_count == 0) {
        /* If the block has no predecessors, this means that
         * it is unreachable. Just return a default value. */
        WASMValtype valtype = wasm_valtype_of(f->wasm_func, localidx);
        value = ir_get_default(valtype);
    } else if (block->pred_count == 1) {
        /* If the block has a single predecessor, just query
         * it recursively for a definition. No phi needed */
        struct list_head *head = list_next(&block->pred_list);
        IRPredecessor *pred = container_of(head, IRPredecessor, link);
        err = ir_read_local(f, pred->ptr, localidx, &value);
        if (err) return err;
    } else {
       /* If the block has multiple predecessors we collect the
        * definitions from all predecessors and construct a phi function,
        * which joins them into a single new value. This phi function is
        * recorded as current definition in this basic block.
        * In both cases, looking for a value in a predecessor might in
        * turn lead to further recursive look-ups.*/

        /* Break potential cycles with operandless phi:
         * looking for a value in a predecessor might in turn lead to further
         * recursive look-ups. Due to loops in the program, those might lead
         * to endless recursion. Therefore, before recursing, we first create
         * the phi function without operands and record it as the current
         * definition for the variable in the block. Then, we determine the
         * phi function’s operands. If a recursive look-up arrives back at the
         * block, this phi function will provide a definition and the recursion
         * will end. */
        WASMValtype valtype = wasm_valtype_of(f->wasm_func, localidx);
        IRPhi *phi = ir_create_phi(f, block, (IRType)valtype);
        if (phi == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
        value = IR_REF_PHI(phi);
        ir_write_local(block, localidx, value);
        err = add_phi_operands(f, phi, localidx, &value);
        if (err) return err;
    }
    ir_write_local(block, localidx, value);
    if (out != NULL) *out = value;
    return WASMC_OK;
}

static int add_phi_operands(IRFunction *f, IRPhi *phi,
                             uint32_t localidx, IRReference *out) {

    /* Determine phi operands from the predecessors */
    IRReference local;
    bool err;

    IRPredecessor *pred;
    list_for_each_entry(pred, &phi->block->pred_list, link) {
        err = ir_read_local(f, pred->ptr, localidx, &local);
        if (err) return err;
        err = ir_append_phi_arg(phi, local, pred->ptr);
        if (err) return err;
    }
    /* Recursive look-up might leave redundant
     * phi functions, try to remove them */
    err = try_remove_trivial_phi(f, phi, out);
    if (err) return err;

    return WASMC_OK;
}

/* A phi function is trivial iff it just references itself
 * and one other value v any number of times: exists v such
 * that t = phi(x1, x2, ..., xn) and x1 ..., xn in {t, v}.
 *
 * Examples of trivial phi:
 * - t = phi()
 * - t = phi(t)
 * - t = phi(t, t, t, t)
 * - t = phi(v)
 * - t = phi(v, v, v)
 * - t = phi(t, v, t, t)
 * - t = phi(t, v, t, v, t, v)
 *
 * Examples of not trivial phi:
 * - t = phi(v1, v2)
 * - t = phi(t, v1, t, v2)
 * 
 * A trivial phi function can be removed and the value v is used instead.
 * As a special case, the phi function might use no other value besides
 * itself. This means that it is unreachable, We replace it by a default value. */
static int try_remove_trivial_phi(IRFunction *f, IRPhi *phi, IRReference *out) {

    IRReference same =  IR_REF_UNDEFINED;
    IRPhiArg *phi_arg;
    list_for_each_entry(phi_arg, &phi->phi_arg_list, link) {
        if (ir_reference_equal(&same, &phi_arg->value) ||
            (phi_arg->value.type == IR_REF_TYPE_PHI && phi_arg->value.as.phi == phi)) {
            /* Unique value or self-reference */
            continue;
        }
        if (same.type != IR_REF_TYPE_UNDEFINED) {
            /* The phi merges at least two values: not trivial */
            if (out != NULL) {
                *out = (IRReference) {
                    .type = IR_REF_TYPE_PHI,
                    .as.phi = phi,
                };
            }
            return false;
        }
        same = phi_arg->value;
    }

    if (same.type == IR_REF_TYPE_UNDEFINED) {
        same = ir_get_default((WASMValtype)phi->type);
    }

    /* Reroute all uses of phi to same */
    IRUse *use;
    list_for_each_entry(use, &phi->use_list, link) {
        *use->ref = same;
        int err = ir_add_usage(use->ref);
        if (err) return err;
    }

    /* remove 'phi' */
    list_del(&phi->link);
    f->tmp_count--;
    list_for_each_entry(phi_arg, &phi->phi_arg_list, link) {
       ir_rm_usage(&phi_arg->value);
    }
    ir_free_phi(phi);

    if (out != NULL) *out = same;
    return WASMC_OK;
}

int ir_seal_block(IRFunction *f, IRBlock *block) {
    for (uint32_t i = 0; i < f->ir_local_count; i++) {
        if (block->incomplete_phi[i] == NULL) continue;
        int err = add_phi_operands(f, block->incomplete_phi[i], i, NULL);
        if (err) return err;
    }
    block->is_sealed = true;
    free(block->incomplete_phi);
    block->incomplete_phi = NULL;
    return false;
}

