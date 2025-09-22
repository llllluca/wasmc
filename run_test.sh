#!/bin/sh

set -

#ANSI escape codes for color
RED='\033[0;31m'
GREEN='\033[0;32m'
#ANSI escape codes for reset color
NO_COLOR='\033[0m'

log_error() {
    echo "${RED}[ERROR]: ${1}${NO_COLOR}" >&2
}

log_info() {
    echo "${GREEN}[INFO]: ${1}${NO_COLOR}"
}

run_test() {
    local name="$1"
    local expected_result="$2"

    if [ -z "$name" ] || [ -z "$expected_result" ]; then
        log_error "run_test() failed, missing 'name' or 'expected_result'."
        exit 1
    fi
    local name_no_extension="$(echo "$name" | cut -d '.' -f 1)"

    log_info "Starting test '${name_no_extension}'."

    wat2wasm "$name" && \
        ../main "${name_no_extension}.wasm" && \
        ../qbe "${name_no_extension}.wasm.ssa" > "${name_no_extension}.s" && \
        cc "${name_no_extension}.s"

    local exit_code="$?"
    if [ "$exit_code" -ne 0 ]; then
        log_error "run_test(${name}, ${expected_result}) failed."
        exit 1
    fi

    ./a.out
    exit_code="$?"
    if [ "$exit_code" -ne "$expected_result" ]; then
        log_error "run_test(${name}, ${expected_result}) failed."
        exit 1
    fi

    rm -f \
        "${name_no_extension}.wasm" \
        "${name_no_extension}.wasm.ssa" \
        "${name_no_extension}.s" \
        "a.out"

    log_info "Test '${name_no_extension}' successfully completed."
}

cd "test/"
run_test "add.wat" 101
run_test "if1.wat" 51
run_test "if2.wat" 52
run_test "if3.wat" 65
run_test "if4.wat" 151
run_test "if5.wat" 99
run_test "if6.wat" 20
run_test "if7.wat" 5
run_test "if8.wat" 10
run_test "if9.wat" 20
run_test "if10.wat" 151
run_test "if11.wat" 156
run_test "if12.wat" 99
run_test "if13.wat" 2
run_test "if14.wat" 22
run_test "if15.wat" 5
run_test "if16.wat" 10
run_test "if17.wat" 72
run_test "block1.wat" 10
run_test "block1.wat" 10
run_test "block2.wat" 29
run_test "block3.wat" 72
run_test "block4.wat" 55
run_test "loop1.wat" 30
run_test "loop2.wat" 101
run_test "eqz.wat" 90
run_test "eq.wat" 110
run_test "ne.wat" 90
run_test "lt_s.wat" 110
run_test "lt_u.wat" 90
run_test "drop.wat" 90
run_test "select.wat" 44


