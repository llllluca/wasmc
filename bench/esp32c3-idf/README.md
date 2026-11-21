# wasmc benchmark on the ESP32-C3 (ESP-IDF)

One module per firmware, one mode per build. `run.sh` builds, flashes and
reads every `.wasm` of a directory in turn.

| Mode | Measures |
|---|---|
| `wasmc` | `wasmc_compile()` on the device: time, heap peak, wasmc work peak; then the WAMR AOT runtime on the AOT file it produced, with the same measures as `aot` (in `aot`) |
| `aot` | WAMR AOT runtime on wamrc's file (compiled on the host at build time): load, instantiate, run time, heap peak |
| `interp` | WAMR interpreter (fast) on the `.wasm`: load, instantiate, run time, heap peak |

It needs ESP-IDF 5.5 and WAMR 2.4.4, the submodule `wasm-micro-runtime/`
at the root of the repository (`git submodule update --init
wasm-micro-runtime`; or a WAMR elsewhere, with `WAMR_PATH` pointing to it).

```sh
. $IDF_PATH/export.sh
cd bench/esp32c3-idf
scripts/run.sh wasmc  /dev/ttyACM0 suites/modules/sightglass
scripts/run.sh aot    /dev/ttyACM0 suites/modules/sightglass
scripts/run.sh interp /dev/ttyACM0 suites/modules/sightglass
```

For each module `run.sh` builds the firmware with it (`build-<mode>/`),
flashes it, and `scripts/read_serial.py` resets the board and keeps what it prints
between `BENCH_BEGIN` and `BENCH_END`. `REPEAT=N scripts/run.sh ...` sets the
measures per module (default 3; rep 0 is cold), and `scripts/run.sh -h` shows the
help.

To run every mode on every suite of `suites/modules/` in one go:

```sh
scripts/runall.sh /dev/ttyACM0
MODES="wasmc aot" SUITES=sightglass scripts/runall.sh /dev/ttyACM0   # a subset
```

It calls `run.sh` once per mode and suite (one `results/<mode>-<date>/` each),
goes on when one fails, and lists the failed ones at the end.

On the terminal, one line per module: its name when it starts, then `ok`,
or `FAILED:` and the first reason (a failed build or flash, no output within
120 s, or the first measure whose `status` isn't `ok`):

```
[1/28] ackermann  ok
[2/28] base64  ok
[12/28] minicsv  FAILED: no output within 120 s
-> results/interp-20261120-123000
```

The build and the flash are separate steps. A flash that fails with an
esptool error (`A fatal error occurred`, such as a byte lost on the serial
line) is tried again, up to 3 attempts in all, each shown on the module's
line as `(flash attempt 2/3)`; every attempt's output is in the module's
`.log`.

The figures go to `results/<mode>-<date>/`, two files per module:

| File | Holds |
|---|---|
| `<module>.out` | what the board printed: one JSON object per line (see [The results](#the-results)), which `summary.py` reads |
| `<module>.log` | the output of `idf.py build flash`, to see why a build or flash failed |

To compare the three modes, `scripts/summary.py` writes a short summary to
share: `results/summary.md` (Markdown) and three charts next to it,
`summary-ratios.png`, `summary-interp.png` and `summary-compile.png`.

```sh
scripts/summary.py                     # newest results/<mode>-* of each mode and suite
scripts/summary.py results/wasmc-DATE1 results/wasmc-DATE2 results/aot-DATE ...
```

It explains the three modes, then gives the headline figures (with a small
table of the compile's time and heap over the modules) and three charts:
wasmc / wamrc and wasmc / interpreter for every module (below 1× wasmc is
better), and the compile's time and heap per module. The modules that
failed, if any, are listed at the end, and the results it used in Sources;
the figures of each module are in those results' `.out` files.
It takes several results per mode, one per suite: the suite is in each
`<module>.out` (older results: the directory of `suites/modules/` with the
module). The text comes from the template `scripts/summary.md.j2`.

It runs with the system Python, `/usr/bin/python3`, and needs Jinja2 and
matplotlib (`apt install python3-jinja2 python3-matplotlib`; without
matplotlib the file has no charts). Run it as `scripts/summary.py`, not
`python3 scripts/summary.py`: in a shell with the ESP-IDF environment,
`python3` is ESP-IDF's, which has neither.

GitHub and GitLab show `summary.md` as it is, with its charts. To send it,
make a PDF with pandoc (through LaTeX), in `results/` where the charts are:

```sh
cd results
pandoc -d ../scripts/summary-pdf.yaml summary.md -o summary.pdf
```

`scripts/summary-pdf.yaml` holds the PDF's options: 12pt text, the `#`
title of the Markdown as the PDF's title, and 2 cm margins for the page of
the compile chart only (the pandoc filter `scripts/summary.lua`), the
others with LaTeX's default ones. `summary.py` prints this command when it
is done.

By hand, one module:

```sh
idf.py -B build-aot -DBENCH_MODE=aot -DBENCH_MODULE=$PWD/suites/modules/sightglass/fib2.wasm build flash monitor
```

## Benchmark suites

`suites/build.sh` builds standard suites into modules for `run.sh`, one
directory per suite in `suites/modules/`:

```sh
suites/build.sh                       # all suites
scripts/run.sh wasmc /dev/ttyACM0 suites/modules/sightglass
scripts/run.sh wasmc /dev/ttyACM0 suites/modules/embench
```

| Suite | Modules | Source |
|---|---|---|
| `sightglass` | 28 Sightglass shootout kernels (base64, ed25519, keccak, heapsort, …) | `suites/sightglass/` |
| `embench` | the 19 Embench-IoT benchmarks (crc32, nettle-aes, picojpeg, wikisort, …) | `suites/embench/` |

`build.sh` only calls each suite's own scripts, which are short and can be
run by themselves:

| Script | Does |
|---|---|
| `suites/sightglass/patch.sh` | copies the kernels to `suites/build/sightglass/` and applies `suites/sightglass/patches/` there |
| `suites/sightglass/compile.sh` | compiles the patched kernels to `suites/modules/sightglass/` |
| `suites/embench/compile.sh` | compiles Embench to `suites/modules/embench/`, straight from its sources (no patches) |

Each suite's source is a git submodule, `suites/<suite>/upstream/`, at a
pinned commit. The scripts check it out if needed (`git submodule update
--init`) and stop if it's modified, because its sources are never changed.
Sightglass's patches, one per kernel, size it for the C3. To see them all:
`cat suites/sightglass/patches/*.patch`. Each suite's directory also holds
the entry that adapts it (`entry.c`); `suites/libc/` is a small
freestanding libc both use. The scripts need clang with the wasm32 target
and `wasm-ld` (Debian's clang 19 works; no WASI sysroot).

Every module is WebAssembly 1.0, imports nothing, has no `memory.grow`
(`malloc` takes a static arena, so the memory can be shrunk), and exports
`__main_void`, whose result is the same in every mode:

- **sightglass**: a hash of what the kernel passes to `BLACK_BOX()`. The
  kernels are sized for the C3: their own `ESP32` size (`-DESP32`), or a
  `#define` changed by `suites/sightglass/patches/<kernel>.patch`; `switch` is left out, as its frame doesn't fit the
  16 KB WAMR stack in the interpreter.
- **embench**: `benchmark()`'s result once `verify_benchmark()` accepts it
  (`0xbad00000 ^ result` if not), with `GLOBAL_SCALE_FACTOR=1`.

The modules are linked without `-O`: with it, clang runs `wasm-opt` when
it's in PATH, which adds sign-extension ops (not WebAssembly 1.0).

## The results

Each `<module>.out` has one JSON object per line (other lines, such as WAMR
warnings or a crash, are kept but not read by `summary.py`):

1. from `run.sh`: `{"module": ..., "suite": ..., "wasm_B": ...}`, the module,
   its suite (the name of the directory `run.sh` was given) and its size;
2. from the firmware, its info line: `mode`, `module`, `opt`, `embedded_B`
   (what the firmware embeds: the `.wasm`, or the AOT file in `aot` mode),
   `free_heap_B` (free heap before the first measure). `opt` is the
   compiler optimization of the firmware and WAMR (`-O2`, `-Os`, `-Og`,
   `-O0`; from `sdkconfig`), which changes the interpreter's speed;
3. one line per measure, with only the fields its mode measures, or
   `{"error": ...}` from `run.sh` if the module didn't run:

| Field | |
|---|---|
| `code_B` | wasmc: AOT output size; aot/interp: size of what WAMR loads |
| `compile_us` | wasmc only |
| `load_us`, `inst_us`, `run_us` | aot/interp only; `inst_us` includes the exec env |
| `heap_peak_B` | most heap in use at once during the measure, above the heap before it. wasmc: work + aux memory + output. aot/interp: input copy in RAM + module + instance (linear memory, 16 KB WAMR stack); for AOT the input copy is held for the whole run (loaded as not freeable: WAMR 2.4.4 mishandles freeable AOT files, see `main.c`), the interpreter frees it after the load |
| `wasmc_work_B` | wasmc's own `work_peak` |
| `stack_B` | native stack used (AOT code and wasmc run on it) |
| `result` | return value of the entry, hex |
| `status` | `ok`, or what failed |
| `aot` | wasmc only: the run of the AOT file wasmc just compiled, an object with the fields of an `aot` run (`rep`, `code_B`, `load_us`, `inst_us`, `run_us`, `heap_peak_B`, `stack_B`, `result`, `status`). Missing if the compile failed. Its `heap_peak_B` counts the AOT file as the input copy, as in `aot` mode |

In wasmc mode a run looks like this:

```json
{ "rep": 0, "code_B": 1864, "compile_us": 10802, "heap_peak_B": 5140, "wasmc_work_B": 3112,
  "stack_B": 2008, "status": "ok",
  "aot": { "rep": 0, "code_B": 1864, "load_us": 1688, "inst_us": 974, "run_us": 62330,
           "heap_peak_B": 161212, "stack_B": 2028, "result": "0x000222a0", "status": "ok" } }
```

A whole `heapsort-Os.out` in aot mode, with one measure:

```
{"module":"heapsort-Os","wasm_B":1052}
{"mode":"aot","module":"heapsort-Os","opt":"-O2","embedded_B":1904,"free_heap_B":319200}
{"rep":0,"code_B":1904,"load_us":7797,"inst_us":2153,"run_us":7865,"heap_peak_B":153280,"stack_B":2092,"result":"0x000222a0","status":"ok"}
```

`summary.py` also reads the JSON files of the former `run.py`
(`results/<mode>-<date>.json`).
