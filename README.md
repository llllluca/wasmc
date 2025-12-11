# wasmc

## Build from source and run on your own machine

The project was developed and tested using a GNU/Linux Debian 13 stable x86-64 machine.

The following Debian 13 stable package are required to build `wasmc` and then run a WASM module compiled with `wasmc` on your own (most probably x86-64) machine:
```bash
sudo apt install git cmake wabt qemu-user
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
wat2wasm -o test/fib2.wasm test/fib2.wat
./build/wasmc test/fib2.wasm > test/fib2.aot
```

`wasmc` outputs a file in the AOT format.
The AOT (Ahead Of Time) format is not a stardard file format for executables, it is used and developed for the [WAMR][WAMR] project.
The AOT format is designed to facilitate compilation from a WASM module to machine code.
To run an AOT file, one need to install the WAMR runtime.
`wasmc` outputs an AOT file for the riscv32 machine architecture, the only way to execute an AOT file for such architure on a x86-64 machine is to cross compile the WAMR runtime for riscv32 and execute the AOT file with the cross compiled WAMR runtime inside QEMU.

In order to cross compile the WAMR runtime, install the riscv32 toolchain binaries (it may take a while):
```bash
wget https://github.com/riscv-collab/riscv-gnu-toolchain/releases/download/2025.11.27/riscv32-glibc-ubuntu-24.04-gcc.tar.xz
sudo tar xf riscv32-glibc-ubuntu-24.04-gcc.tar.xz -C /opt/
sudo chown root:root -R /opt/riscv/
```
Otherwise compile the riscv32 toolchain from source following the instructions at [link][riscv-toolchain] (it takes long).
Then add `/opt/riscv/bin` to the `$PATH` environment variable.

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
./iwasm-2.4.4 -f main test/fib2.aot 10
```
This example calculate the 10-th Fibonacci number using the slow recursive algorithm.

## Build from source and run on the esp32c3 microcontroller
```bash
```

[riscv-toolchain]: https://github.com/riscv/riscv-gnu-toolchain
[WAMR]: https://github.com/bytecodealliance/wasm-micro-runtime/tree/main
