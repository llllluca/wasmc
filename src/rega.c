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
    /* b_succ->live_in non verrà più modificata, posso fare sharing */
    if (b_succ == NULL) return;
    assert(b_succ->is_visited == 1);
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
    }
}

Tmp **build_intervals(Fn *f) {
    number_instrs(f);
    unsigned int n = listLength(f->tmp_list);
    Tmp **sorted_intervals = xcalloc(n, sizeof(Tmp *));
    listNode *blk_node;
    listNode *blk_iter = listLast(f->blk_list);
    while ((blk_node = listPrev(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        assert(b->is_visited == 0);
        b->is_visited = 1;

        list *live = listCreate();
        live_init(live, b, b->s1);
        live_init(live, b, b->s2);

        listNode *live_node;
        listNode *live_iter = listFirst(live);
        while ((live_node = listNext(&live_iter)) != NULL) {
            Tmp *t = listNodeValue(live_node);
            add_range(t, b->id, b->jmp.id);
        }

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
            // output operand
            listNode *tmp_node = ins->to.val.tmp_node;
            if (ins->to.type == RTmp && tmp_node != NULL) {
                Tmp *t = listNodeValue(tmp_node);
                set_start(t, ins->id);
                live_remove(live, t);
                sorted_intervals[--n] = t;
            }
            // input operand
            for (unsigned int i = 0; i < 2; i++) {
                tmp_node = ins->arg[i].val.tmp_node;
                if (ins->arg[i].type == RTmp && tmp_node != NULL) {
                    Tmp *t = listNodeValue(tmp_node);
                    add_range(t, b->id, ins->id);
                    listAddNodeHead(live, t);
                }
            }
        }

        listNode *phi_node;
        listNode *phi_iter = listLast(b->phi_list);
        while ((phi_node = listPrev(&phi_iter)) != NULL) {
            Phi *phi = listNodeValue(phi_node);
            listNode *tmp_node = phi->to.val.tmp_node;
            assert(phi->to.type == RTmp && tmp_node != NULL);
            Tmp *t = listNodeValue(tmp_node);
            live_remove(live, t);
            sorted_intervals[--n] = t;
        }

        //TODO: loop are not implemented here
        b->live_in = live;
    }

    // Free b->live_in for all block b
    blk_iter = listFirst(f->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        listRelease(b->live_in);
        b->live_in = NULL;
    }

    printf("listLength = %ld\n", listLength(f->tmp_list));
    printf("n = %d\n", n);
    listNode *tmp_node;
    listNode *tmp_iter = listFirst(f->tmp_list);
    while ((tmp_node = listNext(&tmp_iter)) != NULL) {
        Tmp *t = listNodeValue(tmp_node);
        printf("%s\n", t->name);
    }
    assert(n == 0);


    return sorted_intervals;
}

static void expire_old_intervals(list *active, live_interval *i, unsigned char*free_regs) {
    listNode *active_node;
    listNode *active_iter = listFirst(active);
    while ((active_node = listNext(&active_iter)) != NULL) {
        live_interval *j = listNodeValue(active_node);
        if (j->end > i->start) return;
        listDelNode(active, active_node);
        free_regs[j->assign.reg] = 1;
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

static void spill_at_interval(list *active, live_interval *i, unsigned int *num_stack_slots) {
    listNode *spill_node = listLast(active);
    live_interval *spill = listNodeValue(spill_node);
    if (spill->end > i->end) {
        i->assign_type = ASSIGN_TYPE_REGISTER;
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

static unsigned int select_reg(unsigned char *free_regs, unsigned int nregs) {
    for (unsigned int i = 0; i < nregs; i++) {
        if (free_regs[i] == 1) {
            free_regs[i] = 0;
            return i;
        }
    }
    assert(0);
    return 0; // dead code, only to remove compiler warning
}

void linear_scan(Fn *f, unsigned int registers,
                 unsigned int *num_stack_slots) {

    assert(registers > 0);
    Tmp **sorted_intervals = build_intervals(f);
    printfn(f, stdout);
    unsigned int n = listLength(f->tmp_list);

    // free_regs[i] is 1 if the register i is free and 0 otherwise
    unsigned char *free_regs = xcalloc(registers, sizeof(unsigned char));
    memset(free_regs, 1, registers);
    list *active = listCreate();
    *num_stack_slots = 0;

    for (unsigned int j = 0; j < n; j++) {
        live_interval *i = sorted_intervals[j]->i;
        expire_old_intervals(active, i, free_regs);
        if (listLength(active) == registers) {
            spill_at_interval(active, i, num_stack_slots);
        } else {
            i->assign_type = ASSIGN_TYPE_REGISTER;
            i->assign.reg = select_reg(free_regs, registers);
            active_insert(active, i);
        }
    }

    free(sorted_intervals);
    free(free_regs);
}

