#include "../all.h"
#include <assert.h>
#include <stdbool.h>


typedef struct omap_elem {
    char *asm_instr;
    /* true if asm_instr can be a register-immediate instruction,
     * false otherwise. A register-immediate instruction can have
     * one operand (narg == 1) or two operands (narg == 2),
     * the operand that hold the immediate value is the first operand
     * if narg == 1 and is the second operand if narg == 2. */
    bool can_be_reg_imm;
    /* The immediate value v must be imm_min <= v <= imm_max */
    int imm_min;
    int imm_max;
    /* true if asm_instr is commutative, false otherwise */
    bool is_commutative;
    /* argn can be only 1 or 2 */
    unsigned int argn;
} omap_elem;

const omap_elem omap[] = {
	[ADD_INSTR] = {
        .asm_instr = "add%k %=, %0, %1",
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2,
    },
    [SUB_INSTR] = {
        .asm_instr = "sub %=, %0, %1",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [MUL_INSTR] = {
        .asm_instr = "mul %=, %0, %1",
        .can_be_reg_imm = false,
        .is_commutative = true,
        .argn = 2,
    },
    [DIV_INSTR] = {
        .asm_instr = "div %=, %0, %1",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [UDIV_INSTR] = {
        .asm_instr = "divu %=, %0, %1",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [REM_INSTR] = {
        .asm_instr = "rem %=, %0, %1",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [UREM_INSTR] = {
        .asm_instr = "remu %=, %0, %1",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [AND_INSTR] = {
        .asm_instr = "and%k %=, %0, %1",
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2
    },
    [OR_INSTR] = {
        .asm_instr = "or%k %=, %0, %1",
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2,
    },
    [SHL_INSTR] = {
        .asm_instr = "sll%k %=, %0, %1",
        .can_be_reg_imm = true,
        .is_commutative = false,
        .imm_min = -16,
        .imm_max =  15,
        .argn = 2,
    },
    [SAR_INSTR] = {
        .asm_instr = "sra%k %=, %0, %1",
        .can_be_reg_imm = true,
        .is_commutative = false,
        .imm_min = -16,
        .imm_max =  15,
        .argn = 2,
    },
    [EQZW_INSTR] = {
        .asm_instr = "seqz %=, %0",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [XOR_INSTR] = {
        .asm_instr = "xor%k %=, %0, %1",
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = true,
        .argn = 2,
    },
    [CSLTW_INSTR] = {
        .asm_instr = "slt%k %=, %0, %1",
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = false,
        .argn = 2,
    },
    [CULTW_INSTR] = {
        .asm_instr = "slt%ku %=, %0, %1",
        .can_be_reg_imm = true,
        .imm_min = -2048,
        .imm_max =  2047,
        .is_commutative = false,
        .argn = 2,
    },
    [STOREW_INSTR] = {
        .asm_instr = "sw %0, 0(%1)",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 2,
    },
    [LOADW_INSTR] = {
        .asm_instr = "lw %=, 0(%0)",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [LOADUB_INSTR] = {
        .asm_instr = "lw %=, 0(%0)",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [STOREB_INSTR] = {
        .asm_instr = "sb %0, 0(%1)",
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 1,
    },
    [SHR_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [CNEW_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [CALL_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [PAR_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [ARG_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [EXTSW_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [COPY_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [PUSH_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
    [POP_INSTR] = {
        .asm_instr = NULL,
        .can_be_reg_imm = false,
        .is_commutative = false,
        .argn = 0,
    },
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
            switch (arg->val.con->type) {
                case CInt64: {
                    fprintf(f, "\tli %s, %"PRId64"\n",
                    rname[r], arg->val.con->val.i);
                    arg->type = RLoc;
                    arg->val.loc.type = REGISTER;
                    arg->val.loc.as.reg = r;
                } break;
                case CAddr: {
                    fprintf(f, "\tla %s, %s\n",
                    rname[r], arg->val.con->val.addr);
                    arg->type = RLoc;
                    arg->val.loc.type = REGISTER;
                    arg->val.loc.as.reg = r;
                } break;
                default:
                    assert(0);
            }
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

static bool is_immediate_val(Ref *r, int imm_min, int imm_max) {
    if (r->type == RCon) {
        Con *c = r->val.con;
        if (c->type == CInt64) {
            if (imm_min <= c->val.i && c->val.i <= imm_max) {
                return true;
            }
        }
    }
    return false;
}

static void fix_ins_args(Ins *i, FILE *f) {
    const omap_elem *e = &omap[i->op];
    if (e->can_be_reg_imm) {
        if (e->argn == 2) {
            bool imm = is_immediate_val(
                &i->arg[1],
                e->imm_min,
                e->imm_max);
            if (imm) {
                fix_arg(&i->arg[0], rv32_reserved_reg[0], f);
                return;
            } else if (e->is_commutative) {
                bool imm = is_immediate_val(
                    &i->arg[0],
                    e->imm_min,
                    e->imm_max);
                if (imm) {
                    Ref tmp = i->arg[0];
                    i->arg[0] = i->arg[1];
                    i->arg[1] = tmp;
                    fix_arg(&i->arg[0], rv32_reserved_reg[0], f);
                    return;
                }
            }
        } else if (e->argn == 1) {
            bool imm = is_immediate_val(
                &i->arg[0],
                e->imm_min,
                e->imm_max);
            if (imm) return;
        }
    }

    for (unsigned int j = 0; j < e->argn; j++) {
        fix_arg(&i->arg[j], rv32_reserved_reg[j], f);
    }
}

static void emitf(const omap_elem *e, Ins *i, FILE *f) {
    char *s = e->asm_instr;
    assert(s != NULL);
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
            case 'k': {
                bool imm = is_immediate_val(
                    &i->arg[e->argn-1],
                    e->imm_min,
                    e->imm_max);
                if (imm) {
                    fputc('i', f);
                }
            } break;
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
                switch (r.type) {
                    case RLoc: {
                        assert(r.val.loc.type == REGISTER);
                        fputs(rname[r.val.loc.as.reg], f);
                    } break;
                    case RCon: {
                        assert(r.val.con->type == CInt64);
                        fprintf(f, "%"PRId64, r.val.con->val.i);
                    } break;
                    default:
                        assert(0);
                }
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
                                /* mv rd, rs1 is a assembler pseudo instruction, it is
                                * transformed by the assembler to addi rd, rs1, 0 */
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
                    fprintf(f, "\tli %s, %"PRId64"\n",
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

static void emitins(Fn *fn, Ins *i, FILE *f) {
    /* handle special instruction cases */
    switch (i->op) {
        case COPY_INSTR: {
            emit_copy(i, f);
        } return;
        case CALL_INSTR: {
            assert(i->arg[0].type == RCon);
            assert(i->arg[0].val.con->type == CAddr);
            fprintf(f, "\tcall %s\n", i->arg[0].val.con->val.addr);
        } return;
        case PUSH_INSTR: {
            assert(i->arg[0].type == RLoc);
            assert(i->arg[0].val.loc.type == REGISTER);
            fprintf(f, "\tadd sp, sp, -4\n");
            fprintf(f, "\tsw %s, 0(sp)\n", rname[i->arg[0].val.loc.as.reg]);
        } return;
        case POP_INSTR: {
            assert(i->to.type == RLoc);
            assert(i->to.val.loc.type == REGISTER);
            fprintf(f, "\tlw %s, 0(sp)\n", rname[i->to.val.loc.as.reg]);
            fprintf(f, "\tadd sp, sp, 4\n");
        } return;
        /* There is no subi r1, r2, 42 instruction,
         * but it can be implemented as addi r1, r2, -42 */
        case SUB_INSTR: {
            const omap_elem *e = &omap[ADD_INSTR];
            if (is_immediate_val(&i->arg[1], e->imm_min, e->imm_max)) {
                i->op = ADD_INSTR;
                i->arg[1] = newIntConst(fn, i->arg[1].val.con->val.i * -1);
            }
        } break;
        default: {
        } break;
    }

    /* handle general instruction cases */
    fix_ins_args(i, f);
    emitf(&omap[i->op], i, f);

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

    assert(fn->num_stack_slots == 0);
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

    bool lbl = false;
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
            emitins(fn, i, f);
        }
        lbl = true;
        switch (b->jmp.type) {
            case HALT_JUMP_TYPE: {
                fprintf(f, "\tebreak\n");
            } break;
            case RET0_JUMP_TYPE: {
                if (frame <= 2048) {
                    fprintf(f,
                        "\tadd sp, sp, %d\n",
                        frame);
                } else {
                    fprintf(f,
                        "\tli t6, %d\n"
                        "\tadd sp, sp, t6\n",
                        frame);
                }
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
                if (b->succ[0] != blk_next) {
                    fprintf(f, "\tj .L%s\n", b->succ[0]->name);
                } else {
                    lbl = false;
                }
            } break;
            case JNZ_JUMP_TYPE: {
                fix_arg(&b->jmp.arg, rv32_reserved_reg[0], f);
                listNode *blk_node_next = listNextNode(blk_node);
                Blk *blk_next = NULL;
                if (blk_node_next != NULL) {
                    blk_next = listNodeValue(blk_node_next);
                }
                if (blk_next == b->succ[0]) {
                    fprintf(f,
                        "\tbeqz %s, .L%s\n",
                        rname[b->jmp.arg.val.loc.as.reg],
                        b->succ[1]->name);
                } else if (blk_next == b->succ[1]) {
                    fprintf(f,
                        "\tbnez %s, .L%s\n",
                        rname[b->jmp.arg.val.loc.as.reg],
                        b->succ[0]->name);
                } else panic();
            } break;
            default:
                panic();
        }
    }

    fprintf(f, ".type %s, @function\n", fn->name);
    fprintf(f, ".size %s, .-%s\n\n", fn->name, fn->name);
}

static void rv32_datalnk(Data *d, FILE *f) {
    char *section = "bss";
    listNode *data_node;
    listNode *data_iter = listFirst(d->dataField_list);
    while ((data_node = listNext(&data_iter)) != NULL) {
        DataField *df = listNodeValue(data_node);
        if (df->type != DZeros) {
            section = "data";
            break;
        }
    }
    fprintf(f, ".%s\n", section);
    if (d->lnk.align) {
        fprintf(f, ".balign %d\n", d->lnk.align);
    }
    if (d->lnk.is_exported) {
        fprintf(f, ".globl %s\n", d->name);
    }
    fprintf(f, "%s:\n", d->name);
}

void rv32_emitdata(Data *d, FILE *f) {
    rv32_datalnk(d, f);
    listNode *data_node;
    listNode *data_iter = listFirst(d->dataField_list);
    while ((data_node = listNext(&data_iter)) != NULL) {
        DataField *df = listNodeValue(data_node);
        switch (df->type) {
            case DByte:
                assert(0);
                break;
            case DWord:
                fprintf(f, "\t.int %"PRId32"\n", df->val.w);
                break;
            case DLong:
                assert(0);
                break;
            case DZeros:
                fprintf(f, "\t.fill %"PRId64",1,0\n", df->val.z);
                break;
            case DString:
                fprintf(f, "\t.ascii %s\n", df->val.s);
                break;
            default:
                assert(0);
        }
    }
    fprintf(f, "\n");
}

