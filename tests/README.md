# WASM 1.0 core tests (wasmc fork)

This directory holds a fork of the official WebAssembly 1.0 core test
suite from [WebAssembly/spec](https://github.com/WebAssembly/spec)
(`test/core/`). It was imported unchanged in commit `cd3af90`, so
`git diff cd3af90 -- tests/` shows every change made since then.

`make test` converts each `.wast` file to JSON with `wast2json` and runs it
with `run_tests` (see `../run_tests.c`).

## Why a fork

The upstream suite is written for a full WebAssembly engine with a text
parser and a linker. wasmc and `run_tests` are narrower:

- **wasmc reads only the binary format.** Upstream checks text-parser
  errors with `(module quote "...")`, and wasmc cannot load those modules.
- **`run_tests` runs one module at a time.** It has no `spectest` host
  module (`print_i32`, `global_i32`, `memory`, `table`, ...). It also has
  no `register`, no `assert_unlinkable`, and no actions on named
  modules other than the current one.
- **Imports are not detected.** WAMR instantiates a module even when its
  imported functions are not linked, and calling one of them jumps to
  address 0 and crashes.

The fork keeps the upstream files and changes only what it has to, so
the suite runs cleanly: no skips, and every remaining command is a real
check. `run_tests` has no skip logic. A command it cannot run is
reported as `[ERROR]`, so an unsupported test that comes back shows up
at once.

The changes never edit a test's meaning silently. Every changed file
starts with a `;; wasmc fork:` note that explains its changes. Every
removed command is still in the file, commented out with `;;`, so the
upstream text is preserved and can be diffed.

## Changes

### 1. Text modules turned into binary (25 commands)

Some `assert_malformed (module quote ...)` cases test an error that the
binary format can also express. These were rewritten as
`(module binary ...)` with the same expected message. The original text
is kept above each one as a `;; Text version (wasmc reads binary only):`
comment.

| File           | Cases | Binary equivalent                                         |
|----------------|------:|-----------------------------------------------------------|
| `address.wast` |     1 | memarg `offset=2^32` becomes an oversized LEB ("integer too large") |
| `const.wast`   |     8 | an out-of-range `i32/i64.const` becomes an oversized LEB             |
| `imports.wast` |    16 | an import after a func, table, memory or global becomes an import section placed after that section (section order) |

### 2. Text-only modules commented out (229 commands)

The remaining `module quote` cases test syntax that exists only in the
text format, so they have no binary form. Examples are float and int
literal spelling, instruction mnemonics, `align=` values that are not a
power of two, block labels, and the order of `(param)`, `(result)` and
`(type)` clauses. These were commented out in:

`align`, `block`, `call_indirect`, `const`, `float_literals`, `func`,
`if`, `int_literals`, `load`, `loop`, `store`, `type`.

`token.wast` was deleted. It contained only two text-tokenizer cases.

### 3. `utf8-invalid-encoding.wast` rewritten in binary (176 commands)

Upstream, all 176 cases are text modules,
`(module quote "(func (export \"...\"))")`, so the whole file tests the
text parser. It was replaced in place with a generated binary version.
Each case now exports a function under the same invalid UTF-8 byte
sequence, which tests wasmc's export-name decoder instead. The cases are
in the same order, and each has its original name as a comment. All 176
pass.

### 4. `spectest` imports commented out (118 commands)

Modules that import from the `spectest` host module were commented out,
together with every command that runs against them. The files affected
are `binary-leb128`, `data`, `elem`, `func_ptrs`, `globals`, `imports`,
`names` and `start`. `assert_invalid` and `assert_malformed` on such
modules were kept, because validation and decoding need no linking.

### 5. Multi-module commands commented out (83 commands)

The following were commented out in `data`, `elem`, `exports` and
`imports`:

- modules that import from other test modules
- `register`
- `assert_unlinkable`
- actions on a named module (`(invoke $M "f")`, `(get $M "g")`)
- the commands that depend on them

### 6. `linking.wast` deleted

`linking.wast` is entirely about cross-module linking (`register`,
shared tables and memories, `assert_unlinkable`), so nothing in it can
run here.

## Result

The last line of `make test` (71 upstream files plus wasmc's own four,
see below):

```
[TOTAL]           75 files, 0 not fully passed    20897 checks passed
```

## Updating from upstream

Copy the new upstream files over these, then reapply the changes above.
Use `git diff cd3af90 -- tests/` as the reference. After that,
`make test` should report 0 files not fully passed. Any `[ERROR]` line
points to a newly added command that needs one of the treatments above.

## wasmc's own tests

`wasmc_i64.wast` is not from upstream. It covers i64 code that the
upstream tests reach only with parameters: shifts and rotations by
constant amounts, add/sub/and/or/xor with constants, i64 values whose high
word wasmc knows to be zero, and random code with many live values and
loops (fixed seeds), which spills and reuses spill slots.
`spectest-interp` (wabt) checks the expected values independently of wasmc.

`ssa_regions.wast` is not from upstream either. It has functions with many
written locals and many `block`/`if` constructs, which exercise the SSA
builder's regions (`compile.c`, `setup_regions()`); wabt's `wasm-interp`
gave the expected values.

`wasmc_types.wast` is not from upstream either. It checks wasmc's limit of
65536 function types (type indices are 16-bit in wasmc and in WAMR's AOT
format) with a module of 65537 types that must be rejected. Before the
limit, that module compiled, and its `call_indirect` called a function of
the wrong type instead of trapping. The comment at the top of the file
explains how the module is built.

`wasmc_args.wast` is not from upstream either. It calls functions with more
arguments than the RV32 argument registers: a0 holds exec_env, so only a1-a7
are left and the rest go on the stack (`emit.c`, `abi_place()`). It covers
12 i32 arguments (direct, computed at run time, and through
`call_indirect`), an i64 split between a7 and the stack, i64s in 8-byte
aligned stack slots with a hole before them, soft-float f32/f64 arguments,
16 i64 arguments, a function that passes its stack parameters on, recursion
with stack arguments, and exported functions that the host calls with stack
arguments. Only those host calls check wasmc against the real psABI: wasm
callers and callees both use `abi_place()`, so they would agree even on a
wrong layout. wabt's `spectest-interp` gave the expected values.
