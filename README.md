# wasmc

## Build from source and run on your own machine

The project was developed and tested using a GNU/Linux Debian 13 stable x86-64 machine.

The following Debian 13 stable package are required to build `wasmc`.
```bash
sudo apt install git cmake
```
Also the following Debian 13 stable package are required to run a WASM module compiled with `wasmc` on your own (most probably x86-64) machine:
```bash
sudo apt install qemu-user
```
Build from source:
```bash
mkdir build/
cd build/
cmake ..
make
cd ..
```

`wasmc` usage:
```bash
./build/apps/wasmc -o fib2.aot tests/wasm/fib2-Os.wasm
```

`wasmc` outputs a file in the AOT format, the deafult file name is `a.aot`.
The AOT (Ahead Of Time) format is not a stardard file format for executables, it is used and developed for the [WAMR][WAMR] project.
The AOT format is designed to facilitate compilation from a WASM module to machine code.
To run an AOT file, one need to install the WAMR runtime.
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
cd product-mini/platforms/linux/
mkdir build/
cd build/
cmake -DWAMR_BUILD_TARGET="RISCV32" -DCMAKE_C_COMPILER="riscv32-unknown-linux-gnu-gcc" -DCMAKE_C_FLAGS="-static" ..
make
```
The linker produce some warnings that i don't know how to remove, so just ignore them.
Then place the `iwasm-2.4.4` executable in a path in the `$PATH` environment variable.

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

## Testing

### How to run tests
Make sure you can build `wasmc` from source and run it on your own machine using the instruction of the previous section.
In order to run the memory check tests, `valgrind` is required. On Debian 13 you can install `valgrind` with:
```bash
sudo apt install valgrind
```
Build the project first, the run all tests with:
```bash
cd build/
ctest
```
The memory check tests takes long, you can skip them with:
```bash
cd build/
ctest -E memcheck
```

### How to create a new test
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

On Debian 13 you can install `clang` or `wat2wasm` with:
```bash
sudo apt install wabt clang
```

Add the new test entry in `tests/CMakeLists.txt`

## Useful commands

Generate riscv32 AOT format from WASM module using `warmc`:
```bash
wamrc --target=riscv32 --format=aot --stack-bounds-checks=0 --bounds-checks=0 -o fib2.aot tests/wasm/fib2.wasm
```

Generate riscv32 object ELF format from WASM module using `warmc`:
```bash
wamrc --target=riscv32 --format=object --stack-bounds-checks=0 --bounds-checks=0 -o fib2.aot tests/wasm/fib2.wasm
```

Disassemble text section of a riscv32 object file:
```bash
 riscv32-unknown-linux-gnu-objdump -d fib2.o
```

Display the relocation entries in a riscv32 object file:
```bash
riscv32-unknown-linux-gnu-objdump -r fib2.o
```

Disassemble a flat riscv32 binary file using objdump:
```bash
riscv32-unknown-linux-gnu-objdump -b binary -m riscv:rv32 -D text.bin
```

### How to register the AOT format as a [miscellaneous binary format][binfmt_msc].
Create this wrapper script `/usr/local/bin/iwasm-2.4.4-binfmt` with execution permission.
```bash
#!/bin/sh

exec iwasm-2.4.4 -f main $1 0 0

```
Then register the AOT format:
```bash
su -
cp /path/to/iwasm-2.4.4 /usr/local/bin/
echo ":aot:M::\x00\x61\x6f\x74\x05\x00\x00\x00::/usr/local/bin/iwasm-2.4.4-binfmt:P" > /proc/sys/fs/binfmt_misc/register
exit
```

To unregister the AOT format use:
```bash
echo -1 > /proc/sys/fs/binfmt_misc/aot
```

[binfmt_msc]: https://en.wikipedia.org/wiki/Binfmt_misc
[riscv-toolchain]: https://github.com/riscv/riscv-gnu-toolchain
[WAMR]: https://github.com/bytecodealliance/wasm-micro-runtime/tree/main
