;;;
;;; process.scm - process interface
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
;;;  $Id: process.scm,v 1.2 2001-03-30 07:44:07 shiro Exp $
;;;

;; process interface, mostly compatible with STk's, but implemented
;; as an object on top of basic system interface.

(define-module gauche.process
  (export <process> run-process process? process-alive? process-pid
          process-input process-output process-error
          process-wait process-exit-status
          process-send-signal process-kill process-stop process-continue
          process-list))
(select-module gauche.process)

(define-class <process> ()
  ((pid       :initform -1 :getter process-pid)
   (command   :initform #f :getter process-command :init-keyword :command)
   (status    :initform #f :getter process-exit-status)
   (input     :initform #f :getter process-input)
   (output    :initform #f :getter process-output)
   (error     :initform #f :getter process-error)
   (processes :allocation :class :initform '())
  ))

(define-method write-object ((p <process>) port)
  (format port "#<process ~a ~s ~a>"
          (process-pid p)
          (process-command p)
          (if (process-alive? p)
              "active"
              "inactive")))

;; create process and run.
(define (run-process command . args)
  (define (check-key args)
    (when (null? (cdr args))
      (error "~s key requires an argument following" (car args))))

  (define (check-iokey args)
    (check-key args)
    (unless (or (string? (cadr args)) (eqv? (cadr args) :pipe))
      (error "~s key requires a string or :pipe following, but got ~s"
             (car args) (cadr args))))
    
  (let loop ((args args) (argv '())
             (input #f) (output #f) (error #f) (wait #f) (fork #t))
    (cond ((null? args)
           (let ((proc  (make <process> :command command)))
             (receive (iomap toclose)
               (if (or input output error)
                   (%setup-iomap proc input output error)
                   (values #f '()))
               (%run-process proc (cons command (reverse argv))
                             iomap toclose wait fork))))
          ((eqv? (car args) :input)
           (check-iokey args)
           (loop (cddr args) argv (cadr args) output error wait fork))
          ((eqv? (car args) :output)
           (check-iokey args)
           (loop (cddr args) argv input (cadr args) error wait fork))
          ((eqv? (car args) :error)
           (check-iokey args)
           (loop (cddr args) argv input output (cadr args) wait fork))
          ((eqv? (car args) :fork)
           (check-key args)
           (loop (cddr args) argv input output error wait (cadr args)))
          ((eqv? (car args) :wait)
           (check-key args)
           (loop (cddr args) argv input output error (cadr args) fork))
          (else
           (loop (cdr args) (cons (car args) argv)
                 input output error wait fork))
          ))
  )

(define (%setup-iomap proc input output error)
  (let* ((toclose '())
         (iomap `(,(cons 0 (cond ((string? input) (open-input-file input))
                                 ((eqv? input :pipe)
                                  (let ((pp (sys-pipe)))
                                    (slot-set! proc 'input (cadr pp))
                                    (set! toclose (cons (car pp) toclose))
                                    (car pp)))
                                 (else 0)))
                  ,(cons 1 (cond ((string? output) (open-output-file output))
                                 ((eqv? output :pipe)
                                  (let ((pp (sys-pipe)))
                                    (slot-set! proc 'output (car pp))
                                    (set! toclose (cons (cadr pp) toclose))
                                    (cadr pp)))
                                 (else 1)))
                  ,(cons 2 (cond ((string? error) (open-output-file error))
                                 ((eqv? error :pipe)
                                  (let ((pp (sys-pipe)))
                                    (slot-set! proc 'error (car pp))
                                    (set! toclose (cons (cadr pp) toclose))
                                    (cadr pp)))
                                 (else 2)))
                  ))
        )
    (values iomap toclose)))

(define (%run-process proc argv iomap toclose wait fork)
  (if fork
      (let ((pid (sys-fork)))
        (if (zero? pid)
            (sys-exec (car argv) argv iomap)
            (begin
              (slot-set! proc 'processes
                         (cons proc (slot-ref proc 'processes)))
              (slot-set! proc 'pid pid)
              (map (lambda (p)
                     (if (input-port? p)
                         (close-input-port p)
                         (close-output-port p)))
                   toclose)
              (when wait
                (slot-set! proc 'status
                           (cdr (sys-waitpid pid))))
              proc)))
      (sys-exec (car argv) argv iomap)))

;; other basic interfaces
(define (process? obj) (is-a? obj <process>))
(define (process-alive? process)
  (and (not (process-exit-status process))
       (>= (process-pid process) 0)))
(define (process-list) (class-slot-ref <process> 'processes))

;; wait
(define (process-wait process)
  (if (process-alive? process)
      (let ((result (sys-waitpid (process-pid process))))
        (slot-set! process 'status (cdr result))
        #t)
      #f))

;; signal
(define (process-send-signal process signal)
  (when (process-alive? process)
    (sys-kill (process-pid process) signal)))
(define (process-kill process) (process-send-signal process SIGKILL))
(define (process-stop process) (process-send-signal process SIGSTOP))
(define (process-continue process) (process-send-signal process SIGCONT))

(provide "gauche/process")
