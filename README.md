# wasmc

`wasmc` is a lightweight Ahead-of-Time compiler that runs directly on resource-constrained devices, translating WebAssembly (WASM) modules into native 32-bit RISC-V machine code.
`wasmc` is developed and tested on the ESP32-C3 microcontroller, which has 400 KB of RAM.
`wasmc` emits executables in the AOT format natively used by the [WebAssembly Micro Runtime (WAMR)][WAMR] project, making its output directly compatible
with the WAMR runtime.
The AOT format is designed to facilitate compilation from a WASM module to machine code.However, it is not a standardized executable format and lacks official documentation.
The `aotfmt/` directory therefore provides documentation for the WAMR AOT executable format, reconstructed through reverse engineering of the WAMR source code.
This documentation is complemented by `readaot`, an inspection utility analogous to `readelf` for AOT binaries.

## Build from source and run on your own machine

Build from source:
```bash
mkdir build/
cd build/
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cd ..
```

Use `-DCMAKE_BUILD_TYPE=Debug` for a debug build (`-ggdb -O0`).

`wasmc` usage:
```bash
./build/wasmc -o fib2.aot tests/wasm/fib2-Os.wasm
```
If no output filename is specified with the `-o` flag, the default filename is `a.aot`.
To run an AOT executable, one need to install the WAMR runtime.
`wasmc` outputs an AOT file for the 32-bit RISC-V architecture, the only way to execute an AOT file for such architecture on a x86-64 machine is to cross compile the WAMR runtime for 32-bit RISC-V and execute the AOT file with the cross compiled WAMR runtime inside QEMU.


In order to cross compile the WAMR runtime, install the 32-bit RISC-V toolchain binaries (it may take a while):
```bash
wget https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2025.11.27/riscv32-glibc-ubuntu-24.04-gcc.tar.xz
sudo tar xf riscv32-glibc-ubuntu-24.04-gcc.tar.xz -C /opt/
sudo chown root:root -R /opt/riscv/
```
Otherwise compile the 32-bit RISC-V toolchain from source following the instructions at [link][riscv-toolchain] (it takes long).
 In both cases add `/opt/riscv/bin` to the `$PATH` environment variable.

Cross compile the WAMR runtime:
```bash
git clone https://github.com/bytecodealliance/wasm-micro-runtime.git
cd wasm-micro-runtime
git checkout WAMR-2.4.4
mkdir build_riscv32
cd build_riscv32
cmake -DWAMR_BUILD_TARGET="RISCV32" -DCMAKE_C_COMPILER="riscv32-unknown-linux-gnu-gcc" -DCMAKE_C_FLAGS="-static" ../product-mini/platforms/linux/
make
```
The linker produce some warnings that i don't know how to remove, so just ignore them.
Then place the `iwasm-2.4.4` executable in a path in the `$PATH` environment variable.

Install QEMU and its binfmt support. On Debian, you can install them with:
```bash
sudo install qemu-user qemu-user-binfmt
```

Execute the AOT file with the cross compiled WAMR runtime inside QEMU:
```bash
iwasm-2.4.4 -f fib2 fib2.aot 10
```
This example calculate the 10-th Fibonacci number using the slow recursive algorithm.

## Build from source and run on the esp32c3 microcontroller

```bash
cd esp32c3-idf/
./path/to/esp-idf/export.sh
idf.py build
idf.py -p <port> flash monitor
```


## How to run tests

Make sure you can build `wasmc` from source and run it on your own machine using the instruction of the previous section.
Build the project first, then run all tests with:
```bash
cd build/
ctest
```
The memory check tests takes long, you can skip them with:
```bash
cd build/
ctest -E memcheck
```

## How to create a new test
Create a new `.wat` o `.c` test source file and place it in `tests/wasm/src/`.

Compile the `.c` test source file using:
```bash
cd tests/wasm/
clang -O<level> --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-all -o <name>.wasm src/<name>.c
```
Compile the `.wat` test source file using:
```bash
cd tests/wasm/
wat2wasm src/<name>.wat
```

Add the new test entry in `tests/CMakeLists.txt`

## Useful commands

Generate riscv32 AOT format from WASM module using `warmc`:
```bash
wamrc --target=riscv32 --target-abi=ilp32 --cpu=generic-rv32 --cpu-features=+m --format=aot --stack-bounds-checks=0 --bounds-checks=0 -o fib2.aot path/to/file.wasm
```

Generate riscv32 object ELF format from WASM module using `warmc`:
```bash
wamrc --target=riscv32 --target-abi=ilp32 --cpu=generic-rv32 --cpu-features=+m --format=object --stack-bounds-checks=0 --bounds-checks=0 -o fib2.aot path/to/file.wasm
```

Disassemble text section of a riscv32 object file:
```bash
 riscv32-unknown-linux-gnu-objdump -d path/to/file.o
```

Display the relocation entries in a riscv32 object file:
```bash
riscv32-unknown-linux-gnu-objdump -r path/to/file.o
```

Disassemble a flat riscv32 binary file using objdump:
```bash
riscv32-unknown-linux-gnu-objdump -b binary -m riscv:rv32 -D path/to/file.bin
```

[riscv-toolchain]: https://github.com/riscv/riscv-gnu-toolchain
[WAMR]: https://github.com/bytecodealliance/wasm-micro-runtime/tree/main
