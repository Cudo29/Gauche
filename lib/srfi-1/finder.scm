;;;
;;; Find and alike of SRFI-1
;;;

;; $Id: finder.scm,v 1.1 2001-04-06 09:53:46 shiro Exp $

;; This code is based on the reference implementation by Olin Shivers
;;
;; Copyright (c) 1998, 1999 by Olin Shivers. You may do as you please with
;; this code as long as you do not remove this copyright notice or
;; hold me liable for its use. Please send bug reports to shivers@ai.mit.edu.

(select-module srfi-1)


(define (find pred list)
  (cond ((find-tail pred list) => car)
	(else #f)))

(define (find-tail pred list)
  (check-arg procedure? pred)
  (let lp ((list list))
    (and (not (null-list? list))
	 (if (pred (car list)) list
	     (lp (cdr list))))))

(define (take-while pred lis)
  (check-arg procedure? pred)
  (let recur ((lis lis))
    (if (null-list? lis) '()
	(let ((x (car lis)))
	  (if (pred x)
	      (cons x (recur (cdr lis)))
	      '())))))

(define (drop-while pred lis)
  (check-arg procedure? pred)
  (let lp ((lis lis))
    (if (null-list? lis) '()
	(if (pred (car lis))
	    (lp (cdr lis))
	    lis))))

(define (take-while! pred lis)
  (check-arg procedure? pred)
  (if (or (null-list? lis) (not (pred (car lis)))) '()
      (begin (let lp ((prev lis) (rest (cdr lis)))
	       (if (pair? rest)
		   (let ((x (car rest)))
		     (if (pred x) (lp rest (cdr rest))
			 (set-cdr! prev '())))))
	     lis)))

(define (span pred lis)
  (check-arg procedure? pred)
  (let recur ((lis lis))
    (if (null-list? lis) (values '() '())
	(let ((x (car lis)))
	  (if (pred x)
	      (receive (prefix suffix) (recur (cdr lis))
		(values (cons x prefix) suffix))
	      (values '() lis))))))

(define (span! pred lis)
  (check-arg procedure? pred)
  (if (or (null-list? lis) (not (pred (car lis)))) (values '() lis)
      (let ((suffix (let lp ((prev lis) (rest (cdr lis)))
		      (if (null-list? rest) rest
			  (let ((x (car rest)))
			    (if (pred x) (lp rest (cdr rest))
				(begin (set-cdr! prev '())
				       rest)))))))
	(values lis suffix))))
  

(define (break  pred lis) (span  (lambda (x) (not (pred x))) lis))
(define (break! pred lis) (span! (lambda (x) (not (pred x))) lis))

(define (any pred lis1 . lists)
  (check-arg procedure? pred)
  (if (pair? lists)

      ;; N-ary case
      (receive (heads tails) (%cars+cdrs (cons lis1 lists))
	(and (pair? heads)
	     (let lp ((heads heads) (tails tails))
	       (receive (next-heads next-tails) (%cars+cdrs tails)
		 (if (pair? next-heads)
		     (or (apply pred heads) (lp next-heads next-tails))
		     (apply pred heads)))))) ; Last PRED app is tail call.

      ;; Fast path
      (and (not (null-list? lis1))
	   (let lp ((head (car lis1)) (tail (cdr lis1)))
	     (if (null-list? tail)
		 (pred head)		; Last PRED app is tail call.
		 (or (pred head) (lp (car tail) (cdr tail))))))))

(define (every pred lis1 . lists)
  (check-arg procedure? pred)
  (if (pair? lists)

      ;; N-ary case
      (receive (heads tails) (%cars+cdrs (cons lis1 lists))
	(or (not (pair? heads))
	    (let lp ((heads heads) (tails tails))
	      (receive (next-heads next-tails) (%cars+cdrs tails)
		(if (pair? next-heads)
		    (and (apply pred heads) (lp next-heads next-tails))
		    (apply pred heads)))))) ; Last PRED app is tail call.

      ;; Fast path
      (or (null-list? lis1)
	  (let lp ((head (car lis1))  (tail (cdr lis1)))
	    (if (null-list? tail)
		(pred head)	; Last PRED app is tail call.
		(and (pred head) (lp (car tail) (cdr tail))))))))

(define (list-index pred lis1 . lists)
  (check-arg procedure? pred)
  (if (pair? lists)

      ;; N-ary case
      (let lp ((lists (cons lis1 lists)) (n 0))
	(receive (heads tails) (%cars+cdrs lists)
	  (and (pair? heads)
	       (if (apply pred heads) n
		   (lp tails (+ n 1))))))

      ;; Fast path
      (let lp ((lis lis1) (n 0))
	(and (not (null-list? lis))
	     (if (pred (car lis)) n (lp (cdr lis) (+ n 1)))))))
