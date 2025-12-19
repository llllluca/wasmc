(module
  (type (;0;) (func (result i32)))
  (type (;1;) (func (param i32 i32) (result i32)))
  (func (;0;) (type 0) (result i32)
    i32.const 100
    i32.const 1
    call 1)
  (func (;1;) (type 1) (param i32 i32) (result i32)
        (local i32 i32)
        i32.const 3
        local.set 2
        (loop
          local.get 3
          i32.const 10
          i32.add
          local.set 3

          local.get 2
          i32.const 1
          i32.sub
          local.set 2
          local.get 2
          br_if 0
        )
        local.get 3
    )
  (export "main" (func 0)))
