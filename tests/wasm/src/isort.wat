(module
  (type (;0;) (func))
  (type (;1;) (func (param i32 i32)))
  (type (;2;) (func (result i32)))
  (type (;3;) (func (param i32 i32) (result i32)))
  (func $__wasm_call_ctors (type 0))
  (func $insertionSort (type 1) (param i32 i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 2
    i32.const 32
    local.set 3
    local.get 2
    local.get 3
    i32.sub
    local.set 4
    local.get 4
    local.get 0
    i32.store offset=28
    local.get 4
    local.get 1
    i32.store offset=24
    i32.const 1
    local.set 5
    local.get 4
    local.get 5
    i32.store offset=20
    block  ;; label = @1
      loop  ;; label = @2
        local.get 4
        i32.load offset=20
        local.set 6
        local.get 4
        i32.load offset=24
        local.set 7
        local.get 6
        local.set 8
        local.get 7
        local.set 9
        local.get 8
        local.get 9
        i32.lt_s
        local.set 10
        i32.const 1
        local.set 11
        local.get 10
        local.get 11
        i32.and
        local.set 12
        local.get 12
        i32.eqz
        br_if 1 (;@1;)
        local.get 4
        i32.load offset=28
        local.set 13
        local.get 4
        i32.load offset=20
        local.set 14
        i32.const 2
        local.set 15
        local.get 14
        local.get 15
        i32.shl
        local.set 16
        local.get 13
        local.get 16
        i32.add
        local.set 17
        local.get 17
        i32.load
        local.set 18
        local.get 4
        local.get 18
        i32.store offset=16
        local.get 4
        i32.load offset=20
        local.set 19
        i32.const 1
        local.set 20
        local.get 19
        local.get 20
        i32.sub
        local.set 21
        local.get 4
        local.get 21
        i32.store offset=12
        loop  ;; label = @3
          local.get 4
          i32.load offset=12
          local.set 22
          i32.const 0
          local.set 23
          local.get 22
          local.set 24
          local.get 23
          local.set 25
          local.get 24
          local.get 25
          i32.ge_s
          local.set 26
          i32.const 0
          local.set 27
          i32.const 1
          local.set 28
          local.get 26
          local.get 28
          i32.and
          local.set 29
          local.get 27
          local.set 30
          block  ;; label = @4
            local.get 29
            i32.eqz
            br_if 0 (;@4;)
            local.get 4
            i32.load offset=28
            local.set 31
            local.get 4
            i32.load offset=12
            local.set 32
            i32.const 2
            local.set 33
            local.get 32
            local.get 33
            i32.shl
            local.set 34
            local.get 31
            local.get 34
            i32.add
            local.set 35
            local.get 35
            i32.load
            local.set 36
            local.get 4
            i32.load offset=16
            local.set 37
            local.get 36
            local.set 38
            local.get 37
            local.set 39
            local.get 38
            local.get 39
            i32.gt_s
            local.set 40
            local.get 40
            local.set 30
          end
          local.get 30
          local.set 41
          i32.const 1
          local.set 42
          local.get 41
          local.get 42
          i32.and
          local.set 43
          block  ;; label = @4
            local.get 43
            i32.eqz
            br_if 0 (;@4;)
            local.get 4
            i32.load offset=28
            local.set 44
            local.get 4
            i32.load offset=12
            local.set 45
            i32.const 2
            local.set 46
            local.get 45
            local.get 46
            i32.shl
            local.set 47
            local.get 44
            local.get 47
            i32.add
            local.set 48
            local.get 48
            i32.load
            local.set 49
            local.get 4
            i32.load offset=28
            local.set 50
            local.get 4
            i32.load offset=12
            local.set 51
            i32.const 1
            local.set 52
            local.get 51
            local.get 52
            i32.add
            local.set 53
            i32.const 2
            local.set 54
            local.get 53
            local.get 54
            i32.shl
            local.set 55
            local.get 50
            local.get 55
            i32.add
            local.set 56
            local.get 56
            local.get 49
            i32.store
            local.get 4
            i32.load offset=12
            local.set 57
            i32.const 1
            local.set 58
            local.get 57
            local.get 58
            i32.sub
            local.set 59
            local.get 4
            local.get 59
            i32.store offset=12
            br 1 (;@3;)
          end
        end
        local.get 4
        i32.load offset=16
        local.set 60
        local.get 4
        i32.load offset=28
        local.set 61
        local.get 4
        i32.load offset=12
        local.set 62
        i32.const 1
        local.set 63
        local.get 62
        local.get 63
        i32.add
        local.set 64
        i32.const 2
        local.set 65
        local.get 64
        local.get 65
        i32.shl
        local.set 66
        local.get 61
        local.get 66
        i32.add
        local.set 67
        local.get 67
        local.get 60
        i32.store
        local.get 4
        i32.load offset=20
        local.set 68
        i32.const 1
        local.set 69
        local.get 68
        local.get 69
        i32.add
        local.set 70
        local.get 4
        local.get 70
        i32.store offset=20
        br 0 (;@2;)
      end
    end
    return)
  (func $__original_main (type 2) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 0
    i32.const 16
    local.set 1
    local.get 0
    local.get 1
    i32.sub
    local.set 2
    local.get 2
    global.set $__stack_pointer
    i32.const 0
    local.set 3
    local.get 2
    local.get 3
    i32.store offset=12
    i32.const 1024
    local.set 4
    i32.const 10
    local.set 5
    local.get 4
    local.get 5
    call $insertionSort
    i32.const 0
    local.set 6
    local.get 6
    i32.load offset=1024
    local.set 7
    i32.const 16
    local.set 8
    local.get 2
    local.get 8
    i32.add
    local.set 9
    local.get 9
    global.set $__stack_pointer
    local.get 7
    return)
  (func $main (type 3) (param i32 i32) (result i32)
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
  (global (;4;) i32 (i32.const 1024))
  (global (;5;) i32 (i32.const 66608))
  (global (;6;) i32 (i32.const 0))
  (global (;7;) i32 (i32.const 1))
  (export "memory" (memory 0))
  (export "__wasm_call_ctors" (func $__wasm_call_ctors))
  (export "insertionSort" (func $insertionSort))
  (export "__original_main" (func $__original_main))
  (export "x" (global 1))
  (export "main" (func $main))
  (export "__main_void" (func $__original_main))
  (export "__indirect_function_table" (table 0))
  (export "__dso_handle" (global 2))
  (export "__data_end" (global 3))
  (export "__global_base" (global 4))
  (export "__heap_base" (global 5))
  (export "__memory_base" (global 6))
  (export "__table_base" (global 7))
  (data $.data (i32.const 1024) "\0a\00\00\00\22\00\00\00{\00\00\00W\09\00\00\17\00\00\00\00\00\00\00\15\00\00\00C\00\00\00 \00\00\00W\00\00\00"))
