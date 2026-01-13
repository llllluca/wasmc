#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "libqbe.h"
#define BITMAP_IMPLEMENTATION
#include "bitmap.h"
#include "rv32/rv32i.h"

typedef enum MoveStatus {
    TO_MOVE,
    BEING_MOVED,
    MOVED,
} MoveStatus;

typedef struct Move {
    struct list_head link;
    IRReference from;
    Location to;
    MoveStatus status;
} Move;

#define for_each_output_operand(ref, ins) \
    for ((ref) = &(ins)->to; \
         ref = find_next_output_operand((ins), (ref)), (ref) != NULL; \
         (ref)++)

#define for_each_input_operand(ref, ins) \
    for ((ref) = &(ins)->to; \
         ref = find_next_input_operand((ins), (ref)), (ref) != NULL; \
         (ref)++)

#define LOCATION_STACK_SLOT(ir_func) \
    (Location) { \
        .type = LOCATION_TYPE_STACK_SLOT, \
        .as.stack_slot = (ir_func)->stack_slot_count++ \
    }

static bool location_equal(Location *loc1, Location *loc2) {
    if (loc1->type != loc2->type) return false;
    switch (loc1->type) {
    case LOCATION_TYPE_NONE:
        return true;
    case LOCATION_TYPE_REGISTER:
        return loc1->as.reg == loc2->as.reg;
    case LOCATION_TYPE_STACK_SLOT:
        return loc1->as.stack_slot == loc2->as.stack_slot;
    default:
        assert(0);
    }
}

void print_live_intervals(LiveInterval *live_intervals, unsigned int len) {
    for (unsigned int tmp_id = 0; tmp_id < len; tmp_id++) {
        printf("tmp_id: %u\n", tmp_id);
        printf("    start: %u\n", live_intervals[tmp_id].start);
        printf("    end:   %u\n", live_intervals[tmp_id].end);
    }
}

static void number_instructions(IRFunction *f) {
    unsigned int next_seqnum = 0;
    IRBlock *block;
    list_for_each_entry(block, &f->block_list, next) {
        block->seqnum = next_seqnum++;
        IRInstr *ins;
        list_for_each_entry(ins, &block->instr_list, next) {
            ins->seqnum = next_seqnum++;
        }
        block->jump.seqnum = next_seqnum++;
    }
}

static void add_range(LiveInterval *live_interval,
                      unsigned int start, unsigned int end) {

    assert(start < end);
    if (start < live_interval->start) {
        live_interval->start = start;
    }
    if (end > live_interval->end) {
        live_interval->end = end;
    }
}

static void set_start(LiveInterval *live_interval, unsigned int start) {
    if (live_interval->end == 0) {
        /* This happens when we have a temporary
         * variable that is defined but never used. */
        live_interval->start = start;
        live_interval->end = start;
    } else {
        assert(start < live_interval->end);
        live_interval->start = start;
    }
}

static IRReference *find_next_output_operand(IRInstr *ins, IRReference *offset) {
    if (offset != &ins->to) return NULL;
    if (ins->op == IR_OPCODE_STORE || ins->op == IR_OPCODE_STORE8) {
        return NULL;
    }
    if (ins->to.type != IR_REF_TYPE_TMP) {
        return NULL;
    }
    return &ins->to;
}

static IRReference *find_next_input_operand(IRInstr *ins, IRReference *offset) {

    if (offset < &ins->to|| &ins->arg[1] < offset) return NULL;

    if (ins->op != IR_OPCODE_STORE && ins->op != IR_OPCODE_STORE8) {
        if (offset == &ins->to) {
            offset = &ins->arg[0];
        }
    }

    for (IRReference *r = offset; r <= &ins->arg[1]; r++) {
        if (r->type == IR_REF_TYPE_TMP || r->type == IR_REF_TYPE_PHI) {
            return r;
        }
    }
    return NULL;
}

static IRPhiArg *input_of(IRPhi *phi, IRBlock *block) {

    IRPhiArg *phi_arg;
    list_for_each_entry(phi_arg, &phi->phi_arg_list, next) {
        if (block == phi_arg->predecessor) {
            return phi_arg;
        }
    }
    return NULL;
}

LiveInterval **build_intervals(IRFunction *f) {
    /* When building the body of a function, when a temporary variable is created,
     * it is assigned an id incrementally. next_tmp_id hold the next id to assign.
     * Some temporary variables can later be removed in a non-LIFO way. tmp_count
     * hold the total number of temporary variable currently used. */
    unsigned int n = f->next_tmp_id;
    unsigned int front = f->tmp_count;
    LiveInterval **sorted_intervals = NULL;
    LiveInterval *live_intervals = NULL;
    unsigned long *live = NULL;

    sorted_intervals = calloc(f->tmp_count, sizeof(struct LiveInterval *));
    if (sorted_intervals == NULL) goto ERROR;
    memset(sorted_intervals, 0, f->tmp_count);

    live_intervals = calloc(n, sizeof(struct LiveInterval));
    if (live_intervals == NULL) goto ERROR;

    /* Initialize the live intervals */
    for (unsigned int i = 0; i < n; i++) {
        live_intervals[i].start = UINT_MAX;
        live_intervals[i].end = 0;
        live_intervals[i].assign.type = LOCATION_TYPE_NONE;
        live_intervals[i].register_hint = -1;
    }

    /* Iterate for all blocks in reverse order so that all uses of temporary
     * are seen before its definition. Therefore successors of a block are
     * processed before this block. Only for loop, the loop header cannot be
     * processed before the loop end, so loops are handled as a special case. */
    IRBlock *block;
    list_for_each_entry_reverse(block, &f->block_list, next) {

        /* Initialize the set of temporaries that are live at the end of block b.
         * The initial set of tempraries that are live at the end of block b is
         * the union of all temporaries live at the beginning of the successor of b. */
        live = bitmap_alloc(n);
        if (live == NULL) goto ERROR;
        bitmap_zero(live, n);

        for (unsigned int i = 0; i < 2; i++) {
            IRBlock *successor = block->succ[i];
            /* successor->live_in can be NULL if successor is a back link of a loop */
            if (successor != NULL && successor->live_in != NULL) {
                bitmap_or(live, live, successor->live_in, n);
            }
        }

        /* Additionaly, phi functions of the successors contribute to the initial
         * live set. For each Phi, the input operand corresponting to block is
         * added to the live set. */
        IRPhi *phi;
        for (unsigned int i = 0; i < 2; i++) {
            IRBlock *successor = block->succ[i];
            if (successor == NULL) continue;
            list_for_each_entry(phi, &successor->phi_list, next) {
                IRPhiArg *phi_arg = input_of(phi, block);
                if (phi_arg->value.type == IR_REF_TYPE_TMP) {
                    bitmap_set_bit(live, phi_arg->value.as.tmp_id);
                }
            }
        }

        /* For each live temporaries, an initial lifetime interval covering
         * the entire block is added to te live set. This live range might me
         * shortened later if the definition of a temporary is encountered. */
        unsigned int tmp_id;
        for_each_set_bit(tmp_id, live, n) {
            add_range(&live_intervals[tmp_id], block->seqnum, block->jump.seqnum);
        }

        /* Next all instructions of b are processed in reverse order, hence
         * the jump at the end of b is the first instruction processed. An
         * output operand (i.e., a definition of a temporary) shortens the lifetime
         * interval of the temporary; the start position of the lifetime interval is
         * set to the current instruction number. Additionally, the temporary is removed
         * from the list of live temporary. An input operand (i.e. a use of a temporary)
         * adds a new lifetime interval (the new lifetime interval is merged if
         * a lifetime interval is already present). The new lifetime interval
         * starts at the beginning of the block, and again might be shortened
         * later. Additionally, the temporary is added to the list of live temporary.*/

        //The final jump can contain an input operand (i.e. a use of a temporary).
        if (block->jump.type == IR_JUMP_TYPE_JNZ || block->jump.type == IR_JUMP_TYPE_RET1) {
            IRReference jump_arg = block->jump.arg;
            if (jump_arg.type == IR_REF_TYPE_TMP || jump_arg.type == IR_REF_TYPE_PHI) {
                unsigned int tmp_id = jump_arg.type == IR_REF_TYPE_TMP ? jump_arg.as.tmp_id : jump_arg.as.phi->id;
                add_range(&live_intervals[tmp_id], block->seqnum, block->jump.seqnum);
                bitmap_set_bit(live, tmp_id);
            }
        }

        IRInstr *ins;
        list_for_each_entry_reverse(ins, &block->instr_list, next) {

            /* All the IRInstr contain a single a output operand (i.e. a definition
             * of a temporary) in the 'to' field. The store opcodes family are special
             * instruction because also the 'to' field is a input operand */
            unsigned int tmp_id;
            IRReference *opd;
            for_each_output_operand(opd, ins) {
                tmp_id = opd->as.tmp_id;
                LiveInterval *live_int = &live_intervals[tmp_id];
                set_start(live_int, ins->seqnum);
                bitmap_clear_bit(live, tmp_id);
                /* When a definition of a temporary is encountered, its lifetime
                 * interval is saved to the array in ascending order of start
                 * point. The following line works because the instructions
                 * are numbered in such a way when iterated in reverse order,
                 * the instructions are encountered in descending order of
                 * sequence number. */
                sorted_intervals[--front] = live_int;
            }

            for_each_input_operand(opd, ins) {
                tmp_id = opd->type == IR_REF_TYPE_TMP ? opd->as.tmp_id : opd->as.phi->id;
                add_range(&live_intervals[tmp_id], block->seqnum, ins->seqnum);
                bitmap_set_bit(live, tmp_id);
            }
        }
        /* Phi functions are iterated separately. Because the lifetime
         * interval of a phi function starts at the beginning of the block,
         * it is not necessary to shorten the lifetime interval for its
         * output operand. The operand is only removed from the set of
         * live registers. The input operands of the phi function are not
         * handled here, because this is done independently when the different
         * predecessors are processed. Thus, neither an input operand nor the
         * output operand of a phi function is live at the beginning of the
         * phi function’s block. */
        list_for_each_entry(phi, &block->phi_list, next) {
            bitmap_clear_bit(live, phi->id);
            sorted_intervals[--front] = &live_intervals[phi->id];
        }

        if (block->is_loop_header) {
            unsigned int max = 0;
            IRLoopEnd *loop_end;
            assert(!list_empty(&block->loop_end_block_list));
            list_for_each_entry(loop_end, &block->loop_end_block_list, next) {
                if (loop_end->ptr->jump.seqnum > max) {
                    max = loop_end->ptr->jump.seqnum;
                }
            }
            list_release(&block->loop_end_block_list, free, IRLoopEnd, next);
            INIT_LIST_HEAD(&block->loop_end_block_list);

            unsigned int tmp_id;
            for_each_set_bit(tmp_id, live, n) {
                add_range(&live_intervals[tmp_id], block->seqnum, max);
            }
        }
        block->live_in = live;
    }

    f->live_intervals = live_intervals;

    list_for_each_entry(block, &f->block_list, next) {
        bitmap_free(block->live_in);
        block->live_in = NULL;
    }

    /* Assert that the length of sorted_intervals is equal to the length of
     * tmp list of the function */
    assert(front == 0);

    return sorted_intervals;

ERROR:
    if (sorted_intervals) free(sorted_intervals);
    if (live_intervals) free(live_intervals);
    if (live) bitmap_free(live);
    return NULL;
}

static void register_hints(IRFunction *f) {

    uint32_t tmp_id;

    /* Register hints for function parameters */
    unsigned int par_index = 0;
    IRInstr *ins;
    list_for_each_entry(ins, &f->start->instr_list, next) {
        if (ins->op != IR_OPCODE_PAR) break;
        tmp_id = ins->to.as.tmp_id;
        LiveInterval *live_int = &f->live_intervals[tmp_id];
        if (live_int->register_hint == -1) {
            live_int->register_hint = rv32_arg_reg[par_index++];
        }
    }

    unsigned int arg_index = 0;
    IRBlock *block;
    list_for_each_entry(block, &f->block_list, next) {
        IRInstr *ins;
        /* Register hints for function calls */
        list_for_each_entry(ins, &block->instr_list, next) {
            if (ins->op == IR_OPCODE_ARG) {
                IRReference arg = ins->arg[0];
                if (arg.type == IR_REF_TYPE_TMP || arg.type == IR_REF_TYPE_PHI) {
                    tmp_id = arg.type == IR_REF_TYPE_TMP ? arg.as.tmp_id : arg.as.phi->id;
                    LiveInterval *live_int = &f->live_intervals[tmp_id];
                    if (live_int->register_hint == -1) {
                        live_int->register_hint = rv32_arg_reg[arg_index++];
                    }

                }
            } else if (ins->op == IR_OPCODE_CALL) {
                arg_index = 0;
                if (ins->to.type == IR_REF_TYPE_TMP) {
                    tmp_id = ins->to.as.tmp_id;
                    LiveInterval *live_int = &f->live_intervals[tmp_id];
                    if (live_int->register_hint == -1) {
                        live_int->register_hint = A0;
                    }
                }
            }
        }

        /* Register hints for non-void reuturns */
        if (block->jump.type == IR_JUMP_TYPE_RET1) {
            IRReference arg = block->jump.arg;
            if (arg.type == IR_REF_TYPE_TMP || arg.type == IR_REF_TYPE_PHI) {
                tmp_id = arg.type == IR_REF_TYPE_TMP ? arg.as.tmp_id : arg.as.phi->id;
                LiveInterval *live_int = &f->live_intervals[tmp_id];
                if (live_int->register_hint == -1) {
                    live_int->register_hint = A0;
                }
            }
        }
    }
}

static __always_inline void
active_insert(struct list_head *active_list, LiveInterval *live_int) {

    /* Insert live_int in the active_list maintaining
     * active_list sorted by increasing end point */
    LiveInterval *iter;
    list_for_each_entry(iter, active_list, link) {
        if (iter->end > live_int->end) {
            break;
        }
    }
    list_add_tail(&live_int->link, &iter->link);
}

static __always_inline void
assign_register(IRFunction *f, LiveInterval *live_int,
                unsigned long *reg_pool, unsigned int nreg) {

    unsigned long reg;
    if (live_int->register_hint != -1) {
        reg = live_int->register_hint;
    } else {
        reg = find_next_bit(reg_pool, nreg, 0);
    }
    live_int->assign.type = LOCATION_TYPE_REGISTER;
    bitmap_clear_bit(reg_pool, reg);
    live_int->assign.as.reg = reg;
    //TODO: use bitmap instead array of bool
    if (rv32_is_callee_saved[reg]) {
        f->regs_to_preserve[reg] = true;
    }
}

static void linear_scan(IRFunction *f, LiveInterval **sorted_intervals,
                        unsigned long *reg_pool, unsigned int nreg) {

    unsigned int n = f->tmp_count;
    /* Invariant: active_list is kept sorted in order of increasing end point */
    struct list_head active_list;
    unsigned int active_count = 0;
    INIT_LIST_HEAD(&active_list);

    for (unsigned int i = 0; i < n; i++) {
        /* The intervals in sorted_intervals are sorted
         * in order of increasing start point */
        LiveInterval *live_int = sorted_intervals[i];

        /* Expire old intervals */
        LiveInterval *active_int, *iter;
        /* for each interval in order of encreasing end point */
        list_for_each_entry_safe_reverse(active_int, iter, &active_list, link) {
            if (active_int->end > live_int->start) break;
            /* remove active_int from active_list */
            list_del(&active_int->link);
            active_count--;
            /* add the register assigned to active_int back to the free register pool */
            bitmap_set_bit(reg_pool, active_int->assign.as.reg);
        }

        if (active_count == nreg) {
            /* One of the current live intervals (from active_list or live_int)
             * must be spilled. The heuristic used here spills the interval that end
             * last, furthest away from the current point. We can find the interval
             * to spill quckly because active_list is sorted by increasing end point:
             * the interval to spill either live_int or the last interval in
             * active_list, whichever ends later. */
            LiveInterval *spill =
                container_of(list_prev(&active_list), LiveInterval, link);
            if (spill->end > live_int->end) {
                live_int->assign = spill->assign;
                spill->assign = LOCATION_STACK_SLOT(f);
                list_del(&spill->link);
                /* add live_int to active_list sorted by increasing end point */
                active_insert(&active_list, live_int);
            } else {
                live_int->assign = LOCATION_STACK_SLOT(f);
            }
        } else {
            /* assign to live_int a register removed from the pool of free registers */
            assign_register(f, live_int, reg_pool, nreg);
            /* add live_int to active_list sorted by increasing end point */
            active_insert(&active_list, live_int);
            active_count++;
        }
    }
}

static __always_inline void
replace_tmp_with_location(IRFunction *f, IRReference *r) {

    unsigned int tmp_id;
    if (r->type != IR_REF_TYPE_TMP) return;
    tmp_id = r->as.tmp_id;
    r->type = IR_REF_TYPE_LOCATION;
    r->as.location = &f->live_intervals[tmp_id].assign;
}

static void replace_tmps_with_locations(IRFunction *f) {

    IRBlock *block;
    list_for_each_entry(block, &f->block_list, next) {
        IRInstr *ins;
        list_for_each_entry(ins, &block->instr_list, next) {
            replace_tmp_with_location(f, &ins->to);
            replace_tmp_with_location(f, &ins->arg[0]);
            replace_tmp_with_location(f, &ins->arg[1]);
        }
        if (block->jump.type == IR_JUMP_TYPE_JNZ ||
            block->jump.type == IR_JUMP_TYPE_RET1) {
            replace_tmp_with_location(f, &block->jump.arg);
        }
    }
}

#if 0

#define MOVE_FROM_EQ_TO(from, to) \
    (from.type != CONST && LOCATION_EQ(from.as.loc, to))

static Ins *alloc_copy(location *dst, move_from *src) {
    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->op = IR_OPCODE_COPY;
    ins->type = IR_TYPE_I32;

    ins->to.type = REF_TYPE_LOCATION;
    ins->to.as.loc = *dst;

    switch (src->type) {
        case CONST:
            ins->arg[0].type = REF_TYPE_INT32_CONST;
            ins->arg[0].as.int32_const = src->as.int32_const;
            break;
        case LOCATION: {
            ins->arg[0].type = REF_TYPE_LOCATION;
            ins->arg[0].as.loc = src->as.loc;
        } break;
        default:
            panic();
    }
    ins->arg[1] = UNDEFINED_REF;
    return ins;
}



static void insert_move_at_start(Blk *b, list *ins_list) {
    listNode *ins_node;
    listNode *ins_iter = listLast(ins_list);
    while ((ins_node = listPrev(&ins_iter)) != NULL) {
        listUnlinkNode(ins_list, ins_node);
        listLinkNodeHead(b->ins_list, ins_node);
    }
}

static void insert_move_at_end(Blk *b, list *ins_list) {
    listNode *ins_node;
    listNode *ins_iter = listLast(ins_list);
    while ((ins_node = listPrev(&ins_iter)) != NULL) {
        listUnlinkNode(ins_list, ins_node);
        listLinkNodeTail(b->ins_list, ins_node);
    }
}

static void insert_move_sequence(Fn* f,
    list *move_seq, Blk *pred, Blk *succ) {

    if (listLength(move_seq) == 0) return;
    if (listLength(succ->preds) > 1 &&
        pred->succ[0] != NULL && pred->succ[1] != NULL) {
        /* succ has pred and other predecessors and
         * pred has succ and another one successor */
        Blk *b = newBlock(f);
        if (pred->succ[0] == succ) {
            pred->succ[0] = b;
        } else {
            pred->succ[1] = b;
        }
        insert_move_at_start(b, move_seq);
        jmp(f, b, succ);
    } else if (pred->succ[0] != NULL && pred->succ[1] != NULL) {
        /* succ has only pred as predecessor and
         * pred has succ and another one successor */
        insert_move_at_start(succ, move_seq);
    } else {
        /* succ has pred and other predecessors and
         * pred has only succ as successor */
        insert_move_at_end(pred, move_seq);
    }
}


#endif

static bool has_same_location(IRFunction *f, unsigned int to_id, IRReference *from) {
    if (from->type != IR_REF_TYPE_TMP && from->type != IR_REF_TYPE_PHI) {
        return false;
    }
    Location *to_loc = &f->live_intervals[to_id].assign;
    unsigned int from_id;
    from_id = from->type == IR_REF_TYPE_TMP ? from->as.tmp_id : from->as.phi->id;
    Location *from_loc = &f->live_intervals[from_id].assign;
    return location_equal(to_loc, from_loc);
}

static bool parallel_move_from_phis(IRFunction *f, IRBlock *pred,
                                    IRBlock *succ, struct list_head *out) {
    IRPhi *phi;
    list_for_each_entry(phi, &succ->phi_list, next) {
        IRPhiArg *phi_arg = input_of(phi, pred);
        if (!has_same_location(f, phi->id, &phi_arg->value)) {
            Move *move = malloc(sizeof(struct Move));
            if (move == NULL) return true;
            move->to = f->live_intervals[phi->id].assign;
            move->from = phi_arg->value;
            list_add_tail(&move->link, out);
        }
    }
    return false;
}

/*
static void move_one(move *m, list* par_move, list *move_seq) {
    if (MOVE_FROM_EQ_TO(m->from, m->to)) return;
    m->status = BEING_MOVED;
    location tmp = {
        .type = REGISTER,
        .as.reg = rv32_priv_reg[0],
    };
    listNode *move_node;
    listNode *move_iter = listFirst(par_move);
    while ((move_node = listNext(&move_iter)) != NULL) {
        move *x = listNodeValue(move_node);
        if (MOVE_FROM_EQ_TO(x->from, m->to)) {
            switch (x->status) {
                case TO_MOVE:
                    move_one(x, par_move, move_seq);
                    break;
                case BEING_MOVED: {
                    Ins *i = alloc_copy(&tmp, &x->from);
                    listAddNodeTail(move_seq, i);
                    x->from.type = LOCATION;
                    x->from.as.loc = tmp;
                } break;
                case MOVED:
                    break;
                default:
                    panic();
            }
        }
    }
    Ins *i = alloc_copy(&m->to, &m->from);
    listAddNodeHead(move_seq, i);
    m->status = MOVED;
}

static list *sequentialize(list *par_move) {
    list *move_seq = listCreate();
    listNode *move_node;
    listNode *move_iter = listFirst(par_move);
    while ((move_node = listNext(&move_iter)) != NULL) {
        move *move = listNodeValue(move_node);
        move->status = TO_MOVE;
    }

    move_iter = listFirst(par_move);
    while ((move_node = listNext(&move_iter)) != NULL) {
        move *move = listNodeValue(move_node);
        if (move->status == TO_MOVE) {
            move_one(move, par_move, move_seq);
        }
    }

    return move_seq;
}
*/

static bool resolve_edge(IRFunction *f, IRBlock *pred, IRBlock *succ) {

    struct list_head pmove;
    INIT_LIST_HEAD(&pmove);

    bool err = false;

    err = parallel_move_from_phis(f, pred, succ, &pmove);
    if (err) goto ERROR;

    /*
    if (!list_empty(&pmove)) {
        printf("parallel move:\n");
    }
    Move *move2, *iter2;
    list_for_each_entry_safe(move2, iter2, &pmove, link) {
        ir_print_location(&move2->to, stdout);
        printf(" = ");
        ir_print_ref(move2->from, stdout);
        printf("\n");
    }
    */

    struct list_head smove;
    INIT_LIST_HEAD(&smove);

    //err = sequentialize_parallel_copy(&pmove, &smove);
    //if (err) goto ERROR;

    //err = insert_copy_sequence(f, &smove, pred, succ);
    //if (err) goto ERROR;

ERROR:
    ; /* avoid c parser errors */
    /* free pmove */
    Move *move, *iter;
    list_for_each_entry_safe(move, iter, &pmove, link) {
        list_del(&move->link);
        free(move);
    }
    /* free smove */
    return err;
}

/* Before code generation, it is necessary to eliminate phi-functions since
 * these are not executable machine instructions. This elimination phase is
 * called SSA destruction. */
static bool ssa_deconstruction(IRFunction *f) {

    IRBlock *block;
    IRBlock *successor;
    bool err;

    list_for_each_entry(block, &f->block_list, next) {
        successor = block->succ[0];
        if (successor != NULL) {
            err = resolve_edge(f, block, successor);
            if (err) return err;
        }
        successor = block->succ[1];
        if (successor != NULL) {
            err = resolve_edge(f, block, successor);
            if (err) return err;
        }
    }

    /* phis don't exists anymore, replace the phi references with tmp references */
    list_for_each_entry(block, &f->block_list, next) {
        IRPhi *phi;
        list_for_each_entry(phi, &block->phi_list, next) {
            IRUse *use;
            list_for_each_entry(use, &phi->use_list, next) {
                use->ref->type = IR_REF_TYPE_TMP;
                use->ref->as.tmp_id = phi->id;
            }
        }
    }

    /* free phi_list for all block b */
    list_for_each_entry(block, &f->block_list, next) {
        IRPhi *phi, *iter;
        list_for_each_entry_safe(phi, iter, &block->phi_list, next) {
            list_del(&phi->next);
            ir_free_phi(phi);
        }
    }

    return false;
}

#if 0
static void ensure_abi_constraints(IRFunction *f) {
    location a0 = {
        .type = REGISTER,
        .as.reg = A0,
    };
    list *par_move = listCreate();
    listSetFreeMethod(par_move, free);
    unsigned int arg_index = 0;

    listNode *ins_node;
    listNode *ins_iter = listFirst(f->start->ins_list);
    while ((ins_node = listNext(&ins_iter)) != NULL) {
        Ins *ins = listNodeValue(ins_node);
        if (ins->op != IR_OPCODE_PAR) break;
        assert(ins->to.type == REF_TYPE_TMP);
        Tmp *t = listNodeValue(ins->to.as.tmp_node);
        if (arg_index > RV32_ARG_NUM_REG) panic();
        move_from from = {
            .type = LOCATION,
            .as.loc = {
                .type = REGISTER,
                .as.reg = rv32_arg_reg[arg_index++],
            },
        };
        location to = t->i->assign;
        if (!MOVE_FROM_EQ_TO(from, to)) {
            move *move = xmalloc(sizeof(struct move));
            move->from = from;
            move->to = to;
            listAddNodeTail(par_move, move);
        }
        listDelNode(f->start->ins_list, ins_node);
    }

    list *move_seq = sequentialize(par_move);
    listNode *copy_node;
    listNode *copy_iter = listFirst(move_seq);
    while ((copy_node = listNext(&copy_iter)) != NULL) {
        listUnlinkNode(move_seq, copy_node);
        listLinkNodeHead(f->start->ins_list, copy_node);
    }
    arg_index = 0;
    listRelease(move_seq);
    listEmpty(par_move);

    listNode *blk_node;
    listNode *blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);

        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *ins = listNodeValue(ins_node);
            if (ins->op == IR_OPCODE_ARG) {
                Ref *r = &ins->arg[0];
                move_from from;
                switch (r->type) {
                    case REF_TYPE_TMP: {
                        Tmp *t = listNodeValue(r->as.tmp_node);
                        from.type = LOCATION;
                        from.as.loc = t->i->assign;
                    } break;
                    case REF_TYPE_INT32_CONST: {
                        from.type = CONST;
                        from.as.int32_const = r->as.int32_const;
                    } break;
                    default:
                        assert(0);
                }
                listDelNode(b->ins_list, ins_node);

                location to;
                to.type = REGISTER;
                if (arg_index > RV32_ARG_NUM_REG) panic();
                to.as.reg = rv32_arg_reg[arg_index++];

                if (!MOVE_FROM_EQ_TO(from, to)) {
                    move *move = xmalloc(sizeof(struct move));
                    move->from = from;
                    move->to = to;
                    listAddNodeTail(par_move, move);
                }
            }
            else if (ins->op == IR_OPCODE_CALL) {
                listNode *call_node = ins_node;

                Ins *func_call = listNodeValue(call_node);
                list *survivors = listCreate();
                for (unsigned int j = 0; intervals[j] != NULL; j++) {
                    live_interval *li = intervals[j];
                    if (li->assign.type == REGISTER &&
                        li->start < func_call->id &&
                        li->end > func_call->id) {

                        listAddNodeTail(survivors, li);
                    }
                }

                listNode *survivor_node;
                listNode *survivor_iter = listFirst(survivors);
                while ((survivor_node = listNext(&survivor_iter)) != NULL) {
                    live_interval *li = listNodeValue(survivor_node);
                    Ins *push_ins = xmalloc(sizeof(struct Ins));
                    push_ins->op = IR_OPCODE_PUSH;
                    push_ins->type = IR_TYPE_I32;
                    push_ins->to = UNDEFINED_REF;
                    push_ins->arg[0].type = REF_TYPE_LOCATION;
                    push_ins->arg[0].as.loc = li->assign;
                    push_ins->arg[1] = UNDEFINED_REF;
                    listInsertNodeBefore(b->ins_list, call_node, push_ins);
                }

                survivor_iter = listFirst(survivors);
                while ((survivor_node = listNext(&survivor_iter)) != NULL) {
                    live_interval *li = listNodeValue(survivor_node);
                    Ins *pop_ins = xmalloc(sizeof(struct Ins));
                    pop_ins->op = IR_OPCODE_POP;
                    pop_ins->type = IR_TYPE_I32;
                    pop_ins->to.type = REF_TYPE_LOCATION;
                    pop_ins->to.as.loc = li->assign;
                    pop_ins->arg[0] = UNDEFINED_REF;
                    pop_ins->arg[1] = UNDEFINED_REF;
                    listInsertNodeAfter(b->ins_list, call_node, pop_ins);
                }

                listRelease(survivors);

                list *move_seq = sequentialize(par_move);
                listNode *copy_node;
                listNode *copy_iter = listLast(move_seq);
                while ((copy_node = listPrev(&copy_iter)) != NULL) {
                    listUnlinkNode(move_seq, copy_node);
                    listLinkNodeBefore(b->ins_list, call_node, copy_node);
                }
                arg_index = 0;
                listRelease(move_seq);
                listEmpty(par_move);

                if (func_call->to.type == REF_TYPE_TMP && func_call->to.as.tmp_node != NULL) {
                    Tmp *t = listNodeValue(func_call->to.as.tmp_node);
                    if (!LOCATION_EQ(a0, t->i->assign)) {
                        move_from src = {
                            .type = LOCATION,
                            .as.loc = a0
                        };
                        Ins *ins = alloc_copy(&t->i->assign, &src);
                        listInsertNodeAfter(b->ins_list, call_node, ins);
                    }
                    func_call->to = UNDEFINED_REF;
                }
            }
        }

        if (b->jmp.type == RET1_JUMP_TYPE) {
            move_from src;
            Ref *r = &b->jmp.arg;
            switch (r->type) {
                case REF_TYPE_TMP: {
                    Tmp *t = listNodeValue(r->as.tmp_node);
                    src.type = LOCATION;
                    src.as.loc = t->i->assign;
                } break;
                case REF_TYPE_INT32_CONST: {
                    src.type = CONST;
                    src.as.int32_const = r->as.int32_const;
                } break;
                default:
                    assert(0);
            }
            if (!MOVE_FROM_EQ_TO(src, a0)) {
                Ins *ins = alloc_copy(&a0, &src);
                listAddNodeTail(b->ins_list, ins);
            }
            b->jmp.type = RET0_JUMP_TYPE;
        }
    }
    listRelease(par_move);
}
#endif

bool register_allocation(IRFunction *f) {

    number_instructions(f);
    LiveInterval **sorted_intervals = build_intervals(f);
    if (sorted_intervals == NULL) return true;
    register_hints(f);

    unsigned long reg_pool = 0;
    unsigned int nreg = RV32_GP_NUM_REG + RV32_ARG_NUM_REG;
    for (unsigned int i = 0; i < RV32_GP_NUM_REG; i++) {
        bitmap_set_bit(&reg_pool, rv32_gp_reg[i]);
    }
    for (unsigned int i = 0; i < RV32_ARG_NUM_REG; i++) {
        bitmap_set_bit(&reg_pool, rv32_gp_reg[i]);
    }
    linear_scan(f, sorted_intervals, &reg_pool, nreg);
    free(sorted_intervals);
    //ensure_abi_constraints(f);

    bool err;
    err = ssa_deconstruction(f);
    if (err) return true;
    replace_tmps_with_locations(f);

    return false;
}
