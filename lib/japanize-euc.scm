;;;
;;; This is just a joke.
;;; You need to compile Gauche in EUC-JP code.
;;;

;; $Id: japanize-euc.scm,v 1.1 2001-03-07 06:21:01 shiro Exp $

(define-syntax ��
  (syntax-rules () ((_ args body ...) (lambda args body ...))))

(define-syntax ���
  (syntax-rules (��)
    ((_ (f . args) body ...)
     (define (f . args) body ...))
    ((_ var val)
     (define var val))
    ((_ var �� val)
     (define var val))))

(define-syntax �⤷
  (syntax-rules (�ʤ�� �Ǥʤ����)
    ((_ test �ʤ�� then)
     (if test then))
    ((_ test �ʤ�� then �Ǥʤ���� else)
     (if test then else))
    ((_ test �Ǥʤ���� else)
     (unless test else))
    ((_ test then)
     (if test then))
    ((_ test then else)
     (if test then else))))

(define-syntax ����
  (syntax-rules (��)
    ((_ var �� val)
     (set! var val))
    ((_ var val)
     (set! var val))))

(define-syntax �ɽ����
  (syntax-rules (��)
    ((_ ((var �� val) ...) body ...)
     (let ((var val) ...) body ...))
    ((_ ((var val) ...) body ...)
     (let ((var val) ...) body ...))
    ))

(define-syntax �缡�ɽ����
  (syntax-rules (��)
    ((_ ((var �� val) ...) body ...)
     (let* ((var val) ...) body ...))
    ((_ ((var val) ...) body ...)
     (let* ((var val) ...) body ...))
    ))
    
(define-syntax �Ƶ��ɽ����
  (syntax-rules (��)
    ((_ ((var �� val) ...) body ...)
     (letrec ((var val) ...) body ...))
    ((_ ((var val) ...) body ...)
     (letrec ((var val) ...) body ...))
    ))
    

(define �� <)
(define �� <=)
(define �� =)
(define �� >)
(define �� >=)

(define �� +)
(define �� -)
(define �� *)
(define �� /)

(define ʸ���󢪥ꥹ�� string->list)
(define �եꥹ�� reverse)

;;-----------------------------------------------
;; examples

(��� ���� ��
   (�� (n) (�⤷ (�� n 2) �ʤ�� n �Ǥʤ���� (�� n (���� (�� n 1))))))

(��� ��ʸ��? ��
   (�� (ʸ����)
     (�缡�ɽ���� ((ʸ���ꥹ�� �� (ʸ���󢪥ꥹ�� ʸ����))
                   (��ʸ���ꥹ�� �� (�եꥹ�� ʸ���ꥹ��)))
       (equal? ��ʸ���ꥹ�� ʸ���ꥹ��))))

