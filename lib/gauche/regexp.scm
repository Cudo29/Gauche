;;;
;;; regexp.scm - regexp-related utilities
;;;
;;;  Copyright(C) 2001 by Shiro Kawai (shiro@acm.org)
;;;
;;;  Permission to use, copy, modify, distribute this software and
;;;  accompanying documentation for any purpose is hereby granted,
;;;  provided that existing copyright notices are retained in all
;;;  copies and that this notice is included verbatim in all
;;;  distributions.
;;;  This software is provided as is, without express or implied
;;;  warranty.  In no circumstances the author(s) shall be liable
;;;  for any damages arising out of the use of this software.
;;;
;;;  $Id: regexp.scm,v 1.3 2001-04-16 02:25:28 shiro Exp $
;;;

(select-module gauche)

(define-syntax rxmatch-bind*
  (syntax-rules ()
    ((rxmatch-bind* ?n ?match () ?form ...)
     (begin ?form ...))
    ((rxmatch-bind* ?n ?match (#f ?vars ...) ?form ...)
     (rxmatch-bind* (+ ?n 1) ?match (?vars ...) ?form ...))
    ((rxmatch-bind* ?n ?match (?var ?vars ...) ?form ...)
     (let ((?var (rxmatch-substring ?match ?n)))
       (rxmatch-bind* (+ ?n 1) ?match (?vars ...) ?form ...)))
    ))

(define-syntax rxmatch-let
  (syntax-rules ()
    ((rxmatch-let ?expr (?var ...) ?form ...)
     (cond (?expr
            => (lambda (match)
                 (rxmatch-bind* 0 match (?var ...) ?form ...)))
           (else (error "rxmatch-let: match failed: ~s" '?expr))))))


(define-syntax rxmatch-if
  (syntax-rules ()
    ((rxmatch-if ?expr (?var ...) ?then ?else)
     (cond (?expr
            => (lambda (match)
                 (rxmatch-bind* 0 match (?var ...) ?then)))
           (else ?else)))))

(define-syntax rxmatch-cond
  (syntax-rules (test else =>)
    ((rxmatch-cond)
     #f)
    ((rxmatch-cond (else ?form ...))
     (begin ?form ...))
    ((rxmatch-cond (test ?expr => ?obj) ?clause ...)
     (cond (?expr => ?obj) (else (rxmatch-cond ?clause ...))))
    ((rxmatch-cond (test ?expr ?form ...) ?clause ...)
     (if ?expr (begin ?form ...) (rxmatch-cond ?clause ...)))
    ((rxmatch-cond (?matchexp ?bind ?form ...) ?clause ...)
     (rxmatch-if ?matchexp ?bind
               (begin ?form ...)
               (rxmatch-cond ?clause ...)))))

(define-syntax rxmatch-case
  (syntax-rules (test else =>)
    ((rxmatch-case ?str)
     #f)
    ((rxmatch-case ?str (else ?form ...))
     (begin ?form ...))
    ((rxmatch-case ?str (test ?expr => ?obj) ?clause ...)
     (cond (?expr => ?obj) (else (rxmatch-case ?str ?clause ...))))
    ((rxmatch-case ?str (test ?expr ?form ...) ?clause ...)
     (if ?expr (begin ?form ...) (rxmatch-case ?str ?clause ...)))
    ((rxmatch-case ?str (?re ?bind ?form ...) ?clause ...)
     (rxmatch-if (rxmatch ?re ?str)
         ?bind
       (begin ?form ...)
       (rxmatch-case ?str ?clause ...)))))

;;; scsh compatibility

(define regexp-search rxmatch)
(define match:start rxmatch-start)
(define match:end   rxmatch-end)
(define match:substring rxmatch-substring)

(define-syntax let-match
  (syntax-rules ()
    ((let-match . ?body) (rxmatch-let . ?body))))

(define-syntax if-match
  (syntax-rules ()
    ((if-match . ?body) (rxmatch-if . ?body))))

(define-syntax match-cond
  (syntax-rules ()
    ((match-cond . ?body) (rxmatch-cond . ?body))))

(provide "gauche/regexp")
