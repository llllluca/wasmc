#include "../all.h"
#include <assert.h>

const char * const omap[] = {
	[ADD_INSTR] = "add %=, %0, %1",
    [SUB_INSTR] = "sub %=, %0, %1",
};

const rv32_reg rv32_gp_reg[RV32_GP_NUM_REG] = {
    T0, T1, T2, T3, T4,
    S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, 
};

const rv32_reg rv32_arg_reg[RV32_ARG_NUM_REG] = {
    A0, A1, A2, A3, A4, A5, A6, A7,
};

const rv32_reg rv32_reserved_reg[RV32_RESERVED_NUM_REG] = {
    T5, T6
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


static void fix_arg(Ref *arg, rv32_reg r, FILE *f) {
    switch (arg->type) {
        case RCon: {
            assert(arg->val.con->type == CInt64);
            fprintf(f, "\tli %s, %"PRIi64"\n",
                rname[r], arg->val.con->val.i);
                arg->type = RLoc;
                arg->val.loc.type = REGISTER;
                arg->val.loc.as.reg = r;
        } break;
        case RLoc: {
            switch (arg->val.loc.type) {
                case REGISTER:
                    break;
                case STACK_SLOT: {
                    //TODO: not implemented
                    assert(0);
                } break;
                default:
                    assert(0);
            }
        } break;
        default:
            assert(0);
    }
}

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
                assert(r.type == RLoc);
                assert(r.val.loc.type == REGISTER);
                fputs(rname[r.val.loc.as.reg], f);
            } break;
            case '0': {
                Ref r = i->arg[0];
                assert(r.type == RLoc);
                assert(r.val.loc.type == REGISTER);
                fputs(rname[r.val.loc.as.reg], f);
             } break;
            case '1': {
                Ref r = i->arg[1];
                assert(r.type == RLoc);
                assert(r.val.loc.type == REGISTER);
                fputs(rname[r.val.loc.as.reg], f);
             } break;
            default:
                assert(0);
        }
    }
}

static int slot_fp_offset(unsigned int stack_slot) {
    panic();
    return stack_slot;
}

static void emit_copy(Ins *i, FILE *f) {
    assert(i->to.type == RLoc);
    switch (i->to.val.loc.type) {
        case REGISTER: {
            switch (i->arg[0].type) {

                case RLoc: {
                    location *loc = &i->arg[0].val.loc;
                    switch (loc->type) {
                        case REGISTER: {
                            if (i->to.val.loc.as.reg != loc->as.reg) {
                                fprintf(f, "\tmv %s, %s\n",
                                rname[i->to.val.loc.as.reg],
                                rname[loc->as.reg]);
                            }
                        } break;
                        case STACK_SLOT: {
                            // TODO: not implemented
                            assert(0);
                        } break;

                        default:
                            assert(0);
                    }
                } break;

                case RCon: {
                    assert(i->arg[0].val.con->type == CInt64);
                    fprintf(f, "\tli %s, %"PRIi64"\n",
                            rname[i->to.val.loc.as.reg],
                            i->arg[0].val.con->val.i);
                } break;

                default:
                    assert(0);
            }
        } break;
        case STACK_SLOT: {
            //TODO: not implemented
            assert(0);
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
            fix_arg(&i->arg[0], rv32_reserved_reg[0], f);
            fix_arg(&i->arg[1], rv32_reserved_reg[1], f);
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

void rv32_emitfn(Fn *fn, FILE *f) {

    fputs(".text\n", f);
    if (fn->lnk.is_exported) {
        fprintf(f, ".globl %s\n", fn->name);
    }
    fprintf(f, "%s:\n", fn->name);

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

    boolean lbl = FALSE;
    listNode *blk_node;
    listNode *blk_iter = listFirst(fn->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        if (lbl || listLength(b->preds) > 1) {
            fprintf(f, ".L%s:\n", b->name);
        }
        listNode *ins_node;
        listNode *ins_iter = listFirst(b->ins_list);
        while ((ins_node = listNext(&ins_iter)) != NULL) {
            Ins *i = listNodeValue(ins_node);
            emitins(i, f);
        }
        lbl = TRUE;
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
                listNode *blk_node_next = listNextNode(blk_node);
                Blk *blk_next = NULL;
                if (blk_node_next != NULL) {
                    blk_next = listNodeValue(blk_node_next);
                }
                if (b->s1 != blk_next) {
                    fprintf(f, "\tj .L%s\n", b->s1->name);
                } else {
                    lbl = FALSE;
                }
            } break;
            case JNZ_JUMP_TYPE: {
                fix_arg(&b->jmp.arg, rv32_reserved_reg[0], f);
                listNode *blk_node_next = listNextNode(blk_node);
                Blk *blk_next = NULL;
                if (blk_node_next != NULL) {
                    blk_next = listNodeValue(blk_node_next);
                }
                if (blk_next == b->s1) {
                    fprintf(f,
                        "\tbeqz %s, .L%s\n",
                        rname[b->jmp.arg.val.loc.as.reg],
                        b->s2->name);
                } else if (blk_next == b->s2) {
                    fprintf(f,
                        "\tbnez %s, .L%s\n",
                        rname[b->jmp.arg.val.loc.as.reg],
                        b->s1->name);
                } else panic();
            } break;
            default:
                panic();
        }
    }

    fprintf(f, ".type %s, @function\n", fn->name);
    fprintf(f, ".size %s, .-%s\n\n", fn->name, fn->name);
}
