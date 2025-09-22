(module
  (type (;0;) (func (result i32)))
  (type (;1;) (func (param i32 i32) (result i32)))
  (func (;0;) (type 0) (result i32)
    i32.const 44
    i32.const 100
    call 1)
  (func (;1;) (type 1) (param i32 i32) (result i32)
    i32.const -44
    local.get 0
    i32.lt_u
    (if (result i32)
      (then
        local.get 1
        i32.const 10
        i32.add
      )
      (else
        local.get 1
        i32.const 10
        i32.sub
      )
    )
  )
  (export "_start" (func 0)))
