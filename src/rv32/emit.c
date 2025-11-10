#include "../all.h"
#include <assert.h>

const char * const omap[] = {
	[ADD_INSTR] =  "add %=, %0, %1",
};

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

static void emitf(const char *s, Ins *i, FILE *f) {
    fputc('\t', f);
    for (;;) {
        char c;
        while ((c = *s++) != '%') {
            if (c == '\0') {
                fputc('\n', f);
                return;
            } else {
                fputc(c, f);
            }
        }
        switch ((c = *s++)) {
            case '=': {
                Ref r = i->to;
                assert(r.type == RReg);
                fputs(rname[r.val.reg], f);
            } break;
            case '0': {
                Ref r = i->arg[0];
                assert(r.type == RReg);
                fputs(rname[r.val.reg], f);
             } break;
            case '1': {
                Ref r = i->arg[1];
                switch (r.type) {
                    case RReg: {
                        fputs(rname[r.val.reg], f);
                    } break;
                    case RCon: {
                        Con *c = r.val.con;
                        fprintf(f, "%"PRIi64, c->val.i);
                    } break;
                    default: 
                        panic();
                }
             } break;
            default:
                panic();
        }
    }
}

static int slot_fp_offset(unsigned int stack_slot) {
    panic();
    return stack_slot;
}

static void emit_copy(Ins *i, FILE *f) {
    assert(i->op == COPY_INSTR);
    switch (i->to.type) {
        case RReg: {
            switch (i->arg[0].type) {
                case RReg: {
                    if (i->to.val.reg != i->arg[0].val.reg) {
                        fprintf(f, "\tmv %s, %s\n",
                            rname[i->to.val.reg], rname[i->arg[0].val.reg]);
                    }
                } break;
                case RStack_slot: {
                    int offset = slot_fp_offset(i->arg[0].val.stack_slot);
                    fprintf(f, "\tlw %s, %d(fp)\n",
                            rname[i->to.val.reg], offset);
                } break;
                case RCon: {
                    assert(i->arg[0].val.con->type == CInt64);
                    fprintf(f, "\tli %s, %"PRIi64"\n",
                            rname[i->to.val.reg], i->arg[0].val.con->val.i);
                } break;

                default:
                    panic();
            }
        } break;
        case RStack_slot: {
            switch (i->arg[0].type) {
                case RReg: {
                    int offset = slot_fp_offset(i->to.val.stack_slot);
                    fprintf(f, "\tsw %s, %d(fp)\n", rname[i->to.val.reg], offset);
                } break;
                default:
                    panic();
            }
        } break;
        default:
            panic();
    }
}

static void emitins(Ins *i, FILE *f) {
    switch (i->op) {
        case COPY_INSTR: {
            emit_copy(i, f);
        } break;
        case CALL_INSTR: {
            assert(i->arg[0].type == RCon);
            assert(i->arg[0].val.con->type == CAddr);
            fprintf(f, "\tcall %s\n", i->arg[0].val.con->val.addr);
        } break;
        default: {
            emitf(omap[i->op], i, f);
        } break;
    }
}

/*
  Stack-frame layout:

  +=============+
  | varargs     |
  |  save area  |
  +-------------+
  |  saved ra   |
  |  saved fp   |
  +-------------+ <- fp
  |    ...      |
  | spill slots |
  |    ...      |
  +-------------+
  |    ...      |
  |   locals    |
  |    ...      |
  +-------------+
  |   padding   |
  +-------------+
  | callee-save |
  |  registers  |
  +=============+

*/

static void emitfnlnk(char *name, Lnk *l, FILE *f) {
	fputs(".text\n", f);
	if (l->is_exported) {
		fprintf(f, ".globl %s\n", name);
    }
	fprintf(f, "%s:\n", name);
}

void rv32_emitfn(Fn *fn, FILE *f) {
    /*
    static int id0;
    int lbl, neg, off, frame, *pr, r;
    Blk *b, *s;
    Ins *i;
    */

    emitfnlnk(fn->name, &fn->lnk, f);

    fprintf(f, "\tsw fp, -8(sp)\n");
    fprintf(f, "\tsw ra, -4(sp)\n");
    fprintf(f, "\tadd fp, sp, -8\n");

    unsigned int frame = (8 + 4 * fn->num_stack_slots + 15) & ~15;

    if (frame <= 2048) {
        fprintf(f,
            "\tadd sp, sp, -%d\n",
            frame);
    } else {
        fprintf(f,
            "\tli t6, %d\n"
            "\tsub sp, sp, t6\n",
            frame);
    }

    listNode *blk_node;
    listNode *blk_iter = listFirst(fn->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        if (listLength(b->preds) > 1) {
            //fprintf(f, ".L%d:\n", id0+b->id);
            fprintf(f, ".L%s:\n", b->name);
        }
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            emitins(i, f);
        }
        switch (b->jmp.type) {
            case HALT_JUMP_TYPE: {
                fprintf(f, "\tebreak\n");
            } break;
            case RET0_JUMP_TYPE: {
                fprintf(f,
                    "\tlw ra, 4(fp)\n"
                    "\tlw fp, 0(fp)\n"
                    "\tret\n"
                );
            } break;
            case JMP_JUMP_TYPE: {
                listNode *blk_node_next = listNextNode(ins_node);
                Blk *blk_next = blk_node_next == NULL ?
                    NULL : listNodeValue(blk_node_next);
                if (b->s1 != blk_next) {
                    fprintf(f, "\tj .L%s\n", b->s1->name);
                } 
            } break;
            default:
                panic();
        }
    }
    fprintf(f, ".type %s, @function\n", fn->name);
    fprintf(f, ".size %s, .-%s\n", fn->name, fn->name);
    fprintf(f, "/* end function _f0 */\n\n");
}
