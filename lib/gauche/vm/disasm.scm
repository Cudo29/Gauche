;;;
;;; Disassembler - print VM compiled code in (sort of) human-readable way
;;;

(define-module gauche.vm.disasm
  (use srfi-1)
  (export disasm disasm-code))

(select-module gauche.vm.disasm)

(define *insn-width* 30)
(define *comment-width* 45)
(define *line-buffer* #f)

(define (emit segment)
  (set! *line-buffer* (cons segment *line-buffer*)))

(define (flush)
  (when *line-buffer*
    (for-each (lambda (l) (display l)) (reverse *line-buffer*))
    (newline))
  (set! *line-buffer* '()))

(define (print-indent indent)
  (emit (make-string (* indent 2) #\space)))

(define (print-fill)
  (let ((room (- *insn-width* (reduce + 0 (map string-length *line-buffer*)))))
    (when (positive? room)
      (emit (make-string room #\space)))))

(define (print-insn indent insn param xtra)
  (flush)
  (print-indent indent)
  (emit insn)
  (when param (emit (format #f "~S" param)))
  (when xtra  (emit (format #f " ~S" xtra))))

(define (print-literal indent obj)
  (flush)
  (print-indent indent)
  (emit (format #f "~S" obj)))

(define (print-label label)
  (flush)
  (emit (format #f "L~A::" label)))

(define (print-goto indent label)
  (flush)
  (print-indent indent)
  (emit (format #f "GOTO L~A" label)))

(define (print-note note)
  (let ((ostr (open-output-string)))
    (format ostr ";; ~S" note)
    (let* ((str (get-output-string ostr))
           (note (if (> (string-length str) *comment-width*)
                     (string-append (substring str 0 *comment-width*) "...")
                     str)))
      (print-fill)
      (emit note))))

(define (DISASM proc)
  (disasm-code (closure-code proc)))

(define (disasm-code code)
  (let ((ihash (make-hash-table))
        (label 0))
    
    (define (pass1 code)
      (unless (null? code)
        (if (hash-table-exists? ihash code)
            (hash-table-put! ihash code #t)
            (let* ((insn (and (vm-instruction? (car code))
                              (vm-insn-inspect (car code))))
                   (op   (and insn (car insn))))
              (hash-table-put! ihash code #f)
              (if insn
                  (cond ((member op '("IF" "PRE-CALL"))
                         (pass1 (cadr code))
                         (pass1 (cddr code)))
                        ((member op '("LET" "TAILBIND" "VALUES-BIND"))
                         (pass1 (cddr code)))
                        ((equal? op "LAMBDA")
                         (pass1 (caddr code))
                         (pass1 (cdddr code)))
                        (else
                         (pass1 (cdr code))))
                  (pass1 (cdr code)))
              ))))

    (define (print-code code indent)
      (if (null? code)
          (print-insn indent "RET" #f #f)
          (let* ((insn (and (vm-instruction? (car code))
                            (vm-insn-inspect (car code))))
                 (op   (and insn (car insn))))
            (if insn
                (cond ((member op '("IF" "PRE-CALL"))
                       (print-insn indent op #f #f)
                       (pass2 (cadr code) (+ indent 1))
                       (pass2 (cddr code) indent))
                      ((member op '("LET" "VALUES-BIND" "TAILBIND"))
                       (print-insn indent op (cdr insn) #f)
                       (print-note (cadr code))
                       (pass2 (cddr code) (+ indent 1)))
                      ((equal? op "LAMBDA")
                       (print-insn indent op (cdr insn) #f)
                       (print-note (cadr code))
                       (newline)
                       (pass2 (caddr code) (+ indent 1))
                       (pass2 (cdddr code) indent))
                      ((equal? op "POPENV")
                       (print-insn indent op #f #f)
                       (pass2 (cdr code) (- indent 1)))
                      ((member op '("GREF" "GSET"))
                       (print-insn indent op #f (cadr code))
                       (pass2 (cddr code) indent))
                      ((null? (cdr insn))
                       (print-insn indent op #f #f)
                       (pass2 (cdr code) indent))
                      (else
                       (print-insn indent op (cdr insn) #f)
                       (pass2 (cdr code) indent)))
                (cond ((is-a? (car code) <source-info>)
                       (print-note (source-info (car code)))
                       (pass2 (cdr code) indent))
                      (else
                       (print-literal indent (car code))
                       (pass2 (cdr code) indent)))))
          ))
    
    (define (pass2 code indent)
      (let ((e (hash-table-get ihash code #f)))
        (cond ((eq? e #t)
               (print-label label)
               (hash-table-put! ihash code label)
               (set! label (+ label 1))
               (print-code code indent))
              ((number? e)
               (print-goto indent e))
              (else
               (print-code code indent)))
        ))

    (set! *line-buffer* #f)
    (pass1 code)
    (pass2 code 0)
    (flush)
    ))
            

(provide "gauche/vm/disasm")
