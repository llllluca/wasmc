# wasmc on the ESP32-C3

An ESP-IDF app that compiles WebAssembly **on the device** with wasmc and
runs it with the **WAMR AOT runtime**:

```
.wasm (static const array) --wasmc_compile()--> AOT file in RAM --WAMR--> native RV32IM code
```

It runs four small programs and checks their results:

| Program | Shows |
|---|---|
| `fib.wasm` | calls, i32 arithmetic; compared with the same function in native C |
| `sieve.wasm` | loops, loads and stores in linear memory |
| `collatz.wasm` | 64-bit integer arithmetic |
| `mandel.wasm` | `double` (soft float), and a host function imported from the firmware (`env.putchar`) that draws the Mandelbrot set on the console |

```
fib.wasm: 100 bytes of WASM -> 564 bytes of AOT in 587 us (wasmc memory: 1404 bytes)
  fib(24)                      = 46368    ok        23649 us
  fib(24), native C            = 46368    ok        11738 us
...
```

## Build and run

You need ESP-IDF 5.5 (tested with v5.5.2) and WAMR 2.4.4, the submodule
`wasm-micro-runtime/` at the root of this repository (or a WAMR anywhere
else, with `WAMR_PATH` pointing to it):

```sh
git submodule update --init wasm-micro-runtime
. $IDF_PATH/export.sh
cd examples/esp32c3-idf
idf.py set-target esp32c3
idf.py build flash monitor      # on a board
idf.py qemu monitor             # or without one, in QEMU (timings are not real)
```

The modules are already in `main/wasm_modules.h`. To change them, edit
`wasm/*.c` and run `wasm/build.sh`. It needs clang with the wasm32 target,
`wasm-ld` and `xxd`.

## Files

| File | What it is |
|---|---|
| `CMakeLists.txt` | the project; it adds WAMR's ESP-IDF component (`$WAMR_PATH/build-scripts/esp-idf`) |
| `components/wasmc/CMakeLists.txt` | wasmc as a component, from the repository's `src/` and `include/` |
| `sdkconfig.defaults` | the options below |
| `main/main.c` | the demo: compile, load, call, check |
| `wasm/` | the C sources of the modules and `build.sh` |

## Doing the same in your project

1. **WAMR as a component.** In the project's `CMakeLists.txt`, before
   `project()`:
   `list(APPEND EXTRA_COMPONENT_DIRS "$ENV{WAMR_PATH}/build-scripts/esp-idf")`.
2. **wasmc as a component.** Copy `components/wasmc/CMakeLists.txt` and
   change `WASMC_ROOT` to where wasmc is. Your component then gets
   `PRIV_REQUIRES wasmc wamr pthread` and includes `wasmc/wasmc.h`.
3. **`sdkconfig.defaults`:**
   - `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n`: WAMR runs the AOT code from the
     heap, and memory protection forbids that.
   - `CONFIG_WAMR_ENABLE_AOT=y`, `CONFIG_WAMR_ENABLE_INTERP=n`: the AOT
     runtime only. Turn off the WAMR features you don't use
     (`LIBC_BUILTIN`, `LIBC_WASI`, `LIB_PTHREAD`).
4. **Run WAMR in a pthread**, not in `app_main`: its ESP-IDF port calls
   `pthread_self()`. Give the thread a stack of about 12 KB.
5. **Compile and load:**
   ```c
   static uint8_t ram[8 * 1024];       /* the work memory and the AOT file */
   wasmc_allocator mem;
   wasmc_static_arena arena;
   wasmc_result res;
   wasmc_static_allocator(&mem, &arena, ram, sizeof ram);
   if (wasmc_compile(wasm, wasm_len, &mem, NULL, &res) == WASMC_OK) {
       wasm_module_t m = wasm_runtime_load(res.out, res.out_size, err, sizeof err);
       ...
   }
   ```
   The AOT file stays at the start of `ram` until the next
   `wasmc_compile()` on it: unload the module from WAMR first.
   `arena.work_peak + res.out_size` is the memory a module needs.
6. **Build the modules for WebAssembly 1.0**, which is all wasmc compiles:
   `clang --target=wasm32 -mcpu=mvp -O2 -nostdlib -fno-builtin -Wl,--no-entry`.
   Export functions with `__attribute__((export_name("f")))`, and import host
   functions with `import_module`/`import_name`. Register those with
   `RuntimeInitArgs.native_symbols` (see `env_putchar` in `main/main.c`).
   Keep linear memory small (`-Wl,--initial-memory=65536`), since WAMR
   allocates all of it on instantiation.

Long calls block the idle task: keep each one under the 5 s of the task
watchdog, or feed or disable it.
