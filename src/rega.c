#include "all.h"
#include <assert.h>
#include <string.h>

static Tmp *input_of(Phi *phi, Blk *b) {
    listNode *phi_arg_node;
    listNode *phi_arg_iter = listFirst(phi->phi_arg_list);
    while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
        Phi_arg *phi_arg = listNodeValue(phi_arg_node);
        if (b == phi_arg->b && phi_arg->r.type == RTmp) {
            return phi_arg->r.val.tmp;
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

live_interval *build_intervals(Fn *f) {
    //printf("lenght of f->blk_list: %ld\n", listLength(f->blk_list));
    long n = listLength(f->tmp_list);
    live_interval *intervals = xcalloc(n, sizeof(struct live_interval));
    memset(intervals, 0, n * sizeof(struct live_interval));
    unsigned int next_interval = 0;

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
            if (t->i == NULL) {
                assert(next_interval < n);
                t->i = &intervals[next_interval++];
                t->i->tmp = t;
            }
            t->i->start = b->start_id;
            if (t->i->end == 0) {
                t->i->end = b->end_id;
            }
        }

        listNode *ins_node;
        listNode *ins_iter = listLast(b->ins_list);
        while ((ins_node = listPrev(&ins_iter)) != NULL) {
            Ins *ins = listNodeValue(ins_node);
            Tmp *t = ins->to.val.tmp;
            if (ins->to.type == RTmp && t != NULL) {
                if (t->i == NULL) {
                    assert(next_interval < n);
                    t->i = &intervals[next_interval++];
                    t->i->tmp = t;
                }
                t->i->start = ins->id;
                live_remove(live, t);
            }

            for (unsigned int i = 0; i < 2; i++) {
                t = ins->arg[i].val.tmp;
                if (ins->arg[i].type == RTmp && t != NULL) {
                    if (t->i == NULL) {
                        assert(next_interval < n);
                        t->i = &intervals[next_interval++];
                        t->i->tmp = t;
                    }
                    t->i->start = b->start_id;
                    if (t->i->end == 0) {
                        t->i->end = ins->id;
                    }
                    listAddNodeHead(live, t);
                }
            }
        }
        if (b->jmp.type == JNZ_JUMP_TYPE || b->jmp.type == RET1_JUMP_TYPE) {
            Tmp *t = b->jmp.arg.val.tmp;
            if (t->i == NULL) {
                assert(next_interval < n);
                t->i = &intervals[next_interval++];
                t->i->tmp = t;
            }
            t->i->start = b->start_id;
            if (t->i->end == 0) {
                t->i->end = b->jmp.id;
            }
            listAddNodeHead(live, t);
        }
        listNode *phi_node;
        listNode *phi_iter = listFirst(b->phi_list);
        while ((phi_node = listNext(&phi_iter)) != NULL) {
            Phi *phi = listNodeValue(phi_node);
            Tmp *t = phi->to.val.tmp;
            assert(phi->to.type == RTmp && t != NULL);
            live_remove(live, t);
        }

        //TODO: loop are not implemented here

        b->live_in = live;
        /*
        printf("End of block %s\n", b->name);
        listNode *tmp_node;
        listNode *tmp_iter = listFirst(f->tmp_list);
        while ((tmp_node = listNext(&tmp_iter)) != NULL) {
            Tmp *t = listNodeValue(tmp_node);
            printf("%s: i.start = %d, i.end = %d\n", t->name, t->i.start, t->i.end);
        }
        printf("live at start of block %s:", b->name);
        live_iter = listFirst(live);
        while ((live_node = listNext(&live_iter)) != NULL) {
            Tmp *t = listNodeValue(live_node);
            printf("%s ", t->name);
        }
        printf("\n");
        */
        listRelease(live);
        //TODO: b->live_in can be freed for each block b
    }
    return intervals;
}
static int compare_intervals(const live_interval *lhs, const live_interval *rhs) {
    return lhs->start - rhs->start;
}

static void expire_old_intervals(list *active, live_interval *i, unsigned int *next_reg) {
    listNode *active_node;
    listNode *active_iter = listFirst(active);
    while ((active_node = listNext(&active_iter)) != NULL) {
        live_interval *j = listNodeValue(active_node);
        if (j->end >= i->start) return;
        listDelNode(active, active_node);
        (*next_reg)--;
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

static void spill_at_interval(list *active, live_interval *i, unsigned int *next_stack_slot) {
    listNode *spill_node = listLast(active);
    live_interval *spill = listNodeValue(spill_node);
    if (spill->end > i->end) {
        i->reg = spill->reg;
        spill->location = (*next_stack_slot)++;
        listDelNode(active, spill_node);
        active_insert(active, i);
    } else {
        i->location = (*next_stack_slot)++;
    }
}

live_interval *linear_scan_register_allocator(Fn *f, unsigned int registers) {
    list *active = listCreate();
    unsigned int next_stack_slot = 1;
    unsigned int next_reg = 1;
    long n = listLength(f->tmp_list);
    live_interval *intervals = build_intervals(f);
//TODO: modify build_intervals in order to produce intervals array sorted by increasing start point.
    qsort(intervals, n, sizeof(struct live_interval), 
          (int (*)(const void *, const void*)) compare_intervals);

    for (unsigned int j = 0; j < n; j++) {
        live_interval *i = &intervals[j];
        i->tmp->i = i;
        expire_old_intervals(active, i, &next_reg);
        if (listLength(active) == registers) {
            spill_at_interval(active, i, &next_stack_slot);
        } else {
            i->reg = next_reg++; 
            active_insert(active, i);
        }
    }

    return intervals;
}


