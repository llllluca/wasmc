include_directories(../src/)

# ${CMAKE_SOURCE_DIR} is the project root directory
set(TESTS_DIR ${CMAKE_SOURCE_DIR}/tests)

# the working directory in which the tests
# are executed is wasmc/build/tests/
function(test name expected_result iwasm_args)
    add_test(
        NAME readaot_${name}
        COMMAND sh ${TESTS_DIR}/readaot_test.sh ${TESTS_DIR}/wasm/${name}.wasm)

    add_test(
        NAME ${name}
        COMMAND sh ${TESTS_DIR}/iwasm_test.sh ${TESTS_DIR}/wasm/${name}.wasm ${iwasm_args})
    set_property(
        TEST ${name}
        PROPERTY PASS_REGULAR_EXPRESSION ${expected_result})

    add_test(
        NAME memcheck_${name}
        COMMAND sh ${TESTS_DIR}/valgrind_test.sh ${TESTS_DIR}/wasm/${name}.wasm)
endfunction()

# Function to test a WASM module generated from a handwritten WAT file
function(test_wat name expected_result)
    test(${name} ${expected_result} "")
endfunction()

# Function to test a WASM module compiled from a C source file
function(test_c name expected_result)
    test(${name} ${expected_result} "0 0")
endfunction()


test_wat(add "0x65:i32")
test_wat(if1 "0x33:i32")
test_wat(if2 "0x34:i32")
test_wat(if3 "0x41:i32")
test_wat(if4 "0x97:i32")
test_wat(if5 "0x63:i32")
test_wat(if6 "0x14:i32")
test_wat(if7 "0x5:i32")
test_wat(if8 "0xa:i32")
test_wat(if9 "0x14:i32")
test_wat(if10 "0x97:i32")
test_wat(if11 "0x9c:i32")
test_wat(if12 "0x63:i32")
test_wat(if13 "0x2:i32")
test_wat(if14 "0x16:i32")
test_wat(if15 "0x5:i32")
test_wat(if16 "0xa:i32")
test_wat(if17 "0x48:i32")
test_wat(if18 "0x1:i32")
test_wat(block1 "0xa:i32")
test_wat(block2 "0x1d:i32")
test_wat(block3 "0x48:i32")
test_wat(block4 "0x37:i32")
test_wat(loop1 "0x1e:i32")
test_wat(loop2 "0x65:i32")
test_wat(eqz "0x5a:i32")
test_wat(eq "0x6e:i32")
test_wat(ne "0x5a:i32")
test_wat(lt_s "0x6e:i32")
test_wat(lt_u "0x5a:i32")
test_wat(drop "0x5a:i32")
test_wat(select "0x2c:i32")
test_wat(memory "0xfa:i32")
test_c(sum-O0 "0x15:i32")
test_c(sum-O1 "0x15:i32")
test_c(sum-Os "0x15:i32")
test_c(mean-O0 "0x5:i32")
test_c(mean-O1 "0x5:i32")
#test_c(mean-O2 "0x5:i32") #i64 are not implemented
#test_c(mean-O3 "0x5:i32") #i64 are not implemented
test_c(mean-Os "0x5:i32")
test_wat(data1 "0xa:i32")
test_wat(data2 "0xa:i32")
test_c(mean-even-O0 "0x13:i32")
test_c(mean-even-O1 "0x13:i32")
test_c(mean-even-O2 "0x13:i32")
test_c(mean-even-O3 "0x13:i32")
test_c(mean-even-Os "0x13:i32")
test_c(max-O0 "0xff:i32")
test_c(max-O1 "0xff:i32")
test_c(max-O2 "0xff:i32")
test_c(max-O3 "0xff:i32")
test_c(max-Os "0xff:i32")
test_c(isort-O0 "0x0:i32")
test_c(isort-O1 "0x0:i32")
test_c(isort-O2 "0x0:i32")
test_c(isort-O3 "0x0:i32")
test_c(isort-Os "0x0:i32")
test_c(fib2-O0 "0x59:i32")
test_c(fib2-O1 "0x59:i32")
test_c(fib2-O2 "0x59:i32")
test_c(fib2-O3 "0x59:i32")
test_c(fib2-Os "0x59:i32")
test_c(msort-O0 "0x15:i32")
test_c(msort-O1 "0x15:i32")
#test_c(msort-O2 "0x15:i32") # Segmentation fault
#test_c(msort-O3 "0x15:i32") # Segmentation fault
#test_c(msort-Os "0x15:i32") # Segmentation fault
test_c(qsort-O0 "0x66:i32")
test_c(qsort-O1 "0x66:i32")
test_c(qsort-O2 "0x66:i32")
test_c(qsort-O3 "0x66:i32")
test_c(qsort-Os "0x66:i32")
test_c(ackermann-O0 "0x3fd:i32")
test_c(ackermann-O1 "0x3fd:i32")
test_c(ackermann-O2 "0x3fd:i32")
test_c(ackermann-O3 "0x3fd:i32")
test_c(ackermann-Os "0x3fd:i32")
test_c(base64-O0 "0x2b:i32")
#test_c(base64-O1 "0x2b:i32") # Wrong return value
#test_c(base64-O2 "0x2b:i32") # Wrong return value
#test_c(base64-O3 "0x2b:i32") # Wrong return value
#test_c(base64-Os "0x2b:i32") # Wrong return value
test_c(heapsort-O0 "0x22291:i32")
test_c(heapsort-O1 "0x22291:i32")
#test_c(heapsort-O2 "0x22291:i32") # Infinite loop
#test_c(heapsort-O3 "0x22291:i32") # Infinite loop
#test_c(heapsort-Os "0x22291:i32") # Infinite loop
test_c(matrix-O0 "0x2255727:i32")
test_c(matrix-O1 "0x2255727:i32")
test_c(matrix-O2 "0x2255727:i32")
test_c(matrix-O3 "0x2255727:i32")
test_c(matrix-Os "0x2255727:i32")
test_c(sieve-O0 "0x76c:i32")
test_c(sieve-O1 "0x76c:i32")
test_c(sieve-O2 "0x76c:i32")
test_c(sieve-O3 "0x76c:i32")
test_c(sieve-Os "0x76c:i32")

