# wasmc

`wasmc` is a lightweight ahead-of-time (AOT) compiler that runs directly **on the device**.
It translates [WebAssembly 1.0][WASM-1.0] (WASM) modules into native 32-bit RISC-V code (RV32IM).
It is written for microcontrollers like the ESP32-C3 (400 KB of RAM): a
typical module compiles in a few tens of milliseconds and a few KB of heap.

```
+-------------------------- on the device (e.g. ESP32-C3) --------------------------+
|                                                                                   |
| +--------------+                 +-------------+                  +-------------+ |
| |              | wasmc_compile() |  AOT file   | WAMR AOT runtime | native      | |
| | .wasm module |---------------->|   in RAM    |----------------->| RV32IM code | |
| |              |                 |             |                  | execution   | |
| +--------------+                 +-------------+                  +-------------+ |
|                                                                                   |
+-----------------------------------------------------------------------------------+
```

wasmc emits executables in the AOT file format of the
[WebAssembly Micro Runtime][WAMR] (WAMR) (version 2.4.4), so the files wasmc
makes can be loaded and run by WAMR's AOT runtime as they are, just like the
files of `wamrc`, WAMR's own LLVM-based AOT compiler, which runs only a PC.

The repository also contains:

- `readaot`: an inspection tool for WAMR AOT files, like `readelf` for ELF.
- `docs/aotfmt/`: a description of WAMR's AOT file format, which has no
  official documentation, reconstructed from the WAMR sources.

## Benchmarks

On the ESP32-C3, over 47 modules from the [Embench IoT][embench-iot] and [Sightglass][sightglass] suites,
code compiled by wasmc on the board runs about **10× faster** than WAMR's fast
interpreter and about **18% slower** than code compiled on a PC by `wamrc`.
Its AOT files are about 3% smaller than wamrc's.

Full results, with charts and methodology:
[bench/esp32c3-idf/results/summary.md](bench/esp32c3-idf/results/summary.md).
How to run the benchmarks yourself:
[bench/esp32c3-idf/README.md](bench/esp32c3-idf/README.md).

## WebAssembly support

wasmc compiles the whole of WebAssembly 1.0 (the 2019 W3C recommendation, also
known as the MVP), in its binary format. It passes the official 1.0 core test
suite, adapted to what a single-module AOT compiler can run (see
[tests/README.md](tests/README.md)).

**Not supported:**

- **Features after 1.0.** These include sign-extension operators,
  non-trapping float-to-int conversions, multi-value, bulk memory, reference
  types and SIMD. Recent clang versions enable some of them by default, so
  build with `-mcpu=mvp`, and link without `-O`, which would run `wasm-opt`.
- **The text format** (`.wat`). Convert it with `wat2wasm` first.
- **No cross-module linking.** wasmc compiles one WebAssembly module at a time.

## Getting the sources

WAMR and the benchmark suites are git submodules:

```sh
git clone --recurse-submodules https://github.com/llllluca/wasmc
# or, in an existing clone:
git submodule update --init
```

## On a Linux machine

### Build

You need GCC and make:

```sh
make            # builds build/wasmc and build/readaot
make help       # all the targets (tests, sanitizers, coverage, static analysis)
```

### Compile a WebAssembly module

```sh
build/wasmc -o fib.aot fib.wasm
build/readaot fib.aot        # show and validate the AOT file
```

Without `-o` the output is `a.aot`, `build/wasmc --help` lists the options.
wasmc accepts WebAssembly 1.0 only, so build modules with clang's
`-mcpu=mvp`. Two small examples, with the build commands and the details in
their comments:

- [examples/fib.c](examples/fib.c) uses no libc:

  ```sh
  clang --target=wasm32 -mcpu=mvp -O2 -nostdlib -Wl,--no-entry \
        -o fib.wasm examples/fib.c
  ```

- [examples/hello.c](examples/hello.c) calls `printf`, which WAMR provides
  at run time:

  ```sh
  clang --target=wasm32 -mcpu=mvp -O2 -nostdlib -Wl,--no-entry \
        -Wl,--allow-undefined -o hello.wasm examples/hello.c
  ```

### Run an AOT file

The AOT files contain 32-bit RISC-V code, so on an x86-64 machine they run in a WAMR
built for riscv32 Linux, inside QEMU's user-mode emulator. You need:

- **A riscv32 Linux GCC for the soft-float ABI (`-mabi=ilp32`)**, the ABI of
  wasmc's code. The prebuilt riscv32 toolchains are usually built for
  `ilp32d` and will not work, so build one from
  [riscv-gnu-toolchain][riscv-toolchain]:

  ```sh
  git clone https://github.com/riscv-collab/riscv-gnu-toolchain
  cd riscv-gnu-toolchain
  ./configure --prefix=$HOME/opt/riscv32-ilp32 --with-arch=rv32imac --with-abi=ilp32
  make linux      # takes a while
  ```

- **QEMU user mode**, cmake, and wabt (for the tests). On Debian or Ubuntu:

  ```sh
  sudo apt install qemu-user cmake wabt
  ```

Tell the Makefile where the toolchain is, once, in a `config.mk` file next to
it (not in the repository). `make iwasm` and `make test` both read it:

```sh
echo 'RV32_CC := $(HOME)/opt/riscv32-ilp32/bin/riscv32-unknown-linux-gnu-gcc' > config.mk
```

Then build WAMR's `iwasm` from the submodule with `make iwasm`, and run the
file:

```sh
make iwasm
qemu-riscv32 build/iwasm-rv32/iwasm -f fib fib.aot 20     # prints 0x1a6d:i32
qemu-riscv32 build/iwasm-rv32/iwasm hello.aot             # prints Hello World!
```

### Run the tests

`make test` runs the WebAssembly 1.0 core test suite (`tests/`, see
[tests/README.md](tests/README.md)): each module is compiled by wasmc and run
by WAMR's AOT runtime, all on riscv32 under QEMU. It needs the toolchain, the
packages and the `config.mk` above:

```sh
make test
```

The last line should report every file passed:

```
[TOTAL]           74 files, 0 not fully passed    20876 checks passed
```

`make test-asan` compiles every test and benchmark module with a wasmc built
with AddressSanitizer and UBSan, on the Linux machine, without QEMU.

## On an ESP32-C3

The example in [examples/esp32c3-idf/](examples/esp32c3-idf/) is an ESP-IDF
app that compiles four small WebAssembly programs on the board with wasmc,
runs them with the WAMR AOT runtime and checks their results. You need
[ESP-IDF][esp-idf] 5.5 (tested with v5.5.2) and the `wasm-micro-runtime`
submodule:

```sh
git submodule update --init wasm-micro-runtime
. $IDF_PATH/export.sh
cd examples/esp32c3-idf
idf.py set-target esp32c3
idf.py -p /dev/ttyUSB0 build flash monitor     # on a board
idf.py qemu monitor                            # or without one, in QEMU
```

The output looks like this:

```
fib.wasm: 100 bytes of WASM -> 564 bytes of AOT in 587 us (wasmc memory: 1404 bytes)
  fib(24)                      = 46368    ok        23649 us
  fib(24), native C            = 46368    ok        11738 us
...
```

Its README also explains how to use wasmc in your own ESP-IDF project.

Two more projects run on the ESP32-C3:

- [examples/esp32c3-ed/](examples/esp32c3-ed/): the `ed` text editor of
  Unix V7, compiled to WebAssembly, then compiled by wasmc at every boot and
  used from a serial terminal.
- [bench/esp32c3-idf/](bench/esp32c3-idf/): the benchmarks above.

## Use of AI

wasmc was developed with the help of AI. I designed the project and made the
technical decisions, AI wrote much of the code, tests and documentation
under my direction. Every change was reviewed and checked against
the test suite.

## History

wasmc started as my [master's thesis project][thesis]. The tag `ccnc2027` is
the code used for the thesis. Up to the `ccnc2027` tag, all the code was written
by hand.

## License

wasmc is free software, released under the GNU General Public License,
version 3 or later: see [LICENSE](LICENSE). Third-party code keeps its own
license: `jsmn.h` (MIT), `examples/esp32c3-ed/wasm/ed.c` (Caldera's license),
and the submodules.

[WAMR]: https://github.com/bytecodealliance/wasm-micro-runtime
[riscv-toolchain]: https://github.com/riscv-collab/riscv-gnu-toolchain
[esp-idf]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/get-started/
[embench-iot]: https://github.com/embench/embench-iot
[sightglass]: https://github.com/wasm-micro-runtime/sightglass
[WASM-1.0]: https://www.w3.org/TR/wasm-core-1
[thesis]: https://amslaurea.unibo.it/id/eprint/39078/
