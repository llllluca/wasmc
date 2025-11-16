(module $base64.wasm
  (type (;0;) (func))
  (type (;1;) (func (param i32 i32) (result i32)))
  (type (;2;) (func (result i32)))
  (type (;3;) (func (param i32 i32 i32 i32 i32) (result i32)))
  (type (;4;) (func (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (type (;5;) (func (param i32)))
  (type (;6;) (func (param i32) (result i32)))
  (func $__wasm_call_ctors (type 0))
  (func $strchr (type 1) (param i32 i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
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
    local.get 4
    local.get 5
    i32.store offset=4
    loop  ;; label = @1
      local.get 4
      i32.load offset=4
      local.set 6
      local.get 6
      i32.load8_u
      local.set 7
      i32.const 24
      local.set 8
      local.get 7
      local.get 8
      i32.shl
      local.set 9
      local.get 9
      local.get 8
      i32.shr_s
      local.set 10
      local.get 4
      i32.load offset=8
      local.set 11
      local.get 10
      local.get 11
      i32.ne
      local.set 12
      i32.const 1
      local.set 13
      i32.const 1
      local.set 14
      local.get 12
      local.get 14
      i32.and
      local.set 15
      local.get 13
      local.set 16
      block  ;; label = @2
        local.get 15
        br_if 0 (;@2;)
        local.get 4
        i32.load offset=4
        local.set 17
        local.get 17
        i32.load8_u
        local.set 18
        i32.const 24
        local.set 19
        local.get 18
        local.get 19
        i32.shl
        local.set 20
        local.get 20
        local.get 19
        i32.shr_s
        local.set 21
        i32.const 0
        local.set 22
        local.get 21
        local.get 22
        i32.ne
        local.set 23
        local.get 23
        local.set 16
      end
      local.get 16
      local.set 24
      i32.const 1
      local.set 25
      local.get 24
      local.get 25
      i32.and
      local.set 26
      block  ;; label = @2
        local.get 26
        i32.eqz
        br_if 0 (;@2;)
        local.get 4
        i32.load offset=4
        local.set 27
        i32.const 1
        local.set 28
        local.get 27
        local.get 28
        i32.add
        local.set 29
        local.get 4
        local.get 29
        i32.store offset=4
        br 1 (;@1;)
      end
    end
    local.get 4
    i32.load offset=8
    local.set 30
    local.get 4
    i32.load offset=4
    local.set 31
    local.get 31
    i32.load8_u
    local.set 32
    i32.const 24
    local.set 33
    local.get 32
    local.get 33
    i32.shl
    local.set 34
    local.get 34
    local.get 33
    i32.shr_s
    local.set 35
    local.get 30
    local.get 35
    i32.ne
    local.set 36
    i32.const 1
    local.set 37
    local.get 36
    local.get 37
    i32.and
    local.set 38
    block  ;; label = @1
      local.get 38
      i32.eqz
      br_if 0 (;@1;)
      i32.const 0
      local.set 39
      local.get 4
      local.get 39
      i32.store offset=4
    end
    local.get 4
    i32.load offset=4
    local.set 40
    local.get 40
    return)
  (func $__original_main (type 2) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
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
    i32.const 1000
    local.set 4
    local.get 2
    local.get 4
    i32.store offset=8
    local.get 2
    i32.load offset=8
    local.set 5
    i32.const 1
    local.set 6
    local.get 5
    local.get 6
    call $base64_encoded_len
    local.set 7
    local.get 2
    local.get 7
    i32.store offset=4
    local.get 2
    i32.load offset=4
    local.set 8
    local.get 2
    i32.load offset=8
    local.set 9
    i32.const 2032
    local.set 10
    i32.const 1024
    local.set 11
    i32.const 1
    local.set 12
    local.get 10
    local.get 8
    local.get 11
    local.get 9
    local.get 12
    call $bin2base64
    drop
    local.get 2
    i32.load offset=8
    local.set 13
    local.get 2
    i32.load offset=4
    local.set 14
    i32.const 1024
    local.set 15
    i32.const 2032
    local.set 16
    i32.const 0
    local.set 17
    i32.const 1
    local.set 18
    local.get 15
    local.get 13
    local.get 16
    local.get 14
    local.get 17
    local.get 17
    local.get 17
    local.get 18
    call $base642bin
    drop
    i32.const 0
    local.set 19
    local.get 19
    i32.load8_u offset=2074
    local.set 20
    i32.const 24
    local.set 21
    local.get 20
    local.get 21
    i32.shl
    local.set 22
    local.get 22
    local.get 21
    i32.shr_s
    local.set 23
    i32.const 16
    local.set 24
    local.get 2
    local.get 24
    i32.add
    local.set 25
    local.get 25
    global.set $__stack_pointer
    local.get 23
    return)
  (func $base64_encoded_len (type 1) (param i32 i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
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
    i32.load offset=8
    local.set 5
    local.get 5
    call $base64_check_variant
    local.get 4
    i32.load offset=12
    local.set 6
    i32.const 3
    local.set 7
    local.get 6
    local.get 7
    i32.div_u
    local.set 8
    i32.const 2
    local.set 9
    local.get 8
    local.get 9
    i32.shl
    local.set 10
    local.get 4
    i32.load offset=12
    local.set 11
    local.get 4
    i32.load offset=12
    local.set 12
    i32.const 3
    local.set 13
    local.get 12
    local.get 13
    i32.div_u
    local.set 14
    i32.const 3
    local.set 15
    local.get 14
    local.get 15
    i32.mul
    local.set 16
    local.get 11
    local.get 16
    i32.sub
    local.set 17
    local.get 4
    i32.load offset=12
    local.set 18
    local.get 4
    i32.load offset=12
    local.set 19
    i32.const 3
    local.set 20
    local.get 19
    local.get 20
    i32.div_u
    local.set 21
    i32.const 3
    local.set 22
    local.get 21
    local.get 22
    i32.mul
    local.set 23
    local.get 18
    local.get 23
    i32.sub
    local.set 24
    i32.const 1
    local.set 25
    local.get 24
    local.get 25
    i32.shr_u
    local.set 26
    local.get 17
    local.get 26
    i32.or
    local.set 27
    i32.const 1
    local.set 28
    local.get 27
    local.get 28
    i32.and
    local.set 29
    local.get 4
    i32.load offset=8
    local.set 30
    i32.const 2
    local.set 31
    local.get 30
    local.get 31
    i32.and
    local.set 32
    i32.const 1
    local.set 33
    local.get 32
    local.get 33
    i32.shr_u
    local.set 34
    i32.const 1
    local.set 35
    local.get 34
    local.get 35
    i32.sub
    local.set 36
    i32.const -1
    local.set 37
    local.get 36
    local.get 37
    i32.xor
    local.set 38
    local.get 4
    i32.load offset=12
    local.set 39
    local.get 4
    i32.load offset=12
    local.set 40
    i32.const 3
    local.set 41
    local.get 40
    local.get 41
    i32.div_u
    local.set 42
    i32.const 3
    local.set 43
    local.get 42
    local.get 43
    i32.mul
    local.set 44
    local.get 39
    local.get 44
    i32.sub
    local.set 45
    i32.const 3
    local.set 46
    local.get 46
    local.get 45
    i32.sub
    local.set 47
    local.get 38
    local.get 47
    i32.and
    local.set 48
    i32.const 4
    local.set 49
    local.get 49
    local.get 48
    i32.sub
    local.set 50
    local.get 29
    local.get 50
    i32.mul
    local.set 51
    local.get 10
    local.get 51
    i32.add
    local.set 52
    i32.const 1
    local.set 53
    local.get 52
    local.get 53
    i32.add
    local.set 54
    i32.const 16
    local.set 55
    local.get 4
    local.get 55
    i32.add
    local.set 56
    local.get 56
    global.set $__stack_pointer
    local.get 54
    return)
  (func $bin2base64 (type 3) (param i32 i32 i32 i32 i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 5
    i32.const 48
    local.set 6
    local.get 5
    local.get 6
    i32.sub
    local.set 7
    local.get 7
    global.set $__stack_pointer
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
    local.get 4
    i32.store offset=28
    i32.const 0
    local.set 8
    local.get 7
    local.get 8
    i32.store offset=24
    i32.const 0
    local.set 9
    local.get 7
    local.get 9
    i32.store offset=16
    i32.const 0
    local.set 10
    local.get 7
    local.get 10
    i32.store offset=12
    i32.const 0
    local.set 11
    local.get 7
    local.get 11
    i32.store
    local.get 7
    i32.load offset=28
    local.set 12
    local.get 12
    call $base64_check_variant
    local.get 7
    i32.load offset=32
    local.set 13
    i32.const 3
    local.set 14
    local.get 13
    local.get 14
    i32.div_u
    local.set 15
    local.get 7
    local.get 15
    i32.store offset=8
    local.get 7
    i32.load offset=32
    local.set 16
    local.get 7
    i32.load offset=8
    local.set 17
    i32.const 3
    local.set 18
    local.get 17
    local.get 18
    i32.mul
    local.set 19
    local.get 16
    local.get 19
    i32.sub
    local.set 20
    local.get 7
    local.get 20
    i32.store offset=4
    local.get 7
    i32.load offset=8
    local.set 21
    i32.const 2
    local.set 22
    local.get 21
    local.get 22
    i32.shl
    local.set 23
    local.get 7
    local.get 23
    i32.store offset=20
    local.get 7
    i32.load offset=4
    local.set 24
    block  ;; label = @1
      local.get 24
      i32.eqz
      br_if 0 (;@1;)
      local.get 7
      i32.load offset=28
      local.set 25
      i32.const 2
      local.set 26
      local.get 25
      local.get 26
      i32.and
      local.set 27
      block  ;; label = @2
        block  ;; label = @3
          local.get 27
          br_if 0 (;@3;)
          local.get 7
          i32.load offset=20
          local.set 28
          i32.const 4
          local.set 29
          local.get 28
          local.get 29
          i32.add
          local.set 30
          local.get 7
          local.get 30
          i32.store offset=20
          br 1 (;@2;)
        end
        local.get 7
        i32.load offset=4
        local.set 31
        i32.const 1
        local.set 32
        local.get 31
        local.get 32
        i32.shr_u
        local.set 33
        i32.const 2
        local.set 34
        local.get 33
        local.get 34
        i32.add
        local.set 35
        local.get 7
        i32.load offset=20
        local.set 36
        local.get 36
        local.get 35
        i32.add
        local.set 37
        local.get 7
        local.get 37
        i32.store offset=20
      end
    end
    local.get 7
    i32.load offset=40
    local.set 38
    local.get 7
    i32.load offset=20
    local.set 39
    local.get 38
    local.get 39
    i32.le_u
    local.set 40
    i32.const 1
    local.set 41
    local.get 40
    local.get 41
    i32.and
    local.set 42
    block  ;; label = @1
      local.get 42
      i32.eqz
      br_if 0 (;@1;)
      loop  ;; label = @2
        br 0 (;@2;)
      end
    end
    local.get 7
    i32.load offset=28
    local.set 43
    i32.const 4
    local.set 44
    local.get 43
    local.get 44
    i32.and
    local.set 45
    block  ;; label = @1
      block  ;; label = @2
        local.get 45
        i32.eqz
        br_if 0 (;@2;)
        block  ;; label = @3
          loop  ;; label = @4
            local.get 7
            i32.load offset=12
            local.set 46
            local.get 7
            i32.load offset=32
            local.set 47
            local.get 46
            local.get 47
            i32.lt_u
            local.set 48
            i32.const 1
            local.set 49
            local.get 48
            local.get 49
            i32.and
            local.set 50
            local.get 50
            i32.eqz
            br_if 1 (;@3;)
            local.get 7
            i32.load
            local.set 51
            i32.const 8
            local.set 52
            local.get 51
            local.get 52
            i32.shl
            local.set 53
            local.get 7
            i32.load offset=36
            local.set 54
            local.get 7
            i32.load offset=12
            local.set 55
            i32.const 1
            local.set 56
            local.get 55
            local.get 56
            i32.add
            local.set 57
            local.get 7
            local.get 57
            i32.store offset=12
            local.get 54
            local.get 55
            i32.add
            local.set 58
            local.get 58
            i32.load8_u
            local.set 59
            i32.const 255
            local.set 60
            local.get 59
            local.get 60
            i32.and
            local.set 61
            local.get 53
            local.get 61
            i32.add
            local.set 62
            local.get 7
            local.get 62
            i32.store
            local.get 7
            i32.load offset=24
            local.set 63
            i32.const 8
            local.set 64
            local.get 63
            local.get 64
            i32.add
            local.set 65
            local.get 7
            local.get 65
            i32.store offset=24
            block  ;; label = @5
              loop  ;; label = @6
                local.get 7
                i32.load offset=24
                local.set 66
                i32.const 6
                local.set 67
                local.get 66
                local.get 67
                i32.ge_u
                local.set 68
                i32.const 1
                local.set 69
                local.get 68
                local.get 69
                i32.and
                local.set 70
                local.get 70
                i32.eqz
                br_if 1 (;@5;)
                local.get 7
                i32.load offset=24
                local.set 71
                i32.const 6
                local.set 72
                local.get 71
                local.get 72
                i32.sub
                local.set 73
                local.get 7
                local.get 73
                i32.store offset=24
                local.get 7
                i32.load
                local.set 74
                local.get 7
                i32.load offset=24
                local.set 75
                local.get 74
                local.get 75
                i32.shr_u
                local.set 76
                i32.const 63
                local.set 77
                local.get 76
                local.get 77
                i32.and
                local.set 78
                local.get 78
                call $b64_byte_to_urlsafe_char
                local.set 79
                local.get 7
                i32.load offset=44
                local.set 80
                local.get 7
                i32.load offset=16
                local.set 81
                i32.const 1
                local.set 82
                local.get 81
                local.get 82
                i32.add
                local.set 83
                local.get 7
                local.get 83
                i32.store offset=16
                local.get 80
                local.get 81
                i32.add
                local.set 84
                local.get 84
                local.get 79
                i32.store8
                br 0 (;@6;)
              end
            end
            br 0 (;@4;)
          end
        end
        local.get 7
        i32.load offset=24
        local.set 85
        i32.const 0
        local.set 86
        local.get 85
        local.get 86
        i32.gt_u
        local.set 87
        i32.const 1
        local.set 88
        local.get 87
        local.get 88
        i32.and
        local.set 89
        block  ;; label = @3
          local.get 89
          i32.eqz
          br_if 0 (;@3;)
          local.get 7
          i32.load
          local.set 90
          local.get 7
          i32.load offset=24
          local.set 91
          i32.const 6
          local.set 92
          local.get 92
          local.get 91
          i32.sub
          local.set 93
          local.get 90
          local.get 93
          i32.shl
          local.set 94
          i32.const 63
          local.set 95
          local.get 94
          local.get 95
          i32.and
          local.set 96
          local.get 96
          call $b64_byte_to_urlsafe_char
          local.set 97
          local.get 7
          i32.load offset=44
          local.set 98
          local.get 7
          i32.load offset=16
          local.set 99
          i32.const 1
          local.set 100
          local.get 99
          local.get 100
          i32.add
          local.set 101
          local.get 7
          local.get 101
          i32.store offset=16
          local.get 98
          local.get 99
          i32.add
          local.set 102
          local.get 102
          local.get 97
          i32.store8
        end
        br 1 (;@1;)
      end
      block  ;; label = @2
        loop  ;; label = @3
          local.get 7
          i32.load offset=12
          local.set 103
          local.get 7
          i32.load offset=32
          local.set 104
          local.get 103
          local.get 104
          i32.lt_u
          local.set 105
          i32.const 1
          local.set 106
          local.get 105
          local.get 106
          i32.and
          local.set 107
          local.get 107
          i32.eqz
          br_if 1 (;@2;)
          local.get 7
          i32.load
          local.set 108
          i32.const 8
          local.set 109
          local.get 108
          local.get 109
          i32.shl
          local.set 110
          local.get 7
          i32.load offset=36
          local.set 111
          local.get 7
          i32.load offset=12
          local.set 112
          i32.const 1
          local.set 113
          local.get 112
          local.get 113
          i32.add
          local.set 114
          local.get 7
          local.get 114
          i32.store offset=12
          local.get 111
          local.get 112
          i32.add
          local.set 115
          local.get 115
          i32.load8_u
          local.set 116
          i32.const 255
          local.set 117
          local.get 116
          local.get 117
          i32.and
          local.set 118
          local.get 110
          local.get 118
          i32.add
          local.set 119
          local.get 7
          local.get 119
          i32.store
          local.get 7
          i32.load offset=24
          local.set 120
          i32.const 8
          local.set 121
          local.get 120
          local.get 121
          i32.add
          local.set 122
          local.get 7
          local.get 122
          i32.store offset=24
          block  ;; label = @4
            loop  ;; label = @5
              local.get 7
              i32.load offset=24
              local.set 123
              i32.const 6
              local.set 124
              local.get 123
              local.get 124
              i32.ge_u
              local.set 125
              i32.const 1
              local.set 126
              local.get 125
              local.get 126
              i32.and
              local.set 127
              local.get 127
              i32.eqz
              br_if 1 (;@4;)
              local.get 7
              i32.load offset=24
              local.set 128
              i32.const 6
              local.set 129
              local.get 128
              local.get 129
              i32.sub
              local.set 130
              local.get 7
              local.get 130
              i32.store offset=24
              local.get 7
              i32.load
              local.set 131
              local.get 7
              i32.load offset=24
              local.set 132
              local.get 131
              local.get 132
              i32.shr_u
              local.set 133
              i32.const 63
              local.set 134
              local.get 133
              local.get 134
              i32.and
              local.set 135
              local.get 135
              call $b64_byte_to_char
              local.set 136
              local.get 7
              i32.load offset=44
              local.set 137
              local.get 7
              i32.load offset=16
              local.set 138
              i32.const 1
              local.set 139
              local.get 138
              local.get 139
              i32.add
              local.set 140
              local.get 7
              local.get 140
              i32.store offset=16
              local.get 137
              local.get 138
              i32.add
              local.set 141
              local.get 141
              local.get 136
              i32.store8
              br 0 (;@5;)
            end
          end
          br 0 (;@3;)
        end
      end
      local.get 7
      i32.load offset=24
      local.set 142
      i32.const 0
      local.set 143
      local.get 142
      local.get 143
      i32.gt_u
      local.set 144
      i32.const 1
      local.set 145
      local.get 144
      local.get 145
      i32.and
      local.set 146
      block  ;; label = @2
        local.get 146
        i32.eqz
        br_if 0 (;@2;)
        local.get 7
        i32.load
        local.set 147
        local.get 7
        i32.load offset=24
        local.set 148
        i32.const 6
        local.set 149
        local.get 149
        local.get 148
        i32.sub
        local.set 150
        local.get 147
        local.get 150
        i32.shl
        local.set 151
        i32.const 63
        local.set 152
        local.get 151
        local.get 152
        i32.and
        local.set 153
        local.get 153
        call $b64_byte_to_char
        local.set 154
        local.get 7
        i32.load offset=44
        local.set 155
        local.get 7
        i32.load offset=16
        local.set 156
        i32.const 1
        local.set 157
        local.get 156
        local.get 157
        i32.add
        local.set 158
        local.get 7
        local.get 158
        i32.store offset=16
        local.get 155
        local.get 156
        i32.add
        local.set 159
        local.get 159
        local.get 154
        i32.store8
      end
    end
    block  ;; label = @1
      loop  ;; label = @2
        local.get 7
        i32.load offset=16
        local.set 160
        local.get 7
        i32.load offset=20
        local.set 161
        local.get 160
        local.get 161
        i32.lt_u
        local.set 162
        i32.const 1
        local.set 163
        local.get 162
        local.get 163
        i32.and
        local.set 164
        local.get 164
        i32.eqz
        br_if 1 (;@1;)
        local.get 7
        i32.load offset=44
        local.set 165
        local.get 7
        i32.load offset=16
        local.set 166
        i32.const 1
        local.set 167
        local.get 166
        local.get 167
        i32.add
        local.set 168
        local.get 7
        local.get 168
        i32.store offset=16
        local.get 165
        local.get 166
        i32.add
        local.set 169
        i32.const 61
        local.set 170
        local.get 169
        local.get 170
        i32.store8
        br 0 (;@2;)
      end
    end
    loop  ;; label = @1
      local.get 7
      i32.load offset=44
      local.set 171
      local.get 7
      i32.load offset=16
      local.set 172
      i32.const 1
      local.set 173
      local.get 172
      local.get 173
      i32.add
      local.set 174
      local.get 7
      local.get 174
      i32.store offset=16
      local.get 171
      local.get 172
      i32.add
      local.set 175
      i32.const 0
      local.set 176
      local.get 175
      local.get 176
      i32.store8
      local.get 7
      i32.load offset=16
      local.set 177
      local.get 7
      i32.load offset=40
      local.set 178
      local.get 177
      local.get 178
      i32.lt_u
      local.set 179
      i32.const 1
      local.set 180
      local.get 179
      local.get 180
      i32.and
      local.set 181
      local.get 181
      br_if 0 (;@1;)
    end
    local.get 7
    i32.load offset=44
    local.set 182
    i32.const 48
    local.set 183
    local.get 7
    local.get 183
    i32.add
    local.set 184
    local.get 184
    global.set $__stack_pointer
    local.get 182
    return)
  (func $base642bin (type 4) (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 8
    i32.const 64
    local.set 9
    local.get 8
    local.get 9
    i32.sub
    local.set 10
    local.get 10
    global.set $__stack_pointer
    local.get 10
    local.get 0
    i32.store offset=60
    local.get 10
    local.get 1
    i32.store offset=56
    local.get 10
    local.get 2
    i32.store offset=52
    local.get 10
    local.get 3
    i32.store offset=48
    local.get 10
    local.get 4
    i32.store offset=44
    local.get 10
    local.get 5
    i32.store offset=40
    local.get 10
    local.get 6
    i32.store offset=36
    local.get 10
    local.get 7
    i32.store offset=32
    i32.const 0
    local.set 11
    local.get 10
    local.get 11
    i32.store offset=28
    i32.const 0
    local.set 12
    local.get 10
    local.get 12
    i32.store offset=24
    i32.const 0
    local.set 13
    local.get 10
    local.get 13
    i32.store offset=20
    i32.const 0
    local.set 14
    local.get 10
    local.get 14
    i32.store offset=12
    i32.const 0
    local.set 15
    local.get 10
    local.get 15
    i32.store offset=8
    local.get 10
    i32.load offset=32
    local.set 16
    local.get 16
    call $base64_check_variant
    local.get 10
    i32.load offset=32
    local.set 17
    i32.const 4
    local.set 18
    local.get 17
    local.get 18
    i32.and
    local.set 19
    local.get 10
    local.get 19
    i32.store offset=16
    block  ;; label = @1
      loop  ;; label = @2
        local.get 10
        i32.load offset=24
        local.set 20
        local.get 10
        i32.load offset=48
        local.set 21
        local.get 20
        local.get 21
        i32.lt_u
        local.set 22
        i32.const 1
        local.set 23
        local.get 22
        local.get 23
        i32.and
        local.set 24
        local.get 24
        i32.eqz
        br_if 1 (;@1;)
        local.get 10
        i32.load offset=52
        local.set 25
        local.get 10
        i32.load offset=24
        local.set 26
        local.get 25
        local.get 26
        i32.add
        local.set 27
        local.get 27
        i32.load8_u
        local.set 28
        local.get 10
        local.get 28
        i32.store8 offset=3
        local.get 10
        i32.load offset=16
        local.set 29
        block  ;; label = @3
          block  ;; label = @4
            local.get 29
            i32.eqz
            br_if 0 (;@4;)
            local.get 10
            i32.load8_u offset=3
            local.set 30
            i32.const 24
            local.set 31
            local.get 30
            local.get 31
            i32.shl
            local.set 32
            local.get 32
            local.get 31
            i32.shr_s
            local.set 33
            local.get 33
            call $b64_urlsafe_char_to_byte
            local.set 34
            local.get 10
            local.get 34
            i32.store offset=4
            br 1 (;@3;)
          end
          local.get 10
          i32.load8_u offset=3
          local.set 35
          i32.const 24
          local.set 36
          local.get 35
          local.get 36
          i32.shl
          local.set 37
          local.get 37
          local.get 36
          i32.shr_s
          local.set 38
          local.get 38
          call $b64_char_to_byte
          local.set 39
          local.get 10
          local.get 39
          i32.store offset=4
        end
        local.get 10
        i32.load offset=4
        local.set 40
        i32.const 255
        local.set 41
        local.get 40
        local.get 41
        i32.eq
        local.set 42
        i32.const 1
        local.set 43
        local.get 42
        local.get 43
        i32.and
        local.set 44
        block  ;; label = @3
          local.get 44
          i32.eqz
          br_if 0 (;@3;)
          local.get 10
          i32.load offset=44
          local.set 45
          i32.const 0
          local.set 46
          local.get 45
          local.get 46
          i32.ne
          local.set 47
          i32.const 1
          local.set 48
          local.get 47
          local.get 48
          i32.and
          local.set 49
          block  ;; label = @4
            local.get 49
            i32.eqz
            br_if 0 (;@4;)
            local.get 10
            i32.load offset=44
            local.set 50
            local.get 10
            i32.load8_u offset=3
            local.set 51
            i32.const 24
            local.set 52
            local.get 51
            local.get 52
            i32.shl
            local.set 53
            local.get 53
            local.get 52
            i32.shr_s
            local.set 54
            local.get 50
            local.get 54
            call $strchr
            local.set 55
            i32.const 0
            local.set 56
            local.get 55
            local.get 56
            i32.ne
            local.set 57
            i32.const 1
            local.set 58
            local.get 57
            local.get 58
            i32.and
            local.set 59
            local.get 59
            i32.eqz
            br_if 0 (;@4;)
            local.get 10
            i32.load offset=24
            local.set 60
            i32.const 1
            local.set 61
            local.get 60
            local.get 61
            i32.add
            local.set 62
            local.get 10
            local.get 62
            i32.store offset=24
            br 2 (;@2;)
          end
          br 2 (;@1;)
        end
        local.get 10
        i32.load offset=8
        local.set 63
        i32.const 6
        local.set 64
        local.get 63
        local.get 64
        i32.shl
        local.set 65
        local.get 10
        i32.load offset=4
        local.set 66
        local.get 65
        local.get 66
        i32.add
        local.set 67
        local.get 10
        local.get 67
        i32.store offset=8
        local.get 10
        i32.load offset=28
        local.set 68
        i32.const 6
        local.set 69
        local.get 68
        local.get 69
        i32.add
        local.set 70
        local.get 10
        local.get 70
        i32.store offset=28
        local.get 10
        i32.load offset=28
        local.set 71
        i32.const 8
        local.set 72
        local.get 71
        local.get 72
        i32.ge_u
        local.set 73
        i32.const 1
        local.set 74
        local.get 73
        local.get 74
        i32.and
        local.set 75
        block  ;; label = @3
          local.get 75
          i32.eqz
          br_if 0 (;@3;)
          local.get 10
          i32.load offset=28
          local.set 76
          i32.const 8
          local.set 77
          local.get 76
          local.get 77
          i32.sub
          local.set 78
          local.get 10
          local.get 78
          i32.store offset=28
          local.get 10
          i32.load offset=20
          local.set 79
          local.get 10
          i32.load offset=56
          local.set 80
          local.get 79
          local.get 80
          i32.ge_u
          local.set 81
          i32.const 1
          local.set 82
          local.get 81
          local.get 82
          i32.and
          local.set 83
          block  ;; label = @4
            local.get 83
            i32.eqz
            br_if 0 (;@4;)
            i32.const -1
            local.set 84
            local.get 10
            local.get 84
            i32.store offset=12
            br 3 (;@1;)
          end
          local.get 10
          i32.load offset=8
          local.set 85
          local.get 10
          i32.load offset=28
          local.set 86
          local.get 85
          local.get 86
          i32.shr_u
          local.set 87
          i32.const 255
          local.set 88
          local.get 87
          local.get 88
          i32.and
          local.set 89
          local.get 10
          i32.load offset=60
          local.set 90
          local.get 10
          i32.load offset=20
          local.set 91
          i32.const 1
          local.set 92
          local.get 91
          local.get 92
          i32.add
          local.set 93
          local.get 10
          local.get 93
          i32.store offset=20
          local.get 90
          local.get 91
          i32.add
          local.set 94
          local.get 94
          local.get 89
          i32.store8
        end
        local.get 10
        i32.load offset=24
        local.set 95
        i32.const 1
        local.set 96
        local.get 95
        local.get 96
        i32.add
        local.set 97
        local.get 10
        local.get 97
        i32.store offset=24
        br 0 (;@2;)
      end
    end
    local.get 10
    i32.load offset=28
    local.set 98
    i32.const 4
    local.set 99
    local.get 98
    local.get 99
    i32.gt_u
    local.set 100
    i32.const 1
    local.set 101
    local.get 100
    local.get 101
    i32.and
    local.set 102
    block  ;; label = @1
      block  ;; label = @2
        block  ;; label = @3
          local.get 102
          br_if 0 (;@3;)
          local.get 10
          i32.load offset=8
          local.set 103
          local.get 10
          i32.load offset=28
          local.set 104
          i32.const 1
          local.set 105
          local.get 105
          local.get 104
          i32.shl
          local.set 106
          i32.const 1
          local.set 107
          local.get 106
          local.get 107
          i32.sub
          local.set 108
          local.get 103
          local.get 108
          i32.and
          local.set 109
          local.get 109
          i32.eqz
          br_if 1 (;@2;)
        end
        i32.const -1
        local.set 110
        local.get 10
        local.get 110
        i32.store offset=12
        br 1 (;@1;)
      end
      local.get 10
      i32.load offset=12
      local.set 111
      block  ;; label = @2
        local.get 111
        br_if 0 (;@2;)
        local.get 10
        i32.load offset=32
        local.set 112
        i32.const 2
        local.set 113
        local.get 112
        local.get 113
        i32.and
        local.set 114
        local.get 114
        br_if 0 (;@2;)
        local.get 10
        i32.load offset=52
        local.set 115
        local.get 10
        i32.load offset=48
        local.set 116
        local.get 10
        i32.load offset=44
        local.set 117
        local.get 10
        i32.load offset=28
        local.set 118
        i32.const 1
        local.set 119
        local.get 118
        local.get 119
        i32.shr_u
        local.set 120
        i32.const 24
        local.set 121
        local.get 10
        local.get 121
        i32.add
        local.set 122
        local.get 122
        local.set 123
        local.get 115
        local.get 116
        local.get 123
        local.get 117
        local.get 120
        call $_base642bin_skip_padding
        local.set 124
        local.get 10
        local.get 124
        i32.store offset=12
      end
    end
    local.get 10
    i32.load offset=12
    local.set 125
    block  ;; label = @1
      block  ;; label = @2
        local.get 125
        i32.eqz
        br_if 0 (;@2;)
        i32.const 0
        local.set 126
        local.get 10
        local.get 126
        i32.store offset=20
        br 1 (;@1;)
      end
      local.get 10
      i32.load offset=44
      local.set 127
      i32.const 0
      local.set 128
      local.get 127
      local.get 128
      i32.ne
      local.set 129
      i32.const 1
      local.set 130
      local.get 129
      local.get 130
      i32.and
      local.set 131
      block  ;; label = @2
        local.get 131
        i32.eqz
        br_if 0 (;@2;)
        loop  ;; label = @3
          local.get 10
          i32.load offset=24
          local.set 132
          local.get 10
          i32.load offset=48
          local.set 133
          local.get 132
          local.get 133
          i32.lt_u
          local.set 134
          i32.const 0
          local.set 135
          i32.const 1
          local.set 136
          local.get 134
          local.get 136
          i32.and
          local.set 137
          local.get 135
          local.set 138
          block  ;; label = @4
            local.get 137
            i32.eqz
            br_if 0 (;@4;)
            local.get 10
            i32.load offset=44
            local.set 139
            local.get 10
            i32.load offset=52
            local.set 140
            local.get 10
            i32.load offset=24
            local.set 141
            local.get 140
            local.get 141
            i32.add
            local.set 142
            local.get 142
            i32.load8_u
            local.set 143
            i32.const 24
            local.set 144
            local.get 143
            local.get 144
            i32.shl
            local.set 145
            local.get 145
            local.get 144
            i32.shr_s
            local.set 146
            local.get 139
            local.get 146
            call $strchr
            local.set 147
            i32.const 0
            local.set 148
            local.get 147
            local.get 148
            i32.ne
            local.set 149
            local.get 149
            local.set 138
          end
          local.get 138
          local.set 150
          i32.const 1
          local.set 151
          local.get 150
          local.get 151
          i32.and
          local.set 152
          block  ;; label = @4
            local.get 152
            i32.eqz
            br_if 0 (;@4;)
            local.get 10
            i32.load offset=24
            local.set 153
            i32.const 1
            local.set 154
            local.get 153
            local.get 154
            i32.add
            local.set 155
            local.get 10
            local.get 155
            i32.store offset=24
            br 1 (;@3;)
          end
        end
      end
    end
    local.get 10
    i32.load offset=36
    local.set 156
    i32.const 0
    local.set 157
    local.get 156
    local.get 157
    i32.ne
    local.set 158
    i32.const 1
    local.set 159
    local.get 158
    local.get 159
    i32.and
    local.set 160
    block  ;; label = @1
      block  ;; label = @2
        local.get 160
        i32.eqz
        br_if 0 (;@2;)
        local.get 10
        i32.load offset=52
        local.set 161
        local.get 10
        i32.load offset=24
        local.set 162
        local.get 161
        local.get 162
        i32.add
        local.set 163
        local.get 10
        i32.load offset=36
        local.set 164
        local.get 164
        local.get 163
        i32.store
        br 1 (;@1;)
      end
      local.get 10
      i32.load offset=24
      local.set 165
      local.get 10
      i32.load offset=48
      local.set 166
      local.get 165
      local.get 166
      i32.ne
      local.set 167
      i32.const 1
      local.set 168
      local.get 167
      local.get 168
      i32.and
      local.set 169
      block  ;; label = @2
        local.get 169
        i32.eqz
        br_if 0 (;@2;)
        i32.const -1
        local.set 170
        local.get 10
        local.get 170
        i32.store offset=12
      end
    end
    local.get 10
    i32.load offset=40
    local.set 171
    i32.const 0
    local.set 172
    local.get 171
    local.get 172
    i32.ne
    local.set 173
    i32.const 1
    local.set 174
    local.get 173
    local.get 174
    i32.and
    local.set 175
    block  ;; label = @1
      local.get 175
      i32.eqz
      br_if 0 (;@1;)
      local.get 10
      i32.load offset=20
      local.set 176
      local.get 10
      i32.load offset=40
      local.set 177
      local.get 177
      local.get 176
      i32.store
    end
    local.get 10
    i32.load offset=12
    local.set 178
    i32.const 64
    local.set 179
    local.get 10
    local.get 179
    i32.add
    local.set 180
    local.get 180
    global.set $__stack_pointer
    local.get 178
    return)
  (func $base64_check_variant (type 5) (param i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 1
    i32.const 16
    local.set 2
    local.get 1
    local.get 2
    i32.sub
    local.set 3
    local.get 3
    local.get 0
    i32.store offset=12
    local.get 3
    i32.load offset=12
    local.set 4
    i32.const -7
    local.set 5
    local.get 4
    local.get 5
    i32.and
    local.set 6
    i32.const 1
    local.set 7
    local.get 6
    local.get 7
    i32.ne
    local.set 8
    i32.const 1
    local.set 9
    local.get 8
    local.get 9
    i32.and
    local.set 10
    block  ;; label = @1
      local.get 10
      i32.eqz
      br_if 0 (;@1;)
      loop  ;; label = @2
        br 0 (;@2;)
      end
    end
    return)
  (func $b64_byte_to_urlsafe_char (type 6) (param i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 1
    i32.const 16
    local.set 2
    local.get 1
    local.get 2
    i32.sub
    local.set 3
    local.get 3
    local.get 0
    i32.store offset=12
    local.get 3
    i32.load offset=12
    local.set 4
    i32.const 26
    local.set 5
    local.get 4
    local.get 5
    i32.sub
    local.set 6
    i32.const 8
    local.set 7
    local.get 6
    local.get 7
    i32.shr_u
    local.set 8
    i32.const 255
    local.set 9
    local.get 8
    local.get 9
    i32.and
    local.set 10
    local.get 3
    i32.load offset=12
    local.set 11
    i32.const 65
    local.set 12
    local.get 11
    local.get 12
    i32.add
    local.set 13
    local.get 10
    local.get 13
    i32.and
    local.set 14
    local.get 3
    i32.load offset=12
    local.set 15
    i32.const 26
    local.set 16
    local.get 15
    local.get 16
    i32.sub
    local.set 17
    i32.const 8
    local.set 18
    local.get 17
    local.get 18
    i32.shr_u
    local.set 19
    i32.const 255
    local.set 20
    local.get 19
    local.get 20
    i32.and
    local.set 21
    i32.const 255
    local.set 22
    local.get 21
    local.get 22
    i32.xor
    local.set 23
    local.get 3
    i32.load offset=12
    local.set 24
    i32.const 52
    local.set 25
    local.get 24
    local.get 25
    i32.sub
    local.set 26
    i32.const 8
    local.set 27
    local.get 26
    local.get 27
    i32.shr_u
    local.set 28
    i32.const 255
    local.set 29
    local.get 28
    local.get 29
    i32.and
    local.set 30
    local.get 23
    local.get 30
    i32.and
    local.set 31
    local.get 3
    i32.load offset=12
    local.set 32
    i32.const 71
    local.set 33
    local.get 32
    local.get 33
    i32.add
    local.set 34
    local.get 31
    local.get 34
    i32.and
    local.set 35
    local.get 14
    local.get 35
    i32.or
    local.set 36
    local.get 3
    i32.load offset=12
    local.set 37
    i32.const 52
    local.set 38
    local.get 37
    local.get 38
    i32.sub
    local.set 39
    i32.const 8
    local.set 40
    local.get 39
    local.get 40
    i32.shr_u
    local.set 41
    i32.const 255
    local.set 42
    local.get 41
    local.get 42
    i32.and
    local.set 43
    i32.const 255
    local.set 44
    local.get 43
    local.get 44
    i32.xor
    local.set 45
    local.get 3
    i32.load offset=12
    local.set 46
    i32.const 62
    local.set 47
    local.get 46
    local.get 47
    i32.sub
    local.set 48
    i32.const 8
    local.set 49
    local.get 48
    local.get 49
    i32.shr_u
    local.set 50
    i32.const 255
    local.set 51
    local.get 50
    local.get 51
    i32.and
    local.set 52
    local.get 45
    local.get 52
    i32.and
    local.set 53
    local.get 3
    i32.load offset=12
    local.set 54
    i32.const -4
    local.set 55
    local.get 54
    local.get 55
    i32.add
    local.set 56
    local.get 53
    local.get 56
    i32.and
    local.set 57
    local.get 36
    local.get 57
    i32.or
    local.set 58
    local.get 3
    i32.load offset=12
    local.set 59
    i32.const 62
    local.set 60
    local.get 59
    local.get 60
    i32.xor
    local.set 61
    i32.const 0
    local.set 62
    local.get 62
    local.get 61
    i32.sub
    local.set 63
    i32.const 8
    local.set 64
    local.get 63
    local.get 64
    i32.shr_u
    local.set 65
    i32.const 255
    local.set 66
    local.get 65
    local.get 66
    i32.and
    local.set 67
    i32.const 255
    local.set 68
    local.get 67
    local.get 68
    i32.xor
    local.set 69
    i32.const 45
    local.set 70
    local.get 69
    local.get 70
    i32.and
    local.set 71
    local.get 58
    local.get 71
    i32.or
    local.set 72
    local.get 3
    i32.load offset=12
    local.set 73
    i32.const 63
    local.set 74
    local.get 73
    local.get 74
    i32.xor
    local.set 75
    i32.const 0
    local.set 76
    local.get 76
    local.get 75
    i32.sub
    local.set 77
    i32.const 8
    local.set 78
    local.get 77
    local.get 78
    i32.shr_u
    local.set 79
    i32.const 255
    local.set 80
    local.get 79
    local.get 80
    i32.and
    local.set 81
    i32.const 255
    local.set 82
    local.get 81
    local.get 82
    i32.xor
    local.set 83
    i32.const 95
    local.set 84
    local.get 83
    local.get 84
    i32.and
    local.set 85
    local.get 72
    local.get 85
    i32.or
    local.set 86
    local.get 86
    return)
  (func $b64_byte_to_char (type 6) (param i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 1
    i32.const 16
    local.set 2
    local.get 1
    local.get 2
    i32.sub
    local.set 3
    local.get 3
    local.get 0
    i32.store offset=12
    local.get 3
    i32.load offset=12
    local.set 4
    i32.const 26
    local.set 5
    local.get 4
    local.get 5
    i32.sub
    local.set 6
    i32.const 8
    local.set 7
    local.get 6
    local.get 7
    i32.shr_u
    local.set 8
    i32.const 255
    local.set 9
    local.get 8
    local.get 9
    i32.and
    local.set 10
    local.get 3
    i32.load offset=12
    local.set 11
    i32.const 65
    local.set 12
    local.get 11
    local.get 12
    i32.add
    local.set 13
    local.get 10
    local.get 13
    i32.and
    local.set 14
    local.get 3
    i32.load offset=12
    local.set 15
    i32.const 26
    local.set 16
    local.get 15
    local.get 16
    i32.sub
    local.set 17
    i32.const 8
    local.set 18
    local.get 17
    local.get 18
    i32.shr_u
    local.set 19
    i32.const 255
    local.set 20
    local.get 19
    local.get 20
    i32.and
    local.set 21
    i32.const 255
    local.set 22
    local.get 21
    local.get 22
    i32.xor
    local.set 23
    local.get 3
    i32.load offset=12
    local.set 24
    i32.const 52
    local.set 25
    local.get 24
    local.get 25
    i32.sub
    local.set 26
    i32.const 8
    local.set 27
    local.get 26
    local.get 27
    i32.shr_u
    local.set 28
    i32.const 255
    local.set 29
    local.get 28
    local.get 29
    i32.and
    local.set 30
    local.get 23
    local.get 30
    i32.and
    local.set 31
    local.get 3
    i32.load offset=12
    local.set 32
    i32.const 71
    local.set 33
    local.get 32
    local.get 33
    i32.add
    local.set 34
    local.get 31
    local.get 34
    i32.and
    local.set 35
    local.get 14
    local.get 35
    i32.or
    local.set 36
    local.get 3
    i32.load offset=12
    local.set 37
    i32.const 52
    local.set 38
    local.get 37
    local.get 38
    i32.sub
    local.set 39
    i32.const 8
    local.set 40
    local.get 39
    local.get 40
    i32.shr_u
    local.set 41
    i32.const 255
    local.set 42
    local.get 41
    local.get 42
    i32.and
    local.set 43
    i32.const 255
    local.set 44
    local.get 43
    local.get 44
    i32.xor
    local.set 45
    local.get 3
    i32.load offset=12
    local.set 46
    i32.const 62
    local.set 47
    local.get 46
    local.get 47
    i32.sub
    local.set 48
    i32.const 8
    local.set 49
    local.get 48
    local.get 49
    i32.shr_u
    local.set 50
    i32.const 255
    local.set 51
    local.get 50
    local.get 51
    i32.and
    local.set 52
    local.get 45
    local.get 52
    i32.and
    local.set 53
    local.get 3
    i32.load offset=12
    local.set 54
    i32.const -4
    local.set 55
    local.get 54
    local.get 55
    i32.add
    local.set 56
    local.get 53
    local.get 56
    i32.and
    local.set 57
    local.get 36
    local.get 57
    i32.or
    local.set 58
    local.get 3
    i32.load offset=12
    local.set 59
    i32.const 62
    local.set 60
    local.get 59
    local.get 60
    i32.xor
    local.set 61
    i32.const 0
    local.set 62
    local.get 62
    local.get 61
    i32.sub
    local.set 63
    i32.const 8
    local.set 64
    local.get 63
    local.get 64
    i32.shr_u
    local.set 65
    i32.const 255
    local.set 66
    local.get 65
    local.get 66
    i32.and
    local.set 67
    i32.const 255
    local.set 68
    local.get 67
    local.get 68
    i32.xor
    local.set 69
    i32.const 43
    local.set 70
    local.get 69
    local.get 70
    i32.and
    local.set 71
    local.get 58
    local.get 71
    i32.or
    local.set 72
    local.get 3
    i32.load offset=12
    local.set 73
    i32.const 63
    local.set 74
    local.get 73
    local.get 74
    i32.xor
    local.set 75
    i32.const 0
    local.set 76
    local.get 76
    local.get 75
    i32.sub
    local.set 77
    i32.const 8
    local.set 78
    local.get 77
    local.get 78
    i32.shr_u
    local.set 79
    i32.const 255
    local.set 80
    local.get 79
    local.get 80
    i32.and
    local.set 81
    i32.const 255
    local.set 82
    local.get 81
    local.get 82
    i32.xor
    local.set 83
    i32.const 47
    local.set 84
    local.get 83
    local.get 84
    i32.and
    local.set 85
    local.get 72
    local.get 85
    i32.or
    local.set 86
    local.get 86
    return)
  (func $b64_urlsafe_char_to_byte (type 6) (param i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 1
    i32.const 16
    local.set 2
    local.get 1
    local.get 2
    i32.sub
    local.set 3
    local.get 3
    local.get 0
    i32.store offset=12
    local.get 3
    i32.load offset=12
    local.set 4
    i32.const 65
    local.set 5
    local.get 4
    local.get 5
    i32.sub
    local.set 6
    i32.const 8
    local.set 7
    local.get 6
    local.get 7
    i32.shr_u
    local.set 8
    i32.const 255
    local.set 9
    local.get 8
    local.get 9
    i32.and
    local.set 10
    i32.const 255
    local.set 11
    local.get 10
    local.get 11
    i32.xor
    local.set 12
    local.get 3
    i32.load offset=12
    local.set 13
    i32.const 90
    local.set 14
    local.get 14
    local.get 13
    i32.sub
    local.set 15
    i32.const 8
    local.set 16
    local.get 15
    local.get 16
    i32.shr_u
    local.set 17
    i32.const 255
    local.set 18
    local.get 17
    local.get 18
    i32.and
    local.set 19
    i32.const 255
    local.set 20
    local.get 19
    local.get 20
    i32.xor
    local.set 21
    local.get 12
    local.get 21
    i32.and
    local.set 22
    local.get 3
    i32.load offset=12
    local.set 23
    i32.const 65
    local.set 24
    local.get 23
    local.get 24
    i32.sub
    local.set 25
    local.get 22
    local.get 25
    i32.and
    local.set 26
    local.get 3
    i32.load offset=12
    local.set 27
    i32.const 97
    local.set 28
    local.get 27
    local.get 28
    i32.sub
    local.set 29
    i32.const 8
    local.set 30
    local.get 29
    local.get 30
    i32.shr_u
    local.set 31
    i32.const 255
    local.set 32
    local.get 31
    local.get 32
    i32.and
    local.set 33
    i32.const 255
    local.set 34
    local.get 33
    local.get 34
    i32.xor
    local.set 35
    local.get 3
    i32.load offset=12
    local.set 36
    i32.const 122
    local.set 37
    local.get 37
    local.get 36
    i32.sub
    local.set 38
    i32.const 8
    local.set 39
    local.get 38
    local.get 39
    i32.shr_u
    local.set 40
    i32.const 255
    local.set 41
    local.get 40
    local.get 41
    i32.and
    local.set 42
    i32.const 255
    local.set 43
    local.get 42
    local.get 43
    i32.xor
    local.set 44
    local.get 35
    local.get 44
    i32.and
    local.set 45
    local.get 3
    i32.load offset=12
    local.set 46
    i32.const 71
    local.set 47
    local.get 46
    local.get 47
    i32.sub
    local.set 48
    local.get 45
    local.get 48
    i32.and
    local.set 49
    local.get 26
    local.get 49
    i32.or
    local.set 50
    local.get 3
    i32.load offset=12
    local.set 51
    i32.const 48
    local.set 52
    local.get 51
    local.get 52
    i32.sub
    local.set 53
    i32.const 8
    local.set 54
    local.get 53
    local.get 54
    i32.shr_u
    local.set 55
    i32.const 255
    local.set 56
    local.get 55
    local.get 56
    i32.and
    local.set 57
    i32.const 255
    local.set 58
    local.get 57
    local.get 58
    i32.xor
    local.set 59
    local.get 3
    i32.load offset=12
    local.set 60
    i32.const 57
    local.set 61
    local.get 61
    local.get 60
    i32.sub
    local.set 62
    i32.const 8
    local.set 63
    local.get 62
    local.get 63
    i32.shr_u
    local.set 64
    i32.const 255
    local.set 65
    local.get 64
    local.get 65
    i32.and
    local.set 66
    i32.const 255
    local.set 67
    local.get 66
    local.get 67
    i32.xor
    local.set 68
    local.get 59
    local.get 68
    i32.and
    local.set 69
    local.get 3
    i32.load offset=12
    local.set 70
    i32.const -4
    local.set 71
    local.get 70
    local.get 71
    i32.sub
    local.set 72
    local.get 69
    local.get 72
    i32.and
    local.set 73
    local.get 50
    local.get 73
    i32.or
    local.set 74
    local.get 3
    i32.load offset=12
    local.set 75
    i32.const 45
    local.set 76
    local.get 75
    local.get 76
    i32.xor
    local.set 77
    i32.const 0
    local.set 78
    local.get 78
    local.get 77
    i32.sub
    local.set 79
    i32.const 8
    local.set 80
    local.get 79
    local.get 80
    i32.shr_u
    local.set 81
    i32.const 255
    local.set 82
    local.get 81
    local.get 82
    i32.and
    local.set 83
    i32.const 255
    local.set 84
    local.get 83
    local.get 84
    i32.xor
    local.set 85
    i32.const 62
    local.set 86
    local.get 85
    local.get 86
    i32.and
    local.set 87
    local.get 74
    local.get 87
    i32.or
    local.set 88
    local.get 3
    i32.load offset=12
    local.set 89
    i32.const 95
    local.set 90
    local.get 89
    local.get 90
    i32.xor
    local.set 91
    i32.const 0
    local.set 92
    local.get 92
    local.get 91
    i32.sub
    local.set 93
    i32.const 8
    local.set 94
    local.get 93
    local.get 94
    i32.shr_u
    local.set 95
    i32.const 255
    local.set 96
    local.get 95
    local.get 96
    i32.and
    local.set 97
    i32.const 255
    local.set 98
    local.get 97
    local.get 98
    i32.xor
    local.set 99
    i32.const 63
    local.set 100
    local.get 99
    local.get 100
    i32.and
    local.set 101
    local.get 88
    local.get 101
    i32.or
    local.set 102
    local.get 3
    local.get 102
    i32.store offset=8
    local.get 3
    i32.load offset=8
    local.set 103
    local.get 3
    i32.load offset=8
    local.set 104
    i32.const 0
    local.set 105
    local.get 104
    local.get 105
    i32.xor
    local.set 106
    i32.const 0
    local.set 107
    local.get 107
    local.get 106
    i32.sub
    local.set 108
    i32.const 8
    local.set 109
    local.get 108
    local.get 109
    i32.shr_u
    local.set 110
    i32.const 255
    local.set 111
    local.get 110
    local.get 111
    i32.and
    local.set 112
    i32.const 255
    local.set 113
    local.get 112
    local.get 113
    i32.xor
    local.set 114
    local.get 3
    i32.load offset=12
    local.set 115
    i32.const 65
    local.set 116
    local.get 115
    local.get 116
    i32.xor
    local.set 117
    i32.const 0
    local.set 118
    local.get 118
    local.get 117
    i32.sub
    local.set 119
    i32.const 8
    local.set 120
    local.get 119
    local.get 120
    i32.shr_u
    local.set 121
    i32.const 255
    local.set 122
    local.get 121
    local.get 122
    i32.and
    local.set 123
    i32.const 255
    local.set 124
    local.get 123
    local.get 124
    i32.xor
    local.set 125
    i32.const 255
    local.set 126
    local.get 125
    local.get 126
    i32.xor
    local.set 127
    local.get 114
    local.get 127
    i32.and
    local.set 128
    local.get 103
    local.get 128
    i32.or
    local.set 129
    local.get 129
    return)
  (func $b64_char_to_byte (type 6) (param i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 1
    i32.const 16
    local.set 2
    local.get 1
    local.get 2
    i32.sub
    local.set 3
    local.get 3
    local.get 0
    i32.store offset=12
    local.get 3
    i32.load offset=12
    local.set 4
    i32.const 65
    local.set 5
    local.get 4
    local.get 5
    i32.sub
    local.set 6
    i32.const 8
    local.set 7
    local.get 6
    local.get 7
    i32.shr_u
    local.set 8
    i32.const 255
    local.set 9
    local.get 8
    local.get 9
    i32.and
    local.set 10
    i32.const 255
    local.set 11
    local.get 10
    local.get 11
    i32.xor
    local.set 12
    local.get 3
    i32.load offset=12
    local.set 13
    i32.const 90
    local.set 14
    local.get 14
    local.get 13
    i32.sub
    local.set 15
    i32.const 8
    local.set 16
    local.get 15
    local.get 16
    i32.shr_u
    local.set 17
    i32.const 255
    local.set 18
    local.get 17
    local.get 18
    i32.and
    local.set 19
    i32.const 255
    local.set 20
    local.get 19
    local.get 20
    i32.xor
    local.set 21
    local.get 12
    local.get 21
    i32.and
    local.set 22
    local.get 3
    i32.load offset=12
    local.set 23
    i32.const 65
    local.set 24
    local.get 23
    local.get 24
    i32.sub
    local.set 25
    local.get 22
    local.get 25
    i32.and
    local.set 26
    local.get 3
    i32.load offset=12
    local.set 27
    i32.const 97
    local.set 28
    local.get 27
    local.get 28
    i32.sub
    local.set 29
    i32.const 8
    local.set 30
    local.get 29
    local.get 30
    i32.shr_u
    local.set 31
    i32.const 255
    local.set 32
    local.get 31
    local.get 32
    i32.and
    local.set 33
    i32.const 255
    local.set 34
    local.get 33
    local.get 34
    i32.xor
    local.set 35
    local.get 3
    i32.load offset=12
    local.set 36
    i32.const 122
    local.set 37
    local.get 37
    local.get 36
    i32.sub
    local.set 38
    i32.const 8
    local.set 39
    local.get 38
    local.get 39
    i32.shr_u
    local.set 40
    i32.const 255
    local.set 41
    local.get 40
    local.get 41
    i32.and
    local.set 42
    i32.const 255
    local.set 43
    local.get 42
    local.get 43
    i32.xor
    local.set 44
    local.get 35
    local.get 44
    i32.and
    local.set 45
    local.get 3
    i32.load offset=12
    local.set 46
    i32.const 71
    local.set 47
    local.get 46
    local.get 47
    i32.sub
    local.set 48
    local.get 45
    local.get 48
    i32.and
    local.set 49
    local.get 26
    local.get 49
    i32.or
    local.set 50
    local.get 3
    i32.load offset=12
    local.set 51
    i32.const 48
    local.set 52
    local.get 51
    local.get 52
    i32.sub
    local.set 53
    i32.const 8
    local.set 54
    local.get 53
    local.get 54
    i32.shr_u
    local.set 55
    i32.const 255
    local.set 56
    local.get 55
    local.get 56
    i32.and
    local.set 57
    i32.const 255
    local.set 58
    local.get 57
    local.get 58
    i32.xor
    local.set 59
    local.get 3
    i32.load offset=12
    local.set 60
    i32.const 57
    local.set 61
    local.get 61
    local.get 60
    i32.sub
    local.set 62
    i32.const 8
    local.set 63
    local.get 62
    local.get 63
    i32.shr_u
    local.set 64
    i32.const 255
    local.set 65
    local.get 64
    local.get 65
    i32.and
    local.set 66
    i32.const 255
    local.set 67
    local.get 66
    local.get 67
    i32.xor
    local.set 68
    local.get 59
    local.get 68
    i32.and
    local.set 69
    local.get 3
    i32.load offset=12
    local.set 70
    i32.const -4
    local.set 71
    local.get 70
    local.get 71
    i32.sub
    local.set 72
    local.get 69
    local.get 72
    i32.and
    local.set 73
    local.get 50
    local.get 73
    i32.or
    local.set 74
    local.get 3
    i32.load offset=12
    local.set 75
    i32.const 43
    local.set 76
    local.get 75
    local.get 76
    i32.xor
    local.set 77
    i32.const 0
    local.set 78
    local.get 78
    local.get 77
    i32.sub
    local.set 79
    i32.const 8
    local.set 80
    local.get 79
    local.get 80
    i32.shr_u
    local.set 81
    i32.const 255
    local.set 82
    local.get 81
    local.get 82
    i32.and
    local.set 83
    i32.const 255
    local.set 84
    local.get 83
    local.get 84
    i32.xor
    local.set 85
    i32.const 62
    local.set 86
    local.get 85
    local.get 86
    i32.and
    local.set 87
    local.get 74
    local.get 87
    i32.or
    local.set 88
    local.get 3
    i32.load offset=12
    local.set 89
    i32.const 47
    local.set 90
    local.get 89
    local.get 90
    i32.xor
    local.set 91
    i32.const 0
    local.set 92
    local.get 92
    local.get 91
    i32.sub
    local.set 93
    i32.const 8
    local.set 94
    local.get 93
    local.get 94
    i32.shr_u
    local.set 95
    i32.const 255
    local.set 96
    local.get 95
    local.get 96
    i32.and
    local.set 97
    i32.const 255
    local.set 98
    local.get 97
    local.get 98
    i32.xor
    local.set 99
    i32.const 63
    local.set 100
    local.get 99
    local.get 100
    i32.and
    local.set 101
    local.get 88
    local.get 101
    i32.or
    local.set 102
    local.get 3
    local.get 102
    i32.store offset=8
    local.get 3
    i32.load offset=8
    local.set 103
    local.get 3
    i32.load offset=8
    local.set 104
    i32.const 0
    local.set 105
    local.get 104
    local.get 105
    i32.xor
    local.set 106
    i32.const 0
    local.set 107
    local.get 107
    local.get 106
    i32.sub
    local.set 108
    i32.const 8
    local.set 109
    local.get 108
    local.get 109
    i32.shr_u
    local.set 110
    i32.const 255
    local.set 111
    local.get 110
    local.get 111
    i32.and
    local.set 112
    i32.const 255
    local.set 113
    local.get 112
    local.get 113
    i32.xor
    local.set 114
    local.get 3
    i32.load offset=12
    local.set 115
    i32.const 65
    local.set 116
    local.get 115
    local.get 116
    i32.xor
    local.set 117
    i32.const 0
    local.set 118
    local.get 118
    local.get 117
    i32.sub
    local.set 119
    i32.const 8
    local.set 120
    local.get 119
    local.get 120
    i32.shr_u
    local.set 121
    i32.const 255
    local.set 122
    local.get 121
    local.get 122
    i32.and
    local.set 123
    i32.const 255
    local.set 124
    local.get 123
    local.get 124
    i32.xor
    local.set 125
    i32.const 255
    local.set 126
    local.get 125
    local.get 126
    i32.xor
    local.set 127
    local.get 114
    local.get 127
    i32.and
    local.set 128
    local.get 103
    local.get 128
    i32.or
    local.set 129
    local.get 129
    return)
  (func $_base642bin_skip_padding (type 3) (param i32 i32 i32 i32 i32) (result i32)
    (local i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
    global.get $__stack_pointer
    local.set 5
    i32.const 32
    local.set 6
    local.get 5
    local.get 6
    i32.sub
    local.set 7
    local.get 7
    global.set $__stack_pointer
    local.get 7
    local.get 0
    i32.store offset=24
    local.get 7
    local.get 1
    i32.store offset=20
    local.get 7
    local.get 2
    i32.store offset=16
    local.get 7
    local.get 3
    i32.store offset=12
    local.get 7
    local.get 4
    i32.store offset=8
    block  ;; label = @1
      block  ;; label = @2
        loop  ;; label = @3
          local.get 7
          i32.load offset=8
          local.set 8
          i32.const 0
          local.set 9
          local.get 8
          local.get 9
          i32.gt_u
          local.set 10
          i32.const 1
          local.set 11
          local.get 10
          local.get 11
          i32.and
          local.set 12
          local.get 12
          i32.eqz
          br_if 1 (;@2;)
          local.get 7
          i32.load offset=16
          local.set 13
          local.get 13
          i32.load
          local.set 14
          local.get 7
          i32.load offset=20
          local.set 15
          local.get 14
          local.get 15
          i32.ge_u
          local.set 16
          i32.const 1
          local.set 17
          local.get 16
          local.get 17
          i32.and
          local.set 18
          block  ;; label = @4
            local.get 18
            i32.eqz
            br_if 0 (;@4;)
            i32.const -1
            local.set 19
            local.get 7
            local.get 19
            i32.store offset=28
            br 3 (;@1;)
          end
          local.get 7
          i32.load offset=24
          local.set 20
          local.get 7
          i32.load offset=16
          local.set 21
          local.get 21
          i32.load
          local.set 22
          local.get 20
          local.get 22
          i32.add
          local.set 23
          local.get 23
          i32.load8_u
          local.set 24
          i32.const 24
          local.set 25
          local.get 24
          local.get 25
          i32.shl
          local.set 26
          local.get 26
          local.get 25
          i32.shr_s
          local.set 27
          local.get 7
          local.get 27
          i32.store offset=4
          local.get 7
          i32.load offset=4
          local.set 28
          i32.const 61
          local.set 29
          local.get 28
          local.get 29
          i32.eq
          local.set 30
          i32.const 1
          local.set 31
          local.get 30
          local.get 31
          i32.and
          local.set 32
          block  ;; label = @4
            block  ;; label = @5
              local.get 32
              i32.eqz
              br_if 0 (;@5;)
              local.get 7
              i32.load offset=8
              local.set 33
              i32.const -1
              local.set 34
              local.get 33
              local.get 34
              i32.add
              local.set 35
              local.get 7
              local.get 35
              i32.store offset=8
              br 1 (;@4;)
            end
            local.get 7
            i32.load offset=12
            local.set 36
            i32.const 0
            local.set 37
            local.get 36
            local.get 37
            i32.eq
            local.set 38
            i32.const 1
            local.set 39
            local.get 38
            local.get 39
            i32.and
            local.set 40
            block  ;; label = @5
              block  ;; label = @6
                local.get 40
                br_if 0 (;@6;)
                local.get 7
                i32.load offset=12
                local.set 41
                local.get 7
                i32.load offset=4
                local.set 42
                local.get 41
                local.get 42
                call $strchr
                local.set 43
                i32.const 0
                local.set 44
                local.get 43
                local.get 44
                i32.eq
                local.set 45
                i32.const 1
                local.set 46
                local.get 45
                local.get 46
                i32.and
                local.set 47
                local.get 47
                i32.eqz
                br_if 1 (;@5;)
              end
              i32.const -1
              local.set 48
              local.get 7
              local.get 48
              i32.store offset=28
              br 4 (;@1;)
            end
          end
          local.get 7
          i32.load offset=16
          local.set 49
          local.get 49
          i32.load
          local.set 50
          i32.const 1
          local.set 51
          local.get 50
          local.get 51
          i32.add
          local.set 52
          local.get 49
          local.get 52
          i32.store
          br 0 (;@3;)
        end
      end
      i32.const 0
      local.set 53
      local.get 7
      local.get 53
      i32.store offset=28
    end
    local.get 7
    i32.load offset=28
    local.set 54
    i32.const 32
    local.set 55
    local.get 7
    local.get 55
    i32.add
    local.set 56
    local.get 56
    global.set $__stack_pointer
    local.get 54
    return)
  (func $main (type 1) (param i32 i32) (result i32)
    (local i32)
    call $__original_main
    local.set 2
    local.get 2
    return)
  (table (;0;) 1 1 funcref)
  (memory (;0;) 2)
  (global $__stack_pointer (mut i32) (i32.const 68912))
  (global (;1;) i32 (i32.const 2032))
  (global (;2;) i32 (i32.const 1024))
  (global (;3;) i32 (i32.const 1024))
  (global (;4;) i32 (i32.const 3369))
  (global (;5;) i32 (i32.const 3376))
  (global (;6;) i32 (i32.const 68912))
  (global (;7;) i32 (i32.const 1024))
  (global (;8;) i32 (i32.const 68912))
  (global (;9;) i32 (i32.const 131072))
  (global (;10;) i32 (i32.const 0))
  (global (;11;) i32 (i32.const 1))
  (export "memory" (memory 0))
  (export "__wasm_call_ctors" (func $__wasm_call_ctors))
  (export "strchr" (func $strchr))
  (export "__original_main" (func $__original_main))
  (export "b64" (global 1))
  (export "bin" (global 2))
  (export "main" (func $main))
  (export "__main_void" (func $__original_main))
  (export "__indirect_function_table" (table 0))
  (export "__dso_handle" (global 3))
  (export "__data_end" (global 4))
  (export "__stack_low" (global 5))
  (export "__stack_high" (global 6))
  (export "__global_base" (global 7))
  (export "__heap_base" (global 8))
  (export "__heap_end" (global 9))
  (export "__memory_base" (global 10))
  (export "__table_base" (global 11))
  (data $.data (i32.const 1024) "\b8\16y\f0\fe\fbq;2\ea\19\fa\92\ceI\80\12\e1:\d7\9a\d1s\b4\bc\9d\e8\bf\9d\09\1d_\8b\01\1cP\05\11M\9f2i\01\ac\b0\c0\e3\b4*U)\1b\b7\87mC\02Z\de\9f\92\13\b9\1a\95c\bea\91):\1c\17HZL\99R\f8\1f\9a([\ec\0d\f5\dd\da\b5\aa\9c\83\95\02\f6LS{\f2\db\daw6a\b1\92\07\0d\d0\1aW\e4\b1\fa\b0\d5\c3/\9b\0b\cb\c1\dd\ac\11\f7.\8d\cf\85$C\fcy\85}\82\03=\ce\890\02\e4\aaa\cf8ka91\be\05\d1\d2\96\bf\c8\92-#\d6*\b37Y\e4\9d\f4-KWh\fb\c2\95\9a\a8\fcX\ce\f5\83\84\d8H\13\a9\e6\b6U\f84\ae\d0\dfP\9c\11_\13A\1f0Sif\17S1\86\f7\1d\fd\bbK\c8\a2\80\b1&\9b\e3\cab4\8a\91`\1by\81\d0\82\91\e0\e7\06\81c\f6\8f\91\b9\92\8e\c1\12\b4<\ce \ef*\5c\15K'\dfb\ad\9a^\d6\a9\c7\89\c0\0cU\08\e3\92\a4H\fa7NT\baz\1c\ba\ac\18\16+\f0\de\b8\a9G\af9$0\c5\18\c2f\11g\e3#\95\00\f3\e3:Mg\9c\8e\9f\f7Z\15$\d7+^\ed\5c}\07\1e.\a1\ef\16\0d\b9T#RL\e3\80\90\8f\0e\ea\9dK\a5!\0d\1f\fa\fbD\ce\07t\e7\04\01\98o\05\94\18,[\92\1fJG\d7\e6\cc\92\16\97\e7\a1\c0\96Z\b5\b8XW\c0L\dc\c3\ce\22gR\c3*\9a}\97\7f%\8b2\ab\dfUr\f9\c0e\16NUYu9\18\c9\a1\b7\84\14\e73m>\99\d9\0a\16\d4G\22\d8\ab\c5\f4\b1\92\df\fa\b7+\b3\e8S\13\ef\a8\87\8aoPf\c8\bf\c7\e6\a5\acv\a1\bb,\cc\bb\fa;\f2\e5^\c39A\d4v\01\83\e74\b0\8c\f3V\d1}\04\81\8a\c10*\ebh\8f\82\cc\d9\b1LDu\f2\db\97;\1c[\09\1et\0f\1c\df\f9\1ef<\c9\12%v9\9d\90\d0\bc\1b\d8X\cdwT\b2\bd\a0\0a\05aTi\80A;\a7\ff\1e\a6\9a\acl\0e\9c\9c\01\16@\c6K\182\11\bb\aeq\88\b48y\dd\97|3\e6Z\bd\fd\abG$NF\85\a9!\b1D\83\cb\bf\db\a1\a9\17\1b\5c\03\af\b9{#<H\f9\ceA\af\ed\d9ml\f7\ae\fdiy\fc\03\c1\ec\9b\e3\18\e0\dcf\ed\c1\c9\1am\9f?\81\e97\93\99\96\fe\fek\a9dQJF\be\f2\8e\ecG\0f\b4\b2\e3\16v4\e2\e6G4\ca\0e9\97\1c\86\03\c2\fe\96)\ceD\e9BH&\f8c\12\0d\dfr\84)\cc\1a\e8Z\98F\c9B\1b\8a\8a\b5\0d\a4\ee\acdc\eb\9c\eb5\ca\f8 f\d1\a3\f2\14\9b\85\eeI\f2x\8b/\02\89\b1\8d1}^\03\da\f8e\1f\ebS\8e\f8u\e4\f9\07\a8\1e\0f\b2\17\fd\db\15Vl\11lYT\c2\db\9f\0fx'\ecC\e6OBW\7f0\bf\8a\b1iSy1ExN\dcI\e0\a0\95d\d9\ac\db\e8p\13\c22\b5\cd\ee\aaI\df\14u\bd\e3\b3\c1S%\08\cbt:\8a\cezJ\13U\ab\95\c2\83\9bn\e6/q\96j^\00\ae\b1\cc\c4\99\84\cb\b4\ed\bdm\f2\bfK\1fG$\e8\96\08\c6\96AA\dd\80'W\d5K\f87\fao\ec\13d;\91s\22SEo\81\00\c2\eeI\92\81|!|ip>\a3U\81T\f1\1f\ab#\07\edV\ff\1em>\9a\07\dcd\f3#\92\92\d8B\fcU\a5\8aQ\7f$\be\b3]\0c\bdjU\94\13\dc\f2F\f4$\11Ng\1e\12\d2\8e\18w,\fc=Y\06f\b7\8f\8a\18K\ab\1b\04* r\1e\bd\d0\f0\89\e8\92g3.\02\81\b1\9d\d4^X\9f\8f\95$r\05I\b7{\a8\d8\c2\967\88\81\a1\89\f6\a3%"))
