(module $mean.wasm
  (type (;0;) (func))
  (type (;1;) (func (result i32)))
  (type (;2;) (func (param i32 i32) (result i32)))
  (func $__wasm_call_ctors (type 0))
  (func $__original_main (type 1) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 0
    i32.const 16
    local.set 1
    local.get 0
    local.get 1
    i32.sub
    local.set 2
    i32.const 0
    local.set 3
    local.get 2
    local.get 3
    i32.store offset=12
    i32.const 0
    local.set 4
    local.get 2
    local.get 4
    i32.store offset=8
    block  ;; label = @1
      loop  ;; label = @2
        local.get 2
        i32.load offset=8
        local.set 5
        i32.const 10
        local.set 6
        local.get 5
        local.get 6
        i32.lt_u
        local.set 7
        i32.const 1
        local.set 8
        local.get 7
        local.get 8
        i32.and
        local.set 9
        local.get 9
        i32.eqz
        br_if 1 (;@1;)
        local.get 2
        i32.load offset=8
        local.set 10
        i32.const 1
        local.set 11
        local.get 10
        local.get 11
        i32.add
        local.set 12
        local.get 2
        i32.load offset=8
        local.set 13
        i32.const 1024
        local.set 14
        i32.const 2
        local.set 15
        local.get 13
        local.get 15
        i32.shl
        local.set 16
        local.get 14
        local.get 16
        i32.add
        local.set 17
        local.get 17
        local.get 12
        i32.store
        local.get 2
        i32.load offset=8
        local.set 18
        i32.const 1
        local.set 19
        local.get 18
        local.get 19
        i32.add
        local.set 20
        local.get 2
        local.get 20
        i32.store offset=8
        br 0 (;@2;)
      end
    end
    i32.const 0
    local.set 21
    local.get 2
    local.get 21
    i32.store offset=4
    i32.const 0
    local.set 22
    local.get 2
    local.get 22
    i32.store
    block  ;; label = @1
      loop  ;; label = @2
        local.get 2
        i32.load
        local.set 23
        i32.const 10
        local.set 24
        local.get 23
        local.get 24
        i32.lt_u
        local.set 25
        i32.const 1
        local.set 26
        local.get 25
        local.get 26
        i32.and
        local.set 27
        local.get 27
        i32.eqz
        br_if 1 (;@1;)
        local.get 2
        i32.load
        local.set 28
        i32.const 1024
        local.set 29
        i32.const 2
        local.set 30
        local.get 28
        local.get 30
        i32.shl
        local.set 31
        local.get 29
        local.get 31
        i32.add
        local.set 32
        local.get 32
        i32.load
        local.set 33
        local.get 2
        i32.load offset=4
        local.set 34
        local.get 34
        local.get 33
        i32.add
        local.set 35
        local.get 2
        local.get 35
        i32.store offset=4
        local.get 2
        i32.load
        local.set 36
        i32.const 1
        local.set 37
        local.get 36
        local.get 37
        i32.add
        local.set 38
        local.get 2
        local.get 38
        i32.store
        br 0 (;@2;)
      end
    end
    local.get 2
    i32.load offset=4
    local.set 39
    i32.const 10
    local.set 40
    local.get 39
    local.get 40
    i32.div_s
    local.set 41
    local.get 41
    return)
  (func $main (type 2) (param i32 i32) (result i32)
    (local i32)
    call $__original_main
    local.set 2
    local.get 2
    return)
  (table (;0;) 1 1 funcref)
  (memory (;0;) 2)
  (global $__stack_pointer (mut i32) (i32.const 66608))
  (global (;1;) i32 (i32.const 1024))
  (global (;2;) i32 (i32.const 1024))
  (global (;3;) i32 (i32.const 1064))
  (global (;4;) i32 (i32.const 1072))
  (global (;5;) i32 (i32.const 66608))
  (global (;6;) i32 (i32.const 1024))
  (global (;7;) i32 (i32.const 66608))
  (global (;8;) i32 (i32.const 131072))
  (global (;9;) i32 (i32.const 0))
  (global (;10;) i32 (i32.const 1))
  (export "memory" (memory 0))
  (export "__wasm_call_ctors" (func $__wasm_call_ctors))
  (export "__original_main" (func $__original_main))
  (export "x" (global 1))
  (export "main" (func $main))
  (export "__main_void" (func $__original_main))
  (export "__indirect_function_table" (table 0))
  (export "__dso_handle" (global 2))
  (export "__data_end" (global 3))
  (export "__stack_low" (global 4))
  (export "__stack_high" (global 5))
  (export "__global_base" (global 6))
  (export "__heap_base" (global 7))
  (export "__heap_end" (global 8))
  (export "__memory_base" (global 9))
  (export "__table_base" (global 10)))
