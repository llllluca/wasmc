;; wasmc: not from the upstream suite.
;; Functions with many written locals and many block/if constructs: the SSA
;; builder reads locals through the regions (compile.c setup_regions(),
;; REGIONS_MIN_WRITTEN), skipping the constructs that do not write them.
;; Expected values from wabt's wasm-interp.

(module
  ;; chains of if/else, each arm writing a different local; all read at the end
  (func (export "diamonds") (param $p i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local $e i32) (local $f i32) (local $g i32) (local $h i32) (local $i i32)
    (local.set $a (i32.const 1)) (local.set $b (i32.const 2))
    (local.set $c (i32.const 3)) (local.set $d (i32.const 4))
    (local.set $e (i32.const 5)) (local.set $f (i32.const 6))
    (local.set $g (i32.const 7)) (local.set $h (i32.const 8))
    (local.set $i (i32.const 9))
    (if (i32.and (local.get $p) (i32.const 1))
      (then (local.set $a (i32.add (local.get $a) (i32.const 10))))
      (else (local.set $b (i32.add (local.get $b) (i32.const 20)))))
    (if (i32.and (local.get $p) (i32.const 2))
      (then (local.set $c (i32.mul (local.get $c) (local.get $a))))
      (else (local.set $d (i32.sub (local.get $d) (local.get $b)))))
    (if (i32.and (local.get $p) (i32.const 4))
      (then (local.set $e (i32.add (local.get $e) (local.get $c))))
      (else (local.set $f (i32.xor (local.get $f) (local.get $d)))))
    (if (i32.and (local.get $p) (i32.const 8))
      (then (local.set $g (i32.add (local.get $g) (local.get $e))))
      (else (local.set $h (i32.add (local.get $h) (local.get $f)))))
    (if (i32.and (local.get $p) (i32.const 16))
      (then (local.set $i (i32.add (local.get $i) (local.get $a))))
      (else (local.set $i (i32.sub (local.get $i) (local.get $b)))))
    ;; reads through all five joins
    (i32.add
      (i32.add
        (i32.add (i32.mul (local.get $a) (i32.const 3))
                 (i32.mul (local.get $b) (i32.const 5)))
        (i32.add (i32.mul (local.get $c) (i32.const 7))
                 (i32.mul (local.get $d) (i32.const 11))))
      (i32.add
        (i32.add (i32.mul (local.get $e) (i32.const 13))
                 (i32.mul (local.get $f) (i32.const 17)))
        (i32.add
          (i32.add (i32.mul (local.get $g) (i32.const 19))
                   (i32.mul (local.get $h) (i32.const 23)))
          (i32.mul (local.get $i) (i32.const 29)))))
  )

  ;; a block whose body writes in the block it was entered from, exits to
  ;; outer labels from nested constructs, an if without else, a loop inside
  ;; a region, and reads inside the regions of locals they do not write
  (func (export "exits") (param $p i32) (param $q i32) (result i32)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local $e i32) (local $f i32) (local $g i32) (local $h i32) (local $n i32)
    (local.set $a (i32.const 3)) (local.set $b (i32.const 5))
    (local.set $c (i32.const 7)) (local.set $d (i32.const 11))
    (local.set $e (i32.const 13)) (local.set $f (i32.const 17))
    (local.set $g (i32.const 19)) (local.set $h (i32.const 23))
    (block $out
      ;; written in the head block itself, after the block's entry
      (local.set $a (i32.add (local.get $a) (local.get $p)))
      (block $mid
        (if (i32.gt_s (local.get $p) (i32.const 5))
          (then
            (local.set $b (i32.mul (local.get $b) (local.get $q)))
            (br_if $out (i32.gt_s (local.get $q) (i32.const 3)))
            (local.set $c (i32.add (local.get $c) (local.get $a))))
          (else
            (local.set $d (i32.add (local.get $d) (local.get $b)))
            (br_if $mid (i32.lt_s (local.get $q) (i32.const 2)))
            (local.set $e (i32.sub (local.get $e) (local.get $c)))))
        ;; an if without else
        (if (i32.eq (local.get $q) (i32.const 2))
          (then (local.set $f (i32.add (local.get $f) (local.get $d)))))
        ;; a loop inside the region: g += b, q times
        (local.set $n (local.get $q))
        (block $done
          (loop $l
            (br_if $done (i32.le_s (local.get $n) (i32.const 0)))
            (local.set $g (i32.add (local.get $g) (local.get $b)))
            (local.set $n (i32.sub (local.get $n) (i32.const 1)))
            (br $l))))
      ;; read inside $out of locals $mid does not write
      (local.set $h (i32.add (local.get $h) (i32.add (local.get $e) (local.get $f)))))
    (i32.add
      (i32.add
        (i32.add (i32.mul (local.get $a) (i32.const 3))
                 (i32.mul (local.get $b) (i32.const 5)))
        (i32.add (i32.mul (local.get $c) (i32.const 7))
                 (i32.mul (local.get $d) (i32.const 11))))
      (i32.add
        (i32.add (i32.mul (local.get $e) (i32.const 13))
                 (i32.mul (local.get $f) (i32.const 17)))
        (i32.add (i32.mul (local.get $g) (i32.const 19))
                 (i32.mul (local.get $h) (i32.const 23)))))
  )

  ;; a br_table into nested blocks, each case writing one local, i64 locals too
  (func (export "switch") (param $p i32) (result i64)
    (local $a i32) (local $b i32) (local $c i32) (local $d i32)
    (local $x i64) (local $y i64) (local $z i64) (local $w i64)
    (local.set $a (i32.const 1)) (local.set $b (i32.const 2))
    (local.set $c (i32.const 4)) (local.set $d (i32.const 8))
    (local.set $x (i64.const 100)) (local.set $y (i64.const 200))
    (local.set $z (i64.const 300)) (local.set $w (i64.const 400))
    (block $end
      (block $c3
        (block $c2
          (block $c1
            (block $c0
              (br_table $c0 $c1 $c2 $c3 (local.get $p)))
            (local.set $a (i32.add (local.get $a) (i32.const 16)))
            (local.set $x (i64.mul (local.get $x) (i64.const 3)))
            (br $end))
          (local.set $b (i32.add (local.get $b) (local.get $a)))
          (local.set $y (i64.add (local.get $y) (local.get $x))))
        (local.set $c (i32.add (local.get $c) (local.get $b)))
        (local.set $z (i64.sub (local.get $z) (local.get $y)))
        (br $end))
      (local.set $d (i32.mul (local.get $d) (local.get $c)))
      (local.set $w (i64.add (local.get $w) (local.get $z))))
    (i64.add
      (i64.add (i64.extend_i32_u (i32.add (i32.add (local.get $a) (local.get $b))
                                          (i32.add (local.get $c) (local.get $d))))
               (i64.add (local.get $x) (local.get $y)))
      (i64.add (local.get $z) (local.get $w)))
  )
)

(assert_return (invoke "diamonds" (i32.const 0)) (i32.const -1019))
(assert_return (invoke "diamonds" (i32.const 1)) (i32.const 831))
(assert_return (invoke "diamonds" (i32.const 2)) (i32.const 263))
(assert_return (invoke "diamonds" (i32.const 3)) (i32.const 983))
(assert_return (invoke "diamonds" (i32.const 5)) (i32.const 950))
(assert_return (invoke "diamonds" (i32.const 6)) (i32.const 462))
(assert_return (invoke "diamonds" (i32.const 7)) (i32.const 1572))
(assert_return (invoke "diamonds" (i32.const 9)) (i32.const 834))
(assert_return (invoke "diamonds" (i32.const 12)) (i32.const 234))
(assert_return (invoke "diamonds" (i32.const 15)) (i32.const 2156))
(assert_return (invoke "diamonds" (i32.const 16)) (i32.const -352))
(assert_return (invoke "diamonds" (i32.const 21)) (i32.const 1327))
(assert_return (invoke "diamonds" (i32.const 26)) (i32.const 979))
(assert_return (invoke "diamonds" (i32.const 31)) (i32.const 2533))
(assert_return (invoke "exits" (i32.const 0) (i32.const 0)) (i32.const 2297))
(assert_return (invoke "exits" (i32.const 0) (i32.const 1)) (i32.const 2297))
(assert_return (invoke "exits" (i32.const 0) (i32.const 2)) (i32.const 2875))
(assert_return (invoke "exits" (i32.const 0) (i32.const 3)) (i32.const 2330))
(assert_return (invoke "exits" (i32.const 0) (i32.const 4)) (i32.const 2425))
(assert_return (invoke "exits" (i32.const 0) (i32.const 7)) (i32.const 2710))
(assert_return (invoke "exits" (i32.const 3) (i32.const 0)) (i32.const 2306))
(assert_return (invoke "exits" (i32.const 3) (i32.const 1)) (i32.const 2306))
(assert_return (invoke "exits" (i32.const 3) (i32.const 2)) (i32.const 2884))
(assert_return (invoke "exits" (i32.const 3) (i32.const 3)) (i32.const 2339))
(assert_return (invoke "exits" (i32.const 3) (i32.const 4)) (i32.const 2434))
(assert_return (invoke "exits" (i32.const 3) (i32.const 7)) (i32.const 2719))
(assert_return (invoke "exits" (i32.const 6) (i32.const 0)) (i32.const 2298))
(assert_return (invoke "exits" (i32.const 6) (i32.const 1)) (i32.const 2418))
(assert_return (invoke "exits" (i32.const 6) (i32.const 2)) (i32.const 3168))
(assert_return (invoke "exits" (i32.const 6) (i32.const 3)) (i32.const 3228))
(assert_return (invoke "exits" (i32.const 6) (i32.const 4)) (i32.const 1645))
(assert_return (invoke "exits" (i32.const 6) (i32.const 7)) (i32.const 1720))
(assert_return (invoke "exits" (i32.const 9) (i32.const 0)) (i32.const 2328))
(assert_return (invoke "exits" (i32.const 9) (i32.const 1)) (i32.const 2448))
(assert_return (invoke "exits" (i32.const 9) (i32.const 2)) (i32.const 3198))
(assert_return (invoke "exits" (i32.const 9) (i32.const 3)) (i32.const 3258))
(assert_return (invoke "exits" (i32.const 9) (i32.const 4)) (i32.const 1654))
(assert_return (invoke "exits" (i32.const 9) (i32.const 7)) (i32.const 1729))
(assert_return (invoke "switch" (i32.const 0)) (i64.const 1231))
(assert_return (invoke "switch" (i32.const 1)) (i64.const 819))
(assert_return (invoke "switch" (i32.const 2)) (i64.const 817))
(assert_return (invoke "switch" (i32.const 3)) (i64.const 1339))
(assert_return (invoke "switch" (i32.const 4)) (i64.const 1339))
(assert_return (invoke "switch" (i32.const 9)) (i64.const 1339))
