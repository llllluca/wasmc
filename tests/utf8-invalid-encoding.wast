;;;;;; Invalid UTF-8 export names
;;
;; Binary version of the official utf8-invalid-encoding.wast, whose 176
;; cases are text modules, (module quote "(func (export \"...\"))"),
;; that test the text parser and so could not be run by wasmc. Each case
;; here exports a function under the same invalid UTF-8 byte sequence,
;; so it tests the export section decoder instead. Same order as the
;; original file; the comment above each case is its name in text form.
;; Generated from the original file (in git history, commit cd3af90).

;; "\00\00\fe\ff"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\00\00\fe\ff"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\80"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\8f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\8f"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\90"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\9f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\9f"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\a0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\a0"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\bf"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c0\80"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c0\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c0\bf"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c1\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c1\80"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c1\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c1\bf"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c2\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c2\00"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c2\2e"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c2\2e"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c2\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c2\7f"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c2\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\c2\80\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c2\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c2\c0"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c2"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\c2"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\c2\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\c2\fd"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\df\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\df\00"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\df\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\df\7f"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\df\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\df\c0"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\df\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\df\fd"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\00\a0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\00\a0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\7f\a0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\7f\a0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\80\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\80\a0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\80\a0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\9f\a0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\9f\a0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\9f\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\9f\bf"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\a0\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\a0\00"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\a0\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\a0\7f"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\a0\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\a0\c0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\a0\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\a0\fd"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\c0\a0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\c0\a0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e0\fd\a0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e0\fd\a0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\00\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\2e"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\e1\2e"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\7f\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\80\00"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\80\2e"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\80\2e"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\80\7f"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\e1\80\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\80\c0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\e1\80"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\80\fd"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\c0\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\e1"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\e1\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\e1\fd\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\00\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\7f\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\80\00"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\80\7f"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\80\c0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\80\fd"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\c0\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ec\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ec\fd\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\00\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\7f\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\80\00"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\80\7f"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\80\c0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\80\fd"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\a0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\a0\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\a0\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\a0\bf"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\bf\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\bf\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\bf\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\bf\bf"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\c0\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ed\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ed\fd\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\00\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\7f\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\80\00"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\80\7f"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\80\c0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\80\fd"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\c0\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ee\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ee\fd\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\00\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\7f\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\80\00"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\80\7f"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\80\c0"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\80\fd"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\c0\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ef\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\ef\fd\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\00\90\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\00\90\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\7f\90\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\7f\90\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\80\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\80\90\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\80\90\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\8f\90\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\8f\90\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\8f\bf\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\8f\bf\bf"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\00\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\00\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\7f\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\7f\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\90\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\90\00"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\90\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\90\7f"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\90\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\90\c0"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\90\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\90\fd"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\c0\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\c0\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\90\fd\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\90\fd\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\c0\90\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\c0\90\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f0\fd\90\90"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f0\fd\90\90"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\00\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\00\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\f1\23"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\7f\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\7f\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\00\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\f1\80\23"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\7f\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\80\00"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\80\23"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\80\7f"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\09\01"                    ;; export section
    "\05\f1\80\80\80\80"           ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\80\c0"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\f1\80\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\80\fd"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\c0\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\f1\80"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\80\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\80\fd\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\c0\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\c0\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\f1"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f1\fd\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f1\fd\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\00\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\00\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\7f\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\7f\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\00\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\7f\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\80\00"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\80\7f"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\80\c0"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\80\fd"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\c0\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\80\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\80\fd\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\c0\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\c0\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f3\fd\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f3\fd\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\00\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\00\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\7f\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\7f\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\00\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\00\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\7f\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\7f\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\80\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\80\00"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\80\7f"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\80\7f"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\80\c0"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\80\c0"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\80\fd"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\80\fd"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\c0\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\c0\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\80\fd\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\80\fd\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\90\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\90\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\bf\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\bf\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\c0\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\c0\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f4\fd\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f4\fd\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f5\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f5\80\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f7\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f7\80\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f7\bf\bf\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f7\bf\bf\bf"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\f8\23"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\f8\80\23"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f8\80\80\23"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80\80\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\09\01"                    ;; export section
    "\05\f8\80\80\80\23"           ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80\80\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\0a\01"                    ;; export section
    "\06\f8\80\80\80\80\80"        ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\09\01"                    ;; export section
    "\05\f8\80\80\80\80"           ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\f8\80\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\f8\80\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\f8\80"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\f8"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\f8"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fb\bf\bf\bf\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\09\01"                    ;; export section
    "\05\fb\bf\bf\bf\bf"           ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\fc\23"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\fc\80\23"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\fc\80\80\23"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\09\01"                    ;; export section
    "\05\fc\80\80\80\23"           ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80\80\80\23"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\0a\01"                    ;; export section
    "\06\fc\80\80\80\80\23"        ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80\80\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\0b\01"                    ;; export section
    "\07\fc\80\80\80\80\80\80"     ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\0a\01"                    ;; export section
    "\06\fc\80\80\80\80\80"        ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\09\01"                    ;; export section
    "\05\fc\80\80\80\80"           ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\fc\80\80\80"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\07\01"                    ;; export section
    "\03\fc\80\80"                 ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc\80"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\fc\80"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fc"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\fc"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fd\bf\bf\bf\bf\bf"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\0a\01"                    ;; export section
    "\06\fd\bf\bf\bf\bf\bf"        ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fe"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\fe"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\fe\ff"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\fe\ff"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ff"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\05\01"                    ;; export section
    "\01\ff"                       ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ff\fe\00\00"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\08\01"                    ;; export section
    "\04\ff\fe\00\00"              ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)

;; "\ff\fe"
(assert_malformed
  (module binary
    "\00asm" "\01\00\00\00"
    "\01\04\01\60\00\00"           ;; type section: [] -> []
    "\03\02\01\00"                 ;; function section: 1 func
    "\07\06\01"                    ;; export section
    "\02\ff\fe"                    ;; export name
    "\00\00"                       ;; func 0
    "\0a\04\01\02\00\0b"           ;; code section: empty body
  )
  "invalid UTF-8 encoding"
)
