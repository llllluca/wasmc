#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "libqbe.h"

/* utils.c */
extern void panic(void);
extern void *xcalloc(size_t nmemb, size_t size);
extern void *xmalloc(size_t size);

static Ref *input_of(Phi *phi, Blk *b) {
    listNode *phi_arg_node;
    listNode *phi_arg_iter = listFirst(phi->phi_arg_list);
    while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
        Phi_arg *phi_arg = listNodeValue(phi_arg_node);
        if (b == phi_arg->b) {
            return &phi_arg->r;
        }
    }
    return NULL;
}

static void live_concat(list *live, list *other) {
    listNode *other_node;
    listNode *other_iter = listFirst(other);
    while ((other_node = listNext(&other_iter)) != NULL) {
        listAddNodeTail(live, listNodeValue(other_node));
    }
}

static void live_init(list *live, Blk* b, Blk *b_succ) {
    /* TODO: b_succ->live_in non verrà più modificata, posso fare sharing */
    if (b_succ == NULL) return;
    if (b_succ->live_in != NULL) {
        live_concat(live, b_succ->live_in);
    }
    listNode *phi_node;
    listNode *phi_iter = listFirst(b_succ->phi_list);
    while ((phi_node = listNext(&phi_iter)) != NULL) {
        Phi *phi = listNodeValue(phi_node);
        Ref *r = input_of(phi, b);
        if (r->type == REF_TYPE_TMP) {
            Tmp *t = listNodeValue(r->as.tmp_node);
            listAddNodeTail(live, t);
        }
    }
}

/* TODO: live remove is a linear search on the live list, is inefficient */
static void live_remove(list *live, Tmp *t) {
    listNode *live_node;
    listNode *live_iter = listFirst(live);
    while ((live_node = listNext(&live_iter)) != NULL) {
        if (t == listNodeValue(live_node)) {
            listDelNode(live, live_node);
            break;
        }
    }
}

static void number_instrs(Fn *f) {
    unsigned int next_id = 16;
    listNode *blk_node;
    listNode *blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        b->id = next_id;
        next_id += 2;
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            i->id = next_id;
            next_id += 2;
        }
        b->jmp.id = next_id;
        next_id += 2;
    }
}

typedef enum move_status {
    TO_MOVE,
    BEING_MOVED,
    MOVED,
} move_status;

typedef struct move_from {
    enum {
        CONST,
        LOCATION,
    } type;
    union {
        location loc;
        uint32_t int32_const;
    } as;
} move_from;

typedef struct move {
    move_from from;
    location to;
    move_status status;
} move;

#define MOVE_FROM_EQ_TO(from, to) \
    (from.type != CONST && LOCATION_EQ(from.as.loc, to))

static list *phi_parallel_moves(Blk *pred, Blk *succ) {
    list *mappings = listCreate();
    listSetFreeMethod(mappings, free);
    listNode *phi_node;
    listNode *phi_iter = listFirst(succ->phi_list);
    while ((phi_node = listNext(&phi_iter)) != NULL) {
        Phi *phi = listNodeValue(phi_node);
        Ref *r = input_of(phi, pred);
        if (r == NULL) continue;
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
                panic();
        }

        if (phi->to.type != REF_TYPE_TMP) {
            panic();
        }
        Tmp *t = listNodeValue(phi->to.as.tmp_node);
        location to =  t->i->assign;
        if (!MOVE_FROM_EQ_TO(from, to)) {
            move *move = xmalloc(sizeof(struct move));
            move->from = from;
            move->to = to;
            listAddNodeTail(mappings, move);
        }
    }
    return mappings;
}

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

static void resolve_edge(Fn *f, Blk *pred, Blk *succ) {
    if (succ == NULL) return;
    list *par_move = phi_parallel_moves(pred, succ);
    list *move_seq = sequentialize(par_move);
    insert_move_sequence(f, move_seq, pred, succ);
    listRelease(par_move);
    assert(listLength(move_seq) == 0);
    listRelease(move_seq);
}

static void resolve_ssa(Fn *f) {

    listNode *blk_node;
    listNode *blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *pred = listNodeValue(blk_node);
        resolve_edge(f, pred, pred->succ[0]);
        resolve_edge(f, pred, pred->succ[1]);
    }

    /* free phi_list for all block b */
    blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        listRelease(b->phi_list);
        b->phi_list = NULL;
    }
}

static void add_range(Tmp *t, unsigned int start, unsigned int end) {
    assert(start < end);
    if (t->i == NULL) {
        t->i = xmalloc(sizeof(struct live_interval));
        t->i->start = start;
        t->i->end = end;
        t->i->assign.type = LOCATION_NONE;
        t->i->register_hint = ZERO;
        return;
    }
    if (start < t->i->start) t->i->start = start;
    if (end > t->i->end) t->i->end = end;
}

static void set_start(Tmp *t, unsigned int start) {
    if (t->i != NULL) {
        assert(start < t->i->end);
        t->i->start = start;
    } else {
        // This happens when we don't have a use of t
        t->i = xmalloc(sizeof(struct live_interval));
        t->i->start = start;
        t->i->end = start;
        t->i->assign.type = LOCATION_NONE;
        t->i->register_hint = ZERO;
    }
}

static void fill_register_hints(Fn *f) {

    unsigned int arg_index = 0;
    listNode *ins_node;
    listNode *ins_iter = listFirst(f->start->ins_list);
    while ((ins_node = listNext(&ins_iter)) != NULL) {
        Ins *ins = listNodeValue(ins_node);
        if (ins->op != IR_OPCODE_PAR) break;
        if (ins->to.type == REF_TYPE_TMP && ins->to.as.tmp_node != NULL) {
            Tmp *t = listNodeValue(ins->to.as.tmp_node);
            t->i->register_hint = rv32_arg_reg[arg_index++];
        }
    }
    arg_index = 0;

    listNode *blk_node;
    listNode *blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);

        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *ins = listNodeValue(ins_node);
            if (ins->op == IR_OPCODE_ARG) {
                if (ins->arg[0].type == REF_TYPE_TMP && ins->arg[0].as.tmp_node != NULL) {
                    Tmp *t = listNodeValue(ins->arg[0].as.tmp_node);
                    t->i->register_hint = rv32_arg_reg[arg_index++];
                }
            }
            else if (ins->op == IR_OPCODE_CALL) {
                arg_index = 0;
                if (ins->to.type == REF_TYPE_TMP && ins->to.as.tmp_node != NULL) {
                    Tmp *t = listNodeValue(ins->to.as.tmp_node);
                    t->i->register_hint = A0;
                }
            }
        }

        if (b->jmp.type == RET1_JUMP_TYPE) {
            if (b->jmp.arg.type == REF_TYPE_TMP && b->jmp.arg.as.tmp_node != NULL) {
                Tmp *t = listNodeValue(b->jmp.arg.as.tmp_node);
                t->i->register_hint = A0;
            }
        }
    }
}

static live_interval **build_intervals(Fn *f) {
    number_instrs(f);
    unsigned int n = listLength(f->tmp_list) + 1;
    live_interval **sorted_intervals = xcalloc(n, sizeof(struct live_interval *));
    // the NULL pointer indicates the end of the array
    sorted_intervals[--n] = NULL;

    /* Iterate for all blocks in reverse order so that all uses of Tmp
     * are seen before its definition. Therefore successors of a block are
     * processed before this block. Only for loop, the loop header cannot be
     * processed before the loop end, so loops are handled as a special case. */
    listNode *blk_node;
    listNode *blk_iter = listLast(f->blk_list);
    while ((blk_node = listPrev(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);

        /* Initialize the list of Tmp that are live at the end of block b. The
         * initial list f Tmp that are live at the end of block b is the union
         * of all Tmp live at the beginning of the successor of b. Additionaly,
         * phi functions of the successors contribute to the initial live
         * set. For each Phi, the input operand corresponting to b is added
         * to the live set. */
        list *live = listCreate();
        live_init(live, b, b->succ[0]);
        live_init(live, b, b->succ[1]);

        /* For each live Tmp, an initial lifetime interval covering the entire
         * block is added to te live set. This live range might me shortened
         * later if the definition of a Tmp is encountered. */
        listNode *live_node;
        listNode *live_iter = listFirst(live);
        while ((live_node = listNext(&live_iter)) != NULL) {
            Tmp *t = listNodeValue(live_node);
            add_range(t, b->id, b->jmp.id);
        }

        /* Next all instructions of b are processed in reverse order, hence
         * the jump at the end of b is the first instruction processed. An
         * output operand (i.e., a definition of a Tmp) shortens the lifetime
         * interval of the Tmp; the start position of the lifetime interval is
         * set to the current instruction number. Additionally, the Tmp is removed
         * from the list of live Tmp. An input operand (i.e. a use of a Tmp)
         * adds a new lifetime interval (the new lifetime interval is merged if
         * a lifetime interval is already present). The new lifetime interval
         * starts at the beginning of the block, and again might be shortened
         * later. Additionally, the Tmp is added to the list of live Tmp.*/

        // The final jump can contain an input operand (i.e. a use of a Tmp).
        listNode *tmp_node = b->jmp.arg.as.tmp_node;
        if (b->jmp.arg.type == REF_TYPE_TMP && tmp_node != NULL) {
            Tmp *t = listNodeValue(tmp_node);
            add_range(t, b->id, b->jmp.id);
            listAddNodeHead(live, t);
        }

        listNode *ins_node;
        listNode *ins_iter = listLast(b->ins_list);
        while ((ins_node = listPrev(&ins_iter)) != NULL) {
            Ins *ins = listNodeValue(ins_node);
            listNode *tmp_node = ins->to.as.tmp_node;
            // The storew instruction has 3 input operand and 0 output operand
            if (ins->op == IR_OPCODE_STORE || ins->op == IR_OPCODE_STORE8) {
                if (ins->to.type == REF_TYPE_TMP && tmp_node != NULL) {
                    Tmp *t = listNodeValue(tmp_node);
                    add_range(t, b->id, ins->id);
                    listAddNodeHead(live, t);
                }
            } else {
                // Ins can contain a output operand (i.e. a definition of a Tmp).
                if (ins->to.type == REF_TYPE_TMP && tmp_node != NULL) {
                    Tmp *t = listNodeValue(tmp_node);
                    set_start(t, ins->id);
                    live_remove(live, t);

                    /* When a definition of a Tmp is encountered, its lifetime
                     * interval is saved to the array in ascending order of start
                     * point. The following line works because the instructions
                     * are numbered in such a way when iterated in reverse order,
                     * the instructions are encountered in descending order of
                     * instruction number. */
                    sorted_intervals[--n] = t->i;
                }
            }
            // Ins can contain at most 2 input operands (i.e. a use of a Tmp).
            for (unsigned int i = 0; i < 2; i++) {
                tmp_node = ins->arg[i].as.tmp_node;
                if (ins->arg[i].type == REF_TYPE_TMP && tmp_node != NULL) {
                    Tmp *t = listNodeValue(tmp_node);
                    add_range(t, b->id, ins->id);
                    listAddNodeHead(live, t);
                }
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
        listNode *phi_node;
        listNode *phi_iter = listLast(b->phi_list);
        while ((phi_node = listPrev(&phi_iter)) != NULL) {
            Phi *phi = listNodeValue(phi_node);
            listNode *tmp_node = phi->to.as.tmp_node;
            assert(phi->to.type == REF_TYPE_TMP && tmp_node != NULL);
            Tmp *t = listNodeValue(tmp_node);
            live_remove(live, t);
            sorted_intervals[--n] = t->i;
        }

        if (b->is_loop_header) {
            unsigned int max = 0;
            assert(listLength(b->loop_end_blk_list) > 0);
            listNode *blk_node;
            listNode *blk_iter = listFirst(b->loop_end_blk_list);
            while ((blk_node = listNext(&blk_iter)) != NULL) {
                Blk *b = listNodeValue(blk_node);
                if (b->jmp.id > max) {
                    max = b->jmp.id;
                }
            }
            listRelease(b->loop_end_blk_list);
            b->loop_end_blk_list = NULL;

            listNode *live_node;
            listNode *live_iter = listFirst(live);
            while ((live_node = listNext(&live_iter)) != NULL) {
                Tmp *t = listNodeValue(live_node);
                add_range(t, b->id, max);
            }
        }
        b->live_in = live;
    }

    // Free b->live_in for all block b
    blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        listRelease(b->live_in);
        b->live_in = NULL;
    }

    /* Assert that the length of sorted_intervals is equal to the length of
     * tmp list of the function */
    if (n != 0) {
        listNode *tmp_node;
        listNode *tmp_iter = listFirst(f->tmp_list);
        while ((tmp_node = listNext(&tmp_iter)) != NULL) {
            Tmp *t = listNodeValue(tmp_node);
            assert(t->i != NULL);
        }
        assert(0);
    }

    fill_register_hints(f);
    return sorted_intervals;
}

static bool is_arg_reg(rv32_reg reg) {
    for (unsigned int i = 0; i < RV32_ARG_NUM_REG; i++) {
        if (reg == rv32_arg_reg[i]) {
            return true;
        }
    }
    return false;
}


static void expire_old_intervals(list *active, live_interval *i,
    rv32_reg_pool *gpr, rv32_reg_pool *argr) {

    listNode *active_node;
    listNode *active_iter = listFirst(active);
    while ((active_node = listNext(&active_iter)) != NULL) {
        live_interval *j = listNodeValue(active_node);
        if (j->end > i->start) return;
        listDelNode(active, active_node);
        assert(j->assign.type == REGISTER);
        if (is_arg_reg(j->assign.as.reg)) {
            argr->pool[j->assign.as.reg] = true;
            argr->size++;
        } else {
            gpr->pool[j->assign.as.reg] = true;
            gpr->size++;
        }
    }
}

static void active_insert(list *active, live_interval *i) {
    listNode *active_node;
    listNode *active_iter = listFirst(active);
    while ((active_node = listNext(&active_iter)) != NULL) {
        live_interval *curr = listNodeValue(active_node);
        if (curr->end > i->end) break;
    }
    if (active_node == NULL) {
        listAddNodeTail(active, i);
    } else {
        listInsertNodeBefore(active, active_node, i);
    }
}

static void spill_at_interval(list *active,
    live_interval *i, unsigned int *num_stack_slots) {

    live_interval *spill = NULL;
    listNode *spill_node = NULL;
    listNode *li_node;
    listNode *li_iter = listLast(active);
    while ((li_node = listPrev(&li_iter)) != NULL) {
        live_interval *i = listNodeValue(li_node);
        spill = i;
        spill_node = li_node;
    }
    if (spill == NULL || spill_node == NULL) panic();

    if (spill->end > i->end) {
        i->assign.type = spill->assign.type;
        i->assign.as.reg = spill->assign.as.reg;
        spill->assign.type = STACK_SLOT;
        spill->assign.as.stack_slot = (*num_stack_slots)++;
        listDelNode(active, spill_node);
        active_insert(active, i);
    } else {
        i->assign.type = STACK_SLOT;
        i->assign.as.stack_slot = (*num_stack_slots)++;
    }
}

static bool try_assign_reg(live_interval *i, rv32_reg_pool *r) {
    for (unsigned int j = 0; j < RV32_NUM_REG; j++) {
        if (r->pool[j]) {
            r->pool[j] = false;
            r->size--;
            i->assign.as.reg = j;
            return false;
        }
    }
    return true;
}

static void assign_reg(live_interval *i,
    rv32_reg_pool *gpr, rv32_reg_pool *argr) {

    bool err;
    i->assign.type = REGISTER;
    rv32_reg h = i->register_hint;
    if (h != ZERO) {
        if (is_arg_reg(h)) {
            if (argr->pool[h]) {
                argr->pool[h] = false;
                argr->size--;
                i->assign.as.reg = h;
                return;
            }
        } else {
            if (gpr->pool[h]) {
                gpr->pool[h] = false;
                gpr->size--;
                i->assign.as.reg = h;
                return;
            }
        }
    }

    err = try_assign_reg(i, gpr);
    if (!err) return;
    err = try_assign_reg(i, argr);
    if (!err) return;
    assert(0);
}

static void tmpref_to_location(Ref *r) {
    if (r->type == REF_TYPE_TMP && r->as.tmp_node != NULL) {
        Tmp *t = listNodeValue(r->as.tmp_node);
        assert(t != NULL);
        live_interval *li = t->i;
        assert(li != NULL);
        r->type = REF_TYPE_LOCATION;
        r->as.loc = li->assign;
    }
}

static void handle_register_constraints(Fn *f, live_interval **intervals) {
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

static void to_lower_level_ir(Fn *f) {
    listNode *blk_node;
    listNode *blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);

        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *ins = listNodeValue(ins_node);
            tmpref_to_location(&ins->to);
            tmpref_to_location(&ins->arg[0]);
            tmpref_to_location(&ins->arg[1]);

        }
        if (b->jmp.type == JNZ_JUMP_TYPE ||
            b->jmp.type == RET1_JUMP_TYPE) {
            tmpref_to_location(&b->jmp.arg);
        }
    }
}

void linear_scan(Fn *f, rv32_reg_pool *gpr, rv32_reg_pool *argr) {
    assert(gpr->size > 0);
    assert(argr->size > 0);
    unsigned int nreg = gpr->size + argr->size;
    live_interval **sorted_intervals = build_intervals(f);
    f->num_stack_slots = 0;
    list *active = listCreate();

    for (unsigned int j = 0; sorted_intervals[j] != NULL; j++) {
        live_interval *i = sorted_intervals[j];
        if (i->assign.type != LOCATION_NONE) continue;
        expire_old_intervals(active, i, gpr, argr);
        if (listLength(active) == nreg) {
            spill_at_interval(active, i, &f->num_stack_slots);
        } else {
            assign_reg(i, gpr, argr);
            active_insert(active, i);
        }
    }

    listRelease(active);
    handle_register_constraints(f, sorted_intervals);
    resolve_ssa(f);
    to_lower_level_ir(f);
    for (unsigned int i = 0; sorted_intervals[i] != NULL; i++) {
        for (unsigned int j = 0; j < i; j++) {
            if (sorted_intervals[i] == sorted_intervals[j]) {
                assert(0);
            }
        }
    }

    for (unsigned int j = 0; sorted_intervals[j] != NULL; j++) {
        free(sorted_intervals[j]);
    }
    free(sorted_intervals);
}

