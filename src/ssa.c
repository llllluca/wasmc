#include "all.h"
#include "libqbe.h"
#include <assert.h>

static Ref read_local_rec(func_compile_ctx_t *ctx, Blk *b, uint32_t index);
static Ref add_phi_operands(func_compile_ctx_t *ctx, listNode *phi_node, uint32_t index);
static Ref try_remove_trivial_phi(func_compile_ctx_t *ctx, listNode *phi_node);

void write_local(Blk *b, uint32_t index, Ref value) {
    Ref *ptr = &b->locals[index];
    Use_ptr u = { .local = ptr };
    if (ptr->type == REF_TYPE_TMP && ptr->as.tmp_node != NULL) {
        rmUsage(listNodeValue(ptr->as.tmp_node), ULocal, u);
    }
    b->locals[index] = value;
    if (value.type == REF_TYPE_TMP && value.as.tmp_node != NULL) {
        addUsage(listNodeValue(value.as.tmp_node), ULocal, u);
    }
}

Ref read_local(func_compile_ctx_t *ctx, Blk *b, uint32_t index) {
    if (b->locals[index].type != REF_TYPE_UNDEFINED) {
        return b->locals[index];
    }
    return read_local_rec(ctx, b, index);
}

static Ref read_local_rec(func_compile_ctx_t *ctx, Blk *b, uint32_t index) {
    Ref value;
    if (!b->is_sealed) {
        // Incomplete CFG
        Ref temp = newTemp(ctx->qbe_func);
        listNode *phi_node = newPhi(b, temp, cast(local_type(ctx, index-1)));
        b->incomplete_phis[index] = phi_node;
        value = temp;
    } else if (listLength(b->preds) == 1) {
        // Optimize the common case of one predecessor: no phi needed
        Blk *p = listNodeValue(listFirst(b->preds));
        value = read_local(ctx, p, index);
    } else {
        // Break potential cycles with operandless phi
        Ref temp = newTemp(ctx->qbe_func);
        listNode *phi_node = newPhi(b, temp, cast(local_type(ctx, index-1)));
        write_local(b, index, temp);
        value = add_phi_operands(ctx, phi_node, index);
    }
    write_local(b, index, value);
    return value;
}

static Ref add_phi_operands(func_compile_ctx_t *ctx, listNode *phi_node, uint32_t index) {
    // Determine phi operands from the predecessors
    Phi *phi = listNodeValue(phi_node);
    Blk *b = phi->block;
    listNode *node;
    listNode *iter = listFirst(b->preds);
    while ((node = listNext(&iter)) != NULL) {
        Blk *pred = listNodeValue(node);
        Ref l = read_local(ctx, pred, index);
        phiAppendOperand(phi_node, pred, l);
    }
    return try_remove_trivial_phi(ctx, phi_node);
}

static void phi_replace_by(listNode *phi_node, Ref r) {

    Phi *phi = listNodeValue(phi_node);
    Tmp *to = listNodeValue(phi->to.as.tmp_node);
    listNode *use_node;
    listNode *use_iter = listFirst(to->use_list);
    while ((use_node = listNext(&use_iter)) != NULL) {
        Use *use = listNodeValue(use_node);
        switch (use->type) {
            case UPhi: {
                Phi *p = listNodeValue(use->u.phi);
                listNode *phi_arg_node;
                listNode *phi_arg_iter = listFirst(p->phi_arg_list);
                while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
                    Phi_arg *phi_arg = listNodeValue(phi_arg_node);
                    if (REF_EQ(phi_arg->r, phi->to)) {
                        phi_arg->r = r;
                        if (r.as.tmp_node != NULL) {
                            addUsage(listNodeValue(r.as.tmp_node), UPhi, use->u);
                        }
                    }
                }
            } break;
            case UIns: {
                Ins *i = listNodeValue(use->u.ins);
                for (unsigned int j = 0; j < 2; j++) {
                    if (REF_EQ(i->arg[j], phi->to)) {
                        i->arg[j] = r;
                        if (r.as.tmp_node != NULL) {
                            addUsage(listNodeValue(r.as.tmp_node), UIns, use->u);
                        }
                    }
                }
            } break;
            case UJmp: {
                Blk *b = listNodeValue(use->u.blk);
                b->jmp.arg = r;
                if (r.as.tmp_node != NULL) {
                    addUsage(listNodeValue(r.as.tmp_node), UJmp, use->u);
                }
            } break;
            case ULocal: {
                Ref *l = use->u.local;
                *l = r;
                if (r.as.tmp_node != NULL) {
                    addUsage(listNodeValue(r.as.tmp_node), ULocal, use->u);
                }
            } break;
            default:
                panic();
        }
    }
}

static Ref try_remove_trivial_phi(func_compile_ctx_t *ctx, listNode *phi_node) {
    Phi *phi = listNodeValue(phi_node);
    Ref same = UNDEFINED_REF;
    listNode *phi_arg_node;
    listNode *phi_arg_iter = listFirst(phi->phi_arg_list);
    while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
        Phi_arg *op = listNodeValue(phi_arg_node);
        if (REF_EQ(same, op->r) || REF_EQ(op->r, phi->to)) {
            // Unique value or self-reference
            continue;
        }
        if (same.type != REF_TYPE_UNDEFINED) {
            // The phi merges at least two values: not trivial
            return phi->to;
        }
        same = op->r;
    }

    assert(phi->to.type == REF_TYPE_TMP);
    listNode *to_node = phi->to.as.tmp_node;
    Tmp *to = listNodeValue(to_node);

    phi_replace_by(phi_node, same);
    if (same.type == REF_TYPE_TMP && same.as.tmp_node != NULL) {
        Use_ptr u = { .phi = phi_node };
        rmUsage(listNodeValue(same.as.tmp_node), UPhi, u);
    }
    listDelNode(phi->block->phi_list, phi_node);

    listNode *use_node;
    listNode *use_iter = listFirst(to->use_list);
    while ((use_node = listNext(&use_iter)) != NULL) {
        Use *use = listNodeValue(use_node);
        if (use->type == UPhi && use->u.phi != phi_node) {
            try_remove_trivial_phi(ctx, use->u.phi);
        }
        listDelNode(to->use_list, use_node);
    }

    listDelNode(ctx->qbe_func->tmp_list, to_node);
    return same;
}

void seal_block(func_compile_ctx_t *ctx, Blk *b) {
    assert(b->is_sealed == 0);
    for (uint32_t i = 0; i < ctx->locals_len; i++) {
        if (b->incomplete_phis[i] == NULL) continue;
        add_phi_operands(ctx, b->incomplete_phis[i], i);
    }
    b->is_sealed = true;
}
