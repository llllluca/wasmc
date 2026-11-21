#!/usr/bin/python3
"""Turn the results of run.sh into a Markdown summary, grouped by suite.

  scripts/summary.py [results/wasmc-DATE results/aot-DATE ...] [-o summary.md]

A short, shareable summary: a Markdown file (a YAML header for pandoc) and
three PNG charts next to it. GitHub and
GitLab show it as it is; pandoc makes a PDF of it, to send:

  cd results
  pandoc -d ../scripts/summary-pdf.yaml summary.md -o summary.pdf

summary-pdf.yaml holds the PDF's options (12pt, the "#" title as the PDF's
title, 2 cm margins for the compile chart's page by the filter summary.lua);
summary.py prints this command.

It compares wasmc's AOT with wamrc's AOT and with the interpreter as
ratios (wasmc / the other: below 1× wasmc is better). The file has:
  - the three modes, and the headline figures, with a small table of how
    the compile's time and heap spread over the modules;
  - summary-ratios.png and summary-interp.png: per figure, one dot per
    module (wasmc / wamrc, wasmc / interpreter), and the geometric mean;
  - summary-compile.png: wasmc's compile time and heap peak, per module;
  - the modules that failed, with why (only if some did).
The per-module figures are in the results directories (see Sources).
The text comes from the Jinja2 template summary.md.j2. Runs with
/usr/bin/python3, which has Jinja2 (python3-jinja2) and matplotlib
(python3-matplotlib); without matplotlib it writes the file without charts.

Takes any number of results, several per mode: one per suite, since run.sh
runs one directory of modules, i.e. one suite, at a time. The suite of a
module is in its .out file; for older results, without it, it is the
directory of suites/modules/ that has the module's .wasm. Without
arguments, takes the newest results/<mode>-* of every mode and suite.
"""
import argparse
import glob
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time

import jinja2


try:
    import matplotlib
    matplotlib.use("Agg")
    from matplotlib import pyplot as plt
    from matplotlib.ticker import FixedLocator, NullLocator
except ImportError:
    plt = None

# --- Reading the results -----------------------------------------------------

# The project directory (this script is in scripts/), where results/ is
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODES = ("wasmc", "aot", "interp")          # run.sh's modes, as in results/<mode>-*
RUN_FIELDS = ("code_B", "load_us", "inst_us", "run_us", "heap_peak_B", "stack_B")
COMPILE_FIELDS = ("code_B", "compile_us", "heap_peak_B", "wasmc_work_B", "stack_B")
# The three series compared, by their key in a module's figures, and their
# names in the text; run.sh's mode "aot" is the series "wamrc"
SERIES = (("interp", "interpreter"), ("wamrc", "wamrc AOT"), ("wasmc", "wasmc AOT"))


def read_run(path):
    """A run.sh directory: in each <module>.out, run.sh's lines (module,
    suite, wasm_B, error) and the firmware's (its info line with mode and
    opt, then one line per measure)."""
    rep = {"mode": os.path.basename(path).split("-")[0],
           "date": os.path.basename(path).split("-", 1)[-1],
           "entry": "__main_void", "call": "call_wasm", "modules": []}
    for out in sorted(glob.glob(os.path.join(path, "*.out"))):
        m = {}
        with open(out) as f:
            for line in f:
                try:
                    o = json.loads(line)
                except ValueError:
                    continue              # WAMR warnings, crashes
                if "rep" in o:
                    m.setdefault("runs", []).append(o)
                elif "mode" in o:         # the firmware's info line
                    rep["opt"] = o.get("opt")
                    m["free_heap_B"] = o.get("free_heap_B")
                else:
                    m.update(o)
        rep["modules"].append(m)
    rep["repeat"] = max((len(m.get("runs", [])) for m in rep["modules"]), default=0)
    return rep


def read_result(path):
    """One result: a run.sh directory, or a JSON file of the former run.py."""
    if os.path.isdir(path):
        return read_run(path)
    with open(path) as f:
        return json.load(f)


def summarize(runs, fields):
    """Median and rep 0 of every field; status and result of the runs."""
    out = {"reps": len(runs)}
    for f in fields:
        vals = [r[f] for r in runs if isinstance(r.get(f), (int, float))]
        if vals:
            out[f] = statistics.median(vals)
            out[f + "_cold"] = runs[0].get(f)
    bad = [r.get("status") for r in runs if r.get("status") != "ok"]
    out["status"] = bad[0] if bad else "ok"
    results = {r.get("result") for r in runs if r.get("result")}
    out["result"] = results.pop() if len(results) == 1 else (
        "differs" if results else None)
    return out


def figures(rep):
    """The figures of a result's modules: {module: {"wasm_B", and by mode
    "interp", "wamrc", or "wasmc" and "compile"}}."""
    mode, out = rep.get("mode"), {}
    for m in rep.get("modules", []):
        d = out.setdefault(m["module"], {"wasm_B": m.get("wasm_B")})
        if "error" in m:
            d[{"aot": "wamrc"}.get(mode, mode)] = {"status": m["error"]}
            if mode == "wasmc":
                d["compile"] = {"status": m["error"]}
            continue
        runs = m.get("runs", [])
        if mode == "wasmc":
            d["compile"] = summarize(runs, COMPILE_FIELDS)
            aot = [r["aot"] for r in runs if "aot" in r]
            d["wasmc"] = (summarize(aot, RUN_FIELDS) if aot else
                          {"status": "not run (old wasmc file, or compile failed)"})
        else:
            d["wamrc" if mode == "aot" else "interp"] = summarize(runs, RUN_FIELDS)
    return out


def add_loadinst(mods):
    """Add load + instantiate (loadinst_us) to every series of mods."""
    for d in mods.values():
        for k, _ in SERIES:
            r = d.get(k)
            if has(r, "load_us") and has(r, "inst_us"):
                r["loadinst_us"] = r["load_us"] + r["inst_us"]


# --- Numbers as text ---------------------------------------------------------

def num(v, d=0):
    return f"{v:,.{d}f}"


def us(v):
    """Microseconds, in the unit that suits them: 71 µs, 35.4 ms, 1.20 s."""
    if v is None:
        return "–"
    if v >= 1e6:
        return num(v / 1e6, 2) + " s"
    if v >= 1e3:
        return num(v / 1e3, 1 if v >= 1e4 else 2) + " ms"
    return num(v) + " µs"


def nbytes(v):
    """Bytes: 390 B, 6.4 KB, 186 KB."""
    if v is None:
        return "–"
    if v >= 1024:
        return num(v / 1024, 0 if v >= 10240 else 1) + " KB"
    return num(v) + " B"


def has(o, f):
    """Whether o (a series' figures) has a number for f."""
    return isinstance(o, dict) and isinstance(o.get(f), (int, float))


def geomean(xs):
    return math.exp(sum(map(math.log, xs)) / len(xs)) if xs else None


def count(n):
    """n modules, in words: 1 module, 2 modules."""
    return f"{n} module" + ("" if n == 1 else "s")


TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "summary.md.j2")
PDF_DEFAULTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "summary-pdf.yaml")
SUITES_DIR = os.path.join(HERE, "suites", "modules")

# The figures compared as wasmc / wamrc: (key, title, what it is)
# "ttr_us" is the time to first result: wasmc's includes its compile.
# The third field explains the row under each chart (Markdown).
RATIOS = (
    ("run_us", "Run time",
     "the call of the module's entry function (`__main_void`) alone, once the "
     "module is loaded and instantiated"),
    ("ttr_us", "First result",
     "from the module in RAM to the entry's result: load + instantiate + run, "
     "plus, for wasmc, its compile on the device"),
    ("loadinst_us", "Load + inst.",
     "until the entry can be called: load + instantiate"),
    ("code_B", "Size",
     "the file the runtime loads into RAM: the AOT file each compiler makes"),
    ("heap_peak_B", "Heap peak",
     "the most heap in use at once while loading, instantiating and running: "
     "the module's copy in RAM, the loaded module, and the instance with its "
     "linear memory and its 16 KB WAMR stack"),
    ("stack_B", "Stack",
     "the native stack used by the thread that loads, instantiates and runs "
     "the module"),
)

# The suites' colors in the charts, in this order (categorical, never cycled
# past the last: more suites than colors share the last one)
SUITE_COLORS = ("#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#6250d6")
INK, INK_2, GRID = "#0b0b0b", "#52514e", "#e1e0d9"
# Text sizes in the charts, in points of a 6.5 inch wide figure: the width
# of the PDF's text
FONT, SMALL = 10, 8.5


# --- Loading -----------------------------------------------------------------

def suite_of(name):
    """The suite of a module without one in its results: the directory of
    suites/modules/ with its .wasm, if exactly one has it."""
    found = [os.path.basename(os.path.dirname(p))
             for p in glob.glob(os.path.join(SUITES_DIR, "*", name + ".wasm"))]
    return found[0] if len(found) == 1 else "other"


def read(path):
    """One result (run.sh directory or run.py JSON), every module with its suite."""
    rep = read_result(path)
    if rep.get("mode") not in MODES:
        sys.exit(f"{path}: unknown mode {rep.get('mode')!r}")
    for m in rep.get("modules", []):
        m.setdefault("suite", suite_of(m["module"]))
    rep["suites"] = sorted({m["suite"] for m in rep.get("modules", [])})
    return rep


def pick(paths, explicit):
    """The results to use: one per mode and suite. Passed explicitly, two
    for the same mode and suite are an error; found in results/, the
    newest wins."""
    chosen = {}
    for path in sorted(paths, key=os.path.getmtime):
        rep = read(path)
        for suite in rep["suites"]:
            key = (rep["mode"], suite)
            if explicit and key in chosen:
                sys.exit(f"{path}: a second {rep['mode']} result for suite {suite} "
                         f"(the other: {chosen[key][0]})")
            chosen[key] = (path, rep)
    return chosen


def load(chosen):
    """{suite: {module: {"wasm_B", "interp", "wamrc", "wasmc", "compile"}}}, sources."""
    suites, sources = {}, {}
    for (mode, suite), (path, rep) in sorted(chosen.items()):
        src = sources.setdefault(path, {
            "file": os.path.relpath(path), "mode": mode, "date": rep.get("date"),
            "opt": rep.get("opt"), "repeat": rep.get("repeat"), "call": rep.get("call"),
            "suites": [], "modules": 0})
        src["suites"].append(suite)
        # this suite's modules only (a module's name may be in two suites)
        part = dict(rep, modules=[m for m in rep.get("modules", []) if m["suite"] == suite])
        src["modules"] += len(part["modules"])
        for name, d in figures(part).items():
            dst = suites.setdefault(suite, {}).setdefault(name, {})
            dst.update({k: v for k, v in d.items() if k != "wasm_B" or v is not None})
    return suites, list(sources.values())


# --- The figures -------------------------------------------------------------

def add_ttr(d):
    """Time to first result of every series; wasmc's includes the compile."""
    for k, _ in SERIES:
        r = d.get(k)
        if has(r, "run_us"):
            t = sum(r.get(f) or 0 for f in ("load_us", "inst_us", "run_us"))
            if k == "wasmc":
                t += (d.get("compile") or {}).get("compile_us") or 0
            r["ttr_us"] = t


def ratio(d, field, ref="wamrc"):
    """wasmc / ref (wamrc or interp) of a field, or None."""
    a, b = d.get("wasmc"), d.get(ref)
    if has(a, field) and has(b, field) and b[field] > 0 and a[field] > 0:
        return a[field] / b[field]
    return None


def times(v):
    """A ratio as text: 1.23×, 12×."""
    if v is None:
        return "–"
    return num(v, 0 if v >= 10 else 1 if v >= 3 else 2) + "×"



def check(name, d):
    """Whether a module's entry returned the same value in every series
    that ran it: (ok/bad/none, why). bad: a series failed, or the values
    differ."""
    errors = [f"{n}: {d[k]['status']}" for k, n in SERIES
              if d.get(k) and d[k].get("status") not in (None, "ok")]
    if errors:
        return "bad", "; ".join(errors)
    results = [(n, (d.get(k) or {}).get("result")) for k, n in SERIES]
    got = [r for _, r in results if r]
    if not got:
        return "none", "no result"
    if all(r == got[0] for r in got) and "differs" not in got:
        return "ok", ""
    return "bad", "results differ: " + ", ".join(f"{n} {r or '–'}" for n, r in results)


# --- The tables --------------------------------------------------------------

def md(s):
    """Text as Markdown: the characters pandoc would read as markup escaped
    (an error message may hold <x>, *, _ ...)."""
    return "".join("\\" + c if c in "\\`*_[]<>|#$" else c for c in str(s))


def table(head, rows, compact=False, left=("",)):
    """A pipe table, as its lines: the first column left-aligned, the others
    right-aligned, without the columns empty ("–") in every row. The cells
    are padded so that the source reads as a table too; the dashes of the
    separator are as wide as each column, since pandoc sizes the columns of
    a wide table by them (three dashes each: names wrapped in the PDF).
    compact: no spaces around the cells, so that a small table's lines stay
    within pandoc's 72 columns: then the PDF gives each column its natural
    width, instead of stretching the table to the page's width.
    left: the headers of the columns aligned left besides the first (text,
    not numbers)."""
    pad = "" if compact else " "
    keep = [0] + [i for i in range(1, len(head))
                  if any(r[i] not in ("–", "") for r in rows)]
    cols = [[head[i]] + [r[i] for r in rows] for i in keep]
    width = [max(len(c) for c in col) for col in cols]
    # the separator's dashes: the width as shown, without the ** and * markup
    shown = [max(len(c.replace("*", "")) for c in col) for col in cols]

    lefts = [j == 0 or head[i] in left for j, i in enumerate(keep)]

    def line(cells):
        return ("|" + pad) + (pad + "|" + pad).join(
            c.ljust(w) if l else c.rjust(w)
            for c, w, l in zip(cells, width, lefts)) + (pad + "|")
    extra = len(pad) * 2 - 1  # the separator's cells: as wide as the others
    sep = "|" + "|".join((":" + "-" * (w + extra)) if l else ("-" * (w + extra) + ":")
                         for w, l in zip(shown, lefts)) + "|"
    return [line([head[i] for i in keep]), sep] + [line([r[i] for i in keep]) for r in rows]


def problems(suites):
    """The modules whose check failed: [(module, why)]; the module with its
    suite when there are several."""
    out = []
    for s in sorted(suites):
        for m in sorted(suites[s]):
            st, why = check(m, suites[s][m])
            if st == "bad":
                out.append((md(m if len(suites) == 1 else f"{s} / {m}"), md(why)))
    return out


# --- The charts --------------------------------------------------------------

def suite_color(suites, s):
    i = sorted(suites).index(s)
    return SUITE_COLORS[min(i, len(SUITE_COLORS) - 1)]


def style(ax):
    """Recessive axes: no frame but the bottom, light grid."""
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(colors=INK_2, labelsize=SMALL, length=0)
    ax.grid(axis="x", color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)


def tick_label(t):
    """A power of 2 on a ratio axis: 4×, 1×, 0.5×, 0.25×, 1/8×."""
    return f"{t:g}×" if t >= 0.25 else f"1/{1 / t:g}×"


# The figures of each chart, by reference mode (keys of RATIOS)
CHART_FIELDS = {
    "wamrc": ("run_us", "code_B", "heap_peak_B", "stack_B"),
    "interp": ("run_us", "ttr_us", "heap_peak_B", "stack_B"),
}


def ratio_chart(suites, path, ref="wamrc"):
    """Per figure of CHART_FIELDS[ref], a dot per module (wasmc / ref, log
    scale), the geometric mean as a black tick with its value. False if no
    data."""
    rows = []
    for f, title, _ in RATIOS:
        if f not in CHART_FIELDS[ref]:
            continue
        pts = [(s, ratio(d, f, ref)) for s in sorted(suites) for d in suites[s].values()]
        pts = [(s, v) for s, v in pts if v is not None]
        if pts:
            rows.append((title, pts))
    if not rows:
        return False
    # The axis: just past the smallest and largest ratio, at least 0.5-2×;
    # a tick at every power of 2 in it
    every = [v for _, pts in rows for _, v in pts]
    lo = min(0.5, min(every)) / 1.12
    hi = max(2.0, max(every)) * 1.12

    fig, ax = plt.subplots(figsize=(5.5, 1.0 + 0.6 * len(rows)), dpi=200)
    style(ax)
    ax.set_xscale("log", base=2)
    ax.set_xlim(lo, hi)
    exps = list(range(math.ceil(math.log2(lo)), math.floor(math.log2(hi)) + 1))
    step = 1  # at most 8 ticks, 1× always one of them
    while len(exps[::step]) > 8:
        step += 1
    ticks = [2.0 ** e for e in exps if e % step == 0]
    ax.xaxis.set_major_locator(FixedLocator(ticks))
    ax.xaxis.set_minor_locator(NullLocator())
    ax.set_xticklabels([tick_label(t) for t in ticks])
    ax.axvline(1, color="#c3c2b7", linewidth=1, zorder=1)
    seen = set()
    for i, (title, pts) in enumerate(rows):
        y = len(rows) - 1 - i
        for j, (s, v) in enumerate(pts):
            # a small fixed spread, so that equal ratios don't hide each other
            dy = ((j * 7) % 5 - 2) * 0.06
            ax.scatter(v, y + dy, s=26, color=suite_color(suites, s), alpha=0.85,
                       edgecolors="white", linewidths=0.5, zorder=3,
                       label=s if s not in seen else None)
            seen.add(s)
        g = geomean([v for _, v in pts])
        ax.plot([g, g], [y - 0.3, y + 0.3], color=INK, linewidth=1.6, zorder=4)
        ax.annotate(times(g), (hi, y), xytext=(6, 0), textcoords="offset points",
                    va="center", fontsize=FONT, color=INK, annotation_clip=False)
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([t for t, _ in reversed(rows)], fontsize=FONT, color=INK)
    ax.set_ylim(-0.6, len(rows) - 0.4)
    name = {"wamrc": "wamrc", "interp": "interpreter"}[ref]
    ax.set_xlabel(f"wasmc / {name}  (left of 1×: wasmc better; black tick: geometric mean)",
                  fontsize=SMALL, color=INK_2)
    if len(suites) > 1:
        ax.legend(loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=len(suites),
                  frameon=False, fontsize=FONT, handletextpad=0.2, markerscale=1.4)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    return True


def ms(v):
    """Microseconds as short milliseconds, for a narrow chart: 286 ms, 35.4 ms."""
    return f"{v / 1000:.0f} ms" if v >= 99950 else f"{v / 1000:.1f} ms"


def compile_chart(suites, path):
    """wasmc's compile on the device, per module, slowest first: three
    panels sharing the module names, the .wasm size (what the time and heap
    grow with), the compile time and the heap peak, each on its own axis
    (different units: never one axis for two). False if no data."""
    bars = [(s, m, d["compile"]["compile_us"], d["compile"].get("heap_peak_B"), d.get("wasm_B"))
            for s in sorted(suites) for m, d in suites[s].items()
            if has(d.get("compile"), "compile_us")]
    if not bars:
        return False
    bars.sort(key=lambda b: -b[2])
    n = len(bars)
    # At most 7.4 inches high, to fit one page of the PDF under its heading
    # and text (summary.lua gives that page 2 cm margins): with many modules
    # the bars get thinner, not the text smaller.
    # constrained layout: margins sized to the labels (tight_layout cuts the
    # module names of panels sharing them)
    fig, (asz, at, ah) = plt.subplots(1, 3, sharey=True,
                                      figsize=(6.5, min(7.4, 1.1 + 0.21 * n)),
                                      dpi=200, layout="constrained")
    fig.get_layout_engine().set(wspace=0.04, w_pad=0.02, h_pad=0.02)
    y = list(range(n))[::-1]
    colors = [suite_color(suites, b[0]) for b in bars]
    panels = ((asz, [(b[4] or 0) / 1024 for b in bars],
               [nbytes(b[4]) if b[4] is not None else "–" for b in bars], ".wasm size"),
              (at, [b[2] / 1000 for b in bars], [ms(b[2]) for b in bars], "Compile time"),
              (ah, [(b[3] or 0) / 1024 for b in bars],
               [nbytes(b[3]) if b[3] is not None else "–" for b in bars], "Heap peak"))
    for ax, vals, texts, title in panels:
        style(ax)
        ax.barh(y, vals, height=0.7, color=colors)
        for yi, v, t in zip(y, vals, texts):
            ax.annotate(t, (v, yi), xytext=(2, 0), textcoords="offset points",
                        va="center", fontsize=SMALL, color=INK)
        # room on the right for the longest value label (narrow panels)
        ax.set_xlim(0, max(vals) * 1.55 if max(vals) > 0 else 1)
        ax.set_title(title, fontsize=FONT, color=INK, loc="left")
        ax.tick_params(axis="y", length=0)
        # the values are on the bars: no x axis
        ax.set_xticks([])
        ax.grid(False)
    asz.set_yticks(y)
    asz.set_yticklabels([b[1] for b in bars], fontsize=SMALL, color=INK)
    asz.set_ylim(-0.6, n - 0.4)
    if len(suites) > 1:
        from matplotlib.patches import Patch
        ah.legend(handles=[Patch(color=suite_color(suites, s), label=s) for s in sorted(suites)],
                  loc="lower right", frameon=False, fontsize=FONT)
    fig.savefig(path)
    plt.close(fig)
    return True


# --- The file ----------------------------------------------------------------

def spread(xs):
    """min, 25%, median, 75%, max and arithmetic mean of xs."""
    xs = sorted(xs)
    q1, med, q3 = (statistics.quantiles(xs, n=4, method="inclusive") if len(xs) > 1
                   else (xs[0],) * 3)
    return [xs[0], q1, med, q3, xs[-1], statistics.mean(xs)]


def compile_stats(flat):
    """The compile's headline: a line and a table of how its time and heap
    peak spread over the modules (the slowest, biggest are outliers: the
    median says more than the mean or the max). None without compiles."""
    cs = [d["compile"] for d in flat.values() if has(d.get("compile"), "compile_us")]
    if not cs:
        return None
    t = spread([c["compile_us"] / 1000 for c in cs])
    h = spread([c["heap_peak_B"] / 1024 for c in cs if has(c, "heap_peak_B")])
    rows = [["Time (ms)"] + [f"{v:.1f}" for v in t],
            ["Heap peak (KB)"] + [f"{v:.1f}" for v in h]]
    return {"text": f"On-device compile, {count(len(cs))}: **{us(t[2] * 1000)}** "
                    f"median time, **{nbytes(h[2] * 1024)}** median heap peak",
            "table": table(["", "min", "25%", "median", "75%", "max", "mean (arith.)"], rows,
                           compact=True)}


def comparisons(flat):
    """The headline's comparisons, as text: wasmc's run time against the
    interpreter's and wamrc's, its AOT file size against wamrc's."""
    def ratios(a, b, f):
        return [d[a][f] / d[b][f] for d in flat.values()
                if has(d.get(a), f) and has(d.get(b), f) and d[b][f] > 0 and d[a][f] > 0]
    out = []
    speed = ratios("interp", "wasmc", "run_us")
    if speed:
        out.append(f"wasmc AOT vs interpreter: **{num(geomean(speed), 1)}× faster** "
                   f"(run time, geometric mean of {count(len(speed))})")
    vs = ratios("wasmc", "wamrc", "run_us")
    if vs:
        # w: wasmc's run time / wamrc's. Slower: how much more time it takes
        # (1.19 -> 19% slower); faster: how much more it does in the same
        # time (0.8 -> 1.25 -> 25% faster)
        w = geomean(vs)
        pct = round((w - 1) * 100) if w >= 1 else round((1 / w - 1) * 100)
        out.append("wasmc AOT vs wamrc AOT: **" +
                   ("same speed" if pct == 0 else f"{pct}% slower" if w >= 1 else f"{pct}% faster")
                   + f"** (run time, geometric mean of {count(len(vs))})")
    size = ratios("wasmc", "wamrc", "code_B")
    if size:
        # z: wasmc's AOT file size / wamrc's: 0.97 -> 3% smaller, 1.2 -> 20% bigger
        z = geomean(size)
        pct = round(abs(z - 1) * 100)
        out.append("wasmc AOT file size: **" +
                   ("same size as wamrc's" if pct == 0 else
                    f"{pct}% smaller than wamrc's" if z < 1 else f"{pct}% bigger than wamrc's")
                   + f"** (geometric mean of {count(len(size))})")
    return out


def headline(suites):
    """The headline: the comparisons, the compile's line and table, the
    results check. [{"text", "table": lines or None}]"""
    flat = {f"{s}/{m}": d for s, mods in suites.items() for m, d in mods.items()}
    out = [{"text": t, "table": None} for t in comparisons(flat)]
    comp = compile_stats(flat)
    if comp:
        out.append(comp)
    # Correctness: every module's entry returns the same value in every mode
    n = len(flat)
    bad = sum(check(m.split("/", 1)[1], d)[0] == "bad" for m, d in flat.items())
    ran = [n for k, n in SERIES
           if any((d.get(k) or {}).get("result") for d in flat.values())]
    if len(ran) > 1:
        what = (f"return the same result in all {len(ran)} modes "
                f"({', '.join(ran)})")
    else:
        what = f"ran without errors (only {ran[0] if ran else 'no'} results to compare)"
    out.append({"text": f"Correctness: **{n - bad} of {n}** modules {what}" +
                        (f"; {bad} failed or differ: see Problems" if bad else ""),
                "table": None})
    return out


# What each suite is, for the Sources section (Markdown)
SUITE_INFO = {
    "sightglass": "the shootout kernels of the Bytecode Alliance's Sightglass; those "
                  "too big for the ESP32-C3's memory made smaller with `-DESP32` or a "
                  "patch (`suites/sightglass/patches/`). The entry returns a hash of "
                  "the values the kernel passes to `BLACK_BOX()`",
    "embench": "the Embench IoT benchmarks, one pass of each benchmark's loop "
               "(`GLOBAL_SCALE_FACTOR=1`) and no warm-up. The entry returns "
               "`benchmark()`'s result, or `0xbad00000 ^ result` if Embench's own "
               "check rejects it",
}


def run(cmd, cwd=None):
    """The first line a command prints, or None if it fails."""
    try:
        out = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    lines = out.stdout.strip().splitlines()
    return lines[0] if out.returncode == 0 and lines else None


def build_info(suites):
    """How the modules were built, for the Sources section: per suite its
    source (the git submodule's commit), and the arena and C stack sizes of
    its compile.sh; the clang of this machine (the modules, stripped, don't
    record theirs)."""
    out = []
    for s in sorted(suites):
        d = os.path.join(HERE, "suites", s)
        script = os.path.join(d, "compile.sh")
        sizes = {}
        if os.path.exists(script):
            for k in ("ARENA_SIZE", "STACK_SIZE"):
                m = re.search(rf"^{k}=(\d+)", open(script).read(), re.M)
                if m:
                    sizes[k] = nbytes(int(m.group(1)))
        commit = run(["git", "log", "-1", "--format=%h, %cs"], cwd=os.path.join(d, "upstream")) \
            if os.path.isdir(os.path.join(d, "upstream")) else None
        out.append({"suite": s, "about": SUITE_INFO.get(s), "commit": commit,
                    "arena": sizes.get("ARENA_SIZE"), "stack": sizes.get("STACK_SIZE")})
    def per_suite(key):
        """A size per suite as text: "16 KB" if every suite has the same,
        else "4.0 KB for embench, 64 KB for sightglass"; "" if unknown."""
        known = [(b["suite"], b[key]) for b in out if b[key]]
        if not known:
            return ""
        if len({v for _, v in known}) == 1:
            return known[0][1]
        return ", ".join(f"{v} for {s}" for s, v in known)
    return {"suites": out, "clang": run(["clang", "--version"]),
            "arena": per_suite("arena"), "stack": per_suite("stack")}


def when(date):
    """run.sh's date (20261120-165925) as 2026-11-20 16:59; else as it is."""
    d = str(date or "")
    if len(d) == 15 and d[8] == "-" and (d[:8] + d[9:]).isdigit():
        return f"{d[:4]}-{d[4:6]}-{d[6:8]} {d[9:11]}:{d[11:13]}"
    return d or "?"


def sources_view(sources):
    """The Sources section: what all the runs share, as a sentence, and a
    table of the runs (what they differ in as columns too)."""
    names = {"wasmc": "wasmc AOT", "aot": "wamrc AOT", "interp": "interpreter"}
    order = {m: i for i, m in enumerate(("wasmc", "aot", "interp"))}
    srcs = sorted(sources, key=lambda s: (order.get(s["mode"], 9), s["suites"]))
    opts = {s.get("opt") or "?" for s in srcs}
    reps = {s.get("repeat") for s in srcs}
    shared = []
    if len(opts) == 1:
        shared.append(f"the firmware (with WAMR and wasmc) was built with `{opts.pop()}`")
    if len(reps) == 1:
        shared.append(f"each module ran {reps.pop()} times in a row after one boot")
    # when: in the text, not a column (the table must fit the PDF's default
    # margins at 12pt)
    stamps = sorted(when(s["date"]) for s in srcs)
    days = sorted({t.split()[0] for t in stamps})
    if stamps and len(days) == 1 and len(stamps[0].split()) == 2:
        shared.insert(0, f"the runs were made on {days[0]}, from {stamps[0].split()[1]} "
                         f"to {stamps[-1].split()[1]}")
    elif stamps:
        shared.insert(0, f"the runs were made from {stamps[0]} to {stamps[-1]}")
    head = ["Mode", "Suite", "Modules"]
    if len(opts) > 1:
        head.append("Firmware")
    if len(reps) > 1:
        head.append("Runs")
    head.append("Results directory")
    rows = []
    for s in srcs:
        r = [names.get(s["mode"], s["mode"]), md(", ".join(s["suites"])), str(s["modules"])]
        if len(opts) > 1:
            r.append(f"`{s.get('opt') or '?'}`")
        if len(reps) > 1:
            r.append(str(s.get("repeat")))
        r.append(f"`{os.path.basename(s['file'])}`")
        rows.append(r)
    return {"shared": "; ".join(shared),
            "table": table(head, rows, left=("Suite", "Firmware", "Results directory"))}


def repeats(sources):
    """How many times each module ran, as text: "3", or "2–3" when the
    results differ; "" if unknown."""
    rs = sorted({s["repeat"] for s in sources if isinstance(s.get("repeat"), int) and s["repeat"]})
    if not rs:
        return ""
    return str(rs[0]) if len(rs) == 1 else f"{rs[0]}–{rs[-1]}"


def render(suites, sources, generated, out_path):
    for mods in suites.values():
        add_loadinst(mods)
        for d in mods.values():
            add_ttr(d)
    stem = os.path.splitext(os.path.basename(out_path))[0]
    images = {"ratios": None, "interp": None, "compile": None}  # the PNGs written
    if plt is not None:
        base = os.path.join(os.path.dirname(os.path.abspath(out_path)), stem)
        if ratio_chart(suites, base + "-ratios.png"):
            images["ratios"] = stem + "-ratios.png"
        if ratio_chart(suites, base + "-interp.png", ref="interp"):
            images["interp"] = stem + "-interp.png"
        if compile_chart(suites, base + "-compile.png"):
            images["compile"] = stem + "-compile.png"

    env = jinja2.Environment(loader=jinja2.FileSystemLoader(os.path.dirname(TEMPLATE)),
                             undefined=jinja2.StrictUndefined, keep_trailing_newline=True,
                             trim_blocks=True, lstrip_blocks=True)
    return env.get_template(os.path.basename(TEMPLATE)).render(
        date=generated.split()[0], generated=generated,
        headline=headline(suites), images=images, problems=problems(suites),
        sources=sources_view(sources),
        build=build_info(suites),
        rows={ref: [(t, w) for f, t, w in RATIOS if f in CHART_FIELDS[ref]]
              for ref in CHART_FIELDS},
        reps=repeats(sources))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*", help="run.sh results, one per mode and suite")
    ap.add_argument("-o", "--output", default=os.path.join(HERE, "results", "summary.md"),
                    help="the Markdown file; the charts go next to it")
    args = ap.parse_args()

    paths = args.files or [p for mode in MODES
                           for p in glob.glob(os.path.join(HERE, "results", f"{mode}-*"))
                           if os.path.isdir(p) or p.endswith(".json")]
    if not paths:
        sys.exit("no results: pass them, or run run.sh first")
    chosen = pick(paths, explicit=bool(args.files))
    suites, sources = load(chosen)
    text = render(suites, sources, time.strftime("%Y-%m-%d %H:%M:%S"), args.output)
    with open(args.output, "w") as f:
        f.write(text)
    for (mode, suite), (path, _) in sorted(chosen.items()):
        print(f"  {mode:7} {suite:12} {os.path.relpath(path)}")
    if plt is None:
        print("  (no matplotlib: no charts; apt install python3-matplotlib)")
    print(f"-> {args.output}")
    out_dir = os.path.dirname(os.path.abspath(args.output))
    print(f"PDF: cd {os.path.relpath(out_dir)} && "
          f"pandoc -d {os.path.relpath(PDF_DEFAULTS, out_dir)} {os.path.basename(args.output)} "
          f"-o {os.path.splitext(os.path.basename(args.output))[0]}.pdf")


if __name__ == "__main__":
    main()
