(module
  (type (;0;) (func (result i32)))
  (type (;1;) (func (param i32 i32 i32) (result i32)))
  (func (;0;) (type 0) (result i32)
    i32.const 0 (; param 0 ;)
    i32.const 50 (; param 1 ;)
    i32.const 100 (; param 2 ;)
    call 1)
  (func (;1;) (type 1) (param i32 i32 i32) (result i32)
    local.get 0
    (if (result i32)
      (then 
        local.get 1
        local.get 2
        i32.add (; param 1 + param 2 ;)
      )
      (else 
        local.get 1
        local.get 2
        i32.sub (; param 1 - param 2 ;)
      )
    )
  )
  (export "_start" (func 0)))
