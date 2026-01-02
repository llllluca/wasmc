#include "libqbe.h"
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

/* utils.c */
extern void panic(void);
extern void *xcalloc(size_t nmemb, size_t size);
extern void *xmalloc(size_t size);

void printref(Ref r, FILE *f);

char *rname[] = {
    [ZERO] = "zero",
    [RA] = "ra",
    [SP] = "sp",
    [GP] = "gp",
    [TP] = "tp",
    [T0] = "t0",
    [T1] = "t1",
    [T2] = "t2",
    [FP] = "fp",
    [S1] = "s1",
    [A0] = "a0",
    [A1] = "a1",
    [A2] = "a2",
    [A3] = "a3",
    [A4] = "a4",
    [A5] = "a5",
    [A6] = "a6",
    [A7] = "a7",
    [S2] = "s2",
    [S3] = "s3",
    [S4] = "s4",
    [S5] = "s5",
    [S6] = "s6",
    [S7] = "s7",
    [S8] = "s8",
    [S9] = "s9",
    [S10] = "s10",
    [S11] = "s11",
    [T3] = "t3",
    [T4] = "t4",
    [T5] = "t5",
    [T6] = "t6",
};

const char *optab[] = {
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

/* Function definitions and calls */
    [IR_OPCODE_CALL] = "call",
    [IR_OPCODE_PAR]  = "par",
    [IR_OPCODE_ARG]  = "arg",

    /* Other Operations */
    [IR_OPCODE_COPY] = "copy",
    [IR_OPCODE_PUSH] = "push",
    [IR_OPCODE_POP]  = "pop",
};

void printfn(Fn *fn, FILE *f) {
    char *type_to_str[5] = {
        "void",
        "i32",
        "i64",
        "f32",
        "f64",
    };
    unsigned int n = 0;

    fprintf(f, "function %s %s() {\n", type_to_str[fn->ret_type], fn->name);
    listNode *blk_node;
    listNode *blk_iter = listFirst(fn->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        fprintf(f, "@%u\n", b->id);
        listNode *phi_node;
        if (b->phi_list != NULL) {
            listNode *phi_iter = listFirst(b->phi_list);
            while ((phi_node = listNext(&phi_iter)) != NULL) {
                Phi *p = listNodeValue(phi_node);
                fprintf(f, "\t");
                printref(p->to, f);
                fprintf(f, " = phi %s ", type_to_str[p->type]);
                if (p->phi_arg_list == NULL) {
                    fprintf(f, "\n");
                    continue;
                };
                n = 0;
                listNode *phi_arg_node;
                listNode *phi_arg_iter = listFirst(p->phi_arg_list);
                while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
                    Phi_arg *pa = listNodeValue(phi_arg_node);
                    fprintf(f, "@%u ", pa->b->id);
                    printref(pa->r, f);
                    if (n == listLength(p->phi_arg_list)-1) {
                        fprintf(f, "\n");
                        break;
                    } else {
                        fprintf(f, ", ");
                    }
                    n++;
                }
            }
        }
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            fprintf(f, "\t");
            if (i->op == IR_OPCODE_STORE || i->op == IR_OPCODE_STORE8) {
                fprintf(f, "%s ", optab[i->op]);
                printref(i->to, f);
                fprintf(f, ", ");
                printref(i->arg[0], f);
                fprintf(f, "(");
                printref(i->arg[1], f);
                fprintf(f, ")");
            } else {
                if (i->op != IR_OPCODE_ARG) {
                    printref(i->to, f);
                    fprintf(f, " = ");
                }
                fprintf(f, "%s %s", optab[i->op], type_to_str[i->type]);
                if (i->arg[0].type != REF_TYPE_UNDEFINED) {
                    fprintf(f, " ");
                    printref(i->arg[0], f);
                }
                if (i->arg[1].type != REF_TYPE_UNDEFINED) {
                    fprintf(f, ", ");
                    printref(i->arg[1], f);
                }
            }
            fprintf(f, "\n");
        }
        fprintf(f, "\t");
        switch (b->jmp.type) {
            case RET0_JUMP_TYPE:
                fprintf(f, "ret\n");
                break;
            case RET1_JUMP_TYPE:
                fprintf(f, "ret ");
                printref(b->jmp.arg, f);
                fprintf(f, "\n");
                break;
            case HALT_JUMP_TYPE:
                fprintf(f, "hlt\n");
                break;
            case JMP_JUMP_TYPE:
                fprintf(f, "jmp @%u\n", b->succ[0]->id);
                break;
            case JNZ_JUMP_TYPE:
                fprintf(f, "jnz ");
                printref(b->jmp.arg, f);
                fprintf(f, ", ");
                fprintf(f, "@%u, @%u\n", b->succ[0]->id, b->succ[1]->id);
                break;
            default:
                panic();
        }
    }
    fprintf(f, "}\n");
}

void printref(Ref r, FILE *f) {
    switch (r.type) {
    case REF_TYPE_UNDEFINED:
        fprintf(f, "UNDEFINED_REF");
        break;
    case REF_TYPE_TMP:
        if (r.as.tmp_node != NULL) {
            Tmp *t = listNodeValue(r.as.tmp_node);
            fprintf(f, "t%u", t->id);
        }
        break;
    case REF_TYPE_INT32_CONST:
        printf("%"PRIi32, r.as.int32_const);
        break;
    case REF_TYPE_NAME:
        printf("%s", r.as.name);
        break;
    case REF_TYPE_LOCATION: {
        switch (r.as.loc.type) {
            case REGISTER:
                fprintf(f, "%s", rname[r.as.loc.as.reg]);
                break;
            case STACK_SLOT:
                fprintf(f, "Stack_Slot_%u", r.as.loc.as.stack_slot);
                break;
            default:
                panic();
        }
    } break;
    default:
        panic();
    }
}

/*
static void err(char *s, ...) {
    va_list ap;

    va_start(ap, s);
    fprintf(stderr, "qbe: ");
    vfprintf(stderr, s, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}
*/

Ref newTemp(Fn *f) {

    Tmp *tmp = xmalloc(sizeof(Tmp));
    tmp->use_list = listCreate();
    listSetFreeMethod(tmp->use_list, free);
    listAddNodeTail(f->tmp_list, tmp);
    tmp->i = NULL;
    tmp->id = f->next_tmp_id++;
    listNode *tmp_node = listLast(f->tmp_list);
    return (Ref) { .type = REF_TYPE_TMP, .as.tmp_node = tmp_node };
}

Blk *newBlock(Fn *f) {

    Blk *b = malloc(sizeof(struct Blk));
    if (b == NULL) return NULL;
    memset(b, 0, sizeof(struct Blk));
    b->phi_list = listCreate();
    listSetFreeMethod(b->phi_list, (void (*)(void *))freePhi);
    b->ins_list = listCreate();
    listSetFreeMethod(b->ins_list, free);
    b->preds = listCreate();
    b->id = f->next_blk_id++;
    uint32_t n = f->local_count;
    if (n > 0) {
        b->locals = xcalloc(n, sizeof(struct Ref));
        b->incomplete_phis = xcalloc(n, sizeof(listNode *));
        for (uint32_t i = 0; i < n; i++) {
            b->locals[i] = UNDEFINED_REF;
            b->incomplete_phis[i] = NULL;
        }
    }

    return b;
}

Fn *newFunc(IRType ret_type, char *name, uint32_t local_count) {
    Fn *f = malloc(sizeof(struct Fn));
    if (f == NULL) return NULL;
    memset(f, 0, sizeof(struct Fn));
    f->tmp_list = listCreate();
    listSetFreeMethod(f->tmp_list, (void (*)(void *)) freeTemp);
    f->blk_list = listCreate();
    listSetFreeMethod(f->blk_list, (void (*)(void *)) freeBlock);
    f->ret_type = ret_type;
    f->local_count = local_count;
    strncpy(f->name, name, 16);
    return f;
}


Ref newFuncParam(Fn *f, IRType type) {
    Ref r = newTemp(f);
    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->op = IR_OPCODE_PAR;
    ins->type = type;
    ins->to = r;
    ins->arg[0] = UNDEFINED_REF;
    ins->arg[1] = UNDEFINED_REF;
    listAddNodeTail(f->start->ins_list, ins);
    return r;
}

void ir_append_ins(Blk *b, IROpcode op, IRType type,
                Ref arg0, Ref arg1, Ref arg2) {

    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->op = op;
    ins->type = type;
    ins->to = arg0;
    ins->arg[0] = arg1;
    ins->arg[1] = arg2;
    listAddNodeTail(b->ins_list, ins);
    listNode *ins_node = listLast(b->ins_list);
    if (arg1.type == REF_TYPE_TMP && arg1.as.tmp_node != NULL) {
        addUsage(listNodeValue(arg1.as.tmp_node), UIns, (Use_ptr) { .ins = ins_node });
    }
    if (arg2.type == REF_TYPE_TMP && arg2.as.tmp_node != NULL) {
        addUsage(listNodeValue(arg2.as.tmp_node), UIns, (Use_ptr) { .ins = ins_node });
    }
}

void jmp(Fn *f, Blk *b, Blk *b0) {
    b->jmp.type = JMP_JUMP_TYPE;
    b->succ[0] = b0;
    listAddNodeTail(b0->preds, b);
    listAddNodeTail(f->blk_list, b);
    /*
    if (f->start == b0) {
        err("invalid jump to the start block");
    }
    */
}

void jnz(Fn *f, Blk *b, Ref r, Blk *b0, Blk *b1) {
    b->jmp.type = JNZ_JUMP_TYPE;
    b->jmp.arg = r;
    b->succ[0] = b0;
    b->succ[1] = b1;
    listNode *b_node = listLast(f->blk_list);
    if (b0 != NULL) listAddNodeTail(b0->preds, b);
    if (b1 != NULL) listAddNodeTail(b1->preds, b);
    listAddNodeTail(f->blk_list, b);

    if (r.type == REF_TYPE_TMP && r.as.tmp_node != NULL) {
        addUsage(listNodeValue(r.as.tmp_node), UJmp, (Use_ptr) { .blk = b_node });
    }
    /*
    if (f->start == b0 || f->start == b1) {
        err("invalid jump to the start block");
    }
    */
}

void ret(Fn *f, Blk *b) {
    listAddNodeTail(f->blk_list, b);
    b->jmp.type = RET0_JUMP_TYPE;
}

void retRef(Fn *f, Blk *b, Ref r) {
    listNode *b_node = listLast(f->blk_list);
    b->jmp.type = RET1_JUMP_TYPE;
    b->jmp.arg = r;
    listAddNodeTail(f->blk_list, b);
    if (r.type == REF_TYPE_TMP && r.as.tmp_node != NULL) {
        addUsage(listNodeValue(r.as.tmp_node), UJmp, (Use_ptr) { .blk = b_node });
    }
}

void halt(Fn *f, Blk *b) {
    b->jmp.type = HALT_JUMP_TYPE;
    listAddNodeTail(f->blk_list, b);
}

void newFuncCallArg(Blk *b, IRType type, Ref r) {

    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->op = IR_OPCODE_ARG;
    ins->type = type;
    ins->to = UNDEFINED_REF;
    ins->arg[0] = r;
    ins->arg[1] = UNDEFINED_REF;
    listAddNodeTail(b->ins_list, ins);
    listNode *ins_node = listLast(b->ins_list);
    if (r.type == REF_TYPE_TMP && r.as.tmp_node != NULL) {
        addUsage(listNodeValue(r.as.tmp_node), UIns, (Use_ptr) { .ins = ins_node });
    }
}

listNode *newPhi(Blk *b, Ref temp, IRType type) {
    Phi *phi = xmalloc(sizeof(struct Phi));
    phi->to = temp;
    phi->type = type;
    phi->block = b;
    phi->phi_arg_list = NULL;
    listAddNodeTail(b->phi_list, phi);
    return listLast(b->phi_list);
}

void phiAppendOperand(listNode *phi_node, Blk *b, Ref arg) {
    Phi *phi = listNodeValue(phi_node);
    Phi_arg *pa = xmalloc(sizeof(struct Phi_arg));
    pa->r = arg;
    pa->b = b;
    if (phi->phi_arg_list == NULL) {
        phi->phi_arg_list = listCreate();
        listSetFreeMethod(phi->phi_arg_list, free);
    }
    listAddNodeTail(phi->phi_arg_list, pa);
    if (arg.type == REF_TYPE_TMP && arg.as.tmp_node != NULL) {
        addUsage(listNodeValue(arg.as.tmp_node), UPhi, (Use_ptr) { .phi = phi_node });
    }
}

void freePhi(Phi *phi) {
    listRelease(phi->phi_arg_list);
    free(phi);
}

void addUsage(Tmp *tmp, Use_type type, Use_ptr ptr) {
    if (tmp != NULL) {
        Use *use = xmalloc(sizeof(struct Use));
        use->type = type;
        use->u = ptr;
        listAddNodeTail(tmp->use_list, use);
    }
}

void rmUsage(Tmp *tmp, Use_type type, Use_ptr ptr) {
    if (tmp == NULL) return;
    listNode *use_node;
    listNode *use_iter = listFirst(tmp->use_list);
    while ((use_node = listNext(&use_iter)) != NULL) {
        Use *use = listNodeValue(use_node);
        if (use->type == type && use->u.ins == ptr.ins) {
            listDelNode(tmp->use_list, use_node);
        }
    }
}

void freeFunc(Fn *f) {
    listRelease(f->tmp_list);
    listRelease(f->blk_list);
    free(f);
}

void freeTemp(Tmp *tmp) {
    listRelease(tmp->use_list);
    free(tmp);
}

void freeBlock(Blk *b) {
    listRelease(b->phi_list);
    listRelease(b->ins_list);
    listRelease(b->preds);
    if (b->locals != NULL) {
        free(b->locals);
    }
    if (b->incomplete_phis != NULL) {
        free(b->incomplete_phis);
    }
    free(b);
}

