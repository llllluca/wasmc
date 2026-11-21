# ed on the ESP32-C3

An experiment to test wasmc on real C code: `ed` from Research Unix Seventh
Edition (1979), compiled to one WebAssembly 1.0 module. At every boot the
ESP32-C3 compiles that module with wasmc and runs it with the WAMR AOT runtime,
and you use it from a terminal over the serial line:

```
ed_wasm.h (flash) --wasmc_compile()--> AOT file in RAM --WAMR--> native RV32IM code
                                                                   |         |
                                                  terminal on UART0 --     -- files on LittleFS
```

**State:** the tests pass in QEMU and on the PC. It also boots on a real ESP32-C3,
but `test/test_device.py` has not been run on one yet.

## Build and run

You need ESP-IDF 5.5 (tested with v5.5.2) and WAMR, cloned at the root of this
repository (see `../esp32c3-idf/README.md`). The first build downloads the
`joltwallet/littlefs` component.

```sh
. $IDF_PATH/export.sh
cd examples/esp32c3-ed
idf.py set-target esp32c3
idf.py -p /dev/ttyUSB0 flash
picocom -b 115200 /dev/ttyUSB0      # or any terminal; not idf.py monitor
```

Or without a board:

```sh
idf.py build
idf.py qemu                         # the terminal is QEMU's stdio
python test/test_device.py --qemu   # the tests, also: --port /dev/ttyUSB0
```

The terminal is UART0 at 115200 baud, which is the USB connector on boards with
a USB-to-UART bridge. Boards that only have the C3's own USB (USB-Serial-JTAG)
are not supported.

```
V7 ed, compiled here by wasmc
wasmc: 13093 bytes of WASM -> 38032 bytes of AOT in 369 ms, memory 48752
free heap 140036, lowest so far 140036, free stack at worst ...
files: notes.txt
ed ready
```

ed prints no prompt. A session looks like this, where `?` is its error message:

```
a
hello
.
s/hello/ciao/p
ciao
w notes.txt
5
q
```

`q` starts a new, empty ed, and lists the files. Text you have not written is
lost on reset; files stay across resets.

**The terminal.** Backspace erases a character, Ctrl-U the line, Ctrl-D at the
start of a line is end of file. ed is 7-bit: the bell rings on anything else, and
past 509 characters on a line.

**Limits.** About 13000 lines (`?MEM?`, memory.grow cannot find 128 KB on this
heap) and about 127 KB of temp file (`?TMP`), which holds all the text you have
typed or read, also what you deleted. Both are errors: the text is kept, and
writing it to a file and reloading it with `e` makes room in the temp file. There
is no way to delete a file. The temp file is rewritten on flash as you work, so
do not expect the flash to last for years of heavy use.

## On the PC

```sh
cd examples/esp32c3-ed/host
make test                                         # everything, on both engines
build/run_interp build/ed.wasm file               # WAMR interpreter, on the PC
qemu-riscv32 build/run_aot build/ed.wasm file     # wasmc + WAMR AOT
```

`make test` needs what the root Makefile needs for `make test`, and clang with
the wasm32 target. It also rewrites `main/ed_wasm.h`, like `wasm/build.sh`.

`run.c` builds for two engines. `run_aot` (riscv32, under `qemu-riscv32`)
compiles with `wasmc_compile()` and runs with the WAMR AOT runtime, as the board
does. `run_interp` (the PC) runs the same module with the WAMR interpreter: if the
two disagree, the problem is in wasmc.

## Files

| File | What it is |
|---|---|
| `wasm/ed.c` | V7's `ed.c`, with the changes below |
| `wasm/port.h`, `wasm/port.c` | the little of V7's libc that it uses, over host functions |
| `wasm/build.sh` | builds `ed.wasm` and `main/ed_wasm.h`, with the reasons for its flags |
| `main/main.c` | the firmware: terminal, files, compile, load, run |
| `main/ed_wasm.h` | the module, generated |
| `partitions.csv`, `sdkconfig.defaults` | 1 MB of app and 896 KB of LittleFS: fits a 2 MB flash |
| `test/test_device.py` | ed sessions over the serial line, checked byte by byte |
| `host/run.c`, `host/test.sh` | the module on the PC, and its tests |

## Memory

On the heap: 48752 bytes to compile (wasmc's work memory and
the AOT file), of which the 38 KB of the AOT file stay while the module is loaded,
because WAMR points into it. Then 34 KB of code, 64 KB of linear memory and WAMR's
own data, which leave 140 KB free.

## Changes to ed.c

They are marked `wasmc:`. The source is `usr/src/cmd/ed.c` of the `Research-V7`
branch of <https://github.com/dspinellis/unix-history-repo>.

- **`setjmp`/`longjmp`.** WebAssembly 1.0 has neither. `error()` calls the host,
  which raises an exception: every wasm function returns at once, down to the
  host, which clears the exception and calls `app_loop` again. Linear memory, and
  so all of ed's state, is intact; `app_loop` resets `__stack_pointer`.
- **`-x`, `x`, `!` are gone:** they need `fork` and `exec`.
- **`free(zero)` before `realloc(zero)` is gone:** only V7's malloc allowed it.
- **`filename()` checks its bounds.** A name over 64 bytes overran `file[]`
  and corrupted the editor's state until WAMR stopped it with a trap.
- **No signals:** `signal()` does nothing, so there is no interrupt with Ctrl-C.

What is left of V7 on purpose: no `,p` or `%` (write `1,$p`), and a temp file of
at most 255 blocks of 512 bytes (`?TMP`).

## Host functions

All in module `env`:

| Function | |
|---|---|
| `sys_read`, `sys_write`, `sys_open`, `sys_creat`, `sys_close`, `sys_lseek`, `sys_unlink` | as in V7; fd 0 and 1 are the terminal |
| `sys_getpid` | for the name of the temp file |
| `sys_arg(i, buf, n)` | the editor's argument `i`, or -1 |
| `sys_throw()` | raises the exception that stands for `longjmp` |
| `sys_exit(code)` | the same, and the run ends |

On the PC the kernel provides the line editing and the echo of the terminal; on
the board `main/main.c` does. There, names are relative to `/fs` and `/tmp/e00000`
is `/fs/tmp/e00000`.

## License

`wasm/ed.c`, and the module built from it (`main/ed_wasm.h` and the firmware), are
from Seventh Edition Unix, under the
[Caldera license of 2002](https://www.tuhs.org/Archive/Caldera-license.pdf):

```
Copyright(C) Caldera International Inc. 2001-2002. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code and documentation must retain the above
copyright notice, this list of conditions and the following disclaimer.
Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

All advertising materials mentioning features or use of this software
must display the following acknowledgement:

This product includes software developed or owned by Caldera
International, Inc.

Neither the name of Caldera International, Inc. nor the names of other
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

USE OF THE SOFTWARE PROVIDED FOR UNDER THIS LICENSE BY CALDERA
INTERNATIONAL, INC. AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL CALDERA INTERNATIONAL, INC. BE LIABLE FOR
ANY DIRECT, INDIRECT INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```
