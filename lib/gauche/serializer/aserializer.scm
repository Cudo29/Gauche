;;;
;;; aserializer.scm - a serializer implementation
;;;
;;;  Copyright(C) 2000-2001 by Shiro Kawai (shiro@acm.org)
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
;;;  $Id: aserializer.scm,v 1.1 2001-04-03 11:16:48 shiro Exp $
;;;

;; This is a straightforward port from the one I wrote for STk before.
;; The original module has been working quite well in the application
;; I wrote.  Nevertheless, I want to make this more general.  Expect
;; incompatible changes in the future.

(define-module gauche.serializer.aserializer
  (use gauche.serializer)
  (export <aserializer>))
(select-module gauche.serializer.aserializer)

(define-class <aserializer> (<serializer>)
  ((ref-count  :accessor ref-count-of
               :init-form 0)
   (obj-dict   :getter obj-dict-of
               :init-form (make-hash-table))
   (class-dict :getter class-dict-of
               :init-form (make-hash-table))
   ))

;; tag definitions.  currently, it is upper-compatible from pserializer.
(define *tag-symbol*     'y)
(define *tag-pair*       'p)
(define *tag-vector*     'v)
(define *tag-string*     's)
(define *tag-reference*  'r)
(define *tag-hash-table* 'h)
(define *tag-keyword*    'k)
(define *tag-instance*   'i)

(define-method write-to-serializer ((self <aserializer>) object)
  (define port  (port-of self))
  (define dict  (obj-dict-of self))
  (define cdict (class-dict-of self))

  (define (get-class-info class instance)
    (let ((c (hash-table-get cdict class #f)))
      (or c
          (let ((c (cons (class-name class)
                         (get-serializable-slots instance))))
            (hash-table-put! cdict class c)
            c))))

  (define (write-rec obj)
    (cond ((or (eq? obj #f) (eq? obj #t) (eq? obj '())
               (number? obj) (char? obj))
           (write obj port) (newline port))
          ((hash-table-get dict obj #f)
           => (lambda (ref)
                (write *tag-reference* port) (display #\space port)
                (write ref port) (newline port)))
          (else
           (hash-table-put! dict obj (ref-count-of self))
           (set! (ref-count-of self) (+ (ref-count-of self) 1))
           (cond
            ((pair? obj)
             (write *tag-pair* port) (display #\space port)
             (write-rec (car obj)) (write-rec (cdr obj)))
            ((symbol? obj)
             (write *tag-symbol* port) (display #\space port)
             (write obj port) (newline port))
            ((string? obj)
             (write *tag-string* port) (display #\space port)
             (write obj port) (newline port))
            ((keyword? obj)
             (write *tag-keyword* port) (display #\space port)
             (display obj port) (newline port))
            ((vector? obj)
             (write *tag-vector* port) (display #\space port)
             (write (vector-length obj) port) (newline port)
             (for-each write-rec (vector->list obj)))
            ((hash-table? obj)
             (write *tag-hash-table* port) (display #\space port)
             (let ((num 0))               ; put # of entries
               (hash-table-for-each obj (lambda _ (set! num (+ num 1))))
               (write num port) (newline port))
             (hash-table-for-each         ; put keys and values
              obj
              (lambda (key value) (write-rec key) (write-rec value))))
            ((is-a? obj <object>)
             (write *tag-instance* port) (display #\space port)
             (let* ((class (class-of obj))
                    (cinfo (get-class-info class obj)))
               (write-rec cinfo)
               (for-each (lambda (s)
                           (if (slot-bound? obj s)
                               (write-rec (slot-ref obj s))
                               (write-rec #f serializer))) ;;FIXME
                         (cdr cinfo))))
            (else
             (error "unserializable object: ~s" obj))))
          ));;write rec
  
  (unless (eq? (direction-of self) :out)
     (error "Output serializer required: ~s" self))
  (write-rec object))

(define-method read-from-serializer ((self <aserializer>))
  (define port  (port-of self))
  (define dict  (obj-dict-of self))
  (define cdict (class-dict-of self))

  (define (register obj)
    (let ((cnt (ref-count-of self)))
      (hash-table-put! dict cnt obj)
      (set! (ref-count-of self) (+ cnt 1))
      obj))

  (define (read-rec)
    (let ((tag (read port)))
      (cond ((eof-object? tag) tag)
            ((not (symbol? tag)) tag)
            ((eq? tag *tag-pair*) (let ((cell (cons #t #t)))
                                    (register cell)
                                    (let* ((ca (read-rec))
                                           (cd (read-rec)))
                                      (set-car! cell ca)
                                      (set-cdr! cell cd)
                                      cell)))
            ((eq? tag *tag-reference*) (let* ((ref (read port))
                                              (obj (hash-table-get dict ref #f)))
                                         (or obj (error "Stray reference"))))
            ((eq? tag *tag-symbol*) (register (read port)))
            ((eq? tag *tag-string*) (register (read port)))
            ((eq? tag *tag-instance*) (read-instance))
            ((eq? tag *tag-keyword*) (register (make-keyword (read port))))
            ((eq? tag *tag-hash-table*) (read-hash-table))
            ((eq? tag *tag-vector*) (let* ((len (read port))
                                           (vec (make-vector len)))
                                      (register vec)
                                      (do ((num 0 (+ num 1)))
                                          ((>= num len) vec)
                                        (vector-set! vec num (read-rec)))))
            (else
             (error "encountered unknown tag: ~s" tag)))))

  (define (read-hash-table)
    (let ((num (read port))
          (ht  (make-hash-table)))
      (register ht)
      (do ((num num (- num 1)))
          ((= num 0) ht)
        (let* ((key (read-rec))
               (val (read-rec)))
          (hash-table-put! ht key val)))))

  (define (read-instance)
      ;; we need to register a dummy object before proceed to retrieve
      ;; the rest of information.
      (let* ((index (let ((cnt (ref-count-of self)))
                      (set! (ref-count-of self) (+ cnt 1))
                      cnt))
             (cinfo (read-rec))
             (class (car cinfo))
             (slots (cdr cinfo))
             (obj   (make (eval class #f))) ;Arrgghh
             (exist-slots (get-serializable-slots obj)))
        (hash-table-put! dict index obj)
        (for-each (lambda (s)
                    (let ((val (read-rec)))
                      (when (memq s exist-slots)
                        (slot-set! obj s val))))
                  slots)
        obj))

  (unless (eq? (direction-of self) :in)
     (error "Input serializer required: ~s" self))
  (read-rec))

(provide "gauche/serializer/aserializer")
