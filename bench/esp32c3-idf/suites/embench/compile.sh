#!/bin/sh

set -eu
cd "$(dirname "$0")"

printf "Starting compilation of embench...\n"

git submodule update --init upstream
git -C upstream diff --quiet || { echo "embench/upstream/ is modified"; exit 1; }

# The benchmarks: one directory of .c files each
SRC="upstream/src"
# Embench's support code: support.h and beebsc.c (its own malloc_beebs, rand_beebs, ...)
SUPPORT="upstream/support"
# Object files (.o), intermediate
OBJ="../build/obj-embench"
# The .wasm modules, one per benchmark, for run.sh
OUT_DIR="../modules/embench"

# Bytes of the static arena malloc() takes its memory from (../libc/libc.c):
# it lives in the module's linear memory, which never grows (no memory.grow).
# Small: the benchmarks allocate with Embench's own malloc_beebs
ARENA_SIZE=4096
# Bytes of the C stack (locals, frames) in the module's linear memory; not
# WAMR's own 16 KB stack, set in the firmware (main/main.c)
STACK_SIZE=16384

BENCHMARKS="
aha-mont64
crc32
depthconv
edn
huffbench
matmult-int
md5sum
nettle-aes
nettle-sha256
nsichneu
picojpeg
qrduino
sglib-combined
slre
statemate
tarfind
ud
wikisort
xgboost
"

# Compiler flags, used for compiling and linking:
#   --target=wasm32            32-bit WebAssembly (pointers are 4 bytes)
#   -mcpu=mvp                  WebAssembly 1.0 only: no sign-extension,
#                              bulk memory or other later features
#   -nostdlib                  link no system libc or startup files
#   -isystem ../libc/include   take <stdio.h>, <string.h>, ... from ../libc/
#   -I upstream/support        Embench's headers (support.h, beebsc.h)
#   -DGLOBAL_SCALE_FACTOR=1    one pass of each benchmark's loop (Embench sizes
#                              one pass for about 4 s on a 1 MHz core)
#   -DWARMUP_HEAT=0            no cache warm-up run before the measured one
# Note: Do not pass -Os/-O<n> to the link step; clang may invoke wasm-opt and
# emit post-MVP opcodes outside WebAssembly 1.0.
WASM_CFLAGS="
  --target=wasm32
  -mcpu=mvp
  -nostdlib
  -isystem ../libc/include
  -I $SUPPORT
  -DGLOBAL_SCALE_FACTOR=1
  -DWARMUP_HEAT=0
"

# Linker flags (-Wl, passes each one to wasm-ld):
#   --no-entry                no _start: the firmware calls __main_void,
#                             which entry.c exports
#   --strip-all               drop the name and debug sections: a smaller module
#   -z,stack-size=N           N bytes of C stack in the linear memory
#   --export=__heap_base      export where the stack and data end, so the
#   --export=__data_end       loader (wamrc, wasmc) can shrink the linear
#                             memory to what the module uses
WASM_LDFLAGS="
  -Wl,--no-entry
  -Wl,--strip-all
  -Wl,-z,stack-size=${STACK_SIZE}
  -Wl,--export=__heap_base
  -Wl,--export=__data_end
"

# WASM_CFLAGS and WASM_LDFLAGS on one line each, as the clang commands use them
CFLAGS=$(printf '%s ' $WASM_CFLAGS)
LDFLAGS=$(printf '%s ' $WASM_LDFLAGS)

build_libc() {
    clang $CFLAGS -Os -ffreestanding -DARENA_SIZE="$ARENA_SIZE" -c ../libc/libc.c -o "$OBJ/libc.o"
}

build_benchmark() {
    name="$1"
    objs="$OBJ/libc.o"

    printf '  Compiling %s\n' "${name}.wasm"

    for c in "$SRC/$name"/*.c "$SUPPORT/beebsc.c" entry.c; do
        o="$OBJ/$name-$(basename "$c" .c).o"
        clang $CFLAGS -Os -I "$SRC/$name" -c "$c" -o "$o"
        objs="$objs $o"
    done
    clang $CFLAGS $objs $LDFLAGS -o "$OUT_DIR/${name}.wasm"
}

main() {
    mkdir -p "$OBJ" "$OUT_DIR"
    build_libc

    for b in $BENCHMARKS; do
        build_benchmark "$b"
    done
}

main "$@"
