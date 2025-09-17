(; Expected final value 65 ;)
(module
  (type (;0;) (func (result i32)))
  (type (;1;) (func (param i32 i32 i32) (result i32)))
  (func (;0;) (type 0) (result i32)
    i32.const 1 (; param 0 ;)
    i32.const 50 (; param 1 ;)
    i32.const 101 (; param 2 ;)
    call 1)
  (func (;1;) (type 1) (param i32 i32 i32) (result i32)
    (local i32)
    i32.const 10
    local.set 3
    local.get 0
    (if
      (then 
        local.get 1
        local.get 2
        (if
          (then
            i32.const 22
            i32.const 7
            i32.sub
            local.set 3
          )
        )
        local.get 3
        i32.add
        local.set 3
      )
      (else 
        local.get 2
        local.get 1
        i32.sub
        local.set 3
      )
    )
    local.get 3
  )
  (export "_start" (func 0)))
