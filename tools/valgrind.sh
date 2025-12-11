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

run_valgrind() {
    local name="$1"

    if [ -z "$name" ]; then
        log_error "run_test() failed, missing 'name."
        exit 1
    fi

    log_info "Checking memory leaks for ${name}..."
    wat2wasm "test/${name}.wat" -o "test/${name}.wasm" && \
    valgrind --leak-check=full --quiet --error-exitcode=1 ./build/wasmc "test/${name}.wasm" > /dev/null

    # Check the exit status of Valgrind
    if [ "$?" -eq 0 ]; then
        log_info "No memory leaks detected."
        rm "test/${name}.wasm"
    else
        log_error "Memory leaks detected."
        exit 1
    fi
}

run_valgrind "add"
run_valgrind "if1"
run_valgrind "if2"
run_valgrind "if3"
run_valgrind "if4"
run_valgrind "if5"
run_valgrind "if6"
run_valgrind "if7"
run_valgrind "if8"
run_valgrind "if9"
run_valgrind "if10"
run_valgrind "if11"
run_valgrind "if12"
run_valgrind "if13"
run_valgrind "if14"
run_valgrind "if15"
run_valgrind "if16"
run_valgrind "if17"
run_valgrind "block1"
run_valgrind "block2"
run_valgrind "block3"
run_valgrind "block4"
run_valgrind "loop1"
run_valgrind "loop2"
run_valgrind "eqz"
run_valgrind "eq"
run_valgrind "ne"
run_valgrind "lt_s"
run_valgrind "lt_u"
run_valgrind "drop"
run_valgrind "select"
run_valgrind "memory"
run_valgrind "sum"
run_valgrind "mean"
run_valgrind "data1"
run_valgrind "mean-even"
run_valgrind "data2"
run_valgrind "max"
run_valgrind "isort"
run_valgrind "msort"
run_valgrind "qsort"
run_valgrind "ackermann"
run_valgrind "base64"
run_valgrind "fib2"
#DOESNT WORKS, require support for i64. run_valgrind "gimli"
run_valgrind "heapsort"
run_valgrind "matrix"
#DOESNT WORKS, clang produce wrong code. run_valgrind "minicsv"
run_valgrind "sieve"
#DOESNT WORKS, there is a bug in wasmc. run_valgrind "xchacha20"


