#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "ir.h"
#define BITMAP_IMPLEMENTATION
#include "bitmap.h"

typedef enum MoveStatus {
    TO_MOVE,
    BEING_MOVED,
    MOVED,
} MoveStatus;

typedef struct Move {
    struct list_head link;
    IRReference from;
    Location *to;
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

static Location tmp = {
    .type = LOCATION_TYPE_REGISTER,
    .as.reg = RV32_PRIVATE_REG0,
};

static Location args[RV32_ARG_NUM_REG] = {
    [0] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A0,
    },
    [1] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A1,
    },
    [2] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A2,
    },
    [3] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A3,
    },
    [4] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A4,
    },
    [5] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A5,
    },
    [6] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A6,
    },
    [7] = {
        .type = LOCATION_TYPE_REGISTER,
        .as.reg = A7,
    }
};

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

static void number_instructions(IRFunction *f) {
    unsigned int next_seqnum = 0;
    IRBlock *block;
    list_for_each_entry(block, &f->block_list, link) {
        block->seqnum = next_seqnum++;
        IRInstr *ins;
        list_for_each_entry(ins, &block->instr_list, link) {
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
        if (r->type == IR_REF_TYPE_TMP) {
            return r;
        }
    }
    return NULL;
}

static IRPhiArg *input_of(IRPhi *phi, IRBlock *block) {

    IRPhiArg *phi_arg;
    list_for_each_entry(phi_arg, &phi->phi_arg_list, link) {
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
    list_for_each_entry_reverse(block, &f->block_list, link) {

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
            list_for_each_entry(phi, &successor->phi_list, link) {
                IRPhiArg *phi_arg = input_of(phi, block);
                IRReference v = phi_arg->value;
                if (v.type == IR_REF_TYPE_TMP) {
                    bitmap_set_bit(live, v.as.tmp_id);
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
            if (jump_arg.type == IR_REF_TYPE_TMP) {
                unsigned int tmp_id = jump_arg.as.tmp_id;
                add_range(&live_intervals[tmp_id], block->seqnum, block->jump.seqnum);
                bitmap_set_bit(live, tmp_id);
            }
        }

        IRInstr *ins;
        list_for_each_entry_reverse(ins, &block->instr_list, link) {

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
        list_for_each_entry(phi, &block->phi_list, link) {
            bitmap_clear_bit(live, phi->id);
            sorted_intervals[--front] = &live_intervals[phi->id];
        }

        if (block->is_loop_header) {
            unsigned int max = 0;
            IRLoopEnd *loop_end;
            assert(!list_empty(&block->loop_end_block_list));
            list_for_each_entry(loop_end, &block->loop_end_block_list, link) {
                if (loop_end->ptr->jump.seqnum > max) {
                    max = loop_end->ptr->jump.seqnum;
                }
            }
            list_release(&block->loop_end_block_list, free, IRLoopEnd, link);
            INIT_LIST_HEAD(&block->loop_end_block_list);

            unsigned int tmp_id;
            for_each_set_bit(tmp_id, live, n) {
                add_range(&live_intervals[tmp_id], block->seqnum, max);
            }
        }
        block->live_in = live;
    }

    f->live_intervals = live_intervals;

    list_for_each_entry(block, &f->block_list, link) {
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
    list_for_each_entry(ins, &f->start->instr_list, link) {
        if (ins->op != IR_OPCODE_PAR) break;
        tmp_id = ins->to.as.tmp_id;
        LiveInterval *live_int = &f->live_intervals[tmp_id];
        if (live_int->register_hint == -1) {
            live_int->register_hint = rv32_arg_reg[par_index++];
        }
    }

    unsigned int arg_index = 0;
    IRBlock *block;
    list_for_each_entry(block, &f->block_list, link) {
        IRInstr *ins;
        /* Register hints for function calls */
        list_for_each_entry(ins, &block->instr_list, link) {
            if (ins->op == IR_OPCODE_ARG) {
                IRReference arg = ins->arg[0];
                if (arg.type == IR_REF_TYPE_TMP) {
                    tmp_id = arg.as.tmp_id;
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
            if (arg.type == IR_REF_TYPE_TMP) {
                tmp_id = arg.as.tmp_id;
                LiveInterval *live_int = &f->live_intervals[tmp_id];
                if (live_int->register_hint == -1) {
                    live_int->register_hint = A0;
                }
            }
        }
    }

    list_for_each_entry(block, &f->block_list, link) {
        IRPhi *phi;
        list_for_each_entry(phi, &block->phi_list, link) {
            LiveInterval *phi_live_int = &f->live_intervals[phi->id];
            if (phi_live_int->register_hint == -1) continue;
            IRPhiArg *phi_arg;
            list_for_each_entry(phi_arg, &phi->phi_arg_list, link) {
                IRReference arg = phi_arg->value;
                if (arg.type == IR_REF_TYPE_TMP) {
                    tmp_id = arg.as.tmp_id;
                    LiveInterval *live_int = &f->live_intervals[tmp_id];
                    if (live_int->register_hint == -1) {
                        live_int->register_hint = phi_live_int->register_hint;
                    }
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
    int hint = live_int->register_hint;
    if (hint != -1 && bitmap_test_bit(reg_pool, hint)) {
        reg = hint;
    } else {
        reg = find_next_bit(reg_pool, nreg, 0);
        assert(reg != nreg);
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
        list_for_each_entry_safe(active_int, iter, &active_list, link) {
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

static void replace_references_with_locations(IRFunction *f) {

    IRBlock *block;
    list_for_each_entry(block, &f->block_list, link) {
        IRInstr *ins;
        list_for_each_entry(ins, &block->instr_list, link) {
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

static bool have_same_location(IRFunction *f, Location *to_loc, IRReference *from) {

    unsigned int from_id;
    Location *from_loc;

    switch (from->type) {
    case IR_REF_TYPE_TMP:
        from_id = from->as.tmp_id;
        from_loc = &f->live_intervals[from_id].assign;
        break;
    case IR_REF_TYPE_PHI:
        from_id = from->as.phi->id;
        from_loc = &f->live_intervals[from_id].assign;
        break;
    case IR_REF_TYPE_LOCATION:
        from_loc = from->as.location;
        break;
    default:
        return false;
    }

    return location_equal(to_loc, from_loc);
}

static bool parallel_move_from_phis(IRFunction *f, IRBlock *pred,
                                    IRBlock *succ, struct list_head *out) {
    IRPhi *phi;
    list_for_each_entry(phi, &succ->phi_list, link) {
        IRPhiArg *phi_arg = input_of(phi, pred);
        Location *to_loc = &f->live_intervals[phi->id].assign;
        if (!have_same_location(f, to_loc, &phi_arg->value)) {
            Move *move = malloc(sizeof(struct Move));
            if (move == NULL) return true;
            move->to = &f->live_intervals[phi->id].assign;
            move->from = phi_arg->value;
            list_add_tail(&move->link, out);
        }
    }
    return false;
}

static __always_inline IRInstr *alloc_copy(Location *dst, IRReference *src) {
    IRInstr *ins = malloc(sizeof(struct IRInstr));
    if (ins == NULL) return NULL;
    ins->op = IR_OPCODE_COPY;
    ins->type = IR_TYPE_I32;
    ins->to.type = IR_REF_TYPE_LOCATION;
    ins->to.as.location = dst;
    ins->arg[0] = *src;
    ins->arg[1].type = IR_REF_TYPE_UNDEFINED;
    ins->seqnum = 0;
    return ins;
}

static bool move_one(IRFunction *f, Move *m,
                     struct list_head *pmove, struct list_head *smove) {

    if (have_same_location(f, m->to, &m->from)) {
        return false;
    }
    m->status = BEING_MOVED;
    Move *x;
    list_for_each_entry(x, pmove, link) {
        if (have_same_location(f, m->to, &x->from)) {
            switch (x->status) {
            case TO_MOVE: {
                bool err = move_one(f, x, pmove, smove);
                if (err) return true;
            } break;
            case BEING_MOVED: {
                IRInstr *ins = alloc_copy(&tmp, &x->from);
                if (ins == NULL) return true;
                list_add_tail(&ins->link, smove);
                x->from.type = IR_REF_TYPE_LOCATION;
                x->from.as.location = &tmp;
            } break;
            case MOVED:
                break;
            default:
                assert(0);
            }
        }
    }
    IRInstr *ins = alloc_copy(m->to, &m->from);
    if (ins == NULL) return true;
    list_add_tail(&ins->link, smove);
    m->status = MOVED;
    return false;
}

static bool sequentialize_parallel_move(IRFunction *f, struct list_head *pmove,
                                        struct list_head *smove) {
    Move *move;
    list_for_each_entry(move, pmove, link) {
        move->status = TO_MOVE;
    }

    bool err;
    list_for_each_entry(move, pmove, link) {
        if (move->status == TO_MOVE) {
            err = move_one(f, move, pmove, smove);
            if (err) return true;
        }
    }
    return false;
}


static __always_inline void
insert_move_at_start(IRBlock *b, struct list_head *smove) {

    IRInstr *ins, *iter;
    list_for_each_entry_safe_reverse(ins, iter, smove, link) {
        list_del(&ins->link);
        list_add(&ins->link, &b->instr_list);
    }
}

static __always_inline void
insert_move_at_end(IRBlock *b, struct list_head *smove) {

    IRInstr *ins, *iter;
    list_for_each_entry_safe(ins, iter, smove, link) {
        list_del(&ins->link);
        list_add_tail(&ins->link, &b->instr_list);
    }
}

static bool insert_sequence_of_moves(IRFunction *f, struct list_head *smove,
                                     IRBlock *pred, IRBlock *succ) {
    if (list_empty(smove)) {
        return false;
    }

    bool err;
    if (succ->pred_count > 1 &&
        pred->succ[0] != NULL && pred->succ[1] != NULL) {
        /* 'succ' has 'pred' and other block as predecessors
         * 'pred' has 'succ' and another block as successors
         * => the edge between 'pred' and 'succ' is a critical edge */

        /* Critical edge splitting */
        IRBlock *block = ir_create_sealed_block_without_locals(f);
        if (block == NULL) return true;
        if (pred->succ[0] == succ) {
            pred->succ[0] = block;
        } else {
            pred->succ[1] = block;
        }
        /* 'block' is an empty block, insert the sequence of moves
         * at the start or at then end of 'block' if the same */
        insert_move_at_start(block, smove);

        /* add a jmp from 'block' to 'succ' */
        block->jump.type = IR_JUMP_TYPE_JMP;
        block->succ[0] = succ;
        err = ir_add_predecessor(succ, block);
        if (err) return true;

    } else if (pred->succ[0] != NULL && pred->succ[1] != NULL) {
        /* 'succ' has only 'pred' as predecessor
         * 'pred' has 'succ' and another block as successors
         * => insert the sequence of moves at the start of 'succ' */
        insert_move_at_start(succ, smove);
    } else {
        /* 'succ' has 'pred' and other block as predecessors
         * 'pred' has only 'succ' as successor
         * => insert the sequence of moves at the end of 'pred' */
        insert_move_at_end(pred, smove);
    }
    return false;
}

static bool resolve_edge(IRFunction *f, IRBlock *pred, IRBlock *succ) {

    Move *move, *move_iter;
    IRInstr *ins, *ins_iter;
    bool err = false;

    struct list_head pmove;
    INIT_LIST_HEAD(&pmove);

    struct list_head smove;
    INIT_LIST_HEAD(&smove);

    err = parallel_move_from_phis(f, pred, succ, &pmove);
    if (err) goto ERROR;

    /*
    if (!list_empty(&pmove)) {
        printf("parallel move:\n");
    }
    list_for_each_entry(move, &pmove, link) {
        ir_print_location(move->to, stdout);
        printf(" = ");
        ir_print_ref(move->from, stdout);
        printf("\n");
    }
    */

    err = sequentialize_parallel_move(f, &pmove, &smove);
    if (err) goto ERROR;

    /*
    if (!list_empty(&smove)) {
        printf("sequence move:\n");
    }
    IRInstr *ins2;
    list_for_each_entry(ins2, &smove, next) {
        ir_print_ref(ins2->to, stdout);
        printf(" = ");
        ir_print_ref(ins2->arg[0], stdout);
        printf("\n");
    }
    */

    err = insert_sequence_of_moves(f, &smove, pred, succ);
    if (err) goto ERROR;

ERROR:
    /* free pmove */
    list_for_each_entry_safe(move, move_iter, &pmove, link) {
        list_del(&move->link);
        free(move);
    }
    /* free smove */
    list_for_each_entry_safe(ins, ins_iter, &smove, link) {
        list_del(&ins->link);
        free(move);
    }
    return err;
}

/* Before code generation, it is necessary to eliminate phi-functions since
 * these are not executable machine instructions. This elimination phase is
 * called SSA destruction. */
static bool ssa_deconstruction(IRFunction *f) {

    IRBlock *block, *iter;
    IRBlock *successor;
    bool err;

    list_for_each_entry(block, &f->block_list, link) {
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

    /* All the blocks created from critical edge splitting was placed in the
     * 'f->working_block_list' (we cannont modify the 'f->block_list' while
     * iterate on it), move them in the 'f->block_list' */
    list_for_each_entry_safe(block, iter, &f->working_block_list, link) {
        list_del(&block->link);
        list_add_tail(&block->link, &block->succ[0]->link);
    }

    /* free phi_list for all block b */
    list_for_each_entry(block, &f->block_list, link) {
        IRPhi *phi, *iter;
        list_for_each_entry_safe(phi, iter, &block->phi_list, link) {
            list_del(&phi->link);
            ir_free_phi(phi);
        }
    }

    return false;
}

static bool ensure_function_parameters_constraints(IRFunction *f) {

    bool err = false;
    struct list_head pmove;
    struct list_head smove;
    INIT_LIST_HEAD(&pmove);
    INIT_LIST_HEAD(&smove);
    unsigned int par_index = 0;
    IRInstr *ins, *ins_iter;
    Move *move, *move_iter;

    /* Ensure functions receive parameters from their caller */
        list_for_each_entry_safe(ins, ins_iter, &f->start->instr_list, link) {
        if (ins->op != IR_OPCODE_PAR) break;
        if (par_index > RV32_ARG_NUM_REG) {
        /* Passing function parameters through the stack is not implemented */
            assert(0);
        }
        IRReference from = {
            .type = IR_REF_TYPE_LOCATION,
            .as.location = &args[par_index++],
        };
        Location *to = &f->live_intervals[ins->to.as.tmp_id].assign;

        if (!have_same_location(f, to, &from)) {
            Move *move = malloc(sizeof(struct Move));
            if (move == NULL) {
                err = true;
                goto ERROR;
            }
            move->to = to;
            move->from = from;
            list_add_tail(&move->link, &pmove);
        }
        list_del(&ins->link);
        free(ins);
    }
    err = sequentialize_parallel_move(f, &pmove, &smove);
    if (err) goto ERROR;
    insert_move_at_start(f->start, &smove);

ERROR:
    /* free pmove */
    list_for_each_entry_safe(move, move_iter, &pmove, link) {
        list_del(&move->link);
        free(move);
    }
    /* free smove */
    list_for_each_entry_safe(ins, ins_iter, &smove, link) {
        list_del(&ins->link);
        free(move);
    }
    return err;
}

static bool ensure_function_calls_constraints(IRFunction *f, LiveInterval **intervals) {

    bool err = false;
    struct list_head pmove;
    struct list_head smove;
    INIT_LIST_HEAD(&pmove);
    INIT_LIST_HEAD(&smove);
    unsigned int interval_count = f->tmp_count;
    unsigned int arg_index = 0;
    IRInstr *ins, *ins_iter;
    Move *move, *move_iter;

    IRBlock *block;
    list_for_each_entry(block, &f->block_list, link) {

        list_for_each_entry_safe(ins, ins_iter, &block->instr_list, link) {
            switch (ins->op) {
            case IR_OPCODE_ARG: {
                if (arg_index > RV32_ARG_NUM_REG) {
                /* Passing function call arguments through the stack is not implemented */
                    assert(0);
                }
                IRReference from = ins->arg[0];
                Location *to = &args[arg_index++];
                if (!have_same_location(f, to, &from)) {
                    Move *move = malloc(sizeof(struct Move));
                    if (move == NULL) {
                        err = true;
                        goto ERROR;
                    }
                    move->to = to;
                    move->from = from;
                    list_add_tail(&move->link, &pmove);
                }
                ir_rm_usage(&ins->arg[0]);
                list_del(&ins->link);
                free(ins);
            } break;
            case IR_OPCODE_CALL: {

                struct list_head survivors;
                INIT_LIST_HEAD(&survivors);
                IRInstr *call = ins;

                for (unsigned int j = 0; j < interval_count; j++) {
                    LiveInterval *live_int = intervals[j];
                    if (live_int->assign.type == LOCATION_TYPE_REGISTER &&
                        !rv32_is_callee_saved[live_int->assign.as.reg] &&
                        live_int->start < call->seqnum && live_int->end > call->seqnum) {
                            list_add_tail(&live_int->link, &survivors);
                    }
                }

                LiveInterval *live_int, *live_int_iter;
                list_for_each_entry(live_int, &survivors, link) {
                    IRInstr *push = malloc(sizeof(struct IRInstr));
                    if (push == NULL) {
                        err = true;
                        goto ERROR;
                    }
                    push->op = IR_OPCODE_PUSH;
                    push->type = IR_TYPE_I32;
                    push->to.type = IR_REF_TYPE_UNDEFINED;
                    push->arg[0].type = IR_REF_TYPE_LOCATION;
                    push->arg[0].as.location = &live_int->assign;
                    push->arg[1].type = IR_REF_TYPE_UNDEFINED;
                    push->seqnum = 0;
                    /* Insert 'push' before 'call' */
                    list_add_tail(&push->link, &call->link);
                }

                list_for_each_entry(live_int, &survivors, link) {
                    IRInstr *pop = malloc(sizeof(struct IRInstr));
                    if (pop == NULL) {
                        err = true;
                        goto ERROR;
                    }
                    pop->op = IR_OPCODE_POP;
                    pop->type = IR_TYPE_I32;
                    pop->to.type = IR_REF_TYPE_LOCATION;
                    pop->to.as.location = &live_int->assign;
                    pop->arg[0].type = IR_REF_TYPE_UNDEFINED;
                    pop->arg[1].type = IR_REF_TYPE_UNDEFINED;
                    pop->seqnum = 0;
                    /* Insert 'pop' after 'call' */
                    list_add(&pop->link, &call->link);
                }

                /* Clean the survivors list */
                list_for_each_entry_safe(live_int, live_int_iter, &survivors, link) {
                    list_del(&live_int->link);
                }

                err = sequentialize_parallel_move(f, &pmove, &smove);
                if (err) goto ERROR;

                /* Insert the copies before 'call' */
                IRInstr *ins2, *ins2_iter;
                list_for_each_entry_safe(ins2, ins2_iter, &smove, link) {
                    list_del(&ins2->link);
                    list_add_tail(&ins2->link, &call->link);
                }

                /* Reset arg_index */
                arg_index = 0;
                /* free pmove */
                list_for_each_entry_safe(move, move_iter, &pmove, link) {
                    list_del(&move->link);
                    free(move);
                }

                if (call->to.type != IR_REF_TYPE_UNDEFINED) {
                    if (!have_same_location(f, &args[0], &call->to)) {
                        IRInstr *ins = malloc(sizeof(struct IRInstr));
                        if (ins == NULL) {
                            err = true;
                            goto ERROR;
                        }
                        ins->op = IR_OPCODE_COPY;
                        ins->type = IR_TYPE_I32;
                        ins->to = call->to;
                        ins->arg[0] = IR_REF_LOC(&args[0]);
                        ins->arg[1].type = IR_REF_TYPE_UNDEFINED;
                        ins->seqnum = 0;
                        list_add(&ins->link, &call->link);
                    }
                }
                call->to.type = IR_REF_TYPE_UNDEFINED;
                call->type = IR_TYPE_VOID;
            } break;
            default:
                continue;
            }
        }
    }

ERROR:
    /* free pmove */
    list_for_each_entry_safe(move, move_iter, &pmove, link) {
        list_del(&move->link);
        free(move);
    }
    /* free smove */
    list_for_each_entry_safe(ins, ins_iter, &smove, link) {
        list_del(&ins->link);
        free(move);
    }
    return err;

}

static bool ensure_function_return_value_constraints(IRFunction *f) {

    IRBlock *block;
    list_for_each_entry(block, &f->block_list, link) {
        if (block->jump.type == IR_JUMP_TYPE_RET1) {
            if (!have_same_location(f, &args[0], &block->jump.arg)) {
                IRInstr *ins = alloc_copy(&args[0], &block->jump.arg);
                if (ins == NULL) return true;
                list_add_tail(&ins->link, &block->instr_list);
            }
            block->jump.type = IR_JUMP_TYPE_RET0;
        }
    }
    return false;
}

static bool ensure_abi_constraints(IRFunction *f, LiveInterval **intervals) {

    bool err;
    err = ensure_function_parameters_constraints(f);
    if (err) return true;

    err = ensure_function_calls_constraints(f, intervals);
    if (err) return true;

    err = ensure_function_return_value_constraints(f);
    if (err) return true;

    return false;
}

bool register_allocation(IRFunction *f) {

    number_instructions(f);
    LiveInterval **sorted_intervals = build_intervals(f);
    if (sorted_intervals == NULL) return true;

    /*
    for (unsigned int tmp_id = 0; tmp_id < f->next_tmp_id; tmp_id++) {
        printf("tmp_id: %u\n", tmp_id);
        printf("    start: %u\n", f->live_intervals[tmp_id].start);
        printf("    end:   %u\n", f->live_intervals[tmp_id].end);
    }
    */

    register_hints(f);
    /* create the registers pool */
    unsigned long reg_pool = 0;
    for (unsigned int i = 0; i < RV32_GP_NUM_REG; i++) {
        bitmap_set_bit(&reg_pool, rv32_gp_reg[i]);
    }
    for (unsigned int i = 0; i < RV32_ARG_NUM_REG; i++) {
        bitmap_set_bit(&reg_pool, rv32_arg_reg[i]);
    }
    linear_scan(f, sorted_intervals, &reg_pool, RV32_NUM_REG);

    bool err;
    err = ensure_abi_constraints(f, sorted_intervals);
    if (err) return true;
    free(sorted_intervals);

    err = ssa_deconstruction(f);
    if (err) return true;
    replace_references_with_locations(f);
    return false;
}
