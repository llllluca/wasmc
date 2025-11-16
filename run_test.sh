#!/bin/sh

#ansi escape codes for color
red='\033[0;31m'
green='\033[0;32m'
#ansi escape codes for reset color
no_color='\033[0m'

target="rv32"

log_error() {
    echo "${red}[error]: ${1}${no_color}" >&2
}

log_info() {
    echo "${green}[info]: ${1}${no_color}"
}

run_test() {
    local name="$1"
    local expected_result="$2"

    if [ -z "$name" ] || [ -z "$expected_result" ]; then
        log_error "run_test() failed, missing 'name' or 'expected_result'."
        exit 1
    fi
    log_info "starting test '${name}'."

    case "$target" in
    "rv32")
        wat2wasm "test/${name}.wat" -o "test/${name}.wasm" && \
            ./wasmc "test/${name}.wasm" > "test/${name}.s" && \
            riscv32-unknown-linux-gnu-gcc -no-pie -static "test/${name}.s" -o "test/a.out"
        ;;
    "amd64_sysv")
        wat2wasm "test/${name}.wat" -o "test/${name}.wasm" && \
            ./wasmc "test/${name}.wasm" > "test/${name}.ssa" && \
            ./qbe -t amd64_sysv "test/${name}.ssa" > "test/${name}.s" && \
            cc -g "test/${name}.s" -o "test/a.out"
        ;;
    *)
        log_error "wrong target."
        exit 1
        ;;
    esac

    local exit_code="$?"
    if [ "$exit_code" -ne 0 ]; then
        log_error "compilation of ${name} failed."
        exit 1
    fi

    ./test/a.out
    exit_code="$?"
    if [ "$exit_code" -ne "$expected_result" ]; then
        log_error "${name} failed, the expected exit code is ${expected_result} but received ${exit_code}."
        exit 1
    fi

    rm -f \
        "test/${name}.wasm" \
        "test/${name}.s" \
        "test/a.out"

    log_info "test '${name}' successfully completed."
}

run_test "add" 101
run_test "if1" 51
run_test "if2" 52
run_test "if3" 65
run_test "if4" 151
run_test "if5" 99
run_test "if6" 20
run_test "if7" 5
run_test "if8" 10
run_test "if9" 20
run_test "if10" 151
run_test "if11" 156
run_test "if12" 99
run_test "if13" 2
run_test "if14" 22
run_test "if15" 5
run_test "if16" 10
run_test "if17" 72
run_test "block1" 10
run_test "block2" 29
run_test "block3" 72
run_test "block4" 55
run_test "loop1" 30
run_test "loop2" 101
run_test "eqz" 90
run_test "eq" 110
run_test "ne" 90
run_test "lt_s" 110
run_test "lt_u" 90
run_test "drop" 90
run_test "select" 44
run_test "memory" 250
run_test "sum" 21
run_test "mean" 4
run_test "data1" 10
run_test "mean-even" 19
run_test "data2" 10
run_test "max" 255
run_test "isort" 0
run_test "msort" 21
run_test "qsort" 102
run_test "ackermann" 253
run_test "base64" 43
run_test "fib2" 165
#DOESNT WORKS, require support for i64. run_test "gimli" 238
run_test "heapsort" 145
run_test "matrix" 3
#DOESNT WORKS, clang produce wrong code. run_test "minicsv" 0
run_test "sieve" 160
#DOESNT WORKS, there is a bug in wasmc. run_test "xchacha20" 146


