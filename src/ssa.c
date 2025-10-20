#include "all.h"
#include "libqbe.h"
#include <assert.h>

static Ref read_local_rec(func_compile_ctx_t *ctx, Blk *b, uint32_t index);
static Ref add_phi_operands(func_compile_ctx_t *ctx, listNode *phi_node, uint32_t index);
static Ref try_remove_trivial_phi(listNode *phi_node);

void write_local(Blk *b, uint32_t index, Ref value) {
    b->locals[index] = value;
}

Ref read_local(func_compile_ctx_t *ctx, Blk *b, uint32_t index) {
    if (b->locals[index].val.tmp != NULL) {
        return b->locals[index];
    }
    return read_local_rec(ctx, b, index);
}

static Ref read_local_rec(func_compile_ctx_t *ctx, Blk *b, uint32_t index) {
    Ref value;
    if (!b->is_sealed) {
        // Incomplete CFG
        Ref temp = newTemp(ctx->qbe_func);
        Phi *phi = newPhi(temp, cast(local_type(ctx, index)));
        listNode *phi_node = addPhiToBlock(b, phi);
        b->incomplete_phis[index] = phi_node;
        value = temp;
    } else if (listLength(b->preds) == 1) {
        // Optimize the common case of one predecessor: no phi needed
        Blk *p = listNodeValue(listFirst(b->preds));
        value = read_local(ctx, p, index);
    } else {
        // Break potential cycles with operandless phi
        Ref temp = newTemp(ctx->qbe_func);
        Phi *phi = newPhi(temp, cast(local_type(ctx, index)));
        listNode *phi_node = addPhiToBlock(b, phi);
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
        phiAppendOperand(phi, pred, l);
    }
    return try_remove_trivial_phi(phi_node);
}

static void phi_replace_by(Phi *phi, Ref r) {

    Tmp *to = phi->to.val.tmp;
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
                    if (req(phi_arg->r, phi->to)) {
                        phi_arg->r = r;
                        Use_ptr u = { .phi = use->u.phi };
                        addUsage(r.val.tmp, UPhi, u);
                    }
                }
            } break;
            case UIns: {
                Ins *i = listNodeValue(use->u.ins);
                for (unsigned int j = 0; j < 2; j++) {
                    if (req(i->arg[j], phi->to)) {
                        i->arg[j] = r;
                        Use_ptr u = { .ins = use->u.ins };
                        addUsage(r.val.tmp, UIns, u);
                    }
                }
            } break;
            case UJmp: {
                Blk *b = listNodeValue(use->u.blk);
                b->jmp.arg = r;
                Use_ptr u = { .blk = use->u.blk };
                addUsage(r.val.tmp, UJmp, u);
            } break;
            default:
                panic();
        }
    }
}

static Ref try_remove_trivial_phi(listNode *phi_node) {
    Phi *phi = listNodeValue(phi_node);
    Ref same = UNDEF_TMP_REF;
    listNode *phi_arg_node;
    listNode *phi_arg_iter = listFirst(phi->phi_arg_list);
    while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
        Phi_arg *op = listNodeValue(phi_arg_node);
        if (req(same, op->r) || req(op->r, phi->to)) {
            // Unique value or self-reference
            continue;
        }
        if (!req(same, UNDEF_TMP_REF)) {
            // The phi merges at least two values: not trivial
            return phi->to;
        }
        same = op->r;
    }

    phi_replace_by(phi, same);
    assert(phi->to.type == RTmp);
    Tmp *to = phi->to.val.tmp;
    listDelNode(phi->block->phi_list, phi_node);

    listNode *use_node;
    listNode *use_iter = listFirst(to->use_list);
    while ((use_node = listNext(&use_iter)) != NULL) {
        Use *use = listNodeValue(use_node);
        if (use->type == UPhi) {
            try_remove_trivial_phi(use->u.phi);
        }
    }

    return same;
}

void seal_block(func_compile_ctx_t *ctx, Blk *b) {
    assert(b->is_sealed == 0);
    for (uint32_t i = 0; i < ctx->locals_len; i++) {
        if (b->incomplete_phis[i] == NULL) continue;
        add_phi_operands(ctx, b->incomplete_phis[i], i);
    }
    b->is_sealed = TRUE;
}
