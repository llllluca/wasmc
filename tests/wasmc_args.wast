;; wasmc: not from the upstream suite. Calls with more arguments than the
;; RV32 argument registers.
;;
;; a0 holds exec_env, so WASM arguments get a1..a7 and the rest go on the
;; stack, in the caller's outgoing argument area (emit.c abi_place()):
;;   - an i32/f32 past a7 takes a 4-byte stack slot;
;;   - an i64/f64 with only a7 left is split: low word in a7, high word in
;;     the first stack slot;
;;   - an i64/f64 with no register left takes an 8-byte aligned stack slot,
;;     leaving a 4-byte hole after an odd number of 4-byte slots.
;; The callees fold their parameters into an order-sensitive hash
;; (acc = rotl(acc, 7) xor p, as i64), so a parameter read from the wrong
;; place, a missing high word or a swap changes the result. Other callees
;; return a single parameter, which checks it bit for bit. The expected
;; values come from wabt's spectest-interp.

(module
  (type $t12 (func (param i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
                   (result i64)))

  (table funcref (elem $h12 $h12_rev))

  ;; ---- 12 x i32: a1..a7, then sp+0..sp+16 -------------------------------

  (func $h12 (type $t12)
    (local $acc i64)
    (local.set $acc (i64.extend_i32_u (local.get 0)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 1))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 2))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 3))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 4))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 5))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 6))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 7))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 8))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 9))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 10))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 11))))
    (local.get $acc))

  ;; same type, parameters folded in reverse order
  (func $h12_rev (type $t12)
    (local $acc i64)
    (local.set $acc (i64.extend_i32_u (local.get 11)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 10))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 9))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 8))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 7))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 6))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 5))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 4))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 3))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 2))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 1))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 0))))
    (local.get $acc))

  (func $last12 (param i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
                (result i32)
    (local.get 11))

  (func (export "i32x12")
                (result i64)
    (call $h12 (i32.const 0x01010101) (i32.const 0x02020202)
               (i32.const 0x03030303) (i32.const 0x04040404)
               (i32.const 0x05050505) (i32.const 0x06060606)
               (i32.const 0x07070707) (i32.const 0x08080808)
               (i32.const 0x09090909) (i32.const 0x0a0a0a0a)
               (i32.const 0x0b0b0b0b) (i32.const 0xfedcba98)))

  (func (export "i32x12-last") (result i32)
    (call $last12 (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                  (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                  (i32.const 9) (i32.const 10) (i32.const 11)
                  (i32.const 0xdeadbeef)))

  ;; arguments computed at run time, from parameters live across the call
  (func (export "i32x12-dyn") (param $x i32) (result i64)
    (call $h12 (local.get $x)
               (i32.add (local.get $x) (i32.const 1))
               (i32.mul (local.get $x) (i32.const 3))
               (i32.xor (local.get $x) (i32.const -1))
               (i32.shl (local.get $x) (i32.const 4))
               (i32.sub (i32.const 0) (local.get $x))
               (i32.rotl (local.get $x) (i32.const 13))
               (i32.add (local.get $x) (i32.const 0x1000))
               (i32.shr_u (local.get $x) (i32.const 3))
               (i32.or (local.get $x) (i32.const 0x80000000))
               (i32.and (local.get $x) (i32.const 0xffff))
               (i32.mul (local.get $x) (local.get $x))))

  ;; call_indirect with stack arguments
  (func (export "i32x12-indirect") (param $i i32) (result i64)
    (call_indirect (type $t12)
      (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
      (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
      (i32.const 9) (i32.const 10) (i32.const 11) (i32.const 12)
      (local.get $i)))

  ;; ---- i64 split between a7 and the stack -------------------------------
  ;; (i32 x6, i64, i32): i32s in a1..a6, the i64 low in a7 and high at
  ;; sp+0, the last i32 at sp+4

  (func $split_i64 (param i32 i32 i32 i32 i32 i32 i64 i32) (result i64)
    (local.get 6))
  (func $split_after (param i32 i32 i32 i32 i32 i32 i64 i32) (result i32)
    (local.get 7))
  (func $split_hash (param i32 i32 i32 i32 i32 i32 i64 i32) (result i64)
    (local $acc i64)
    (local.set $acc (i64.extend_i32_u (local.get 0)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 1))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 2))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 3))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 4))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 5))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 6)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 7))))
    (local.get $acc))

  (func (export "split-i64") (result i64)
    (call $split_i64 (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                     (i32.const 5) (i32.const 6)
                     (i64.const 0x8877665544332211) (i32.const 7)))
  (func (export "split-after") (result i32)
    (call $split_after (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                       (i32.const 5) (i32.const 6)
                       (i64.const 0x8877665544332211) (i32.const 0xcafef00d)))
  (func (export "split-hash") (param $x i64) (result i64)
    (call $split_hash (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                      (i32.const 5) (i32.const 6)
                      (local.get $x) (i32.const 7)))

  ;; ---- i64 in 8-byte aligned stack slots --------------------------------
  ;; (i32 x8, i64, i32, i64): i32s in a1..a7 and sp+0, the first i64 at
  ;; sp+8 (hole at sp+4), the i32 at sp+16, the second i64 at sp+24

  (func $aligned_a (param i32 i32 i32 i32 i32 i32 i32 i32 i64 i32 i64)
                   (result i64)
    (local.get 8))
  (func $aligned_mid (param i32 i32 i32 i32 i32 i32 i32 i32 i64 i32 i64)
                     (result i32)
    (local.get 9))
  (func $aligned_b (param i32 i32 i32 i32 i32 i32 i32 i32 i64 i32 i64)
                   (result i64)
    (local.get 10))

  (func (export "aligned-a") (result i64)
    (call $aligned_a (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                     (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                     (i64.const 0x0123456789abcdef) (i32.const 9)
                     (i64.const 0xfedcba9876543210)))
  (func (export "aligned-mid") (result i32)
    (call $aligned_mid (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                       (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                       (i64.const 0x0123456789abcdef) (i32.const 0x13579bdf)
                       (i64.const 0xfedcba9876543210)))
  (func (export "aligned-b") (result i64)
    (call $aligned_b (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                     (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                     (i64.const 0x0123456789abcdef) (i32.const 9)
                     (i64.const 0xfedcba9876543210)))

  ;; ---- floats (soft-float: passed like i32/i64) -------------------------
  ;; (f64 x3, f64, f32, f64, f32): three f64 in a1..a6, the fourth split
  ;; a7/sp+0, f32 at sp+4, f64 at sp+8, f32 at sp+16

  (func $floats (param f64 f64 f64 f64 f32 f64 f32) (result i64)
    (local $acc i64)
    (local.set $acc (i64.reinterpret_f64 (local.get 0)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.reinterpret_f64 (local.get 1))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.reinterpret_f64 (local.get 2))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.reinterpret_f64 (local.get 3))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (i32.reinterpret_f32 (local.get 4)))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.reinterpret_f64 (local.get 5))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (i32.reinterpret_f32 (local.get 6)))))
    (local.get $acc))
  (func $floats_sum (param f64 f64 f64 f64 f32 f64 f32) (result f64)
    (f64.add
      (f64.add (f64.add (local.get 0) (local.get 1))
               (f64.add (local.get 2) (local.get 3)))
      (f64.add (f64.promote_f32 (local.get 4))
               (f64.add (local.get 5) (f64.promote_f32 (local.get 6))))))

  (func (export "floats") (result i64)
    (call $floats (f64.const 1.5) (f64.const -2.25) (f64.const 3.125)
                  (f64.const 1e300) (f32.const 0.1) (f64.const -0x1.fp-1000)
                  (f32.const -7.75)))
  (func (export "floats-sum") (result f64)
    (call $floats_sum (f64.const 1) (f64.const 2) (f64.const 4)
                      (f64.const 8) (f32.const 16) (f64.const 32)
                      (f32.const 64)))

  ;; ---- 16 x i64: only the first three in registers ----------------------

  (func $h16 (param i64 i64 i64 i64 i64 i64 i64 i64
                    i64 i64 i64 i64 i64 i64 i64 i64) (result i64)
    (local $acc i64)
    (local.set $acc (local.get 0))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 1)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 2)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 3)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 4)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 5)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 6)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 7)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 8)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 9)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 10)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 11)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 12)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 13)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 14)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 15)))
    (local.get $acc))

  (func (export "i64x16") (result i64)
    (call $h16 (i64.const 0x9e3779b97f4a7c15) (i64.const 0xbf58476d1ce4e5b9)
               (i64.const 0x94d049bb133111eb) (i64.const 0x2545f4914f6cdd1d)
               (i64.const 0x6a09e667f3bcc908) (i64.const 0xbb67ae8584caa73b)
               (i64.const 0x3c6ef372fe94f82b) (i64.const 0xa54ff53a5f1d36f1)
               (i64.const 0x510e527fade682d1) (i64.const 0x9b05688c2b3e6c1f)
               (i64.const 0x1f83d9abfb41bd6b) (i64.const 0x5be0cd19137e2179)
               (i64.const 0xcbbb9d5dc1059ed8) (i64.const 0x629a292a367cd507)
               (i64.const 0x9159015a3070dd17) (i64.const 0x0123456789abcdef)))

  ;; ---- stack parameters passed on: read incoming, write outgoing --------
  ;; exported, so the host also calls it with stack arguments

  (func $pass (export "pass")
              (param i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32 i32)
              (result i64)
    ;; rotate the arguments by one, then hash in reverse
    (call $h12_rev (local.get 11) (local.get 0) (local.get 1) (local.get 2)
                   (local.get 3) (local.get 4) (local.get 5) (local.get 6)
                   (local.get 7) (local.get 8) (local.get 9) (local.get 10)))

  ;; exported callee with stack i64s, called by the host
  (func (export "mixed-export") (param i32 i64 i32 i64 i32 i64 i32 i64 i32 i64)
                                (result i64)
    (local $acc i64)
    (local.set $acc (i64.extend_i32_u (local.get 0)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 1)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 2))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 3)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 4))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 5)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 6))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 7)))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (i64.extend_i32_u (local.get 8))))
    (local.set $acc (i64.xor (i64.rotl (local.get $acc) (i64.const 7)) (local.get 9)))
    (local.get $acc))

  ;; ---- recursion: every level passes 10 arguments, 3 on the stack -------

  (func $rec (export "rec")
             (param $n i32) (param i32 i32 i32 i32 i32 i32 i32 i32 i32)
             (result i64)
    (if (result i64) (i32.eqz (local.get $n))
      (then
        (call $h12 (local.get 1) (local.get 2) (local.get 3) (local.get 4)
                   (local.get 5) (local.get 6) (local.get 7) (local.get 8)
                   (local.get 9) (i32.const 0) (i32.const 0) (i32.const 0)))
      (else
        ;; rotate the nine values and perturb one
        (call $rec (i32.sub (local.get $n) (i32.const 1))
                   (local.get 9)
                   (i32.add (local.get 1) (local.get $n))
                   (local.get 2) (local.get 3) (local.get 4)
                   (local.get 5) (local.get 6) (local.get 7) (local.get 8)))))
)

(assert_return (invoke "i32x12") (i64.const 0xaeab833ef9de900f))
(assert_return (invoke "i32x12-last") (i32.const 0xdeadbeef))
(assert_return (invoke "i32x12-dyn" (i32.const 0x12345678)) (i64.const 0x31fd1a49cdd87524))
(assert_return (invoke "i32x12-dyn" (i32.const -1)) (i64.const 0x801c24f87061df87))
(assert_return (invoke "i32x12-indirect" (i32.const 0)) (i64.const 0x840a18388122a50d))
(assert_return (invoke "i32x12-indirect" (i32.const 1)) (i64.const 0x09101c30508143c4))

(assert_return (invoke "split-i64") (i64.const 0x8877665544332211))
(assert_return (invoke "split-after") (i32.const 0xcafef00d))
(assert_return (invoke "split-hash" (i64.const 0xfedcba9876543210)) (i64.const 0x6e5f44236ab88878))
(assert_return (invoke "split-hash" (i64.const 1)) (i64.const 0x0002081840a18087))

(assert_return (invoke "aligned-a") (i64.const 0x0123456789abcdef))
(assert_return (invoke "aligned-mid") (i32.const 0x13579bdf))
(assert_return (invoke "aligned-b") (i64.const 0xfedcba9876543210))

(assert_return (invoke "floats") (i64.const 0x38110f84a45416bc))
(assert_return (invoke "floats-sum") (f64.const 127))

(assert_return (invoke "i64x16") (i64.const 0xc72a3aae771b58c9))

(assert_return (invoke "pass"
                 (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                 (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                 (i32.const 9) (i32.const 10) (i32.const 11) (i32.const 12))
               (i64.const 0x880e18284061e208))
(assert_return (invoke "mixed-export"
                 (i32.const 0x11111111) (i64.const 0x2222222222222222)
                 (i32.const 0x33333333) (i64.const 0x4444444444444444)
                 (i32.const 0x55555555) (i64.const 0x6666666666666666)
                 (i32.const 0x77777777) (i64.const 0x8888888888888888)
                 (i32.const 0x99999999) (i64.const 0xaaaaaaaaaaaaaaaa))
               (i64.const 0x911f99d7f779ffb1))
(assert_return (invoke "rec" (i32.const 0)
                 (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                 (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                 (i32.const 9))
               (i64.const 0x840a183881202081))
(assert_return (invoke "rec" (i32.const 100)
                 (i32.const 1) (i32.const 2) (i32.const 3) (i32.const 4)
                 (i32.const 5) (i32.const 6) (i32.const 7) (i32.const 8)
                 (i32.const 9))
               (i64.const 0x9736bd9c79abf6c1))
