#include "libqbe.h"
#include <stdarg.h>

static uint *nblk = NULL;
static Blk **linked_list_tail = NULL;
static Phi *phi_list_head = NULL;
static func_return_type rcls;

Target T;

#define Ke -2 // must match Ke in qbe-1.2/parse.c
#define Km Kl // must match Km in qbe-1.2/parse.c
const Op optab[NOp] = {
#define O(op, t, cf) [O##op]={#op, t, cf},
    #include "qbe-1.2/ops.h"
};

#if QBE_DEBUG != 0
char debug['Z'+1] = {
	['P'] = 0, /* parsing */
	['M'] = 0, /* memory optimization */
	['N'] = 0, /* ssa construction */
	['C'] = 0, /* copy elimination */
	['F'] = 0, /* constant folding */
	['A'] = 0, /* abi lowering */
	['I'] = 0, /* instruction selection */
	['L'] = 0, /* liveness */
	['S'] = 0, /* spilling */
	['R'] = 0, /* reg. allocation */
};

void printfn(Fn *fn, FILE *f) {
	static char ktoc[] = "wlsd";
	static char *jtoa[NJmp] = {
	#define X(j) [J##j] = #j,
		JMPS(X)
	#undef X
	};
	Blk *b;
	Phi *p;
	Ins *i;
	uint n;

	fprintf(f, "function $%s() {\n", fn->name);
	for (b=fn->start; b; b=b->link) {
		fprintf(f, "@%s\n", b->name);
		for (p=b->phi; p; p=p->link) {
			fprintf(f, "\t");
			printref(p->to, fn, f);
			fprintf(f, " =%c phi ", ktoc[p->cls]);
			assert(p->narg);
			for (n=0;; n++) {
				fprintf(f, "@%s ", p->blk[n]->name);
				printref(p->arg[n], fn, f);
				if (n == p->narg-1) {
					fprintf(f, "\n");
					break;
				} else
					fprintf(f, ", ");
			}
		}
		for (i=b->ins; i<&b->ins[b->nins]; i++) {
			fprintf(f, "\t");
			if (!req(i->to, R)) {
				printref(i->to, fn, f);
				fprintf(f, " =%c ", ktoc[i->cls]);
			}
			assert(optab[i->op].name);
			fprintf(f, "%s", optab[i->op].name);
			if (req(i->to, R))
				switch (i->op) {
				case Oarg:
				case Oswap:
				case Oxcmp:
				case Oacmp:
				case Oacmn:
				case Oafcmp:
				case Oxtest:
				case Oxdiv:
				case Oxidiv:
					fputc(ktoc[i->cls], f);
				}
			if (!req(i->arg[0], R)) {
				fprintf(f, " ");
				printref(i->arg[0], fn, f);
			}
			if (!req(i->arg[1], R)) {
				fprintf(f, ", ");
				printref(i->arg[1], fn, f);
			}
			fprintf(f, "\n");
		}
		switch (b->jmp.type) {
		case Jret0:
		case Jretsb:
		case Jretub:
		case Jretsh:
		case Jretuh:
		case Jretw:
		case Jretl:
		case Jrets:
		case Jretd:
		case Jretc:
			fprintf(f, "\t%s", jtoa[b->jmp.type]);
			if (b->jmp.type != Jret0 || !req(b->jmp.arg, R)) {
				fprintf(f, " ");
				printref(b->jmp.arg, fn, f);
			}
			if (b->jmp.type == Jretc)
				fprintf(f, ", :%s", typ[fn->retty].name);
			fprintf(f, "\n");
			break;
		case Jhlt:
			fprintf(f, "\thlt\n");
			break;
		case Jjmp:
			if (b->s1 != b->link)
				fprintf(f, "\tjmp @%s\n", b->s1->name);
			break;
		default:
			fprintf(f, "\t%s ", jtoa[b->jmp.type]);
			if (b->jmp.type == Jjnz) {
				printref(b->jmp.arg, fn, f);
				fprintf(f, ", ");
			}
			assert(b->s1 && b->s2);
			fprintf(f, "@%s, @%s\n", b->s1->name, b->s2->name);
			break;
		}
	}
	fprintf(f, "}\n");
}

static void printcon(Con *c, FILE *f) {
	switch (c->type) {
	case CUndef:
		break;
	case CAddr:
		if (c->sym.type == SThr)
			fprintf(f, "thread ");
		fprintf(f, "$%s", str(c->sym.id));
		if (c->bits.i)
			fprintf(f, "%+"PRIi64, c->bits.i);
		break;
	case CBits:
		if (c->flt == 1)
			fprintf(f, "s_%f", c->bits.s);
		else if (c->flt == 2)
			fprintf(f, "d_%lf", c->bits.d);
		else
			fprintf(f, "%"PRIi64, c->bits.i);
		break;
	}
}

void printref(Ref r, Fn *fn, FILE *f) {
	int i;
	Mem *m;

	switch (rtype(r)) {
	case RTmp:
		if (r.val < Tmp0)
			fprintf(f, "R%d", r.val);
		else
			fprintf(f, "%%%s", fn->tmp[r.val].name);
		break;
	case RCon:
		if (req(r, UNDEF))
			fprintf(f, "UNDEF");
		else
			printcon(&fn->con[r.val], f);
		break;
	case RSlot:
		fprintf(f, "S%d", rsval(r));
		break;
	case RCall:
		fprintf(f, "%04x", r.val);
		break;
	case RType:
		fprintf(f, ":%s", typ[r.val].name);
		break;
	case RMem:
		i = 0;
		m = &fn->mem[r.val];
		fputc('[', f);
		if (m->offset.type != CUndef) {
			printcon(&m->offset, f);
			i = 1;
		}
		if (!req(m->base, R)) {
			if (i)
				fprintf(f, " + ");
			printref(m->base, fn, f);
			i = 1;
		}
		if (!req(m->index, R)) {
			if (i)
				fprintf(f, " + ");
			fprintf(f, "%d * ", m->scale);
			printref(m->index, fn, f);
		}
		fputc(']', f);
		break;
	case RInt:
		fprintf(f, "%d", rsval(r));
		break;
	}
}
#endif

void err(char *s, ...) {
	va_list ap;

	va_start(ap, s);
	fprintf(stderr, "qbe: ");
	vfprintf(stderr, s, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

static int usecheck(Ref r, int k, Fn *fn) {
	return rtype(r) != RTmp || fn->tmp[r.val].cls == k
		|| (fn->tmp[r.val].cls == Kl && k == Kw);
}

void typecheck(Fn *fn) {
}

/*
void typecheck(Fn *fn) {
	Blk *b;
	Phi *p;
	Ins *i;
	uint n;
	int k;
	Tmp *t;
	Ref r;
	BSet pb[1], ppb[1];

	fillpreds(fn);
	bsinit(pb, fn->nblk);
	bsinit(ppb, fn->nblk);
	for (b=fn->start; b; b=b->link) {
		for (p=b->phi; p; p=p->link)
			fn->tmp[p->to.val].cls = p->cls;
		for (i=b->ins; i<&b->ins[b->nins]; i++)
			if (rtype(i->to) == RTmp) {
				t = &fn->tmp[i->to.val];
				if (clsmerge(&t->cls, i->cls))
					err("temporary %%%s is assigned with"
						" multiple types", t->name);
			}
	}
	for (b=fn->start; b; b=b->link) {
		bszero(pb);
		for (n=0; n<b->npred; n++)
			bsset(pb, b->pred[n]->id);
		for (p=b->phi; p; p=p->link) {
			bszero(ppb);
			t = &fn->tmp[p->to.val];
			for (n=0; n<p->narg; n++) {
				k = t->cls;
				if (bshas(ppb, p->blk[n]->id))
					err("multiple entries for @%s in phi %%%s",
						p->blk[n]->name, t->name);
				if (!usecheck(p->arg[n], k, fn))
					err("invalid type for operand %%%s in phi %%%s",
						fn->tmp[p->arg[n].val].name, t->name);
				bsset(ppb, p->blk[n]->id);
			}
			if (!bsequal(pb, ppb))
				err("predecessors not matched in phi %%%s", t->name);
		}
		for (i=b->ins; i<&b->ins[b->nins]; i++)
			for (n=0; n<2; n++) {
				k = optab[i->op].argcls[n][i->cls];
				r = i->arg[n];
				t = &fn->tmp[r.val];
				if (k == Ke)
					err("invalid instruction type in %s",
						optab[i->op].name);
				if (rtype(r) == RType)
					continue;
				if (rtype(r) != -1 && k == Kx)
					err("no %s operand expected in %s",
						n == 1 ? "second" : "first",
						optab[i->op].name);
				if (rtype(r) == -1 && k != Kx)
					err("missing %s operand in %s",
						n == 1 ? "second" : "first",
						optab[i->op].name);
				if (!usecheck(r, k, fn))
					err("invalid type for %s operand %%%s in %s",
						n == 1 ? "second" : "first",
						t->name, optab[i->op].name);
			}
		r = b->jmp.arg;
		if (isret(b->jmp.type)) {
			if (b->jmp.type == Jretc)
				k = Kl;
			else if (b->jmp.type >= Jretsb)
				k = Kw;
			else
				k = b->jmp.type - Jretw;
			if (!usecheck(r, k, fn))
				goto JErr;
		}
		if (b->jmp.type == Jjnz && !usecheck(r, Kw, fn))
		JErr:
			err("invalid type for jump argument %%%s in block @%s",
				fn->tmp[r.val].name, b->name);
		if (b->s1 && b->s1->jmp.type == Jxxx)
			err("block @%s is used undefined", b->s1->name);
		if (b->s2 && b->s2->jmp.type == Jxxx)
			err("block @%s is used undefined", b->s2->name);
	}
}
*/

void optimizeFunc(Fn *fn) {
    uint n;

    /* T.abi0(fn) eliminate sub-word abi op variants for targets that
     * treat char/short/... as words. The IR obtained from the compilation
     * of WebAssembly doesn't use sub-word abi op variants. */
    //T.abi0(fn);
    fillrpo(fn);
    fillpreds(fn);
    filluse(fn);
    //----------------------
    promote(fn);
    filluse(fn);
    ssa(fn);
    filluse(fn);
    ssacheck(fn);
    fillalias(fn);
    loadopt(fn);
    filluse(fn);
    fillalias(fn);
    coalesce(fn);
    filluse(fn);
    ssacheck(fn);
    copy(fn);
    filluse(fn);
    fold(fn);
    T.abi1(fn);
    simpl(fn);
    fillpreds(fn);
    filluse(fn);
    T.isel(fn);
    fillrpo(fn);
    filllive(fn);
    fillloop(fn);
    fillcost(fn);
    spill(fn);
    rega(fn);
    fillrpo(fn);
    simpljmp(fn);
    fillpreds(fn);
    fillrpo(fn);
    assert(fn->rpo[0] == fn->start);
    for (n=0;; n++)
        if (n == fn->nblk-1) {
            fn->rpo[n]->link = 0;
            break;
        } else
            fn->rpo[n]->link = fn->rpo[n+1];
    T.emitfn(fn, stdout);
    fprintf(stdout, "/* end function %s */\n\n", fn->name);
    freeall();
}

#define DATA_NAME_LEN 16
static char data_name[DATA_NAME_LEN];
static Lnk data_lnk;

void newData(char *name, Lnk *lnk) {
    strncpy(data_name, name, DATA_NAME_LEN);
    data_lnk = *lnk;
    Dat d;
    d.type = DStart;
    d.name = data_name;
    d.lnk = &data_lnk;
    emitdat(&d, stdout);
}

void addNumDataField(int type, int64_t num) {
    Dat d;
    d.name = data_name;
    d.lnk = &data_lnk;
    d.type = type;
    d.isstr = 0;
    d.isref = 0;
    memset(&d.u, 0, sizeof(d.u));
    d.u.num = num;
    emitdat(&d, stdout);
}

void addStrDataField(int type, char *str) {
    Dat d;
    d.name = data_name;
    d.lnk = &data_lnk;
    d.type = type;
    d.isstr = 1;
    d.isref = 0;
    memset(&d.u, 0, sizeof(d.u));
    d.u.str = str;
    emitdat(&d, stdout);
}

void closeData(void) {
    Dat d;
    d.name = data_name;
    d.lnk = &data_lnk;
    d.type = DEnd;
    emitdat(&d, stdout);
    fputs("/* end data */\n\n", stdout);
    freeall();
}

Ref newTemp(Fn *f) {

    int index = f->ntmp;
    /* f->ntmp is incremented in newtmp() */
    newtmp(NULL, NO_TYPE, f);
    //snprintf(f->tmp[index].name, NString, "t%d", index);
    return TMP(index);
}

static void closeBlock(Blk *b) {
    b->nins = curi - insb;
    idup(&b->ins, insb, b->nins);
    curi = insb;
    *linked_list_tail = b;
    linked_list_tail = &b->link;
    if (phi_list_head != NULL) {
        b->phi = phi_list_head;
        phi_list_head = NULL;
    }
}

Fn *newFunc(Lnk *link_info, func_return_type ret_type, char *name) {
    rcls = ret_type;
    curi = insb; 
    Fn *f = alloc(sizeof(Fn));
    f->ntmp = 0;
    f->ncon = 2;
    f->tmp = vnew(f->ntmp, sizeof(Tmp), PFn);
    f->con = vnew(f->ncon, sizeof(Con), PFn);
    for (int i = 0; i < Tmp0; ++i) {
        if (T.fpr0 <= i && i < T.fpr0 + T.nfpr) {
            newtmp(NULL, Kd, f);
        } else {
            newtmp(NULL, Kl, f);
        }
    }
    f->con[0].type = CBits;
    f->con[0].bits.i = 0xdeaddead;  /* UNDEF */
    f->con[1].type = CBits;
    linked_list_tail = &f->start;
    nblk = &f->nblk;
    f->nblk = 0;
    if (link_info != NULL) {
        f->lnk = *link_info;
    } else {
        f->lnk = (Lnk) {0};
    }
    /* curf->retty is different from Kx only form custom data type */
    f->retty = NO_TYPE;
    if (name == NULL) {
        err("function need a explicit name");
    }
    strncpy(f->name, name, NString-1);
    f->mem = vnew(0, sizeof(Mem), PFn);
    f->nmem = 0;
    f->rpo = 0;
    return f;
}

Ref newFuncParam(Fn *f, simple_type type) {
    Ref r = newTemp(f);
    *curi = (Ins) {
        .op = PAR_INSTR,
        .cls = type,
        .to = r,
        .arg = {R}
    };
    curi++;
    return r;
}

Blk *newBlock() {
    Blk *b = newblk();
    b->id = (*nblk)++;

    //snprintf(b->name, NString, "l%d", b->id);
    return b;
}

Ref newIntConst(Fn *f, int64_t val) {
    Con c;
    memset(&c, 0, sizeof c);
    c.type = CBits;
    c.bits.i = val;
    return newcon(&c, f);
}

void instr(Ref r, simple_type type, instr_opcode op, Ref arg1, Ref arg2) {
    curi->op = op;
    curi->cls = type;
    curi->to = r;
    curi->arg[0] = arg1;
    curi->arg[1] = arg2;
    curi++;
}

void jmp(Fn *f, Blk *b, Blk *b0) {
    b->jmp.type = Jjmp;
    b->s1 = b0;
    closeBlock(b);
    if (f->start == b0) {
        err("invalid jump to the start block");
    }
}

void jnz(Fn *f, Blk *b, Ref r, Blk *b0, Blk *b1) {
    b->jmp.type = Jjnz;
    b->jmp.arg = r;
    b->s1 = b0;
    b->s2 = b1;
    closeBlock(b);
    if (f->start == b0 || f->start == b1) {
        err("invalid jump to the start block");
    }
}

void ret(Blk *b) {
    b->jmp.type = Jret0;
    closeBlock(b);
}

void retRef(Blk *b, Ref r) {
    b->jmp.type = Jretw + rcls;
    b->jmp.arg = r;
    closeBlock(b);
}

void halt(Blk *b) {
    b->jmp.type = Jhlt;
    closeBlock(b);
}

Ref newAddrConst(Fn *f, char *addrName) {
    Con c;
    memset(&c, 0, sizeof(Con));
    c.type = CAddr;
    c.sym.id = intern(addrName);
    return newcon(&c, f);
}

void newFuncCallArg(simple_type type, Ref r) {
    *curi = (Ins) {
        .op = ARG_INSTR,
        .cls = type,
        .to = R,
        .arg = {r}
    };
    curi++;
}

void phi(Ref temp, simple_type type, Blk *b0, Ref r0, Blk *b1, Ref r1) {
    Phi *phi = alloc(sizeof(Phi));
    phi->to = temp;
    phi->cls = type;
    phi->narg = 2;
    phi->arg = vnew(phi->narg, sizeof(Ref), PFn);
    phi->arg[0] = r0;
    phi->arg[1] = r1;
    phi->blk = vnew(phi->narg, sizeof(Blk), PFn);
    phi->blk[0] = b0;
    phi->blk[1] = b1;

    if (phi_list_head == NULL) {
        phi_list_head = phi;
    } else {
        Phi *ptr = phi_list_head;
        Phi *prev;
        do {
            prev = ptr;
            ptr = ptr->link;
        } while(ptr != NULL);
        prev->link = phi;
    }
}



