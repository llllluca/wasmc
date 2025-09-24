(module
  (type (;0;) (func (result i32)))
  (func (type 0) (result i32)
    i32.const 1024
    i32.load
    return)
  (export "_start" (func 0))
  (memory (;0;) 2)
  (data $.data (i32.const 1024) "\0a\00\00\00\22\00\00\00{\00\00\00W\09\00\00\17\00\00\00\00\00\00\00\15\00\00\00C\00\00\00 \00\00\00W\00\00\00"))
