#!/bin/sh

set -eu

# --- Defaults & Tunables ---
REPEAT="${REPEAT:-3}"        # Runs per module (1st is cold from flash)
FLASH_TRIES=3                # Flash attempts per module, on esptool errors

usage() {
    cat <<EOF
Usage: scripts/run.sh MODE PORT DIR

Benchmark every .wasm of DIR on the ESP32-C3, one module at a time.

  MODE   wasmc    compile on the device with wasmc, run with WAMR AOT
         aot      compile on the host with wamrc, run with WAMR AOT
         interp   run with the WAMR interpreter
  PORT   the board's serial port, e.g. /dev/ttyACM0
  DIR    the directory with the .wasm modules, e.g. suites/modules/sightglass

  REPEAT=N scripts/run.sh ...   measures per module (default 3)

Results go to results/<mode>-<date>/; then: scripts/summary.py
EOF
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

parse_args() {
    if [ $# -eq 1 ] && { [ "$1" = -h ] || [ "$1" = --help ]; }; then
        usage
        exit 0
    fi

    if [ $# -ne 3 ]; then
        usage >&2
        exit 1
    fi

    MODE="$1"
    PORT="$2"
    RAW_DIR="$3"

    case "$MODE" in
        wasmc|aot|interp) ;;
        *) die "MODE must be wasmc, aot or interp, not '$MODE'" ;;
    esac
}

validate_env() {
    # Resolve before cd'ing into the script directory
    WASM_DIR=$(realpath -m "$RAW_DIR")

    ls "$WASM_DIR"/*.wasm > /dev/null 2>&1 ||
        die "no .wasm modules in '$RAW_DIR' (build them with suites/build.sh)"

    command -v idf.py > /dev/null 2>&1 ||
        die "idf.py is not in PATH: set up the ESP-IDF environment first"
}

# Print "ok", or "FAILED: " and the first error or failed status in the
# .out file $1 (one JSON object per line)
show_result() {
    reason=$(grep -oE '"(error|status)":"[^"]*"' "$1" | grep -v '"status":"ok"' |
             head -n 1 | cut -d '"' -f 4)
    if [ -n "$reason" ]; then
        printf 'FAILED: %s\n' "$reason"
    elif ! grep -q '"rep"' "$1"; then
        printf 'FAILED: no measures\n'
    else
        printf 'ok\n'
    fi
}

# Flash the firmware built in $1, appending esptool's output to the log $2.
# An esptool error ("A fatal error occurred", e.g. a byte lost on the
# serial line) is retried, up to FLASH_TRIES attempts in all; any other
# failure is not. Each retry is shown on the module's line.
flash() {
    attempt=1
    while :; do
        printf '\n--- flash, attempt %s of %s ---\n' "$attempt" "$FLASH_TRIES" >> "$2"
        if idf.py -B "$1" -p "$PORT" flash > "$2.flash" 2>&1; then
            cat "$2.flash" >> "$2"
            rm -f "$2.flash"
            return 0
        fi
        cat "$2.flash" >> "$2"
        if ! grep -q 'A fatal error occurred' "$2.flash" ||
                [ "$attempt" -ge "$FLASH_TRIES" ]; then
            rm -f "$2.flash"
            return 1
        fi
        rm -f "$2.flash"
        attempt=$((attempt + 1))
        printf '(flash attempt %s/%s) ' "$attempt" "$FLASH_TRIES"
    done
}

run_one_module() {
    wasm="$1"
    results="$2"
    build_dir="$3"
    progress="$4"   # e.g. 3/28

    name=$(basename "$wasm" .wasm)
    size=$(stat -c %s "$wasm")
    out="$results/$name.out"
    log="$results/$name.log"

    # The result goes on the same line when the module is done
    printf '[%s] %s  ' "$progress" "$name"

    # Metadata header line expected by summary.py; the suite
    # is the name of DIR, e.g. sightglass
    printf '{"module":"%s","suite":"%s","wasm_B":%s}\n' \
        "$name" "$(basename "$WASM_DIR")" "$size" > "$out"

    # Pass -D flags every iteration because CMake caches prior values
    if ! idf.py -B "$build_dir" \
            -DBENCH_MODE="$MODE" \
            -DBENCH_MODULE="$wasm" \
            -DBENCH_REPEAT="$REPEAT" \
            -DBENCH_ENTRY=__main_void \
            -DBENCH_EXECUTE_FUNC=OFF \
            build > "$log" 2>&1; then
        printf '{"error":"build failed, see %s"}\n' "$log" >> "$out"
        show_result "$out"
        return 0
    fi

    if ! flash "$build_dir" "$log"; then
        printf '{"error":"flash failed, see %s"}\n' "$log" >> "$out"
        show_result "$out"
        return 0
    fi

    if ! python3 scripts/read_serial.py "$PORT" 120 >> "$out"; then
        printf '{"error":"no output within 120 s"}\n' >> "$out"
    fi

    show_result "$out"
}

main() {
    parse_args "$@"
    validate_env

    # The project directory (this script is in scripts/): idf.py, build-<mode>/
    # and results/ are there
    cd "$(dirname "$0")/.."

    build_dir="build-$MODE"
    results="results/$MODE-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$results"

    total=$(ls "$WASM_DIR"/*.wasm | wc -l)
    i=0
    for wasm in "$WASM_DIR"/*.wasm; do
        i=$((i + 1))
        run_one_module "$wasm" "$results" "$build_dir" "$i/$total"
    done

    printf -- '-> %s\n' "$results"
}

main "$@"
