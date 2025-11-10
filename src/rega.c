#include "all.h"
#include <assert.h>
#include <string.h>

static Tmp *input_of(Phi *phi, Blk *b) {
    listNode *phi_arg_node;
    listNode *phi_arg_iter = listFirst(phi->phi_arg_list);
    while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
        Phi_arg *phi_arg = listNodeValue(phi_arg_node);
        if (b == phi_arg->b && phi_arg->r.type == RTmp) {
            return listNodeValue(phi_arg->r.val.tmp_node);
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
        Tmp *t = input_of(phi, b);
        if (t != NULL) {
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

static void add_range(Tmp *t, unsigned int start, unsigned int end) {
    assert(start < end);
    if (t->i == NULL) {
        t->i = xmalloc(sizeof(struct live_interval));
        t->i->start = start;
        t->i->end = end;
        t->i->assign_type = ASSIGN_TYPE_NONE;
        t->i->can_spill = 1;
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
        t->i->assign_type = ASSIGN_TYPE_NONE;
        t->i->can_spill = 1;
    }
}

live_interval **build_intervals(Fn *f) {
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
        live_init(live, b, b->s1);
        live_init(live, b, b->s2);

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
        listNode *tmp_node = b->jmp.arg.val.tmp_node;
        if (b->jmp.arg.type == RTmp && tmp_node != NULL) {
            Tmp *t = listNodeValue(tmp_node);
            add_range(t, b->id, b->jmp.id);
            listAddNodeHead(live, t);
        }

        listNode *ins_node;
        listNode *ins_iter = listLast(b->ins_list);
        while ((ins_node = listPrev(&ins_iter)) != NULL) {
            Ins *ins = listNodeValue(ins_node);
            // Ins can contain a output operand (i.e. a definition of a Tmp).
            listNode *tmp_node = ins->to.val.tmp_node;
            if (ins->to.type == RTmp && tmp_node != NULL) {
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
            // Ins can contain at most 2 input operands (i.e. a use of a Tmp).
            for (unsigned int i = 0; i < 2; i++) {
                tmp_node = ins->arg[i].val.tmp_node;
                if (ins->arg[i].type == RTmp && tmp_node != NULL) {
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
            listNode *tmp_node = phi->to.val.tmp_node;
            assert(phi->to.type == RTmp && tmp_node != NULL);
            Tmp *t = listNodeValue(tmp_node);
            live_remove(live, t);
            sorted_intervals[--n] = t->i;
        }

        //TODO: loop are not implemented

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
    assert(n == 0);
    return sorted_intervals;
}

static void expire_old_intervals(list *active, live_interval *i,
    rv32_reg_pool *gpr, rv32_reg_pool *argr) {

    listNode *active_node;
    listNode *active_iter = listFirst(active);
    while ((active_node = listNext(&active_iter)) != NULL) {
        live_interval *j = listNodeValue(active_node);
        if (j->end > i->start) return;
        listDelNode(active, active_node);
        switch (j->assign_type) {
            case ASSIGN_TYPE_GP_REGISTER:
                gpr->pool[j->assign.reg] = 1;
                gpr->size++;
                break;
            case ASSIGN_TYPE_ARG_REGISTER:
                argr->pool[j->assign.reg] = 1;
                argr->size++;
                break;
            default:
                assert(0);
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
        if (i->can_spill) {
            spill = i;
            spill_node = li_node;
        }
    }
    if (spill == NULL || spill_node == NULL) panic();

    if (spill->end > i->end) {
        i->assign_type = spill->assign_type;
        i->assign.reg = spill->assign.reg;
        spill->assign_type = ASSIGN_TYPE_STACK_SLOT;
        spill->assign.stack_slot = (*num_stack_slots)++;
        listDelNode(active, spill_node);
        active_insert(active, i);
    } else {
        i->assign_type = ASSIGN_TYPE_STACK_SLOT;
        i->assign.stack_slot = (*num_stack_slots)++;
    }
}

static boolean try_assign_reg(live_interval *i, rv32_reg_pool *r) {
    for (unsigned int j = 0; j < RV32_NUM_REG; j++) {
        if (r->pool[j] == TRUE) {
            r->pool[j] = FALSE;
            r->size--;
            i->assign.reg = j;
            return FALSE;
        }
    }
    return TRUE;
}

static void assign_reg(live_interval *i,
    rv32_reg_pool *gpr, rv32_reg_pool *argr) {

    boolean err;
    i->assign_type = ASSIGN_TYPE_GP_REGISTER;
    err = try_assign_reg(i, gpr);
    if (!err) return;
    i->assign_type = ASSIGN_TYPE_ARG_REGISTER;
    err = try_assign_reg(i, argr);
    if (!err) return;
    assert(0);
}

static void handle_func_params(Fn *f, rv32_reg_pool *argr, list *active) {
    listNode *ins_node;
    listNode *ins_iter = listFirst(f->start->ins_list);
    while ((ins_node = listNext(&ins_iter)) != NULL) {
        Ins *ins = listNodeValue(ins_node);
        if (ins->op != PAR_INSTR) return;
        assert(ins->to.type == RTmp);
        Tmp *tmp = listNodeValue(ins->to.val.tmp_node);
        live_interval *i = tmp->i;
        i->assign_type = ASSIGN_TYPE_ARG_REGISTER;
        i->can_spill = 0;
        boolean err = try_assign_reg(i, argr);
        if (err) panic();
        active_insert(active, i);
        listDelNode(f->start->ins_list, ins_node);
    }
}

static void handle_func_call(Blk *b, live_interval **intervals,
    listNode *first_arg_node, listNode *func_call_node) {

    Ins *func_call = listNodeValue(func_call_node);
    list *survivors = listCreate();
    for (unsigned int j = 0; intervals[j] != NULL; j++) {
        live_interval *li = intervals[j];
        if ((li->assign_type == ASSIGN_TYPE_GP_REGISTER ||
            li->assign_type == ASSIGN_TYPE_ARG_REGISTER) &&
            li->start < func_call->id && li->end > func_call->id) {
            listAddNodeTail(survivors, li);
        }
    }

    assert(listLength(survivors) == 0);
    /*
    listNode *li_node;
    listNode *li_iter = listFirst(survivors);
    while ((li_node = listNext(&li_iter)) != NULL) {
        live_interval *li = listNodeValue(li_node);
        //TODO: insert push
    }
    */

    // check if the current func_call has at least one argument
    if (first_arg_node != NULL) {
        unsigned int arg_index = 0;
        listNode *ins_node;
        listNode *ins_iter = first_arg_node;
        while ((ins_node = listNext(&ins_iter)) != func_call_node) {
            Ins *ins = listNodeValue(ins_node);
            if (arg_index >= RV32_ARG_NUM_REG) panic();
            ins->to.val.reg = rv32_arg_reg[arg_index++];
            ins->to.type = RReg;
            ins->op = COPY_INSTR;
        }
    }

    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->type = func_call->type;
    ins->op = COPY_INSTR;
    ins->to.type = RReg;
    ins->to = func_call->to;
    ins->arg[0].type = RReg;
    ins->arg[0].val.reg = A0;
    ins->arg[1] = UNDEF_TMP_REF;
    listInsertNodeAfter(b->ins_list, func_call_node, ins);
    func_call->to = UNDEF_TMP_REF;

    /*
    li_iter = listFirst(survivors);
    while ((li_node = listNext(&li_iter)) != NULL) {
        live_interval *li = listNodeValue(li_node);
        //TODO: insert pop
    }
    */
    listRelease(survivors);
}

static void change_tmpref_to_reg_or_stack_slot(Ref *r) {
    if (r->type == RTmp && r->val.tmp_node != NULL) {
        Tmp *t = listNodeValue(r->val.tmp_node);
        assert(t != NULL);
        live_interval *li = t->i;
        assert(li != NULL);
        switch (li->assign_type) {
            case ASSIGN_TYPE_GP_REGISTER:
            case ASSIGN_TYPE_ARG_REGISTER:
                r->type = RReg;
                r->val.reg = li->assign.reg;
                break;

            case ASSIGN_TYPE_STACK_SLOT:
                r->type = RStack_slot;
                r->val.reg = li->assign.stack_slot;
                break;

            default:
                panic();
        }
    }
}

static void to_lower_level_ir(Fn *f, live_interval **intervals) {
    listNode *first_arg_node = NULL;
    listNode *blk_node;
    listNode *blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);

        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *ins = listNodeValue(ins_node);
            change_tmpref_to_reg_or_stack_slot(&ins->to);
            change_tmpref_to_reg_or_stack_slot(&ins->arg[0]);
            change_tmpref_to_reg_or_stack_slot(&ins->arg[1]);
            if (ins->op == ARG_INSTR && first_arg_node == NULL) {
                first_arg_node = ins_node;
            }
            else if (ins->op == CALL_INSTR) {
                handle_func_call(
                     b,
                    intervals,
                    first_arg_node,
                    ins_node);
                first_arg_node = NULL;
            }

        }
        if (b->jmp.type == JNZ_JUMP_TYPE || b->jmp.type == RET1_JUMP_TYPE) {
            change_tmpref_to_reg_or_stack_slot(&b->jmp.arg);
        }

        if (b->jmp.type == RET1_JUMP_TYPE) {
            Ins *ins = xmalloc(sizeof(struct Ins));
            ins->op = COPY_INSTR;
            ins->to.type = RReg;
            ins->to.val.reg = A0;
            ins->type = f->ret_type;
            ins->arg[0] = b->jmp.arg;
            ins->arg[1] = UNDEF_TMP_REF;
            listAddNodeTail(b->ins_list, ins);
        }
        // set the jump to ret0
        b->jmp.type = RET0_JUMP_TYPE;
        b->jmp.arg = UNDEF_TMP_REF;
    }

}

void linear_scan(Fn *f, rv32_reg_pool *gpr, rv32_reg_pool *argr) {
    assert(gpr->size > 0);
    assert(argr->size > 0);
    unsigned int nreg = gpr->size + argr->size;
    live_interval **sorted_intervals = build_intervals(f);
    f->num_stack_slots = 0;
    list *active = listCreate();
    handle_func_params(f, argr, active);

    for (unsigned int j = 0; sorted_intervals[j] != NULL; j++) {
        live_interval *i = sorted_intervals[j];
        if (i->assign_type != ASSIGN_TYPE_NONE) continue;
        expire_old_intervals(active, i, gpr, argr);
        if (listLength(active) == nreg) {
            spill_at_interval(active, i, &f->num_stack_slots);
        } else {
            assign_reg(i, gpr, argr);
            active_insert(active, i);
        }
    }

    listRelease(active);
    to_lower_level_ir(f, sorted_intervals);
    for (unsigned int j = 0; sorted_intervals[j] != NULL; j++) {
        free(sorted_intervals[j]);
    }
    free(sorted_intervals);
}

