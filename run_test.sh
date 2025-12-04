#!/bin/sh

#ansi escape codes for color
red='\033[0;31m'
green='\033[0;32m'
#ansi escape codes for reset color
no_color='\033[0m'

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

    local result=$(wat2wasm "test/${name}.wat" -o "test/${name}.wasm" && \
        ./wasmc "test/${name}.wasm" > "test/${name}.aot" && \
        ./iwasm-2.4.4 -f main "test/${name}.wasm")

    if [ "$result" != "$expected_result" ]; then
        log_error "${name} failed, the expected result is ${expected_result} but received ${result}."
        exit 1
    fi

    rm -f "test/${name}.wasm" "test/${name}.aot"

    log_info "test '${name}' successfully completed."
}

run_test "add" "0x65:i32"
run_test "if1" "0x33:i32"
run_test "if2" "0x34:i32"
run_test "if3" "0x41:i32"
run_test "if4" "0x97:i32"
run_test "if5" "0x63:i32"
run_test "if6" "0x14:i32"
run_test "if7" "0x5:i32"
run_test "if8" "0xa:i32"
run_test "if9" "0x14:i32"
run_test "if10" "0x97:i32"
run_test "if11" "0x9c:i32"
run_test "if12" "0x63:i32"
run_test "if13" "0x2:i32"
run_test "if14" "0x16:i32"
run_test "if15" "0x5:i32"
run_test "if16" "0xa:i32"
run_test "if17" "0x48:i32"
run_test "block1" "0xa:i32"
run_test "block2" "0x1d:i32"
run_test "block3" "0x48:i32"
run_test "block4" "0x37:i32"
run_test "loop1" "0x1e:i32"
run_test "loop2" "0x65:i32"
run_test "eqz" "0x5a:i32"
run_test "eq" "0x6e:i32"
run_test "ne" "0x5a:i32"
run_test "lt_s" "0x6e:i32"
run_test "lt_u" "0x5a:i32"
run_test "drop" "0x5a:i32"
run_test "select" "0x2c:i32"
#run_test "memory" 250
#run_test "sum" 21
#run_test "mean" 4
#run_test "data1" 10
#run_test "mean-even" 19
#run_test "data2" 10
#run_test "max" 255
#run_test "isort" 0
#run_test "msort" 21
#run_test "qsort" 102
#run_test "ackermann" 253
#run_test "base64" 43
#run_test "fib2" 165
##DOESNT WORKS, require support for i64. run_test "gimli" 238
#run_test "heapsort" 145
#run_test "matrix" 3
##DOESNT WORKS, clang produce wrong code. run_test "minicsv" 0
#run_test "sieve" 160
##DOESNT WORKS, there is a bug in wasmc. run_test "xchacha20" 146

