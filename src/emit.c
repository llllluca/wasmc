#include "all.h"
#include <assert.h>
#include <string.h>

static void err(char *s, ...) {
	va_list ap;

	va_start(ap, s);
	fprintf(stderr, "qbe: ");
	vfprintf(stderr, s, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

enum {
	SecText,
	SecData,
	SecBss,
};

void emitlnk(char *n, Lnk *l, int s, FILE *f) {
	static char *sec[2][3] = {
		[0][SecText] = ".text",
		[0][SecData] = ".data",
		[0][SecBss] = ".bss",
		[1][SecText] = ".abort \"unreachable\"",
		[1][SecData] = ".section .tdata,\"awT\"",
		[1][SecBss] = ".section .tbss,\"awT\"",
	};
	char *pfx, *sfx;

	pfx = n[0] == '"' ? "" : "_";
	sfx = "";
	if (l->sec) {
		fprintf(f, ".section %s", l->sec);
		if (l->secf)
			fprintf(f, ",%s", l->secf);
	} else
		fputs(sec[l->thread != 0][s], f);
	fputc('\n', f);
	if (l->align)
		fprintf(f, ".balign %d\n", l->align);
	if (l->export)
		fprintf(f, ".globl %s%s\n", pfx, n);
	fprintf(f, "%s%s%s:\n", pfx, n, sfx);
}

void emitdat(Dat *d, FILE *f) {
	static char *dtoa[] = {
		[DB] = "\t.byte",
		[DH] = "\t.short",
		[DW] = "\t.int",
		[DL] = "\t.quad"
	};
	static int64_t zero;
	char *p;

	switch (d->type) {
	case DStart:
		zero = 0;
		break;
	case DEnd:
		if (zero != -1) {
			emitlnk(d->name, d->lnk, SecBss, f);
			fprintf(f, "\t.fill %"PRId64",1,0\n", zero);
		}
		break;
	case DZ:
		if (zero != -1)
			zero += d->u.num;
		else
			fprintf(f, "\t.fill %"PRId64",1,0\n", d->u.num);
		break;
	default:
		if (zero != -1) {
			emitlnk(d->name, d->lnk, SecData, f);
			if (zero > 0)
				fprintf(f, "\t.fill %"PRId64",1,0\n", zero);
			zero = -1;
		}
		if (d->isstr) {
			if (d->type != DB)
				err("strings only supported for 'b' currently");
			fprintf(f, "\t.ascii %s\n", d->u.str);
		}
		else if (d->isref) {
			p = d->u.ref.name[0] == '"' ? "" : "_";
			fprintf(f, "%s %s%s%+"PRId64"\n",
				dtoa[d->type], p, d->u.ref.name,
				d->u.ref.off);
		}
		else {
			fprintf(f, "%s %"PRId64"\n",
				dtoa[d->type], d->u.num);
		}
		break;
	}
}
