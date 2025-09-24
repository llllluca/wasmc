(module
  (type (;0;) (func))
  (type (;1;) (func (param i32 i32)))
  (type (;2;) (func (param i32 i32 i32) (result i32)))
  (type (;3;) (func (param i32 i32 i32)))
  (type (;4;) (func (result i32)))
  (type (;5;) (func (param i32 i32) (result i32)))
  (func $__wasm_call_ctors (type 0))
  (func $swap (type 1) (param i32 i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 2
    i32.const 16
    local.set 3
    local.get 2
    local.get 3
    i32.sub
    local.set 4
    local.get 4
    local.get 0
    i32.store offset=12
    local.get 4
    local.get 1
    i32.store offset=8
    local.get 4
    i32.load offset=12
    local.set 5
    local.get 5
    i32.load
    local.set 6
    local.get 4
    local.get 6
    i32.store offset=4
    local.get 4
    i32.load offset=8
    local.set 7
    local.get 7
    i32.load
    local.set 8
    local.get 4
    i32.load offset=12
    local.set 9
    local.get 9
    local.get 8
    i32.store
    local.get 4
    i32.load offset=4
    local.set 10
    local.get 4
    i32.load offset=8
    local.set 11
    local.get 11
    local.get 10
    i32.store
    return)
  (func $partition (type 2) (param i32 i32 i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 3
    i32.const 32
    local.set 4
    local.get 3
    local.get 4
    i32.sub
    local.set 5
    local.get 5
    global.set $__stack_pointer
    local.get 5
    local.get 0
    i32.store offset=28
    local.get 5
    local.get 1
    i32.store offset=24
    local.get 5
    local.get 2
    i32.store offset=20
    local.get 5
    i32.load offset=28
    local.set 6
    local.get 5
    i32.load offset=24
    local.set 7
    i32.const 2
    local.set 8
    local.get 7
    local.get 8
    i32.shl
    local.set 9
    local.get 6
    local.get 9
    i32.add
    local.set 10
    local.get 10
    i32.load
    local.set 11
    local.get 5
    local.get 11
    i32.store offset=16
    local.get 5
    i32.load offset=24
    local.set 12
    local.get 5
    local.get 12
    i32.store offset=12
    local.get 5
    i32.load offset=20
    local.set 13
    local.get 5
    local.get 13
    i32.store offset=8
    block  ;; label = @1
      loop  ;; label = @2
        local.get 5
        i32.load offset=12
        local.set 14
        local.get 5
        i32.load offset=8
        local.set 15
        local.get 14
        local.set 16
        local.get 15
        local.set 17
        local.get 16
        local.get 17
        i32.lt_s
        local.set 18
        i32.const 1
        local.set 19
        local.get 18
        local.get 19
        i32.and
        local.set 20
        local.get 20
        i32.eqz
        br_if 1 (;@1;)
        loop  ;; label = @3
          local.get 5
          i32.load offset=28
          local.set 21
          local.get 5
          i32.load offset=12
          local.set 22
          i32.const 2
          local.set 23
          local.get 22
          local.get 23
          i32.shl
          local.set 24
          local.get 21
          local.get 24
          i32.add
          local.set 25
          local.get 25
          i32.load
          local.set 26
          local.get 5
          i32.load offset=16
          local.set 27
          local.get 26
          local.set 28
          local.get 27
          local.set 29
          local.get 28
          local.get 29
          i32.le_s
          local.set 30
          i32.const 0
          local.set 31
          i32.const 1
          local.set 32
          local.get 30
          local.get 32
          i32.and
          local.set 33
          local.get 31
          local.set 34
          block  ;; label = @4
            local.get 33
            i32.eqz
            br_if 0 (;@4;)
            local.get 5
            i32.load offset=12
            local.set 35
            local.get 5
            i32.load offset=20
            local.set 36
            i32.const 1
            local.set 37
            local.get 36
            local.get 37
            i32.sub
            local.set 38
            local.get 35
            local.set 39
            local.get 38
            local.set 40
            local.get 39
            local.get 40
            i32.le_s
            local.set 41
            local.get 41
            local.set 34
          end
          local.get 34
          local.set 42
          i32.const 1
          local.set 43
          local.get 42
          local.get 43
          i32.and
          local.set 44
          block  ;; label = @4
            local.get 44
            i32.eqz
            br_if 0 (;@4;)
            local.get 5
            i32.load offset=12
            local.set 45
            i32.const 1
            local.set 46
            local.get 45
            local.get 46
            i32.add
            local.set 47
            local.get 5
            local.get 47
            i32.store offset=12
            br 1 (;@3;)
          end
        end
        loop  ;; label = @3
          local.get 5
          i32.load offset=28
          local.set 48
          local.get 5
          i32.load offset=8
          local.set 49
          i32.const 2
          local.set 50
          local.get 49
          local.get 50
          i32.shl
          local.set 51
          local.get 48
          local.get 51
          i32.add
          local.set 52
          local.get 52
          i32.load
          local.set 53
          local.get 5
          i32.load offset=16
          local.set 54
          local.get 53
          local.set 55
          local.get 54
          local.set 56
          local.get 55
          local.get 56
          i32.gt_s
          local.set 57
          i32.const 0
          local.set 58
          i32.const 1
          local.set 59
          local.get 57
          local.get 59
          i32.and
          local.set 60
          local.get 58
          local.set 61
          block  ;; label = @4
            local.get 60
            i32.eqz
            br_if 0 (;@4;)
            local.get 5
            i32.load offset=8
            local.set 62
            local.get 5
            i32.load offset=24
            local.set 63
            i32.const 1
            local.set 64
            local.get 63
            local.get 64
            i32.add
            local.set 65
            local.get 62
            local.set 66
            local.get 65
            local.set 67
            local.get 66
            local.get 67
            i32.ge_s
            local.set 68
            local.get 68
            local.set 61
          end
          local.get 61
          local.set 69
          i32.const 1
          local.set 70
          local.get 69
          local.get 70
          i32.and
          local.set 71
          block  ;; label = @4
            local.get 71
            i32.eqz
            br_if 0 (;@4;)
            local.get 5
            i32.load offset=8
            local.set 72
            i32.const -1
            local.set 73
            local.get 72
            local.get 73
            i32.add
            local.set 74
            local.get 5
            local.get 74
            i32.store offset=8
            br 1 (;@3;)
          end
        end
        local.get 5
        i32.load offset=12
        local.set 75
        local.get 5
        i32.load offset=8
        local.set 76
        local.get 75
        local.set 77
        local.get 76
        local.set 78
        local.get 77
        local.get 78
        i32.lt_s
        local.set 79
        i32.const 1
        local.set 80
        local.get 79
        local.get 80
        i32.and
        local.set 81
        block  ;; label = @3
          local.get 81
          i32.eqz
          br_if 0 (;@3;)
          local.get 5
          i32.load offset=28
          local.set 82
          local.get 5
          i32.load offset=12
          local.set 83
          i32.const 2
          local.set 84
          local.get 83
          local.get 84
          i32.shl
          local.set 85
          local.get 82
          local.get 85
          i32.add
          local.set 86
          local.get 5
          i32.load offset=28
          local.set 87
          local.get 5
          i32.load offset=8
          local.set 88
          i32.const 2
          local.set 89
          local.get 88
          local.get 89
          i32.shl
          local.set 90
          local.get 87
          local.get 90
          i32.add
          local.set 91
          local.get 86
          local.get 91
          call $swap
        end
        br 0 (;@2;)
      end
    end
    local.get 5
    i32.load offset=28
    local.set 92
    local.get 5
    i32.load offset=24
    local.set 93
    i32.const 2
    local.set 94
    local.get 93
    local.get 94
    i32.shl
    local.set 95
    local.get 92
    local.get 95
    i32.add
    local.set 96
    local.get 5
    i32.load offset=28
    local.set 97
    local.get 5
    i32.load offset=8
    local.set 98
    i32.const 2
    local.set 99
    local.get 98
    local.get 99
    i32.shl
    local.set 100
    local.get 97
    local.get 100
    i32.add
    local.set 101
    local.get 96
    local.get 101
    call $swap
    local.get 5
    i32.load offset=8
    local.set 102
    i32.const 32
    local.set 103
    local.get 5
    local.get 103
    i32.add
    local.set 104
    local.get 104
    global.set $__stack_pointer
    local.get 102
    return)
  (func $_quickSort (type 3) (param i32 i32 i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 3
    i32.const 16
    local.set 4
    local.get 3
    local.get 4
    i32.sub
    local.set 5
    local.get 5
    global.set $__stack_pointer
    local.get 5
    local.get 0
    i32.store offset=12
    local.get 5
    local.get 1
    i32.store offset=8
    local.get 5
    local.get 2
    i32.store offset=4
    local.get 5
    i32.load offset=8
    local.set 6
    local.get 5
    i32.load offset=4
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
    block  ;; label = @1
      local.get 12
      i32.eqz
      br_if 0 (;@1;)
      local.get 5
      i32.load offset=12
      local.set 13
      local.get 5
      i32.load offset=8
      local.set 14
      local.get 5
      i32.load offset=4
      local.set 15
      local.get 13
      local.get 14
      local.get 15
      call $partition
      local.set 16
      local.get 5
      local.get 16
      i32.store
      local.get 5
      i32.load offset=12
      local.set 17
      local.get 5
      i32.load offset=8
      local.set 18
      local.get 5
      i32.load
      local.set 19
      i32.const 1
      local.set 20
      local.get 19
      local.get 20
      i32.sub
      local.set 21
      local.get 17
      local.get 18
      local.get 21
      call $_quickSort
      local.get 5
      i32.load offset=12
      local.set 22
      local.get 5
      i32.load
      local.set 23
      i32.const 1
      local.set 24
      local.get 23
      local.get 24
      i32.add
      local.set 25
      local.get 5
      i32.load offset=4
      local.set 26
      local.get 22
      local.get 25
      local.get 26
      call $_quickSort
    end
    i32.const 16
    local.set 27
    local.get 5
    local.get 27
    i32.add
    local.set 28
    local.get 28
    global.set $__stack_pointer
    return)
  (func $quickSort (type 1) (param i32 i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 2
    i32.const 16
    local.set 3
    local.get 2
    local.get 3
    i32.sub
    local.set 4
    local.get 4
    global.set $__stack_pointer
    local.get 4
    local.get 0
    i32.store offset=12
    local.get 4
    local.get 1
    i32.store offset=8
    local.get 4
    i32.load offset=12
    local.set 5
    local.get 4
    i32.load offset=8
    local.set 6
    i32.const 1
    local.set 7
    local.get 6
    local.get 7
    i32.sub
    local.set 8
    i32.const 0
    local.set 9
    local.get 5
    local.get 9
    local.get 8
    call $_quickSort
    i32.const 16
    local.set 10
    local.get 4
    local.get 10
    i32.add
    local.set 11
    local.get 11
    global.set $__stack_pointer
    return)
  (func $__original_main (type 4) (result i32)
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
    i32.const 337
    local.set 5
    local.get 4
    local.get 5
    call $quickSort
    i32.const 0
    local.set 6
    local.get 6
    i32.load offset=2364
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
  (func $main (type 5) (param i32 i32) (result i32)
    (local i32)
    call $__original_main
    local.set 2
    local.get 2
    return)
  (table (;0;) 1 1 funcref)
  (memory (;0;) 2)
  (global $__stack_pointer (mut i32) (i32.const 67920))
  (global (;1;) i32 (i32.const 1024))
  (global (;2;) i32 (i32.const 1024))
  (global (;3;) i32 (i32.const 2372))
  (global (;4;) i32 (i32.const 1024))
  (global (;5;) i32 (i32.const 67920))
  (global (;6;) i32 (i32.const 0))
  (global (;7;) i32 (i32.const 1))
  (export "memory" (memory 0))
  (export "__wasm_call_ctors" (func $__wasm_call_ctors))
  (export "swap" (func $swap))
  (export "partition" (func $partition))
  (export "_quickSort" (func $_quickSort))
  (export "quickSort" (func $quickSort))
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
  (data $.data (i32.const 1024) "\13\00\00\00\0c\00\00\00\a7\01\00\00\17\00\00\008\00\00\00N\00\00\00\22\00\00\00Z\00\00\00\05\00\00\00C\00\00\00f\00\00\00\0c\00\00\00-\00\00\00X\00\00\00\03\00\00\00L\00\00\00\15\00\00\00\00\00\00\00'\00\00\00,\00\00\00\0f\00\00\00C\00\00\00Y\00\00\00\16\00\00\00!\00\00\006\00\00\00\0b\00\00\00\09\00\00\00\1b\00\00\000\00\00\00H\00\00\00\0f\00\00\00<\00\00\00c\00\00\00\1e\00\00\00\0e\00\00\00S\00\00\00\01\00\00\00\19\00\00\00%\00\00\00\5c\00\00\002\00\00\00\08\00\00\00B\00\00\00)\00\00\00\12\00\00\00I\00\00\00\14\00\00\00\05\00\00\00\1f\00\00\00\07\00\00\001\00\00\00@\00\00\00\0b\00\00\00&\00\00\00V\00\00\00\04\00\00\00\11\00\00\007\00\00\00d\00\00\00\1d\00\00\00\0d\00\00\00*\00\00\00[\00\00\00\13\00\00\00\18\00\00\00D\00\00\00\06\00\00\00P\00\00\00\03\00\00\009\00\00\00\10\00\00\00\1e\00\00\00J\00\00\00\02\00\00\00.\00\00\00R\00\00\00\19\00\00\00\0a\00\00\00?\00\00\00\01\00\00\00#\00\00\00\08\00\00\00K\00\00\00\1a\00\00\00;\00\00\00\0e\00\00\00 \00\00\00W\00\00\00\14\00\00\00\04\00\00\00F\00\00\00#\00\00\00\0c\00\00\00A\00\00\00\09\00\00\00\12\00\00\005\00\00\00\1b\00\00\00^\00\00\00Q\00\00\00(\00\00\00\02\00\00\00G\00\00\00!\00\00\00,\00\00\00_\00\00\00\11\00\00\00\17\00\00\00:\00\00\00\0a\00\00\00a\00\00\00\0f\00\00\00\18\00\00\00E\00\00\00\0c\00\00\00$\00\00\00\0d\00\00\00N\00\00\00c\00\00\00\03\00\00\00X\00\00\00.\00\00\00\15\00\00\00>\00\00\00\1d\00\00\00\1c\00\00\00T\00\00\00\07\00\00\00\0c\00\00\00]\00\00\00\10\00\00\00\05\00\00\00/\00\00\00O\00\00\00\22\00\00\00\14\00\00\00\0b\00\00\00\08\00\00\00'\00\00\00Z\00\00\00\0f\00\00\00\01\00\00\00B\00\00\00\02\00\00\006\00\00\00H\00\00\00\13\00\00\00!\00\00\00U\00\00\00\06\00\00\00\0a\00\00\00)\00\00\00\0b\00\00\00L\00\00\00\18\00\00\00\12\00\00\00<\00\00\00\05\00\00\00\07\00\00\00%\00\00\00\5c\00\00\00\04\00\00\00\0e\00\00\00D\00\00\00\01\00\00\001\00\00\00P\00\00\00\1e\00\00\00\03\00\00\00:\00\00\00\19\00\00\00\16\00\00\00c\00\00\00\0e\00\00\00\11\00\00\00I\00\00\00\09\00\00\00\0c\00\00\00,\00\00\00\14\00\00\00\08\00\00\00=\00\00\00\03\00\00\00#\00\00\00Z\00\00\00\10\00\00\00\13\00\00\00M\00\00\00\0c\00\00\00\02\00\00\00@\00\00\00\05\00\00\002\00\00\00R\00\00\00\16\00\00\00\0b\00\00\00'\00\00\00\08\00\00\00\1b\00\00\00-\00\00\00\0a\00\00\00F\00\00\00\01\00\00\00\0f\00\00\00$\00\00\00X\00\00\00\04\00\00\00\12\00\00\006\00\00\00\13\00\00\00\09\00\00\00?\00\00\00\07\00\00\00\1d\00\00\00K\00\00\00\02\00\00\00*\00\00\00[\00\00\00\0b\00\00\00\14\00\00\00B\00\00\00\03\00\00\00&\00\00\00T\00\00\00\0e\00\00\00\0c\00\00\009\00\00\00\08\00\00\00.\00\00\00c\00\00\00\05\00\00\00\0f\00\00\00I\00\00\00\14\00\00\00\04\00\00\00<\00\00\00\01\00\00\00 \00\00\00N\00\00\00\09\00\00\00\15\00\00\00X\00\00\00\0c\00\00\00\0a\00\00\00)\00\00\00\06\00\00\00'\00\00\00\5c\00\00\00\03\00\00\00\0e\00\00\00D\00\00\00\01\00\00\001\00\00\00P\00\00\00\1e\00\00\00\03\00\00\00:\00\00\00\19\00\00\00\16\00\00\00c\00\00\00\0e\00\00\00\11\00\00\00I\00\00\00\09\00\00\00\0c\00\00\00,\00\00\00\14\00\00\00\08\00\00\00=\00\00\00\03\00\00\00#\00\00\00Z\00\00\00\10\00\00\00\13\00\00\00M\00\00\00\0c\00\00\00\02\00\00\00@\00\00\00\05\00\00\002\00\00\00R\00\00\00\16\00\00\00\0b\00\00\00'\00\00\00\08\00\00\00\1b\00\00\00-\00\00\00\0a\00\00\00F\00\00\00\01\00\00\00\0f\00\00\00$\00\00\00X\00\00\00\04\00\00\00\12\00\00\006\00\00\00\13\00\00\00\09\00\00\00?\00\00\00\07\00\00\00\1d\00\00\00K\00\00\00\02\00\00\00*\00\00\00[\00\00\00\0b\00\00\00\14\00\00\00B\00\00\00\03\00\00\00&\00\00\00T\00\00\00\0e\00\00\00\0c\00\00\009\00\00\00\08\00\00\00.\00\00\00c\00\00\00\05\00\00\00\0f\00\00\00I\00\00\00\14\00\00\00\04\00\00\00<\00\00\00\01\00\00\00 \00\00\00N\00\00\00\09\00\00\00\15\00\00\00X\00\00\00\0c\00\00\00\0a\00\00\00)\00\00\00\06\00\00\00'\00\00\00\5c\00\00\00\03\00\00\00"))
