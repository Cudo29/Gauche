;;;
;;; procedure.scm - auxiliary procedure utilities.  to be autoloaded.
;;;  
;;;   Copyright (c) 2000-2003 Shiro Kawai, All rights reserved.
;;;   
;;;   Redistribution and use in source and binary forms, with or without
;;;   modification, are permitted provided that the following conditions
;;;   are met:
;;;   
;;;   1. Redistributions of source code must retain the above copyright
;;;      notice, this list of conditions and the following disclaimer.
;;;  
;;;   2. Redistributions in binary form must reproduce the above copyright
;;;      notice, this list of conditions and the following disclaimer in the
;;;      documentation and/or other materials provided with the distribution.
;;;  
;;;   3. Neither the name of the authors nor the names of its contributors
;;;      may be used to endorse or promote products derived from this
;;;      software without specific prior written permission.
;;;  
;;;   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;;;   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;;;   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;;;   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;;;   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;;;   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
;;;   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
;;;   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
;;;   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
;;;   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
;;;   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;  
;;;  $Id: procedure.scm,v 1.11.2.3 2005-01-02 00:10:27 shirok Exp $
;;;

(define-module gauche.procedure
  (use srfi-1)
  (use srfi-2)
  (export compose pa$ map$ for-each$ apply$
          any-pred every-pred
          let-optionals* let-keywords* get-optional
          arity procedure-arity-includes?
          <arity-at-least> arity-at-least? arity-at-least-value
          case-lambda disasm
          ))

(select-module gauche.procedure)

;; Combinator utilities -----------------------------------------

(define (pa$ fn . args)                  ;partial apply
  (lambda more-args (apply fn (append args more-args))))

(define (compose f g . more)
  (if (null? more)
      (lambda args
        (call-with-values (lambda () (apply g args)) f))
      (compose f (apply compose g more))))

(define (compose$ f) (pa$ compose f))

(define (map$ proc)      (pa$ map proc))
(define (for-each$ proc) (pa$ for-each proc))
(define (apply$ proc)    (pa$ apply proc))

(define (any-pred . preds)
  (lambda args
    (let loop ((preds preds))
      (cond ((null? preds) #f)
            ((apply (car preds) args))
            (else (loop (cdr preds)))))))

(define (every-pred . preds)
  (if (null? preds)
      (lambda args #t)
      (lambda args
        (let loop ((preds preds))
          (cond ((null? (cdr preds))
                 (apply (car preds) args))
                ((apply (car preds) args)
                 (loop (cdr preds)))
                (else #f))))))

;; Macros for optional arguments ---------------------------

(define-syntax get-optional
  (syntax-rules ()
    ((_ args default)
     (let ((a args))
       (if (pair? a) (car a) default)))
    ((_ . other)
     (syntax-error "badly formed get-optional" (get-optional . other)))
    ))

(define-syntax let-optionals*
  (syntax-rules ()
    ((_ "binds" arg binds () body) (let* binds . body))
    ((_ "binds" arg (binds ...) ((var default) . more) body)
     (let-optionals* "binds"
         (if (null? tmp) tmp (cdr tmp))
       (binds ...
              (tmp arg)
              (var (if (null? tmp) default (car tmp))))
       more
       body))
    ((_ "binds" arg (binds ...) (var . more) body)
     (let-optionals* "binds"
         (if (null? tmp) tmp (cdr tmp))
       (binds ...
              (tmp arg)
              (var (if (null? tmp) (undefined) (car tmp))))
       more
       body))
    ((_ "binds" arg (binds ...) var body)
     (let-optionals* "binds"
         arg
       (binds ... (var arg))
       ()
       body))
    ((_ arg vars . body)
     (let-optionals* "binds" arg () vars body))
    ((_ . other)
     (syntax-error "badly formed let-optionals*" (let-optionals* . other)))
    ))

;; We want to generate corresponding keyword for each variable
;; beforehand, so I use a traditional macro as a wrapper.

(define-macro (let-keywords* arg vars . body)
  (let* ((tmp (gensym))
         (triplets
          (map (lambda (var&default)
                 (or (and-let* (((list? var&default))
                                (var (unwrap-syntax (car var&default)))
                                ((symbol? var)))
                       (case (length var&default)
                         ((2) `(,(car var&default)
                                ,(make-keyword var)
                                ,(cadr var&default)))
                         ((3) `(,(car var&default)
                                ,(unwrap-syntax (cadr var&default))
                                ,(caddr var&default)))
                         (else #f)))
                     (error "bad binding form in let-keywords*" var&default)))
               vars)))
    `(let* ((,tmp ,arg)
            ,@(map (lambda (binds)
                     `(,(car binds)
                       (get-keyword* ,(cadr binds) ,tmp ,(caddr binds))))
                   triplets))
       ,@body)))

;; Procedure arity -----------------------------------------

(define-class <arity-at-least> ()
  ((value :init-keyword :value :init-value 0)))

(define-method write-object ((obj <arity-at-least>) port)
  (format port "#<arity-at-least ~a>" (ref obj 'value)))

(define (arity-at-least? x) (is-a? x <arity-at-least>))

(define (arity-at-least-value x)
  (check-arg arity-at-least? x)
  (ref x 'value))

(define (arity proc)
  (cond
   ((or (is-a? proc <procedure>) (is-a? proc <method>))
    (if (ref proc 'optional)
        (make <arity-at-least> :value (ref proc 'required))
        (ref proc 'required)))
   ((is-a? proc <generic>)
    (map arity (ref proc 'methods)))
   (else
    (errorf "cannot get arity of ~s" proc))))

(define (procedure-arity-includes? proc k)
  (let1 a (arity proc)
    (define (check a)
      (cond ((integer? a) (= a k))
            ((arity-at-least? a) (>= k (arity-at-least-value a)))
            (else (errorf "implementation error in (procedure-arity-includes? ~s ~s)" proc k))))
    (if (list? a)
        (any check a)
        (check a))))

;; case-lambda (srfi-16) ---------------------------------------

;; This is a temporary implementation.  There's a plan to replace it
;; for more efficient dispatching mechanism.  (But I'm not sure when).

(define-syntax case-lambda
  (syntax-rules ()
    ((case-lambda (arg . body) ...)
     (make-dispatcher (list (lambda arg . body) ...)))
    ((case-lambda . _)
     (syntax-error "malformed case-lambda" (case-lambda . _)))))

;; support procedure
(define (make-dispatcher closures)
  (lambda args
    (let ((len (length args)))
      (cond ((find (lambda (p) (procedure-arity-includes? p len)) closures)
             => (cut apply <> args))
            (else
             (error "wrong number of arguments to case-lambda:" args))))))

;; disassembler.
;; I'm not sure whether this should be here or not, but fot the time being...

(define (disasm proc)
  ;; kludge
  (let ((dumper (if (find-module 'gauche.internal)
                  (eval '(with-module gauche.internal vm-dump-code)
                        (find-module 'gauche))
                  vm-dump-code)))
    (dumper (closure-code proc))))

(provide "gauche/procedure")
