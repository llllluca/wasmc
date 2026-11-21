#!/bin/sh

set -eu
cd "$(dirname "$0")"

printf "Starting compilation of sightglass...\n"

git submodule update --init upstream
git -C upstream diff --quiet || { echo "sightglass/upstream/ is modified"; exit 1; }

./patch.sh

# The kernels (.c) with patches/ applied, made by ./patch.sh
SRC="../build/sightglass"
# Object files (.o), intermediate
OBJ="../build/obj-sightglass"
# The .wasm modules, one per kernel, for run.sh
OUT_DIR="../modules/sightglass"

# Bytes of the static arena malloc() takes its memory from (../libc/libc.c):
# it lives in the module's linear memory, which never grows (no memory.grow)
ARENA_SIZE=65536
# Bytes of the C stack (locals, frames) in the module's linear memory; not
# WAMR's own 16 KB stack, set in the firmware (main/main.c)
STACK_SIZE=16384

# Kernels built with -DESP32, which picks the size their source already has
# for the ESP32 (gimli, matrix, seqhash are also patched: still too big)
ESP32_KERNELS="
base64
fib2
gimli
heapsort
matrix
nestedloop
nestedloop2
random
seqhash
sieve
switch2
"

# Kernels whose size is set by their patch in patches/ ('switch' is
# excluded: its frame exceeds the 16 KB WAMR stack in the interpreter)
PATCHED_KERNELS="
ackermann
ctype
ed25519
keccak
matrix2
memmove
minicsv
nestedloop3
random2
ratelimit
strcat
strcat2
strchr
strlen
strtok
xblabla20
xchacha20
"

# Compiler flags, used for compiling and linking:
#   --target=wasm32                   32-bit WebAssembly (pointers are 4 bytes)
#   -mcpu=mvp                         WebAssembly 1.0 only: no sign-extension,
#                                     bulk memory or other later features
#   -nostdlib                         link no system libc or startup files
#   -isystem ../libc/include          take <stdio.h>, <string.h>, ... from ../libc/
#   -I upstream/include               Sightglass's headers (sightglass.h)
#   -I upstream/benchmarks/shootout   the kernels' own headers
# Note: Do not pass -Os/-O<n> to the link step; clang may invoke wasm-opt and
# emit post-MVP opcodes outside WebAssembly 1.0.
WASM_CFLAGS="
  --target=wasm32
  -mcpu=mvp
  -nostdlib
  -isystem ../libc/include
  -I upstream/include
  -I upstream/benchmarks/shootout
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

build_kernel() {
    name="$1"
    extra_flag="${2-}"
    bench_flags="-DBENCH=$name -Dblack_box=bench_black_box $extra_flag"

    printf '  Compiling %s\n' "${name}.wasm"

    clang $CFLAGS -Os $bench_flags -c "$SRC/$name.c" -o "$OBJ/$name.o"
    clang $CFLAGS -Os $bench_flags -c entry.c -o "$OBJ/${name}-entry.o"
    clang $CFLAGS "$OBJ/libc.o" "$OBJ/$name.o" "$OBJ/${name}-entry.o" $LDFLAGS -o "$OUT_DIR/${name}.wasm"
}

build_group() {
    extra_flag="${1-}"
    shift
    for k in "$@"; do
        build_kernel "$k" "$extra_flag"
    done
}

main() {
    mkdir -p "$OBJ" "$OUT_DIR"
    build_libc

    build_group "-DESP32" $ESP32_KERNELS
    build_group "" $PATCHED_KERNELS
}

main "$@"
