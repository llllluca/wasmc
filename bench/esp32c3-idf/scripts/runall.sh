#!/bin/sh

set -eu

# --- Defaults & Tunables ---
MODES="${MODES:-wasmc aot interp}"   # Modes to run, in this order
SUITES_ROOT="suites/modules"

usage() {
    cat <<EOF
Usage: scripts/runall.sh PORT

Run scripts/run.sh for every mode and every suite of suites/modules/:
one results/<mode>-<date>/ directory per pair.

  PORT   the board's serial port, e.g. /dev/ttyACM0

  MODES="wasmc aot" scripts/runall.sh ...      only these modes
  SUITES="sightglass" scripts/runall.sh ...    only these suites
  REPEAT=N scripts/runall.sh ...               passed on to run.sh

A failed run is reported and the others go on. Then: scripts/summary.py
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
    if [ $# -ne 1 ]; then
        usage >&2
        exit 1
    fi
    PORT="$1"
}

discover_suites() {
    if [ -n "${SUITES:-}" ]; then
        printf '%s\n' "$SUITES"
        return 0
    fi

    for d in "$SUITES_ROOT"/*/; do
        [ -d "$d" ] && basename "$d"
    done
}

run_matrix() {
    suites=$(discover_suites)
    [ -n "$suites" ] ||
        die "no suites in $SUITES_ROOT/ (build them with suites/build.sh)"

    failed=""
    for mode in $MODES; do
        for suite in $suites; do
            printf '\n=== %s / %s ===\n' "$mode" "$suite"
            if ! scripts/run.sh "$mode" "$PORT" "$SUITES_ROOT/$suite"; then
                failed="${failed}${failed:+, }$mode/$suite"
            fi
        done
    done

    if [ -n "$failed" ]; then
        printf '\nFailed: %s\n' "$failed" >&2
        exit 1
    fi
}

# Print the time since $START, as H:MM:SS
print_elapsed() {
    t=$(( $(date +%s) - START ))
    printf 'Total time: %d:%02d:%02d\n' $((t / 3600)) $((t / 60 % 60)) $((t % 60))
}

main() {
    parse_args "$@"

    # The total time, printed however the run ends (done, failed, stopped)
    START=$(date +%s)
    trap print_elapsed EXIT
    trap 'exit 130' INT TERM

    # Script lives in scripts/, so step up to project root
    cd "$(dirname "$0")/.."

    run_matrix
    printf '\nAll done; now: scripts/summary.py\n'
}

main "$@"
