include_directories(../src/)

function(test name expected_result iwasm)
    add_test(NAME ${name}
        COMMAND sh -c "${CMAKE_BINARY_DIR}/wasmc wasm/${name}.wasm && ${iwasm}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
    set_property(TEST ${name}
        PROPERTY PASS_REGULAR_EXPRESSION ${expected_result})

    add_test(NAME memcheck_${name}
        COMMAND valgrind
        --error-exitcode=1
        --tool=memcheck
        --leak-check=full
        --track-origins=yes
        ${CMAKE_BINARY_DIR}/wasmc wasm/${name}.wasm
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
endfunction()

# Function to test a WASM module generated from a handwritten WAT file
function(test_wat name expected_result)
    test(${name} ${expected_result} "iwasm-2.4.4 -f main a.aot")
endfunction()

# Function to test a WASM module compiled from a C source file
function(test_c name expected_result)
    test(${name} ${expected_result} "iwasm-2.4.4 -f main a.aot 0 0")
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
test_c(sum "0x15:i32")
test_c(mean "0x5:i32")
test_wat(data1 "0xa:i32")
test_wat(data2 "0xa:i32")
test_c(mean-even "0x13:i32")
test_c(max "0xff:i32")
test_c(isort "0x0:i32")
test_c(fib2 "0x59:i32")
test_c(msort "0x15:i32")
test_c(qsort "0x66:i32")
test_c(ackermann "0x3fd:i32")
test_c(base64 "0x2b:i32")
test_c(heapsort "0x22291:i32")
test_c(matrix "0x2255727:i32")
test_c(sieve "0x10aa9a0:i32")

