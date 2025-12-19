include_directories(../src/)

set(TEST_DIR ../../tests/wasm)

add_test(NAME compile_add COMMAND wasmc ${TEST_DIR}/add.wasm)
add_test(NAME test_add COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_add PROPERTY PASS_REGULAR_EXPRESSION "0x65:i32")

add_test(NAME compile_if1 COMMAND wasmc ${TEST_DIR}/if1.wasm)
add_test(NAME test_if1 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if1 PROPERTY PASS_REGULAR_EXPRESSION "0x33:i32")

add_test(NAME compile_if2 COMMAND wasmc ${TEST_DIR}/if2.wasm)
add_test(NAME test_if2 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if2 PROPERTY PASS_REGULAR_EXPRESSION "0x34:i32")

add_test(NAME compile_if3 COMMAND wasmc ${TEST_DIR}/if3.wasm)
add_test(NAME test_if3 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if3 PROPERTY PASS_REGULAR_EXPRESSION "0x41:i32")

add_test(NAME compile_if4 COMMAND wasmc ${TEST_DIR}/if4.wasm)
add_test(NAME test_if4 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if4 PROPERTY PASS_REGULAR_EXPRESSION "0x97:i32")

add_test(NAME compile_if5 COMMAND wasmc ${TEST_DIR}/if5.wasm)
add_test(NAME test_if5 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if5 PROPERTY PASS_REGULAR_EXPRESSION "0x63:i32")

add_test(NAME compile_if6 COMMAND wasmc ${TEST_DIR}/if6.wasm)
add_test(NAME test_if6 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if6 PROPERTY PASS_REGULAR_EXPRESSION "0x14:i32")

add_test(NAME compile_if7 COMMAND wasmc ${TEST_DIR}/if7.wasm)
add_test(NAME test_if7 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if7 PROPERTY PASS_REGULAR_EXPRESSION "0x5:i32")

add_test(NAME compile_if8 COMMAND wasmc ${TEST_DIR}/if8.wasm)
add_test(NAME test_if8 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if8 PROPERTY PASS_REGULAR_EXPRESSION "0xa:i32")

add_test(NAME compile_if9 COMMAND wasmc ${TEST_DIR}/if9.wasm)
add_test(NAME test_if9 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if9 PROPERTY PASS_REGULAR_EXPRESSION "0x14:i32")

add_test(NAME compile_if10 COMMAND wasmc ${TEST_DIR}/if10.wasm)
add_test(NAME test_if10 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if10 PROPERTY PASS_REGULAR_EXPRESSION "0x97:i32")

add_test(NAME compile_if11 COMMAND wasmc ${TEST_DIR}/if11.wasm)
add_test(NAME test_if11 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if11 PROPERTY PASS_REGULAR_EXPRESSION "0x9c:i32")

add_test(NAME compile_if12 COMMAND wasmc ${TEST_DIR}/if12.wasm)
add_test(NAME test_if12 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if12 PROPERTY PASS_REGULAR_EXPRESSION "0x63:i32")

add_test(NAME compile_if13 COMMAND wasmc ${TEST_DIR}/if13.wasm)
add_test(NAME test_if13 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if13 PROPERTY PASS_REGULAR_EXPRESSION "0x2:i32")

add_test(NAME compile_if14 COMMAND wasmc ${TEST_DIR}/if14.wasm)
add_test(NAME test_if14 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if14 PROPERTY PASS_REGULAR_EXPRESSION "0x16:i32")

add_test(NAME compile_if15 COMMAND wasmc ${TEST_DIR}/if15.wasm)
add_test(NAME test_if15 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if15 PROPERTY PASS_REGULAR_EXPRESSION "0x5:i32")

add_test(NAME compile_if16 COMMAND wasmc ${TEST_DIR}/if16.wasm)
add_test(NAME test_if16 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if16 PROPERTY PASS_REGULAR_EXPRESSION "0xa:i32")

add_test(NAME compile_if17 COMMAND wasmc ${TEST_DIR}/if17.wasm)
add_test(NAME test_if17 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_if17 PROPERTY PASS_REGULAR_EXPRESSION "0x48:i32")

add_test(NAME compile_block1 COMMAND wasmc ${TEST_DIR}/block1.wasm)
add_test(NAME test_block1 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_block1 PROPERTY PASS_REGULAR_EXPRESSION "0xa:i32")

add_test(NAME compile_block2 COMMAND wasmc ${TEST_DIR}/block2.wasm)
add_test(NAME test_block2 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_block2 PROPERTY PASS_REGULAR_EXPRESSION "0x1d:i32")

add_test(NAME compile_block3 COMMAND wasmc ${TEST_DIR}/block3.wasm)
add_test(NAME test_block3 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_block3 PROPERTY PASS_REGULAR_EXPRESSION "0x48:i32")

add_test(NAME compile_block4 COMMAND wasmc ${TEST_DIR}/block4.wasm)
add_test(NAME test_block4 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_block4 PROPERTY PASS_REGULAR_EXPRESSION "0x37:i32")

add_test(NAME compile_loop1 COMMAND wasmc ${TEST_DIR}/loop1.wasm)
add_test(NAME test_loop1 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_loop1 PROPERTY PASS_REGULAR_EXPRESSION "0x1e:i32")

add_test(NAME compile_loop2 COMMAND wasmc ${TEST_DIR}/loop2.wasm)
add_test(NAME test_loop2 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_loop2 PROPERTY PASS_REGULAR_EXPRESSION "0x65:i32")

add_test(NAME compile_eqz COMMAND wasmc ${TEST_DIR}/eqz.wasm)
add_test(NAME test_eqz COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_eqz PROPERTY PASS_REGULAR_EXPRESSION "0x5a:i32")

add_test(NAME compile_eq COMMAND wasmc ${TEST_DIR}/eq.wasm)
add_test(NAME test_eq COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_eq PROPERTY PASS_REGULAR_EXPRESSION "0x6e:i32")

add_test(NAME compile_ne COMMAND wasmc ${TEST_DIR}/ne.wasm)
add_test(NAME test_ne COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_ne PROPERTY PASS_REGULAR_EXPRESSION "0x5a:i32")

add_test(NAME compile_lt_s COMMAND wasmc ${TEST_DIR}/lt_s.wasm)
add_test(NAME test_lt_s COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_lt_s PROPERTY PASS_REGULAR_EXPRESSION "0x6e:i32")

add_test(NAME compile_lt_u COMMAND wasmc ${TEST_DIR}/lt_u.wasm)
add_test(NAME test_lt_u COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_lt_u PROPERTY PASS_REGULAR_EXPRESSION "0x5a:i32")

add_test(NAME compile_drop COMMAND wasmc ${TEST_DIR}/drop.wasm)
add_test(NAME test_drop COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_drop PROPERTY PASS_REGULAR_EXPRESSION "0x5a:i32")

add_test(NAME compile_select COMMAND wasmc ${TEST_DIR}/select.wasm)
add_test(NAME test_select COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_select PROPERTY PASS_REGULAR_EXPRESSION "0x2c:i32")

add_test(NAME compile_memory COMMAND wasmc ${TEST_DIR}/memory.wasm)
add_test(NAME test_memory COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_memory PROPERTY PASS_REGULAR_EXPRESSION "0xfa:i32")

add_test(NAME compile_sum COMMAND wasmc ${TEST_DIR}/sum.wasm)
add_test(NAME test_sum COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_sum PROPERTY PASS_REGULAR_EXPRESSION "0x15:i32")

add_test(NAME compile_mean COMMAND wasmc ${TEST_DIR}/mean.wasm)
add_test(NAME test_mean COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_mean PROPERTY PASS_REGULAR_EXPRESSION "0x5:i32")

add_test(NAME compile_data1 COMMAND wasmc ${TEST_DIR}/data1.wasm)
add_test(NAME test_data1 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_data1 PROPERTY PASS_REGULAR_EXPRESSION "0xa:i32")

add_test(NAME compile_data2 COMMAND wasmc ${TEST_DIR}/data2.wasm)
add_test(NAME test_data2 COMMAND iwasm-2.4.4 -f main a.aot)
set_property(TEST test_data2 PROPERTY PASS_REGULAR_EXPRESSION "0xa:i32")

add_test(NAME compile_mean-even COMMAND wasmc ${TEST_DIR}/mean-even.wasm)
add_test(NAME test_mean-even COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_mean-even PROPERTY PASS_REGULAR_EXPRESSION "0x13:i32")

add_test(NAME compile_max COMMAND wasmc ${TEST_DIR}/max.wasm)
add_test(NAME test_max COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_max PROPERTY PASS_REGULAR_EXPRESSION "0xff:i32")

add_test(NAME compile_isort COMMAND wasmc ${TEST_DIR}/isort.wasm)
add_test(NAME test_isort COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_isort PROPERTY PASS_REGULAR_EXPRESSION "0x0:i32")

add_test(NAME compile_fib2 COMMAND wasmc ${TEST_DIR}/fib2.wasm)
add_test(NAME test_fib2 COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_fib2 PROPERTY PASS_REGULAR_EXPRESSION "0x59:i32")

add_test(NAME compile_msort COMMAND wasmc ${TEST_DIR}/msort.wasm)
add_test(NAME test_msort COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_msort PROPERTY PASS_REGULAR_EXPRESSION "0x15:i32")

add_test(NAME compile_qsort COMMAND wasmc ${TEST_DIR}/qsort.wasm)
add_test(NAME test_qsort COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_qsort PROPERTY PASS_REGULAR_EXPRESSION "0x66:i32")

add_test(NAME compile_ackermann COMMAND wasmc ${TEST_DIR}/ackermann.wasm)
add_test(NAME test_ackermann COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_ackermann PROPERTY PASS_REGULAR_EXPRESSION "0x3fd:i32")

add_test(NAME compile_base64 COMMAND wasmc ${TEST_DIR}/base64.wasm)
add_test(NAME test_base64 COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_base64 PROPERTY PASS_REGULAR_EXPRESSION "0x2b:i32")

add_test(NAME compile_heapsort COMMAND wasmc ${TEST_DIR}/heapsort.wasm)
add_test(NAME test_heapsort COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_heapsort PROPERTY PASS_REGULAR_EXPRESSION "0x22291:i32")

add_test(NAME compile_matrix COMMAND wasmc ${TEST_DIR}/matrix.wasm)
add_test(NAME test_matrix COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_matrix PROPERTY PASS_REGULAR_EXPRESSION "0x2255727:i32")

add_test(NAME compile_sieve COMMAND wasmc ${TEST_DIR}/sieve.wasm)
add_test(NAME test_sieve COMMAND iwasm-2.4.4 -f main a.aot 0 0)
set_property(TEST test_sieve PROPERTY PASS_REGULAR_EXPRESSION "0x10aa9a0:i32")

