/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * leb128.h - Constants of the LEB128 variable-length integers of the
 * WebAssembly binary format (spec 5.2.2).
 *
 * Each byte carries 7 bits of the value, least significant group first;
 * bit 7 says that another byte follows. In the last byte of a signed value,
 * bit 6 is the sign.
 */
#ifndef LEB128_H_INCLUDED
#define LEB128_H_INCLUDED

enum {
    LEB_PAYLOAD_BITS = 7,
    LEB_PAYLOAD_MASK = 0x7F,
    LEB_MORE = 0x80,
    LEB_SIGN = 0x40,
    LEB_BITS32 = 32, /* value widths of u32/i32 and i64 */
    LEB_BITS64 = 64,
};

#endif /* LEB128_H_INCLUDED */
