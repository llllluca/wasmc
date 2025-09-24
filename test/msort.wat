(module
  (type (;0;) (func))
  (type (;1;) (func (param i32 i32 i32 i32)))
  (type (;2;) (func (param i32 i32 i32)))
  (type (;3;) (func (param i32 i32)))
  (type (;4;) (func (result i32)))
  (type (;5;) (func (param i32 i32) (result i32)))
  (func $__wasm_call_ctors (type 0))
  (func $merge (type 1) (param i32 i32 i32 i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 4
    i32.const 48
    local.set 5
    local.get 4
    local.get 5
    i32.sub
    local.set 6
    local.get 6
    local.set 7
    local.get 7
    local.get 0
    i32.store offset=44
    local.get 7
    local.get 1
    i32.store offset=40
    local.get 7
    local.get 2
    i32.store offset=36
    local.get 7
    local.get 3
    i32.store offset=32
    local.get 7
    i32.load offset=36
    local.set 8
    local.get 7
    i32.load offset=40
    local.set 9
    local.get 8
    local.get 9
    i32.sub
    local.set 10
    i32.const 1
    local.set 11
    local.get 10
    local.get 11
    i32.add
    local.set 12
    local.get 7
    local.get 12
    i32.store offset=16
    local.get 7
    i32.load offset=32
    local.set 13
    local.get 7
    i32.load offset=36
    local.set 14
    local.get 13
    local.get 14
    i32.sub
    local.set 15
    local.get 7
    local.get 15
    i32.store offset=12
    local.get 7
    i32.load offset=16
    local.set 16
    local.get 6
    local.set 17
    local.get 7
    local.get 17
    i32.store offset=8
    i32.const 2
    local.set 18
    local.get 16
    local.get 18
    i32.shl
    local.set 19
    i32.const 15
    local.set 20
    local.get 19
    local.get 20
    i32.add
    local.set 21
    i32.const -16
    local.set 22
    local.get 21
    local.get 22
    i32.and
    local.set 23
    local.get 6
    local.set 24
    local.get 24
    local.get 23
    i32.sub
    local.set 25
    local.get 25
    local.set 6
    local.get 7
    local.get 16
    i32.store offset=4
    local.get 7
    i32.load offset=12
    local.set 26
    local.get 26
    local.get 18
    i32.shl
    local.set 27
    local.get 27
    local.get 20
    i32.add
    local.set 28
    local.get 28
    local.get 22
    i32.and
    local.set 29
    local.get 6
    local.set 30
    local.get 30
    local.get 29
    i32.sub
    local.set 31
    local.get 31
    local.set 6
    local.get 7
    local.get 26
    i32.store
    i32.const 0
    local.set 32
    local.get 7
    local.get 32
    i32.store offset=28
    block  ;; label = @1
      loop  ;; label = @2
        local.get 7
        i32.load offset=28
        local.set 33
        local.get 7
        i32.load offset=16
        local.set 34
        local.get 33
        local.set 35
        local.get 34
        local.set 36
        local.get 35
        local.get 36
        i32.lt_s
        local.set 37
        i32.const 1
        local.set 38
        local.get 37
        local.get 38
        i32.and
        local.set 39
        local.get 39
        i32.eqz
        br_if 1 (;@1;)
        local.get 7
        i32.load offset=44
        local.set 40
        local.get 7
        i32.load offset=40
        local.set 41
        local.get 7
        i32.load offset=28
        local.set 42
        local.get 41
        local.get 42
        i32.add
        local.set 43
        i32.const 2
        local.set 44
        local.get 43
        local.get 44
        i32.shl
        local.set 45
        local.get 40
        local.get 45
        i32.add
        local.set 46
        local.get 46
        i32.load
        local.set 47
        local.get 7
        i32.load offset=28
        local.set 48
        i32.const 2
        local.set 49
        local.get 48
        local.get 49
        i32.shl
        local.set 50
        local.get 25
        local.get 50
        i32.add
        local.set 51
        local.get 51
        local.get 47
        i32.store
        local.get 7
        i32.load offset=28
        local.set 52
        i32.const 1
        local.set 53
        local.get 52
        local.get 53
        i32.add
        local.set 54
        local.get 7
        local.get 54
        i32.store offset=28
        br 0 (;@2;)
      end
    end
    i32.const 0
    local.set 55
    local.get 7
    local.get 55
    i32.store offset=24
    block  ;; label = @1
      loop  ;; label = @2
        local.get 7
        i32.load offset=24
        local.set 56
        local.get 7
        i32.load offset=12
        local.set 57
        local.get 56
        local.set 58
        local.get 57
        local.set 59
        local.get 58
        local.get 59
        i32.lt_s
        local.set 60
        i32.const 1
        local.set 61
        local.get 60
        local.get 61
        i32.and
        local.set 62
        local.get 62
        i32.eqz
        br_if 1 (;@1;)
        local.get 7
        i32.load offset=44
        local.set 63
        local.get 7
        i32.load offset=36
        local.set 64
        i32.const 1
        local.set 65
        local.get 64
        local.get 65
        i32.add
        local.set 66
        local.get 7
        i32.load offset=24
        local.set 67
        local.get 66
        local.get 67
        i32.add
        local.set 68
        i32.const 2
        local.set 69
        local.get 68
        local.get 69
        i32.shl
        local.set 70
        local.get 63
        local.get 70
        i32.add
        local.set 71
        local.get 71
        i32.load
        local.set 72
        local.get 7
        i32.load offset=24
        local.set 73
        i32.const 2
        local.set 74
        local.get 73
        local.get 74
        i32.shl
        local.set 75
        local.get 31
        local.get 75
        i32.add
        local.set 76
        local.get 76
        local.get 72
        i32.store
        local.get 7
        i32.load offset=24
        local.set 77
        i32.const 1
        local.set 78
        local.get 77
        local.get 78
        i32.add
        local.set 79
        local.get 7
        local.get 79
        i32.store offset=24
        br 0 (;@2;)
      end
    end
    i32.const 0
    local.set 80
    local.get 7
    local.get 80
    i32.store offset=28
    i32.const 0
    local.set 81
    local.get 7
    local.get 81
    i32.store offset=24
    local.get 7
    i32.load offset=40
    local.set 82
    local.get 7
    local.get 82
    i32.store offset=20
    loop  ;; label = @1
      local.get 7
      i32.load offset=28
      local.set 83
      local.get 7
      i32.load offset=16
      local.set 84
      local.get 83
      local.set 85
      local.get 84
      local.set 86
      local.get 85
      local.get 86
      i32.lt_s
      local.set 87
      i32.const 0
      local.set 88
      i32.const 1
      local.set 89
      local.get 87
      local.get 89
      i32.and
      local.set 90
      local.get 88
      local.set 91
      block  ;; label = @2
        local.get 90
        i32.eqz
        br_if 0 (;@2;)
        local.get 7
        i32.load offset=24
        local.set 92
        local.get 7
        i32.load offset=12
        local.set 93
        local.get 92
        local.set 94
        local.get 93
        local.set 95
        local.get 94
        local.get 95
        i32.lt_s
        local.set 96
        local.get 96
        local.set 91
      end
      local.get 91
      local.set 97
      i32.const 1
      local.set 98
      local.get 97
      local.get 98
      i32.and
      local.set 99
      block  ;; label = @2
        local.get 99
        i32.eqz
        br_if 0 (;@2;)
        local.get 7
        i32.load offset=28
        local.set 100
        i32.const 2
        local.set 101
        local.get 100
        local.get 101
        i32.shl
        local.set 102
        local.get 25
        local.get 102
        i32.add
        local.set 103
        local.get 103
        i32.load
        local.set 104
        local.get 7
        i32.load offset=24
        local.set 105
        i32.const 2
        local.set 106
        local.get 105
        local.get 106
        i32.shl
        local.set 107
        local.get 31
        local.get 107
        i32.add
        local.set 108
        local.get 108
        i32.load
        local.set 109
        local.get 104
        local.set 110
        local.get 109
        local.set 111
        local.get 110
        local.get 111
        i32.le_s
        local.set 112
        i32.const 1
        local.set 113
        local.get 112
        local.get 113
        i32.and
        local.set 114
        block  ;; label = @3
          block  ;; label = @4
            local.get 114
            i32.eqz
            br_if 0 (;@4;)
            local.get 7
            i32.load offset=28
            local.set 115
            i32.const 2
            local.set 116
            local.get 115
            local.get 116
            i32.shl
            local.set 117
            local.get 25
            local.get 117
            i32.add
            local.set 118
            local.get 118
            i32.load
            local.set 119
            local.get 7
            i32.load offset=44
            local.set 120
            local.get 7
            i32.load offset=20
            local.set 121
            i32.const 2
            local.set 122
            local.get 121
            local.get 122
            i32.shl
            local.set 123
            local.get 120
            local.get 123
            i32.add
            local.set 124
            local.get 124
            local.get 119
            i32.store
            local.get 7
            i32.load offset=28
            local.set 125
            i32.const 1
            local.set 126
            local.get 125
            local.get 126
            i32.add
            local.set 127
            local.get 7
            local.get 127
            i32.store offset=28
            br 1 (;@3;)
          end
          local.get 7
          i32.load offset=24
          local.set 128
          i32.const 2
          local.set 129
          local.get 128
          local.get 129
          i32.shl
          local.set 130
          local.get 31
          local.get 130
          i32.add
          local.set 131
          local.get 131
          i32.load
          local.set 132
          local.get 7
          i32.load offset=44
          local.set 133
          local.get 7
          i32.load offset=20
          local.set 134
          i32.const 2
          local.set 135
          local.get 134
          local.get 135
          i32.shl
          local.set 136
          local.get 133
          local.get 136
          i32.add
          local.set 137
          local.get 137
          local.get 132
          i32.store
          local.get 7
          i32.load offset=24
          local.set 138
          i32.const 1
          local.set 139
          local.get 138
          local.get 139
          i32.add
          local.set 140
          local.get 7
          local.get 140
          i32.store offset=24
        end
        local.get 7
        i32.load offset=20
        local.set 141
        i32.const 1
        local.set 142
        local.get 141
        local.get 142
        i32.add
        local.set 143
        local.get 7
        local.get 143
        i32.store offset=20
        br 1 (;@1;)
      end
    end
    block  ;; label = @1
      loop  ;; label = @2
        local.get 7
        i32.load offset=28
        local.set 144
        local.get 7
        i32.load offset=16
        local.set 145
        local.get 144
        local.set 146
        local.get 145
        local.set 147
        local.get 146
        local.get 147
        i32.lt_s
        local.set 148
        i32.const 1
        local.set 149
        local.get 148
        local.get 149
        i32.and
        local.set 150
        local.get 150
        i32.eqz
        br_if 1 (;@1;)
        local.get 7
        i32.load offset=28
        local.set 151
        i32.const 2
        local.set 152
        local.get 151
        local.get 152
        i32.shl
        local.set 153
        local.get 25
        local.get 153
        i32.add
        local.set 154
        local.get 154
        i32.load
        local.set 155
        local.get 7
        i32.load offset=44
        local.set 156
        local.get 7
        i32.load offset=20
        local.set 157
        i32.const 2
        local.set 158
        local.get 157
        local.get 158
        i32.shl
        local.set 159
        local.get 156
        local.get 159
        i32.add
        local.set 160
        local.get 160
        local.get 155
        i32.store
        local.get 7
        i32.load offset=28
        local.set 161
        i32.const 1
        local.set 162
        local.get 161
        local.get 162
        i32.add
        local.set 163
        local.get 7
        local.get 163
        i32.store offset=28
        local.get 7
        i32.load offset=20
        local.set 164
        i32.const 1
        local.set 165
        local.get 164
        local.get 165
        i32.add
        local.set 166
        local.get 7
        local.get 166
        i32.store offset=20
        br 0 (;@2;)
      end
    end
    block  ;; label = @1
      loop  ;; label = @2
        local.get 7
        i32.load offset=24
        local.set 167
        local.get 7
        i32.load offset=12
        local.set 168
        local.get 167
        local.set 169
        local.get 168
        local.set 170
        local.get 169
        local.get 170
        i32.lt_s
        local.set 171
        i32.const 1
        local.set 172
        local.get 171
        local.get 172
        i32.and
        local.set 173
        local.get 173
        i32.eqz
        br_if 1 (;@1;)
        local.get 7
        i32.load offset=24
        local.set 174
        i32.const 2
        local.set 175
        local.get 174
        local.get 175
        i32.shl
        local.set 176
        local.get 31
        local.get 176
        i32.add
        local.set 177
        local.get 177
        i32.load
        local.set 178
        local.get 7
        i32.load offset=44
        local.set 179
        local.get 7
        i32.load offset=20
        local.set 180
        i32.const 2
        local.set 181
        local.get 180
        local.get 181
        i32.shl
        local.set 182
        local.get 179
        local.get 182
        i32.add
        local.set 183
        local.get 183
        local.get 178
        i32.store
        local.get 7
        i32.load offset=24
        local.set 184
        i32.const 1
        local.set 185
        local.get 184
        local.get 185
        i32.add
        local.set 186
        local.get 7
        local.get 186
        i32.store offset=24
        local.get 7
        i32.load offset=20
        local.set 187
        i32.const 1
        local.set 188
        local.get 187
        local.get 188
        i32.add
        local.set 189
        local.get 7
        local.get 189
        i32.store offset=20
        br 0 (;@2;)
      end
    end
    local.get 7
    i32.load offset=8
    local.set 190
    local.get 190
    local.set 6
    return)
  (func $_mergeSort (type 2) (param i32 i32 i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
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
      i32.load offset=8
      local.set 13
      local.get 5
      i32.load offset=4
      local.set 14
      local.get 5
      i32.load offset=8
      local.set 15
      local.get 14
      local.get 15
      i32.sub
      local.set 16
      i32.const 2
      local.set 17
      local.get 16
      local.get 17
      i32.div_s
      local.set 18
      local.get 13
      local.get 18
      i32.add
      local.set 19
      local.get 5
      local.get 19
      i32.store
      local.get 5
      i32.load offset=12
      local.set 20
      local.get 5
      i32.load offset=8
      local.set 21
      local.get 5
      i32.load
      local.set 22
      local.get 20
      local.get 21
      local.get 22
      call $_mergeSort
      local.get 5
      i32.load offset=12
      local.set 23
      local.get 5
      i32.load
      local.set 24
      i32.const 1
      local.set 25
      local.get 24
      local.get 25
      i32.add
      local.set 26
      local.get 5
      i32.load offset=4
      local.set 27
      local.get 23
      local.get 26
      local.get 27
      call $_mergeSort
      local.get 5
      i32.load offset=12
      local.set 28
      local.get 5
      i32.load offset=8
      local.set 29
      local.get 5
      i32.load
      local.set 30
      local.get 5
      i32.load offset=4
      local.set 31
      local.get 28
      local.get 29
      local.get 30
      local.get 31
      call $merge
    end
    i32.const 16
    local.set 32
    local.get 5
    local.get 32
    i32.add
    local.set 33
    local.get 33
    global.set $__stack_pointer
    return)
  (func $mergeSort (type 3) (param i32 i32)
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
    call $_mergeSort
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
    i32.const 10
    local.set 5
    local.get 4
    local.get 5
    call $mergeSort
    i32.const 0
    local.set 6
    local.get 6
    i32.load offset=1032
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
  (export "merge" (func $merge))
  (export "_mergeSort" (func $_mergeSort))
  (export "mergeSort" (func $mergeSort))
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
