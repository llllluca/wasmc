/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * ra.h - liveness and linear-scan register allocation.
 *
 * Program positions are derived from slot indices: block bodies are
 * contiguous slot ranges laid out in begin order, so for an instruction in
 * slot i the position is 2*i+1, and the phis of block b are defined at
 * 2*first(b), just before its first instruction. A phi argument is a use at
 * the position of the predecessor's terminator.
 *
 * Every value gets one interval [def, last use], extended to the end of
 * each loop it is live into (a loop = the layout range from its header to
 * its last back-edge predecessor; the CFG of WASM code is reducible and
 * laid out in reverse post order). Linear scan (Poletto/Sarkar) without
 * splitting: a value lives in its register(s), or in a stack slot, for its
 * whole life. A 64-bit value takes two independent registers (one if its
 * high word is known zero: RA_NARROW). Spill slots are reused like
 * registers: a slot whose last owner's interval ended before a value's
 * starts can hold it, so the frame holds the values spilled at the same
 * time, not every value ever spilled.
 *
 * A register becomes free only after the last use of its value: the result
 * of an instruction never shares a register with one of its operands, so
 * the code generator may write the result before it has finished reading
 * the operands.
 *
 * Constants get no location (rematerialized at each use), nor do values
 * that are never used.
 */
#ifndef RA_H_INCLUDED
#define RA_H_INCLUDED

#include "ir.h"

#define RA_SLOT_NONE 0xFFFFFFFFu

typedef struct {
    /* allocatable registers, in order of preference */
    const uint8_t *callee; /* preserved across calls */
    unsigned ncallee;
    const uint8_t
        *caller; /* clobbered by calls: only for values not live across one */
    unsigned ncaller;
    /* does instruction `ref` clobber the caller-saved registers? A value is
     * "live across" it when it is defined before and still used at or after
     * it (its operands included: they are read while the call is set up). */
    int (*is_call)(void *ctx, ir_ref ref);
    void *ctx;
    /* optional [ir_nslots()]: values with RA_FOLDED set are folded into their
     * (single) user by the code generator and get no location; 64-bit values
     * with RA_NARROW set have a high word known to be zero or never read,
     * which the code generator never reads from a location: they take one
     * register or a 4-byte slot */
    const uint8_t *flags;
} ra_target;

#define RA_FOLDED 0x80u
#define RA_NARROW 0x40u

typedef struct {
    uint8_t *reg_lo; /* [ir_nslots()] register of the (low) word, 0 = none */
    uint8_t
        *reg_hi; /* [ir_nslots()] register of the high word (64-bit values) */
    uint32_t *slot_off;   /* [ir_nslots()] spill offset in the spill area, or
                             RA_SLOT_NONE */
    uint32_t spill_bytes; /* size of the spill area */
    uint32_t used_regs;   /* mask of the registers assigned to some value */
} ra_result;

/*
 * Allocates the finalized function `func`. The result arrays are taken from
 * `func`'s scratch region and stay there; temporaries are released. Returns
 * WASMC_OK or WASMC_ERR_OUT_OF_MEMORY.
 */
wasmc_err_t ra_run(ir_func *func, const ra_target *target, ra_result *out);

/* Scratch bytes of ra_run()'s result arrays, which it leaves allocated. */
uint32_t ra_result_bytes(const ir_func *func);

#endif /* RA_H_INCLUDED */
