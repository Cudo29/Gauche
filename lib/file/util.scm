;;;
;;; file/util.scm - filesystem utility functions
;;;  
;;;   Copyright (c) 2000-2004 Shiro Kawai, All rights reserved.
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
;;;  $Id: util.scm,v 1.28.2.2 2004-12-25 03:08:18 shirok Exp $
;;;

;;; This module provides convenient utility functions to handle
;;; files and directories.   Some functions are provided with
;;; different names for compatibility with existing Scheme
;;; implementations.

(define-module file.util
  (use srfi-1)
  (use srfi-2)
  (use srfi-11)
  (use srfi-13)
  (use util.list)
  (export current-directory directory-list directory-list2 directory-fold
          home-directory temporary-directory
          make-directory* create-directory* remove-directory* delete-directory*
          build-path resolve-path expand-path simplify-path decompose-path
          absolute-path? relative-path? find-file-in-paths
          path-extension path-sans-extension path-swap-extension
          file-is-readable? file-is-writable? file-is-executable?
          file-is-symlink?
          file-type file-perm file-mode file-ino file-dev file-rdev file-nlink
          file-uid file-gid file-size file-mtime file-atime file-ctime
          file-eq? file-eqv? file-equal? file-device=?
          file-mtime=? file-mtime<? file-mtime<=? file-mtime>? file-mtime>=?
          file-atime=? file-atime<? file-atime<=? file-atime>? file-atime>=?
          file-ctime=? file-ctime<? file-ctime<=? file-ctime>? file-ctime>=?
          touch-file copy-file move-file
          file->string file->string-list file->list file->sexp-list
          ))
(select-module file.util)

;; common util
(define (%stat opts)
  (if (get-keyword :follow-link? opts #t) sys-stat sys-lstat))

;;;=============================================================
;;; Directory entries

;; "current-directory" is found in ChezScheme, MzScheme, etc.
(define (current-directory . maybe-newdir)
  (cond ((null? maybe-newdir) (sys-getcwd))
        ((null? (cdr maybe-newdir))
         (if (string? (car maybe-newdir))
             (sys-chdir (car maybe-newdir))
             (error "directory name should be a string" (car maybe-newdir))))
        (else
         (error "too many arguments for current-directory" maybe-newdir))))

(define (home-directory . maybe-user)
  (let-optionals* maybe-user ((user (sys-getuid)))
    (and-let* ((ent (cond ((integer? user) (sys-getpwuid user))
                          ((string? user)  (sys-getpwnam user))
                          (else (error "bad user" user)))))
      (slot-ref ent 'dir))))

(define (temporary-directory)
  "/tmp")  ;; would be more smarter

;; utility for directory-list and directory-list2
(define (%directory-filter dir pred filter-add-path?)
  (if filter-add-path?
      (filter (lambda (e) (pred (build-path dir e))) (sys-readdir dir))
      (filter pred (sys-readdir dir))))

(define (%directory-filter-compose opts)
  (apply every-pred
         (cond-list ((get-keyword :children? opts #f)
                     (lambda (e) (not (member (sys-basename e) '("." "..")))))
                    ((get-keyword :filter opts #f)))))

;; directory-list DIR &keyword ADD-PATH? FILTER-ADD-PATH? CHILDREN? FILTER
(define (directory-list dir . opts)
  (let-keywords* opts ((add-path? #f)
                       (filter-add-path? #f))
    (let1 entries
        (sort (%directory-filter dir (%directory-filter-compose opts)
                                 filter-add-path?))
      (if add-path?
          (map (cut build-path dir <>) entries)
          entries))))

;; directory-list2 DIR &optional ADD-DIR? FILTER-ADD-PATH? CHILDREN? FILTER FOLLOW-LINK?
(define (directory-list2 dir . opts)
  (let-keywords* opts ((add-path? #f)
                       (filter-add-path? #f))
    (let* ((filters (%directory-filter-compose opts))
           (selector (let1 stat (%stat opts)
                       (lambda (e)
                         (and (file-exists? e)
                              (eq? (slot-ref (stat e) 'type) 'directory)))))
           (entries (sort (%directory-filter dir filters filter-add-path?))))
      (if add-path?
          (partition selector
                     (map (cut build-path dir <>) entries))
          (partition (lambda (e) (selector (build-path dir e)))
                     entries)))))

;; directory-fold DIR PROC KNIL &keyword LISTER FOLDER FOLLOW-LINK?
(define (directory-fold dir proc knil . opts)
  (let* ((follow (get-keyword :follow-link? opts #t))
         (lister (get-keyword :lister opts
                              (lambda (path knil)
                                (directory-list path
                                                :add-path? #t
                                                :children? #t))))
         (folder (get-keyword :folder opts fold))
         (selector (let1 stat (%stat opts)
                     (lambda (e)
                       (and (file-exists? e)
                            (eq? (slot-ref (stat e) 'type) 'directory))))))
    (define (rec path knil)
      (if (selector path)
          (folder rec knil (lister path knil))
          (proc path knil)))
    (rec dir knil)))

;; mkdir -p
(define (make-directory* dir . opts)
  (let-optionals* opts ((mode #o755))
    (define (rec p)
      (if (file-exists? p)
          (unless (file-is-directory? p)
            (errorf "non-directory ~s is found while creating a directory ~s"
                    (sys-basename p) dir))
          (let1 d (sys-dirname p)
            (rec d)
            (unless (file-is-writable? d)
              (errorf "directory ~s unwritable during creating a directory ~s"
                      d dir))
            (sys-mkdir p mode)
            )))
    ;; some platform complains the last "/"
    (rec (string-trim-right dir #[/]))))

;; synonym
(define create-directory* make-directory*)

;; rm -rf
(define (remove-directory* dir)
  (define (rec d)
    (receive (dirs files)
        (directory-list2 d :add-path? #t :children? #t :follow-link? #f)
      (for-each rec dirs)
      (for-each sys-rmdir dirs)
      (for-each sys-unlink files)))
  (rec dir)
  (sys-rmdir dir))

(define delete-directory* remove-directory*)

;;;=============================================================
;;; Pathnames

(define (build-path base-path . components)
  (define (rec base components)
    (if (null? components)
        base
        (let1 component
            (let1 p (car components)
              (cond ((eq? p 'up) "..")
                    ((eq? p 'same) ".")
                    ((string-prefix? "/" p)
                     (error "can't append absolute path after other path" p))
                    (else p)))
          (rec (cond ((string-null? base) component)
                     ((string-null? component) base)
                     ((string-suffix? "/" base)
                      (string-append base component))
                     (else
                      (string-append base "/" component)))
               (cdr components)))))
  (rec base-path components))

(define (expand-path path)
  (sys-normalize-pathname path :expand #t))

(define (resolve-path path)
  (define (pathcat dir base) (simplify-path (build-path dir base)))
  (define (rec pat)
    (let*-values (((dir)  (sys-dirname pat))
                  ((base) (sys-basename pat))
                  ((ndir p)
                   (if (or (string=? dir "/")  (string=? dir "."))
                       (values dir pat)
                       (let1 nd (rec dir)
                         (values nd (pathcat nd base))))))
      (unless (sys-access p |F_OK|)
        (error "path doesn't exist" path))
      (let loop ((count 0) (p p))
        (cond ((>= count 8) ;; arbitrary upper bound to avoid infinite loop
               (error "possibly looping symlink" pat))
              ((eq? (file-type p :follow-link? #f) 'symlink)
               (loop (+ count 1)
                     (let1 np (sys-readlink p)
                       (if (absolute-path? np) np (pathcat ndir np)))))
              (else p)))
      ))
  (rec (expand-path path)))

(define (simplify-path path)
  (sys-normalize-pathname path :canonicalize #t))

(define (decompose-path path)
  (if (string-suffix? "/" path)
    (values (string-trim-right path #\/) #f #f)
    (let* ((dir (sys-dirname path))
           (base (sys-basename path)))
      (cond ((string-index-right base #\.)
             => (lambda (pos)
                  (if (zero? pos)
                    (values dir base #f)  ; '.' at the beginning doesn't delimit extension
                    (values dir
                            (string-take base pos)
                            (string-drop base (+ pos 1))))))
            (else
             (values dir base #f))))))

(define (absolute-path? path)
  (or (string-prefix? "/" path) (string-prefix? "~" path)))

(define (relative-path? path)
  (not (absolute-path? path)))

(define (path-extension path)
  (receive (dir file ext) (decompose-path path) ext))

(define (path-sans-extension path)
  (receive (dir file ext) (decompose-path path) (build-path dir file)))

(define (path-swap-extension path ext)
  (if ext
    (receive (dir file _) (decompose-path path)
      (build-path dir (string-append file "." ext)))
    (path-sans-extension path)))

(define (find-file-in-paths name . opts)
  (let* ((paths (get-keyword :paths opts
                             (cond ((sys-getenv "PATH")
                                    => (cut string-split <> #\:))
                                   (else '()))))
         (pred  (get-keyword :pred opts file-is-executable?)))
    (if (absolute-path? name)
        (and (pred name) name)
        (let loop ((paths paths))
          (and (not (null? paths))
               (let1 p (build-path (car paths) name)
                 (if (pred p)
                     p
                     (loop (cdr paths))))))
        )))

;;;=============================================================
;;; File attributes

;; convenient accessors for file stats.  accepts string file name or
;; <sys-stat>.  If the named file doesn't exist, returns #f.
;; accepts keyword argument FOLLOW-LINK? 
(define-syntax define-stat-accessor
  (syntax-rules ()
    ((_ name slot)
     (define (name path . opts)
       (and (sys-access path |F_OK|)
            (slot-ref ((%stat opts) path) slot))))))

(define-stat-accessor file-type 'type)
(define-stat-accessor file-perm 'perm)
(define-stat-accessor file-mode 'mode)
(define-stat-accessor file-ino  'ino)
(define-stat-accessor file-dev  'dev)
(define-stat-accessor file-rdev 'rdev)
(define-stat-accessor file-nlink 'nlink)
(define-stat-accessor file-uid   'uid)
(define-stat-accessor file-gid   'gid)
(define-stat-accessor file-size 'size)
(define-stat-accessor file-atime 'atime)
(define-stat-accessor file-mtime 'mtime)
(define-stat-accessor file-ctime 'ctime)

;; file permissions
(define (file-is-readable? path) (sys-access path |R_OK|))
(define (file-is-writable? path) (sys-access path |W_OK|))
(define (file-is-executable? path) (sys-access path |X_OK|))

(define (file-is-symlink? path)
  (and (file-exists? path)
       (eq? (slot-ref (sys-lstat path) 'type) 'symlink)))

;; compares two files are identical, in the sense that:
;;  file-eq?  - two files (or directories) are the same entity. symbolic
;;              links are not followed.   The files must exist.
;;  file-eqv? - two files are the same entity, after resolving symbolic
;;              links.  The files must exist.
;;  file-equal? - the content of two files are the same.

(define (file-eq? f1 f2)
  (let ((s1 (sys-lstat f1))
        (s2 (sys-lstat f2)))
    (and (eqv? (slot-ref s1 'dev) (slot-ref s2 'dev))
         (eqv? (slot-ref s1 'ino) (slot-ref s2 'ino)))))

(define (file-eqv? f1 f2)
  (let ((s1 (sys-stat f1))
        (s2 (sys-stat f2)))
    (and (eqv? (slot-ref s1 'dev) (slot-ref s2 'dev))
         (eqv? (slot-ref s1 'ino) (slot-ref s2 'ino)))))

(define (file-equal? f1 f2)
  (let ((s1 (sys-stat f1))
        (s2 (sys-stat f2)))
    (cond ((and (eqv? (slot-ref s1 'dev) (slot-ref s2 'dev))
                (eqv? (slot-ref s1 'ino) (slot-ref s2 'ino)))
           #t)
          ((not (eq? (slot-ref s1 'type) (slot-ref s2 'type)))
           #f)
          ((not (= (slot-ref s1 'size) (slot-ref s2 'size)))
           #f)
          ((eq? (slot-ref s1 'type) 'directory)
           (error "directory comparison is not supported yet" s1))
          (else
           (call-with-input-file f1
             (lambda (p1)
               (call-with-input-file f2
                 (lambda (p2)
                   (let loop ((b1 (read-block 8192 p1))
                              (b2 (read-block 8192 p2)))
                     (cond ((eof-object? b1) #t)
                           ((string=? b1 b2)
                            (loop (read-block 8192 p1) (read-block 8192 p2)))
                           (else #f))))))))
          )))

;; see if two files or directories exist on the same device.
(define (file-device=? f1 f2)
  (eqv? (slot-ref (sys-stat f1) 'dev) (slot-ref (sys-stat f2) 'dev)))

;; comparing file timestamp.  accepts string file name, <sys-stat>,
;; <time>, or number.
(define-syntax define-time-comparer
  (syntax-rules ()
    ((_ name slot cmp)
     (begin
       (define-method name ((a <sys-stat>) (b <sys-stat>))
         (cmp (slot-ref a slot) (slot-ref b slot)))
       (define-method name ((a <sys-stat>) (b <number>))
         (cmp (slot-ref a slot) b))
       (define-method name ((a <number>) (b <sys-stat>))
         (cmp a (slot-ref b slot)))
       (define-method name ((a <string>) (b <string>))
         (name (sys-stat a) (sys-stat b)))
       (define-method name ((a <string>) b)
         (name (sys-stat a) b))
       (define-method name (a (b <string>))
         (name a (sys-stat b)))
       (define-method name ((a <time>) b)
         (name (slot-ref a 'second) b))
       (define-method name (a  (b <time>))
         (name a (slot-ref b 'second)))
       ))))

(define-time-comparer file-mtime=?  'mtime =)
(define-time-comparer file-mtime<?  'mtime <)
(define-time-comparer file-mtime<=? 'mtime <=)
(define-time-comparer file-mtime>?  'mtime >)
(define-time-comparer file-mtime>=? 'mtime >=)

(define-time-comparer file-ctime=?  'ctime =)
(define-time-comparer file-ctime<?  'ctime <)
(define-time-comparer file-ctime<=? 'ctime <=)
(define-time-comparer file-ctime>?  'ctime >)
(define-time-comparer file-ctime>=? 'ctime >=)

(define-time-comparer file-atime=?  'atime =)
(define-time-comparer file-atime<?  'atime <)
(define-time-comparer file-atime<=? 'atime <=)
(define-time-comparer file-atime>?  'atime >)
(define-time-comparer file-atime>=? 'atime >=)

;;;=============================================================
;;; File operation

(define (touch-file pathname)
  (if (sys-access pathname |F_OK|)
      (sys-utime pathname)
      (close-output-port (open-output-file pathname)))
  (values))

;; copy-file
;;  if-exists     - :error :supersede :backup #f
;;  backup-suffix
;;  safe
;;  keep-timestamp
(define (copy-file src dst . opts)
  (let* ((if-exists (get-keyword :if-exists opts :error))
         (backsfx   (get-keyword :backup-suffix opts ".orig"))
         (safe      (get-keyword :safe opts #f))
         (keeptime  (get-keyword :keep-timestamp opts #f))
         (backfile  (string-append dst backsfx))
         (times     '())
         (tmpfile   #f)
         (inport    #f)
         (outport   #f))
    (define (rollback)
      (cond (inport  => close-input-port))
      (cond (outport => close-output-port))
      (cond (tmpfile => sys-unlink)))
    (define (commit)
      (cond (inport  => close-input-port))
      (cond (outport => close-output-port))
      (when tmpfile
        (when (eq? if-exists :backup) (sys-rename dst backfile))
        (sys-rename tmpfile dst))
      (unless (null? times) (apply sys-utime dst times)))
    (define (open-destination)
      (if safe
          (cond
           ((and (eq? if-exists :error) (file-exists? dst))
            (error "destination file exists" dst))
           ((and (not if-exists) (file-exists? dst)) #f)
           (else
            (set!-values (outport tmpfile) (sys-mkstemp dst)) #t))
          (cond
           ((eq? if-exists :error)
            (set! outport (open-output-file dst :if-exists :error)) #t)
           ((not if-exists)
            (set! outport (open-output-file dst :if-exists #f)) outport)
           ((eq? if-exists :backup)
            (when (file-exists? dst) (sys-rename dst backfile))
            (set! outport (open-output-file dst)) #t)
           (else
            (set! outport (open-output-file dst :if-exists :supersede)) #t))
          ))
    (define (do-copy)
      (with-error-handler
       (lambda (e) (rollback) (raise e))
       (lambda ()
         (set! inport (open-input-file src))
         (when keeptime
           (set! times (let1 stat (sys-fstat inport)
                         (map (cut slot-ref stat <>) '(atime mtime)))))
         (begin0
          (and (open-destination)
               (copy-port inport outport)
               #t)
          (commit)))))

    ;; body of copy-file
    (unless (memq if-exists '(#f :error :supersede :backup))
      (error "argument for :if-exists must be either :error, :supersede, :backup or #f, but got" if-exists))
    (when (and (file-exists? src) (file-exists? dst) (file-eqv? src dst))
      (errorf "source ~s and destination ~s are the same file" src dst))
    (do-copy)
    ))

;; move-file
;;  if-exists  - :error :supersede :backup #f
;;  backup-suffix
(define (move-file src dst . opts)
  (let-keywords* opts
      ((if-exists :error)
       (backsfx   :backup-suffix ".orig"))

    (define (do-rename)
      (if (file-exists? dst)
          (cond ((eq? if-exists :error)
                 (error "destination file exists" dst))
                ((eq? if-exists :supersede)
                 (sys-rename src dst) #t)
                ((eq? if-exists :backup)
                 (sys-rename dst (string-append dst backsfx))
                 (sys-rename src dst) #t)
                (else #f))
          (begin (sys-rename src dst) #t)))
    (define (do-copying)
      (and (copy-file src dst :if-exists if-exists
                      :backup-suffix backsfx 
                      :safe #t :keep-timestamp #t)
           (begin (sys-unlink src) #t)))
    
    ;; body of move-file
    (unless (memq if-exists '(#f :error :supersede :backup))
      (error "argument for :if-exists must be either :error, :supersede, :backup or #f, but got" if-exists))
    (unless (file-exists? src)
      (error "source file does not exist" src))
    (when (and (file-exists? dst) (file-eqv? src dst))
      (errorf "source ~s and destination ~s are the same file" src dst))
    (let1 dstdir (sys-dirname dst)
      (unless (file-exists? dstdir)
        (errorf "can't move to ~s: path does not exist" dst))
      (if (file-device=? src dstdir)
          (do-rename)
          (do-copying)))
    ))

;; copy-directory
;; move-directory

;; file->string, file->list, file->string-list, file->sexp-list
;; shortcuts of port->string etc.

(define (file->string file . opts)
  (apply call-with-input-file file port->string opts))

(define (file->list reader file . opts)
  (apply call-with-input-file file (cut port->list reader <>) opts))

(define (file->string-list file . opts)
  (apply call-with-input-file file (cut port->list read-line <>) opts))

(define (file->sexp-list file . opts)
  (apply call-with-input-file file (cut port->list read <>) opts))

(provide "file/util")
