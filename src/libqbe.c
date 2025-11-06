#include "libqbe.h"
#include "all.h"
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

static unsigned int next_temp_id = 0;

void printref(Ref r, FILE *f);

const char *optab[] = {
    /* Arithmetic and Bits */
    [ADD_INSTR] = "add",
    [SUB_INSTR] = "sub",
    [NEG_INSTR] = "neg",
    [DIV_INSTR] = "div",
    [REM_INSTR] = "rem",
    [UDIV_INSTR] = "udiv",
    [UREM_INSTR] = "urem",
    [MUL_INSTR] = "mul",
    [AND_INSTR] = "and",
    [OR_INSTR] = "or",
    [XOR_INSTR] = "xor",
    [SAR_INSTR] = "sar",
    [SHR_INSTR] = "shr",
    [SHL_INSTR] = "shl",
    /* Comparisons */
    [CEQW_INSTR] = "ceqw",
    [CNEW_INSTR] = "cnew",
    [CSGEW_INSTR] = "csgew",
    [CSGTW_INSTR] = "csgtw",
    [CSLEW_INSTR] = "cslew",
    [CSLTW_INSTR] = "csltw",
    [CUGEW_INSTR] = "cugew",
    [CUGTW_INSTR] = "cugtw",
    [CULEW_INSTR] = "culew",
    [CULTW_INSTR] = "cultw",
    [CEQL_INSTR] = "ceql",
    [CNEL_INSTR] = "cnel",
    [CSGEL_INSTR] = "csgel",
    [CSGTL_INSTR] = "csgtl",
    [CSLEL_INSTR] = "cslel",
    [CSLTL_INSTR] = "csltl",
    [CUGEL_INSTR] = "cugel",
    [CUGTL_INSTR] = "cugtl",
    [CULEL_INSTR] = "culel",
    [CULTL_INSTR] = "cultl",
    [CEQS_INSTR] = "ceqs",
    [CGES_INSTR] = "cges",
    [CGTS_INSTR] = "cgts",
    [CLES_INSTR] = "cles",
    [CLTS_INSTR] = "clts",
    [CNES_INSTR] = "cnes",
    [COS_INSTR] = "cos",
    [CUOS_INSTR] = "cuos",
    [CEQD_INSTR] = "ceqd",
    [CGED_INSTR] = "cged",
    [CGTD_INSTR] = "cgtd",
    [CLED_INSTR] = "cled",
    [CLTD_INSTR] = "cltd",
    [CNED_INSTR] = "cned",
    [COD_INSTR] = "cod",
    [CUOD_INSTR] = "cuod",
    /* Memory */
    [STOREB_INSTR] = "storeb",
    [STOREH_INSTR] = "storeh",
    [STOREW_INSTR] = "storew",
    [STOREL_INSTR] = "storel",
    [STORES_INSTR] = "stores",
    [STORED_INSTR] = "stored",
    [LOADSB_INSTR] = "loadsb",
    [LOADUB_INSTR] = "loadub",
    [LOADSH_INSTR] = "loadsh",
    [LOADUH_INSTR] = "loaduh",
    [LOADSW_INSTR] = "loadsw",
    [LOADUW_INSTR] = "loaduw",
    [LOAD_INSTR] = "load",
    [ALLOC4_INSTR] = "alloc4",
    [ALLOC8_INSTR] = "alloc8",
    [ALLOC16_INSTR] = "alloc16",
    /* Conversions */
    [EXTSB_INSTR] = "extsb",
    [EXTUB_INSTR] = "extub",
    [EXTSH_INSTR] = "extsh",
    [EXTUH_INSTR] = "extuh",
    [EXTSW_INSTR] = "extsw",
    [EXTUW_INSTR] = "extuw",
    [EXTS_INSTR] = "exts",
    [TRUNCD_INSTR] = "truncd",
    [STOSI_INSTR] = "stosi",
    [STOUI_INSTR] = "stoui",
    [DTOSI_INSTR] = "dtosi",
    [DTOUI_INSTR] = "dtoui",
    [SWTOF_INSTR] = "swtof",
    [UWTOF_INSTR] = "uwtof",
    [SLTOF_INSTR] = "sltof",
    [ULTOF_INSTR] = "ultof",
    /* Cast and Copy */
    [CAST_INSTR] = "cast",
    [COPY_INSTR] = "copy",
    /* Variadic */
    [VAARG_INSTR] = "vaarg",
    [VASTART_INSTR] = "vastart",
    /* Call */
    [CALL_INSTR] = "call",
    /* Other */
    [DBGLOC_INSTR] = "dbgloc",
    [NOP_INSTR] = "nop",
    [ADDR_INSTR] = "addr",
    [BLIT0_INSTR] = "blit0",
    [BLIT1_INSTR] = "blit1",
    [SWAP_INSTR] = "swap",
    [SIGN_INSTR] = "sign",
    [SALLOC_INSTR] = "salloc",
    [XIDIV_INSTR] = "xidiv",
    [XDIV_INSTR] = "xdiv",
    [XCMP_INSTR] = "xcmp",
    [XTEST_INSTR] = "xtest",
    [ACMP_INSTR] = "acmp",
    [ACMN_INSTR] = "acmn",
    [AFCMP_INSTR] = "afcmp",
    [REQZ_INSTR] = "reqz",
    [RNEZ_INSTR] = "rnez",
    [PAR_INSTR] = "par",
    [PARSB_INSTR] = "parsb",
    [PARUB_INSTR] = "parub",
    [PARSH_INSTR] = "parsh",
    [PARUH_INSTR] = "paruh",
    [PARC_INSTR] = "parc",
    [PARE_INSTR] = "pare",
    [ARG_INSTR] = "arg",
    [ARGSB_INSTR] = "argsb",
    [ARGUB_INSTR] = "argub",
    [ARGSH_INSTR] = "argsh",
    [ARGUH_INSTR] = "arguh",
    [ARGC_INSTR] = "argc",
    [ARGE_INSTR] = "arge",
    [ARGV_INSTR] = "argv",
    [FLAGIEQ_INSTR] = "flagieq",
    [FLAGINE_INSTR] = "flagine",
    [FLAGISGE_INSTR] = "flagisge",
    [FLAGISGT_INSTR] = "flagisgt",
    [FLAGISLE_INSTR] = "flagisle",
    [FLAGISLT_INSTR] = "flagislt",
    [FLAGIUGE_INSTR] = "flagiuge",
    [FLAGIUGT_INSTR] = "flagiugt",
    [FLAGIULE_INSTR] = "flagiule",
    [FLAGIULT_INSTR] = "flagiult",
    [FLAGFEQ_INSTR] = "flagfeq",
    [FLAGFGE_INSTR] = "flagfge",
    [FLAGFGT_INSTR] = "flagfgt",
    [FLAGFLE_INSTR] = "flagfle",
    [FLAGFLT_INSTR] = "flagflt",
    [FLAGFNE_INSTR] = "flagfne",
    [FLAGFO_INSTR] = "flagfo",
    [FLAGFUO_INSTR] = "flagfuo",
};

void printfn(Fn *fn, FILE *f) {
    char ktoc[] = " wlsd";
    unsigned int n = 0;

    if (fn->lnk.is_exported) {
        fprintf(f, "export ");
    }
    fprintf(f, "function %c $%s() {\n", ktoc[fn->ret_type], fn->name);
    listNode *blk_node;
    listNode *blk_iter = listFirst(fn->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        fprintf(f, "%d:@%s\n", b->id, b->name);
        listNode *phi_node;
        listNode *phi_iter = listFirst(b->phi_list);
        while ((phi_node = listNext(&phi_iter)) != NULL) {
            Phi *p = listNodeValue(phi_node);
            fprintf(f, "\t");
            printref(p->to, f);
            fprintf(f, " =%c phi ", ktoc[p->type]);
            n = 0;
            listNode *phi_arg_node;
            listNode *phi_arg_iter = listFirst(p->phi_arg_list);
            while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
                Phi_arg *pa = listNodeValue(phi_arg_node);
                fprintf(f, "@%s ", pa->b->name);
                printref(pa->r, f);
                if (n == listLength(p->phi_arg_list)-1) {
                    fprintf(f, "\n");
                    break;
                } else {
                    fprintf(f, ", ");
                }
                n++;
            }
            fprintf(f, "\n");
        }
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            fprintf(f, "\t %d:", i->id);
            if (!req(i->to, UNDEF_TMP_REF)) {
                printref(i->to, f);
                fprintf(f, " =%c ", ktoc[i->type]);
            }
            fprintf(f, "%s", optab[i->op]);
            if (!req(i->arg[0], UNDEF_TMP_REF)) {
                fprintf(f, " ");
                printref(i->arg[0], f);
            }
            if (!req(i->arg[1], UNDEF_TMP_REF)) {
                fprintf(f, ", ");
                printref(i->arg[1], f);
            }
            fprintf(f, "\n");
        }
        fprintf(f, "\t %d:", b->jmp.id);
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
                fprintf(f, "jmp @%s\n", b->s1->name);
                break;
            case JNZ_JUMP_TYPE:
                fprintf(f, "jnz ");
                printref(b->jmp.arg, f);
                fprintf(f, ", ");
                fprintf(f, "@%s, @%s\n", b->s1->name, b->s2->name);
                break;
            default:
                panic();
        }
    }
    fprintf(f, "}\n");
}

void printdata(Data *d, FILE *f) {
    
    unsigned int n = 1;
    if (d->lnk.is_exported) {
        fprintf(f, "export ");
    }
    fprintf(f, "data $%s = { ", d->name);
    listNode *node;
    listNode *iter = listFirst(d->dataField_list);
    while ((node = listNext(&iter)) != NULL) {
        DataField *df = listNodeValue(node);
        switch (df->type) {
            case DByte:
                fprintf(f, "b %d", df->val.b);
                break;
            case DWord:
                fprintf(f, "w %"PRId32, df->val.w);
                break;
            case DLong:
                fprintf(f, "l %"PRId64, df->val.l);
                break;
            case DZeros:
                fprintf(f, "z %"PRIu64, df->val.z);
                break;
            case DString:
                fprintf(f, "b %s", df->val.s);
                break;
            default:
                panic();
        }
        if (n < listLength(d->dataField_list)) {
            fprintf(f, ", ");
        }
        n++;
    }
    fprintf(f, " }\n");
}

static void printcon(Con *c, FILE *f) {
    switch (c->type) {
        case CInt64:
            fprintf(f, "%"PRIi64, c->val.i);
            break;
        case CAddr:
            fprintf(f, "$%s", c->val.addr);
            break;
        default:
            break;
    }
}

void printref(Ref r, FILE *f) {
    switch (r.type) {
    case RTmp:
        if (r.val.tmp_node != NULL) {
            Tmp *t = listNodeValue(r.val.tmp_node);
            fprintf(f, "%%%s", t->name);
        } else {
            fprintf(f, "UNDEF_TMP_REF");
        }
        break;
    case RCon:
        printcon(r.val.con, f);
        break;
    default:
        panic();
    }
}

static void err(char *s, ...) {
    va_list ap;

    va_start(ap, s);
    fprintf(stderr, "qbe: ");
    vfprintf(stderr, s, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

Data *newData(Lnk *link_info, char *name) {
    Data *d = xmalloc(sizeof(Data));
    strncpy(d->name, name, NString);
    d->lnk = *link_info;
    d->dataField_list = listCreate();
    listSetFreeMethod(d->dataField_list, (void (*)(void *))freeDataField);
    return d;
}

void dataAppendByteField(Data *d, unsigned char b) {
    DataField *df = xmalloc(sizeof(DataField));
    df->type = DByte;
    df->val.b = b;
    listAddNodeTail(d->dataField_list, df);
}

void dataAppendWordField(Data *d, int32_t w) {
    DataField *df = xmalloc(sizeof(DataField));
    df->type = DWord;
    df->val.w = w;
    listAddNodeTail(d->dataField_list, df);
}

void dataAppendLongField(Data *d, int64_t l) {
    DataField *df = xmalloc(sizeof(DataField));
    df->type = DLong;
    df->val.l = l;
    listAddNodeTail(d->dataField_list, df);
}

void dataAppendZerosField(Data *d, uint64_t z) {
    DataField *df = xmalloc(sizeof(DataField));
    df->type = DZeros;
    df->val.z = z;
    listAddNodeTail(d->dataField_list, df);
}

void dataAppendStringField(Data *d, char *s, unsigned int len) {
    DataField *df = xmalloc(sizeof(DataField));
    char *str = xcalloc(len, sizeof(char));
    memcpy(str, s, len);
    df->type = DString;
    df->val.s = str;
    listAddNodeTail(d->dataField_list, df);
}

Ref newTemp(Fn *f) {

    Tmp *tmp = xmalloc(sizeof(Tmp));
    snprintf(tmp->name, NString, "t%d", next_temp_id++);
    tmp->cls = NO_TYPE;
    tmp->use_list = listCreate();
    listSetFreeMethod(tmp->use_list, free);
    listAddNodeTail(f->tmp_list, tmp);
    tmp->i = NULL;
    listNode *tmp_node = listLast(f->tmp_list);
    return (Ref) { .type = RTmp, .val.tmp_node = tmp_node };
}

Blk *newBlock(uint32_t nlocals) {

    static unsigned int id = 0;

    Blk *b = xmalloc(sizeof(struct Blk));
    b->phi_list = listCreate();
    listSetFreeMethod(b->phi_list, (void (*)(void *))freePhi);
    b->ins_list = listCreate();
    listSetFreeMethod(b->ins_list, free);
    b->is_visited = 0;
    b->live_in = NULL;
    b->jmp.type = NONE_JUMP_TYPE;
    b->jmp.arg = UNDEF_TMP_REF;
    b->s1 = NULL;
    b->s2 = NULL;
    b->preds = listCreate();
    snprintf(b->name, NString, "l%d", id++);
    b->locals = NULL;
    b->incomplete_phis = NULL;
    if (nlocals > 0) {
        b->locals = xcalloc(nlocals, sizeof(struct Ref));
        b->incomplete_phis = xcalloc(nlocals, sizeof(listNode *));
        for (uint32_t i = 0; i < nlocals; i++) {
            b->locals[i] = UNDEF_TMP_REF;
            b->incomplete_phis[i] = NULL;
        }
    }
    b->is_sealed = 0;
    return b;
}

Fn *newFunc(Lnk *link_info, simple_type ret_type, char *name, Blk *start) {
    next_temp_id = 0;
    Fn *f = xmalloc(sizeof(struct Fn));
    f->tmp_list = listCreate();
    listSetFreeMethod(f->tmp_list, (void (*)(void *)) freeTemp);
    f->con_list = listCreate();
    listSetFreeMethod(f->con_list, free);
    f->blk_list = listCreate();
    listSetFreeMethod(f->blk_list, (void (*)(void *)) freeBlock);
    listAddNodeTail(f->blk_list, start);
    f->start = start;
    f->ret_type = ret_type;
    f->lnk = *link_info;
    if (name == NULL) {
        err("function need a explicit name");
    }
    strncpy(f->name, name, NString-1);
    return f;
}

Ref newFuncParam(Fn *f, simple_type type) {
    Ref r = newTemp(f);
    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->op = PAR_INSTR;
    ins->type = type;
    ins->to = r;
    ins->arg[0] = UNDEF_TMP_REF;
    ins->arg[1] = UNDEF_TMP_REF;
    listAddNodeTail(f->start->ins_list, ins);
    return r;
}


Ref newIntConst(Fn *f, int64_t val) {
    Con *con = xmalloc(sizeof(struct Con));
    con->type = CInt64;
    con->val.i = val;
    listAddNodeTail(f->con_list, con);
    return (Ref) { .type = RCon, .val.con = con };
}

void instr(Blk *b, Ref r, simple_type type, instr_opcode op, Ref arg1, Ref arg2) {
    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->op = op;
    ins->type = type;
    ins->to = r;
    ins->arg[0] = arg1;
    ins->arg[1] = arg2;
    listAddNodeTail(b->ins_list, ins);
    listNode *ins_node = listLast(b->ins_list);
    if (arg1.type == RTmp && arg1.val.tmp_node != NULL) {
        addUsage(listNodeValue(arg1.val.tmp_node), UIns, (Use_ptr) { .ins = ins_node });
    }
    if (arg2.type == RTmp && arg2.val.tmp_node != NULL) {
        addUsage(listNodeValue(arg2.val.tmp_node), UIns, (Use_ptr) { .ins = ins_node });
    }
}

void jmp(Fn *f, Blk *b, Blk *b0) {
    assert(b0->is_sealed == 0);
    b->jmp.type = JMP_JUMP_TYPE;
    b->s1 = b0;
    if (f->start != b) {
        listAddNodeTail(f->blk_list, b);
    }
    listAddNodeTail(b0->preds, b);
    if (f->start == b0) {
        err("invalid jump to the start block");
    }
}

void jnz(Fn *f, Blk *b, Ref r, Blk *b0, Blk *b1) {
    assert(b0->is_sealed == 0);
    assert(b1->is_sealed == 0);
    b->jmp.type = JNZ_JUMP_TYPE;
    b->jmp.arg = r;
    b->s1 = b0;
    b->s2 = b1;
    listNode *b_node = listFirst(f->blk_list);
    if (f->start != b) {
        listAddNodeTail(f->blk_list, b);
        b_node = listLast(f->blk_list);
    }
    listAddNodeTail(b0->preds, b);
    listAddNodeTail(b1->preds, b);

    if (r.type == RTmp && r.val.tmp_node != NULL) {
        addUsage(listNodeValue(r.val.tmp_node), UJmp, (Use_ptr) { .blk = b_node });
    }
    if (f->start == b0 || f->start == b1) {
        err("invalid jump to the start block");
    }
}

void ret(Fn *f, Blk *b) {
    if (f->start != b) {
        listAddNodeTail(f->blk_list, b);
    }
    b->jmp.type = RET0_JUMP_TYPE;
}

void retRef(Fn *f, Blk *b, Ref r) {
    listNode *b_node = listFirst(f->blk_list);
    if (f->start != b) {
        listAddNodeTail(f->blk_list, b);
        b_node = listLast(f->blk_list);
    }
    b->jmp.type = RET1_JUMP_TYPE;
    b->jmp.arg = r;
    if (r.type == RTmp && r.val.tmp_node != NULL) {
        addUsage(listNodeValue(r.val.tmp_node), UJmp, (Use_ptr) { .blk = b_node });
    }
}

void halt(Fn *f, Blk *b) {
    if (f->start != b) {
        listAddNodeTail(f->blk_list, b);
    }
    b->jmp.type = HALT_JUMP_TYPE;
}

// TODO: intern the string addrName
Ref newAddrConst(Fn *f, char *addrName) {

    Con *con = xmalloc(sizeof(struct Con));
    con->type = CAddr;
    con->val.addr = addrName;
    listAddNodeTail(f->con_list, con);
    return (Ref) { .type = RCon, .val.con = con };
}

void newFuncCallArg(Blk *b, simple_type type, Ref r) {

    Ins *ins = xmalloc(sizeof(struct Ins));
    ins->op = ARG_INSTR;
    ins->type = type;
    ins->to = UNDEF_TMP_REF;
    ins->arg[0] = r;
    ins->arg[1] = UNDEF_TMP_REF;
    listAddNodeTail(b->ins_list, ins);
    listNode *ins_node = listLast(b->ins_list);
    if (r.type == RTmp && r.val.tmp_node != NULL) {
        addUsage(listNodeValue(r.val.tmp_node), UIns, (Use_ptr) { .ins = ins_node });
    }
}

listNode *newPhi(Blk *b, Ref temp, simple_type type) {
    Phi *phi = xmalloc(sizeof(struct Phi));
    phi->to = temp;
    phi->type = type;
    phi->block = b;
    phi->phi_arg_list = listCreate();
    listSetFreeMethod(phi->phi_arg_list, (void (*)(void *))freePhi_arg);
    listAddNodeTail(b->phi_list, phi);
    return listLast(b->phi_list);
}

void phiAppendOperand(listNode *phi_node, Blk *b, Ref arg) {
    Phi *phi = listNodeValue(phi_node);
    Phi_arg *pa = xmalloc(sizeof(struct Phi_arg));
    pa->r = arg;
    pa->b = b;
    listAddNodeTail(phi->phi_arg_list, pa);
    if (arg.type == RTmp && arg.val.tmp_node != NULL) {
        addUsage(listNodeValue(arg.val.tmp_node), UPhi, (Use_ptr) { .phi = phi_node });
    }
}

void freePhi_arg(Phi_arg *arg) {
    free(arg);
}

void freePhi(Phi *phi) {
    listRelease(phi->phi_arg_list);
    free(phi);
}


inline int req(Ref a, Ref b) {
    return a.type == b.type && a.val.tmp_node == b.val.tmp_node;
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
    listRelease(f->con_list);
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

void freeData(Data *d) {
    listRelease(d->dataField_list);
    free(d);
}
void freeDataField(DataField *df) {
    if (df->type == DString) {
        free(df->val.s);
    }
    free(df);
}
