;;;
;;; Generates builtin symbols
;;;
;;; $Id: builtin-syms.scm,v 1.6.2.1 2004-12-23 06:57:21 shirok Exp $
;;;

(use srfi-1)
(use util.list)
(use gauche.cgen)
(use gauche.parameter)
(use gauche.sequence)

(define *unit*
  (make <cgen-unit>
    :name "builtin-syms"
    :preamble "/* Generated from builtin-syms.scm $Revision: 1.6.2.1 $.  DO NOT EDIT */"
    :c-file "builtin-syms.c"
    :h-file "gauche/builtin-syms.h"
    :init-prologue "static void init_builtin_syms(void)\n{"
    :init-epilogue "}"
    ))

(define (main args)
  (parameterize ((cgen-current-unit *unit*))

    (cgen-extern "SCM_EXTERN ScmSymbol Scm_BuiltinSymbols[];")
    (cgen-body "ScmSymbol Scm_BuiltinSymbols[] = {")
    (cgen-body "#define ENTRY(s) \\"
               "  {{ SCM_CLASS2TAG(SCM_CLASS_SYMBOL) }, \\"
               "     SCM_STRING(s) }")
    (cgen-init "#define INTERN(s, i) \\"
               "Scm_HashTablePut(obtable, s, SCM_OBJ(&Scm_BuiltinSymbols[i]))")
    
    (for-each-with-index
     (lambda (index entry)
       (let* ((str (cgen-literal (symbol->string (car entry))))
              (strref (cgen-cexpr str))
              (macro-name (cadr entry)))
         (cgen-extern (format "#define ~a SCM_OBJ(&Scm_BuiltinSymbols[~a])"
                              macro-name index))
         (cgen-body (format "ENTRY(~a)," strref))
         (cgen-init (format "INTERN(~a, ~a);" strref index))
         ))
     (symbols))

    (cgen-body "#undef ENTRY")
    (cgen-body "};")
    (cgen-init "#undef INTERN")
      
    (cgen-emit-h (cgen-current-unit))
    (cgen-emit-c (cgen-current-unit))
    0))

;; add predefined symbols below -------------------------------

(define (symbols)
  '((quote                     SCM_SYM_QUOTE)
    (quasiquote                SCM_SYM_QUASIQUOTE)
    (unquote                   SCM_SYM_UNQUOTE)
    (unquote-splicing          SCM_SYM_UNQUOTE_SPLICING)
    (define                    SCM_SYM_DEFINE)
    (define-constant           SCM_SYM_DEFINE_CONSTANT)
    (define-in-module          SCM_SYM_DEFINE_IN_MODULE)
    (lambda                    SCM_SYM_LAMBDA)
    (if                        SCM_SYM_IF)
    (set!                      SCM_SYM_SET)
    (let                       SCM_SYM_LET)
    (let*                      SCM_SYM_LET_STAR)
    (letrec                    SCM_SYM_LETREC)
    (begin                     SCM_SYM_BEGIN)
    (when                      SCM_SYM_WHEN)
    (unless                    SCM_SYM_UNLESS)
    (and                       SCM_SYM_AND)
    (or                        SCM_SYM_OR)
    (cond                      SCM_SYM_COND)
    (case                      SCM_SYM_CASE)
    (else                      SCM_SYM_ELSE)
    (=>                        SCM_SYM_YIELDS)
    (do                        SCM_SYM_DO)
    (delay                     SCM_SYM_DELAY)
    (receive                   SCM_SYM_RECEIVE)
    (define-module             SCM_SYM_DEFINE_MODULE)
    (with-module               SCM_SYM_WITH_MODULE)
    (select-module             SCM_SYM_SELECT_MODULE)
    (current-module            SCM_SYM_CURRENT_MODULE)
    (import                    SCM_SYM_IMPORT)
    (export                    SCM_SYM_EXPORT)
    (define-macro              SCM_SYM_DEFINE_MACRO)
    (define-syntax             SCM_SYM_DEFINE_SYNTAX)
    (let-syntax                SCM_SYM_LET_SYNTAX)
    (letrec-syntax             SCM_SYM_LETREC_SYNTAX)
    (%syntax-rules             SCM_SYM_SYNTAX_RULES_INT)
    (syntax-rules              SCM_SYM_SYNTAX_RULES)
    (...                       SCM_SYM_ELLIPSIS)
    (%macroexpand              SCM_SYM_MACRO_EXPAND)
    (%macroexpand-1            SCM_SYM_MACRO_EXPAND_1)
    (%asm                      SCM_SYM_ASM)

    ;; class category
    (builtin                   SCM_SYM_BUILTIN)
    (abstract                  SCM_SYM_ABSTRACT)
    (base                      SCM_SYM_BASE)

    ;; modules
    (null                      SCM_SYM_NULL)
    (scheme                    SCM_SYM_SCHEME)
    (gauche                    SCM_SYM_GAUCHE)
    (gauche.gf                 SCM_SYM_GAUCHE_GF)
    (user                      SCM_SYM_USER)
    (|#|                       SCM_SYM_SHARP)

    ;; load
    (*load-path*               SCM_SYM_LOAD_PATH)
    (*load-next*               SCM_SYM_LOAD_NEXT)
    (*load-history*            SCM_SYM_LOAD_HISTORY)
    (*load-port*               SCM_SYM_LOAD_PORT)
    (*load-suffixes*           SCM_SYM_LOAD_SUFFIXES)
    (*dynamic-load-path*       SCM_SYM_DYNAMIC_LOAD_PATH)
    (*cond-features*           SCM_SYM_COND_FEATURES)
    (gauche-windows            SCM_SYM_GAUCHE_WINDOWS)  ;; for feature id
    (gauche-eucjp              SCM_SYM_GAUCHE_EUCJP)    ;; for feature id
    (gauche-sjis               SCM_SYM_GAUCHE_SJIS)     ;; for feature id
    (gauche-utf8               SCM_SYM_GAUCHE_UTF8)     ;; for feature id
    (gauche-none               SCM_SYM_GAUCHE_NONE)     ;; for feature id

    ;; reader, compiler, vm
    (source-info               SCM_SYM_SOURCE_INFO)
    (bind-info                 SCM_SYM_BIND_INFO)
    (arg-info                  SCM_SYM_ARG_INFO)
    (debug-print               SCM_SYM_DEBUG_PRINT)
    (define-reader-ctor        SCM_SYM_DEFINE_READER_CTOR)
    (string-interpolate        SCM_SYM_STRING_INTERPOLATE)
    (big-endian                SCM_SYM_BIG_ENDIAN)    ;; for binary.io, uvector
    (little-endian             SCM_SYM_LITTLE_ENDIAN) ;; ditto

    ;; regexp
    (seq                       SCM_SYM_SEQ)
    (seq-case                  SCM_SYM_SEQ_CASE)
    (seq-uncase                SCM_SYM_SEQ_UNCASE)
    (alt                       SCM_SYM_ALT)
    (rep                       SCM_SYM_REP)
    (rep-min                   SCM_SYM_REP_MIN)
    (rep-bound                 SCM_SYM_REP_BOUND)
    (rep-bound-min             SCM_SYM_REP_BOUND_MIN)
    (rep-while                 SCM_SYM_REP_WHILE)
    (any                       SCM_SYM_ANY)
    (bol                       SCM_SYM_BOL)
    (eol                       SCM_SYM_EOL)
    (wb                        SCM_SYM_WB)
    (nwb                       SCM_SYM_NWB)
    (comp                      SCM_SYM_COMP)
    (*                         SCM_SYM_STAR)
    (*?                        SCM_SYM_STARQ)
    (+                         SCM_SYM_PLUS)
    (+?                        SCM_SYM_PLUSQ)
    (?                         SCM_SYM_QUESTION)
    (??                        SCM_SYM_QUESTIONQ)
    (open-paren                SCM_SYM_OPEN_PAREN)
    (close-paren               SCM_SYM_CLOSE_PAREN)

    ;; system
    (directory                 SCM_SYM_DIRECTORY)
    (regular                   SCM_SYM_REGULAR)
    (character                 SCM_SYM_CHARACTER)
    (block                     SCM_SYM_BLOCK)
    (fifo                      SCM_SYM_FIFO)
    (symlink                   SCM_SYM_SYMLINK)
    (socket                    SCM_SYM_SOCKET)
    (time-utc                  SCM_SYM_TIME_UTC)
    ))

                
