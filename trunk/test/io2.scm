;; test for write/ss and read/ss
;;
;; this test is splitted from io.scm, since this one uses util.isomorph,
;; and has to be done after the test of util.* module.

(use gauche.test)
(use srfi-1)
(use util.isomorph)

(test-start "advanced read/write features")

;;===============================================================
;; Hash-bang handling (#!)
;;

(test-section "hash-bang")

(test* "script hash-bang" 3
       (read-from-string "#!/usr/bin/gosh -i\n3"))
(test* "script hash-bang" 5
       (read-from-string "#! /usr/bin/gosh -i\n5"))
(test* "script hash-bang" (eof-object)
       (read-from-string "#! /usr/bin/gosh -i"))

(test* "#!fold-case" '(hello world)
       (read-from-string "#!fold-case (Hello World)"))
(test* "#!fold-case" '(Hello world)
       (read-from-string "(Hello #!fold-case World)"))
(test* "#!no-fold-case" '(hello World)
       (read-from-string "(#!fold-case Hello #!no-fold-case World)"))

(test* "customized hash-bang" -i
       (let ()
         (define-reader-directive 'usr/bin/gosh (lambda _ (values)))
         (read-from-string "#!usr/bin/gosh -i\n8")))

(test* "customized hash-bang" '(#t #f)
       (let ()
         (define-reader-directive 'true  (lambda _ #t))
         (define-reader-directive 'false (lambda _ #f))
         (read-from-string "#!/usr/bin/gosh -i\n(#!true #!false)")))

;;===============================================================
;; SRFI-10 Reader constructor (#,)
;;

(test-section "srfi-10 reader constructor")

(test "read ctor 1a" '(1 2 #f "4 5")
      (lambda ()
        (define-reader-ctor 'list list)
        (with-input-from-string "#,(list 1 2 #f \"4 5\")" read)))
(test "read ctor 1b" 3
      (lambda ()
        (define-reader-ctor '+ +)
        (with-input-from-string "#,(+ 1 2)" read)))
(define-reader-ctor 'my-vector
  (lambda x (apply vector (cons 'my-vector x))))
(test* "read ctor 2a" '#(my-vector (my-vector 1 2))
       (with-input-from-string "#,(my-vector (my-vector 1 2))" read))
(test* "read ctor 2b" '#(my-vector #(my-vector 1 2))
       (with-input-from-string "#,(my-vector #,(my-vector 1 2))" read))

;;===============================================================
;; Shared structures (#n= and #n#)
;;

;;---------------------------------------------------------------
(test-section "write/ss basic")

(test* "pair" "(#0=(a b) #0#)"
       (let1 x '(a b)
         (write-to-string (list x x) write/ss)))
(test* "pair" "(#0=(a b) . #0#)"
       (let1 x (list 'a 'b)
         (write-to-string (cons x x) write/ss)))
(test* "pair" "(#0=(a b) #1=(a b) #0# . #1#)"
       (let ((x (list 'a 'b))
             (y (list 'a 'b)))
         (write-to-string (list* x y x y) write/ss)))
(test* "pair (circular)" "#0=(a . #0#)"
       (let1 x (list 'a 'b)
         (set-cdr! x x)
         (write-to-string x write/ss)))
(test* "pair (circular)" "#0=(#0# b)"
       (let1 x (list 'a 'b)
         (set-car! x x)
         (write-to-string x write/ss)))
(test* "pair (circular)" "#0=(#0# . #0#)"
       (let1 x (list 'a 'b)
         (set-car! x x)
         (set-cdr! x x)
         (write-to-string x write/ss)))
(test* "pair (circular)" "#0=(a (b . #0#))"
       (let1 x (list 'a (list 'b 'c))
         (set-cdr! (cadr x) x)
         (write-to-string x write/ss)))
(test* "pair (circular)" "#0=(a #1=(b . #0#) . #1#)"
       (let1 x (list 'a (list 'b 'c))
         (set-cdr! (cadr x) x)
         (set-cdr! (cdr x) (cadr x))
         (write-to-string x write/ss)))

(test* "vector" "(#0=#(a b) . #0#)"
       (let1 x (vector 'a 'b)
         (write-to-string (cons x x) write/ss)))
(test* "vector" "(#() . #())"
       (let1 x (vector)
         (write-to-string (cons x x) write/ss)))
(test* "vector" "#(#0=(a b) #0# #0#)"
       (let1 x '(a b)
         (write-to-string (vector x x x) write/ss)))
(test* "vector (circular)" "#0=#(#0#)"
       (let1 x (vector 0)
         (vector-set! x 0 x)
         (write-to-string x write/ss)))

(test* "string" "(#0=\"ab\" . #0#)"
       (let1 x "ab"
         (write-to-string (cons x x) write/ss)))
(test* "string" "(\"\" . \"\")"
       (let1 x ""
         (write-to-string (cons x x) write/ss)))

(test* "more than 10 substructures"
       "(#0=(a) #1=(b) #2=(c) #3=(d) #4=(e) #5=(f) #6=(g) #7=(h) #8=(i) #9=(j) #10=(k) #10# #9# #8# #7# #6# #5# #4# #3# #2# #1# #0#)"
       (let ((a '(a)) (b '(b)) (c '(c)) (d '(d)) (e '(e))
             (f '(f)) (g '(g)) (h '(h)) (i '(i)) (j '(j)) (k '(k)))
         (write-to-string
          (list a b c d e f g h i j k
                k j i h g f e d c b a)
          write/ss)))

(test* "circular list involving abbrev syntax" "#0=((quote . #0#))"
       (write-to-string (cdr #0='#0#) write/ss))

(define-class <foo> ()
  ((a :init-keyword :a)
   (b :init-keyword :b)))
(define-method write-object ((self <foo>) port)
  (format port "#,(foo ~s ~s)" (ref self 'a) (ref self 'b)))

(test* "user defined" "#,(foo #0=(a b) #0#)"
       (let* ((x '(a b))
              (foo (make <foo> :a x :b x)))
         (write-to-string foo write/ss)))
(test* "user defined" "#0=#,(foo #0# #0#)"
       (let ((foo (make <foo> :a #f :b #f)))
         (set! (ref foo 'a) foo)
         (set! (ref foo 'b) foo)
         (write-to-string foo write/ss)))
(test* "user defined" "#0=#,(foo foo #,(foo bar #0#))"
       (let* ((foo (make <foo> :a 'foo :b #f))
              (bar (make <foo> :a 'bar :b foo)))
         (set! (ref foo 'b) bar)
         (write-to-string foo write/ss)))
(test* "user defined" "(#0=#,(foo foo #1=#,(foo bar #0#)) #1#)"
       (let* ((foo (make <foo> :a 'foo :b #f))
              (bar (make <foo> :a 'bar :b foo)))
         (set! (ref foo 'b) bar)
         (write-to-string (list foo bar) write/ss)))
(test* "user defined" "#0=(#1=#,(foo #2=#,(foo bar #1#) #0#) #2#)"
       (let* ((foo (make <foo> :a 'foo :b #f))
              (bar (make <foo> :a 'bar :b foo))
              (baz (list foo bar)))
         (set! (ref foo 'a) bar)
         (set! (ref foo 'b) baz)
         (write-to-string baz write/ss)))

;; write/ss with user-defined write-object method.
;; test by UEYAMA Rui
(define-class <bar> ()
  ((a :init-keyword :a)
   (b :init-keyword :b)))
(define-method write-object ((self <bar>) port)
  (display "#,(bar " port)
  (write/ss (ref self 'a) port)
  (display " " port)
  (write/ss (ref self 'b) port)
  (display ")" port))
(test* "user defined" "#,(bar #0=(a b) #0#)"
       (let* ((x '(a b))
              (bar (make <bar> :a x :b x)))
         (write-to-string bar write/ss)))

;;---------------------------------------------------------------
(test-section "format/ss")

(test* "format/ss" "The answer is #0=(\"a\" . #0#)"
       (let ((a (list "a")))
         (set-cdr! a a)
         (format/ss "The answer is ~s" a)))

(test* "format/ss" "The answer is #0=(a . #0#)"
       (let ((a (list "a")))
         (set-cdr! a a)
         (format/ss "The answer is ~a" a)))

(test* "format/ss" "The answer is #0=(a . #0#) #0=(a . #0#)"
       (let ((a (list 'a)))
         (set-cdr! a a)
         (format/ss "The answer is ~s ~s" a a)))

;;---------------------------------------------------------------
(test-section "read/ss basic")

;; NB: in gauche, read/ss is just an alias of read.
(test* "scalar (harmless)" 0
       (read-from-string "#0=0"))
(test* "scalar (harmless)" 1
       (read-from-string "#1=1"))
(test* "scalar (harmless)" 2
       (read-from-string "#0=#1=2"))
(test* "scalar (harmless)" #f
       (read-from-string "#1=#10=#100=#f"))
(test* "scalar (harmless)" "aaa"
       (read-from-string "#1=#0=\"aaa\""))

(test* "bad syntax" (test-error)
       (read-from-string "#1"))
(test* "bad syntax" (test-error)
       (read-from-string "#3#"))
(test* "bad syntax" (test-error)
       (read-from-string "#99999999999999999999999999999999999=3"))
(test* "bad syntax" (test-error)
       (read-from-string "#99999999999999999999999999999999999#"))

(test* "pair 1" (circular-list 1 2)
       (read-from-string "#0=(1 2 . #0#)")
       isomorphic?)
(test* "pair 2" (let1 r (list #f) (set! (car r) r) r)
       (read-from-string "#0=(#0#)")
       isomorphic?)
(test* "pair 3" (let1 r '(a b) (list r r r))
       (read-from-string "(#0=#1=(a b) #0# #1#)")
       isomorphic?)

(test* "vector" (let* ((r (vector 'a 'b))
                       (s (vector 'c 'd))
                       (t (vector r s r s 'e)))
                  (vector-set! r 1 s)
                  (vector-set! s 1 r)
                  (vector-set! t 4 t)
                  t)
       (read-from-string "#0=#(#1=#(a #2=#(c #1#)) #2# #1# #2# #0#)")
       isomorphic?)

(test* "string" (let* ((r (string #\a #\a))
                       (s (string #\a #\a)))
                  (list r s r s))
       (read-from-string "(#0=\"aa\" #1=\"aa\" #0# #1#)")
       isomorphic?)

;;===============================================================
;; These test is here since srfi-0 must have been tested before.
;;
(test-section "whitespaces")

(test* "skipws" 'a
       (read-from-string
        (cond-expand
         [gauche.ces.utf8 "\u00a0\u1680\u180e\u2000\u200a\u2028\u2029\
                           \u202f\u205f\u3000a"]
         [(or gauche.ces.eucjp gauche.ces.sjis) "\u3000a"]
         [else "a"])))

;;===============================================================
;; Interference between srfi-10 and shared structure
;;

(test-section "combine srfi-10 and srfi-38")

;; NB: this is an experimental feature.  Do not count on this API!
(define-reader-ctor 'foo
  (lambda x `(quote ,x))
  (lambda (obj)
    (pair-for-each (lambda (p)
                     (when (read-reference? (car p))
                       (set-car! p (read-reference-value (car p)))))
                   (cadr obj))))

(test* "user-defined" '#0='(a #0#)
       (read-from-string "#0=#,(foo a #0#)")
       isomorphic?)

(test-end)
