/*
 * vm.c - evaluator
 *
 *   Copyright (c) 2000-2004 Shiro Kawai, All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 * 
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the authors nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  $Id: vm.c,v 1.218.2.1 2004-12-23 06:57:21 shirok Exp $
 */

#define LIBGAUCHE_BODY
#include "gauche.h"
#include "gauche/memory.h"
#include "gauche/class.h"
#include "gauche/exception.h"
#include "gauche/builtin-syms.h"

#include <unistd.h>
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

#ifndef EX_SOFTWARE
/* SRFI-22 requires this. */
#define EX_SOFTWARE 70
#endif

/* An object to mark the boundary frame. */
static ScmObj boundaryFrameMark = SCM_VM_INSN(SCM_VM_HALT);

/* return true if cont is a boundary continuation frame */
#define BOUNDARY_FRAME_P(cont) ((cont)->pc == &boundaryFrameMark)

/* A stub VM code to make VM return immediately */
static ScmObj return_code[] = { SCM_VM_INSN(SCM_VM_RET) };
#define PC_TO_RETURN  return_code


/*
 * The VM. 
 *
 *   VM encapsulates the dynamic status of the current exection.
 *   In Gauche, there's always one active virtual machine per thread,
 *   referred by Scm_VM().   From Scheme, VM is seen as a <thread> object.
 *
 *   From Scheme, VM is viewed as <thread> object.  The class definition
 *   is in thrlib.stub.
 */

static ScmVM *rootVM = NULL;         /* VM for primodial thread */

#ifdef GAUCHE_USE_PTHREADS
static pthread_key_t vm_key;
#define theVM   ((ScmVM*)pthread_getspecific(vm_key))
#else
static ScmVM *theVM;
#endif  /* !GAUCHE_USE_PTHREADS */

static void save_stack(ScmVM *vm);

static ScmSubr default_exception_handler_rec;
#define DEFAULT_EXCEPTION_HANDLER  SCM_OBJ(&default_exception_handler_rec)
static ScmObj throw_cont_calculate_handlers(ScmEscapePoint *, ScmVM *);
static ScmObj throw_cont_body(ScmObj, ScmEscapePoint*, ScmObj);
static void   process_queued_requests(ScmVM *vm);

static ScmEnvFrame *get_env(ScmVM *vm);

/*
 * Constructor
 *   base can be NULL iff called from Scm_Init.
 */

ScmVM *Scm_NewVM(ScmVM *base,
                 ScmModule *module,
                 ScmObj name)
{
    ScmVM *v = SCM_NEW(ScmVM);
    int i;
    
    SCM_SET_CLASS(v, SCM_CLASS_VM);
    v->state = SCM_VM_NEW;
    (void)SCM_INTERNAL_MUTEX_INIT(v->vmlock);
    (void)SCM_INTERNAL_COND_INIT(v->cond);
    v->canceller = NULL;
    v->name = name;
    v->specific = SCM_FALSE;
    v->thunk = NULL;
    v->result = SCM_UNDEFINED;
    v->resultException = SCM_UNDEFINED;
    v->module = module ? module : base->module;
    v->cstack = base ? base->cstack : NULL;
    
    v->curin  = SCM_PORT(Scm_Stdin());
    v->curout = SCM_PORT(Scm_Stdout());
    v->curerr = SCM_PORT(Scm_Stderr());

    Scm_ParameterTableInit(&(v->parameters), base);

    v->compilerFlags = base? base->compilerFlags : 0;
    v->runtimeFlags = base? base->runtimeFlags : 0;
    v->queueNotEmpty = 0;

    v->stack = SCM_NEW_ARRAY(ScmObj, SCM_VM_STACK_SIZE);
    v->sp = v->stack;
    v->stackBase = v->stack;
    v->stackEnd = v->stack + SCM_VM_STACK_SIZE;

    v->env = NULL;
    v->argp = v->stack;
    v->cont = NULL;
    v->pc = PC_TO_RETURN;
    v->base = NULL;
    v->val0 = SCM_UNDEFINED;
    for (i=0; i<SCM_VM_MAX_VALUES; i++) v->vals[i] = SCM_UNDEFINED;
    v->numVals = 1;
    
    v->handlers = SCM_NIL;

    v->exceptionHandler = DEFAULT_EXCEPTION_HANDLER;
    v->escapePoint = NULL;
    v->escapeReason = SCM_VM_ESCAPE_NONE;
    v->escapeData[0] = NULL;
    v->escapeData[1] = NULL;
    v->defaultEscapeHandler = SCM_FALSE;

    v->load_history = SCM_NIL;
    v->load_next = SCM_NIL;
    v->load_port = SCM_FALSE;

    sigemptyset(&v->sigMask);
    Scm_SignalQueueInit(&v->sigq);

    /* stats */
    v->stat.sovCount = 0;
    v->stat.sovTime = 0;

    return v;
}

ScmObj Scm_VMGetResult(ScmVM *vm)
{
    ScmObj head = SCM_NIL, tail;
    int i;
    if (vm->numVals == 0) return SCM_NIL;
    SCM_APPEND1(head, tail, vm->val0);
    for (i=1; i<vm->numVals; i++) {
        SCM_APPEND1(head, tail, vm->vals[i-1]);
    }
    return head;
}

void Scm_VMSetResult(ScmObj obj)
{
    ScmVM *vm = theVM;
    vm->val0 = obj;
    vm->numVals = 1;
}

/*
 * Current VM.
 */
ScmVM *Scm_VM(void)
{
    return theVM;
}

/*
 * Get VM key
 */
#ifdef GAUCHE_USE_PTHREADS
pthread_key_t Scm_VMKey(void)
{
    return vm_key;
}
#endif /*GAUCHE_USE_PTHREADS*/

/*
 * Initialization.  This should be called after modules are initialized.
 */
void Scm__InitVM(void)
{
    /* Create root VM */
#ifdef GAUCHE_USE_PTHREADS
    if (pthread_key_create(&vm_key, NULL) != 0) {
        Scm_Panic("pthread_key_create failed.");
    }
    rootVM = Scm_NewVM(NULL, Scm_SchemeModule(),
                       SCM_MAKE_STR_IMMUTABLE("root"));
    if (pthread_setspecific(vm_key, rootVM) != 0) {
        Scm_Panic("pthread_setspecific failed.");
    }
    rootVM->thread = pthread_self();
#else   /* !GAUCHE_USE_PTHREADS */
    rootVM = theVM = Scm_NewVM(NULL, Scm_SchemeModule(),
                               SCM_MAKE_STR_IMMUTABLE("root"));
#endif  /* !GAUCHE_USE_PTHREADS */
    rootVM->state = SCM_VM_RUNNABLE;

    /* TEMPORARY */
    Scm_InitStaticClass(SCM_CLASS_COMPILED_CODE, "<compiled-code>", Scm_GaucheModule(), NULL, 0);
}

/*====================================================================
 * VM interpreter
 *
 *  Interprets intermediate code CODE on VM.
 */

/*
 * Micro-operations
 */

/* push OBJ to the top of the stack */
#define PUSH_ARG(obj)      (*sp++ = (obj))

/* pop the top object of the stack and store it to VAR */
#define POP_ARG(var)       ((var) = *--sp)

/* fetch an instruction */
#define INCR_PC            (pc++)
#define FETCH_INSN(var)    ((var) = *pc++)
#define FETCH_OPERAND(var) ((var) = *pc)

/* declare local variables for registers, and copy the current VM regs
   to them. */
#define DECL_REGS             DECL_REGS_INT(/**/)
#define DECL_REGS_VOLATILE    DECL_REGS_INT(volatile)

#define DECL_REGS_INT(VOLATILE)                 \
    ScmVM *VOLATILE vm = theVM;                 \
    SCM_PCTYPE VOLATILE pc = vm->pc;            \
    ScmContFrame *VOLATILE cont = vm->cont;     \
    ScmEnvFrame *VOLATILE env = vm->env;        \
    ScmObj *VOLATILE argp = vm->argp;           \
    ScmObj *VOLATILE sp = vm->sp;               \
    VOLATILE ScmObj val0 = vm->val0

/* save VM regs into VM structure. */
#define SAVE_REGS()                             \
    do {                                        \
        vm->pc = pc;                            \
        vm->env = env;                          \
        vm->argp = argp;                        \
        vm->cont = cont;                        \
        vm->sp = sp;                            \
        vm->val0 = val0;                        \
    } while (0)

/* return true if ptr points into the stack area */
#define IN_STACK_P(ptr)                         \
      ((unsigned long)((ptr) - vm->stackBase) < SCM_VM_STACK_SIZE)

#define RESTORE_REGS()                          \
    do {                                        \
        pc = vm->pc;                            \
        env = vm->env;                          \
        argp = vm->argp;                        \
        cont = vm->cont;                        \
        sp = vm->sp;                            \
    } while (0)

/* Check if stack has room at least size bytes. */
#define CHECK_STACK(size)                       \
    do {                                        \
        if (sp >= vm->stackEnd - size) {        \
            SAVE_REGS();                        \
            save_stack(vm);                     \
            RESTORE_REGS();                     \
        }                                       \
    } while (0)

/* Push a continuation frame.  next_pc is the PC from where execution
   will be resumed.  */
#define PUSH_CONT(next_pc)                              \
    do {                                                \
        ScmContFrame *newcont = (ScmContFrame*)sp;      \
        newcont->prev = cont;                           \
        newcont->env = env;                             \
        newcont->argp = argp;                           \
        newcont->size = sp - argp;                      \
        newcont->pc = next_pc;                          \
        newcont->base = vm->base;                       \
        cont = newcont;                                 \
        sp += CONT_FRAME_SIZE;                          \
        argp = sp;                                      \
    } while (0)

/* pop a continuation frame, i.e. return from a procedure. */
#define POP_CONT()                                                      \
    do {                                                                \
        if (cont->argp == NULL) {                                       \
            void *data__[SCM_CCONT_DATA_SIZE];                          \
            ScmObj (*after__)(ScmObj, void**);                          \
            memcpy(data__, (ScmObj*)cont + CONT_FRAME_SIZE,             \
                   cont->size * sizeof(void*));                         \
            after__ = (ScmObj (*)(ScmObj, void**))cont->pc;             \
            if (IN_STACK_P((ScmObj*)cont)) sp = (ScmObj*)cont;          \
            env = cont->env;                                            \
            argp = sp;                                                  \
            pc = PC_TO_RETURN;                                          \
            cont = cont->prev;                                          \
            vm->base = cont->base;                                      \
            SAVE_REGS();                                                \
            val0 = after__(val0, data__);                               \
            RESTORE_REGS();                                             \
        } else if (IN_STACK_P((ScmObj*)cont)) {                         \
            sp   = cont->argp + cont->size;                             \
            env  = cont->env;                                           \
            argp = cont->argp;                                          \
            pc   = cont->pc;                                            \
            vm->base = cont->base;                                      \
            cont = cont->prev;                                          \
        } else {                                                        \
            int size__ = cont->size;                                    \
            argp = sp = vm->stackBase;                                  \
            env = cont->env;                                            \
            pc = cont->pc;                                              \
            vm->base = cont->base;                                      \
            if (cont->argp && size__) {                                 \
                memmove(sp, cont->argp, size__*sizeof(ScmObj*));        \
                sp = argp + size__;                                     \
            }                                                           \
            cont = cont->prev;                                          \
        }                                                               \
    } while (0)

/* push environment header to finish the environment frame.
   env, sp, argp is updated. */
#define FINISH_ENV(info_, up_)                  \
    do {                                        \
        ScmEnvFrame *e__ = (ScmEnvFrame*)sp;    \
        e__->up = up_;                          \
        e__->info = info_;                      \
        e__->size = sp - argp;                  \
        sp += ENV_HDR_SIZE;                     \
        argp = sp;                              \
        env = e__;                              \
    } while (0)

/* extend the current environment by SIZE words.   used for LET. */
#define PUSH_LOCAL_ENV(size_, info_)            \
    do {                                        \
        int i__;                                \
        for (i__=0; i__<size_; i__++) {         \
            *sp++ = SCM_UNDEFINED;              \
        }                                       \
        FINISH_ENV(info_, env);                 \
    } while (0)

/* discard extended environment. */
#define POP_LOCAL_ENV()                         \
    do {                                        \
        sp -= ENV_SIZE(env->size);              \
        env = env->up;                          \
        if (sp < vm->stackBase)                 \
            sp = vm->stackBase;                 \
        argp = sp;                              \
    } while (0)

#define VM_DUMP(delimiter)                      \
    SAVE_REGS();                                \
    fprintf(stderr, delimiter);                 \
    Scm_VMDump(theVM)

#define VM_ASSERT(expr)                                                 \
    do {                                                                \
        if (!(expr))  {                                                 \
            SAVE_REGS();                                                \
            fprintf(stderr, "\"%s\", line %d: Assertion failed: %s\n",  \
                    __FILE__, __LINE__, #expr);                         \
            Scm_VMDump(theVM);                                          \
            Scm_Panic("exitting...\n");                                 \
        }                                                               \
    } while (0)

#define VM_ERR(errargs)                         \
   do {                                         \
      SAVE_REGS();                              \
      Scm_Error errargs;                        \
   } while (0)

/* check the argument count is OK for call to PROC.  if PROC takes &rest
 * args, fold those arguments to the list.  Returns adjusted size of
 * the argument frame.
 */
#define ADJUST_ARGUMENT_FRAME(proc, argc)       \
    do {                                        \
        int reqargs, restarg;                   \
        reqargs = SCM_PROCEDURE_REQUIRED(proc); \
        restarg = SCM_PROCEDURE_OPTIONAL(proc); \
        if (restarg) {                          \
            ScmObj p = SCM_NIL, a;              \
            if (argc < reqargs) goto wna;       \
            /* fold &rest args */               \
            while (argc > reqargs) {            \
                POP_ARG(a);                     \
                p = Scm_Cons(a, p);             \
                argc--;                         \
            }                                   \
            PUSH_ARG(p);                        \
            argc++;                             \
        } else {                                \
            if (argc != reqargs) goto wna;      \
        }                                       \
    } while (0)

/* to take advantage of GCC's `computed goto' feature
   (see gcc.info, "Labels as Values") */
#ifdef __GNUC__
#define SWITCH(val) goto *dispatch_table[val];
#define CAT2(a, b)  a##b
#define CASE(insn)  CAT2(LABEL_, insn) :
#define DEFAULT     LABEL_DEFAULT :
#else /* !__GNUC__ */
#define SWITCH(val) switch (val)
#define CASE(insn)  case insn :
#define DEFAULT     default :
#endif

#define NEXT        continue

/*===================================================================
 * Main loop of VM
 */
/*static*/ void run_loop()
{
    DECL_REGS;
    ScmObj code = SCM_NIL;

#ifdef __GNUC__
    static void *dispatch_table[256] = {
#define DEFINSN(insn, name, nargs)   && CAT2(LABEL_, insn),
#include "gauche/vminsn.h"
#undef DEFINSN
    };
#endif /* __GNUC__ */    
    
    for (;;) {
        /*VM_DUMP("");*/
        
        /* Check if there's a queued processing first. */
        if (vm->queueNotEmpty) {
            CHECK_STACK(CONT_FRAME_SIZE);
            PUSH_CONT(pc);
            SAVE_REGS();
            process_queued_requests(vm);
            RESTORE_REGS();
            POP_CONT();
            continue;
        }

        FETCH_INSN(code);

        if (!SCM_VM_INSNP(code)) {
            /* literal object */
            val0 = code;
            vm->numVals = 1;
            continue;
        }

        /* VM instructions */
        SWITCH(SCM_VM_INSN_CODE(code)) {

            CASE(SCM_VM_PUSH) {
                CHECK_STACK(0);
                PUSH_ARG(val0);
                NEXT;
            }
            CASE(SCM_VM_POP) {
                POP_ARG(val0);
                NEXT;
            }
            CASE(SCM_VM_DUP) {
                ScmObj arg = *(sp-1);
                CHECK_STACK(0);
                PUSH_ARG(arg);
                NEXT;
            }
            CASE(SCM_VM_PRE_CALL) {
                ScmObj *next;
                int reqstack = CONT_FRAME_SIZE+ENV_SIZE(SCM_VM_INSN_ARG(code))+1;
                VM_ASSERT(SCM_INTP(*pc));
                next = vm->base->code + SCM_INT_VALUE(*pc);
                CHECK_STACK(reqstack);
                PUSH_CONT(next);
                INCR_PC;
                NEXT;
            }
            CASE(SCM_VM_PRE_TAIL) {
                /* Note: The call instruction will push extra word for
                   next-method, hence +1 */
                int reqstack = ENV_SIZE(SCM_VM_INSN_ARG(code))+1;
                CHECK_STACK(reqstack);
                NEXT;
            }
            CASE(SCM_VM_CHECK_STACK) {
                int reqstack = SCM_VM_INSN_ARG(code);
                CHECK_STACK(reqstack);
                NEXT;
            }
            CASE(SCM_VM_TAIL_CALL) {
                /* discard the caller's argument frame, and shift
                   the callee's argument frame there.
                   NB: this shifting used to be done after folding
                   &rest arguments.  Benchmark showed this one is better.
                */
                ScmObj *to;
                int argc = sp - argp;

                if (IN_STACK_P((ScmObj*)cont)) {
                    if (cont->argp) {
                        to = (ScmObj*)cont + CONT_FRAME_SIZE;
                    } else {
                        /* C continuation */
                        to = (ScmObj*)cont + CONT_FRAME_SIZE + cont->size;
                    }
                } else {
                    /* continuation has been saved, which means the
                       stack has no longer useful information. */
                    to = vm->stackBase;
                }
                if (argc) memmove(to, argp, argc*sizeof(ScmObj));
                argp = to;
                sp = to + argc;
                /* We discarded the current env, so make sure we don't have
                   a dangling env pointer. */
                env = NULL;
            }
            /* FALLTHROUGH */
            CASE(SCM_VM_CALL) {
                int argc = sp - argp;
                int proctype;
                ScmObj nm, mm, *fp;

                vm->numVals = 1; /* default */

                /* object-apply hook */
                if (!SCM_PROCEDUREP(val0)) {
                    int i;
                    CHECK_STACK(1);
                    for (i=0; i<argc; i++) {
                        *(sp-i) = *(sp-i-1);
                    }
                    *(sp-argc) = val0;
                    sp++; argc++;
                    val0 = SCM_OBJ(&Scm_GenericObjectApply);
                    proctype = SCM_PROC_GENERIC;
                    nm = SCM_FALSE;
                    goto generic;
                }
                /*
                 * We process the common cases first
                 */
                proctype = SCM_PROCEDURE_TYPE(val0);
                if (proctype == SCM_PROC_SUBR) {
                    /* We don't need to complete environment frame.
                       Just need to adjust sp, so that stack-operating
                       procs called from subr won't be confused. */
                    ADJUST_ARGUMENT_FRAME(val0, argc);
                    sp = argp;

                    SAVE_REGS();
                    val0 = SCM_SUBR(val0)->func(argp, argc,
                                                SCM_SUBR(val0)->data);
                    RESTORE_REGS();

                    NEXT;
                }
                if (proctype == SCM_PROC_CLOSURE) {
                    ADJUST_ARGUMENT_FRAME(val0, argc);
                    if (argc) {
                        FINISH_ENV(SCM_PROCEDURE_INFO(val0),
                                   SCM_CLOSURE(val0)->env);
                    } else {
                        env = SCM_CLOSURE(val0)->env;
                        argp = sp;
                    }
                    VM_ASSERT(SCM_COMPILED_CODE_P(SCM_CLOSURE(val0)->code));
                    vm->base = SCM_COMPILED_CODE(SCM_CLOSURE(val0)->code);
                    pc = vm->base->code;
                    VM_ASSERT(pc != NULL);
                    NEXT;
                }
                /*
                 * Generic function application
                 */
                /* First, compute methods */
                nm = SCM_FALSE;
                if (proctype == SCM_PROC_GENERIC) {
                    if (!SCM_GENERICP(val0)) {
                        /* use scheme-defined MOP.  we modify the stack frame
                           so that it is converted to an application of
                           pure generic fn apply-generic. */
                        ScmObj args = SCM_NIL, arg;
                        int i;
                        for (i=0; i<argc; i++) {
                            POP_ARG(arg);
                            args = Scm_Cons(arg, args);
                        }
                        argp = sp;
                        argc = 2;
                        CHECK_STACK(2);
                        PUSH_ARG(val0);
                        PUSH_ARG(args);
                        val0 = SCM_OBJ(&Scm_GenericApplyGeneric);
                    }
                  generic:
                    /* pure generic application */
                    mm = Scm_ComputeApplicableMethods(SCM_GENERIC(val0),
                                                      argp, argc);
                    if (!SCM_NULLP(mm)) {   
                        mm = Scm_SortMethods(mm, argp, argc);
                        nm = Scm_MakeNextMethod(SCM_GENERIC(val0),
                                                SCM_CDR(mm),
                                                argp, argc, TRUE);
                        val0 = SCM_CAR(mm);
                        proctype = SCM_PROC_METHOD;
                    }
                } else if (proctype == SCM_PROC_NEXT_METHOD) {
                    ScmNextMethod *n = SCM_NEXT_METHOD(val0);
                    if (argc == 0) {
                        CHECK_STACK(n->nargs+1);
                        memcpy(sp, n->args, sizeof(ScmObj)*n->nargs);
                        sp += n->nargs;
                        argc = n->nargs;
                    }
                    if (SCM_NULLP(n->methods)) {
                        val0 = SCM_OBJ(n->generic);
                        proctype = SCM_PROC_GENERIC;
                    } else {
                        nm = Scm_MakeNextMethod(n->generic,
                                                SCM_CDR(n->methods),
                                                argp, argc, TRUE);
                        val0 = SCM_CAR(n->methods);
                        proctype = SCM_PROC_METHOD;
                    }
                } else {
                    Scm_Panic("something wrong.");
                }

                fp = argp;
                if (proctype == SCM_PROC_GENERIC) {
                    /* we have no applicable methods.  call fallback fn. */
                    FINISH_ENV(SCM_PROCEDURE_INFO(val0), NULL);
                    SAVE_REGS();
                    val0 = SCM_GENERIC(val0)->fallback(fp,
                                                       argc,
                                                       SCM_GENERIC(val0));
                    RESTORE_REGS();
                    NEXT;
                }

                /*
                 * Now, apply method
                 */
                ADJUST_ARGUMENT_FRAME(val0, argc);
                VM_ASSERT(proctype == SCM_PROC_METHOD);
                VM_ASSERT(!SCM_FALSEP(nm));
                if (SCM_METHOD(val0)->func) {
                    /* C-defined method */
                    FINISH_ENV(SCM_PROCEDURE_INFO(val0), NULL);
                    SAVE_REGS();
                    val0 = SCM_METHOD(val0)->func(SCM_NEXT_METHOD(nm),
                                                  fp,
                                                  argc,
                                                  SCM_METHOD(val0)->data);
                    RESTORE_REGS();
                } else {
                    /* Scheme-defined method.  next-method arg is passed
                       as the last arg (note that rest arg is already
                       folded. */
                    PUSH_ARG(SCM_OBJ(nm));
                    FINISH_ENV(SCM_PROCEDURE_INFO(val0),
                               SCM_METHOD(val0)->env);
                    VM_ASSERT(SCM_COMPILED_CODE_P(SCM_METHOD(val0)->data));
                    vm->base = SCM_COMPILED_CODE(SCM_METHOD(val0)->data);
                    pc = vm->base->code;
                    VM_ASSERT(pc != NULL);
                }
                NEXT;
                /*
                 * Error case (jumped from ADJUST_ARGUMENT_FRAME)
                 */
              wna:
                VM_ERR(("wrong number of arguments for %S (required %d, got %d)",
                        val0, SCM_PROCEDURE_REQUIRED(val0), argc));
            }
            CASE(SCM_VM_JUMP) {
                ScmObj off;
                FETCH_OPERAND(off);
                VM_ASSERT(SCM_INTP(off));
                pc = vm->base->code + SCM_INT_VALUE(off);
                NEXT;
            }
            CASE(SCM_VM_RET) {
                if (cont == NULL || BOUNDARY_FRAME_P(cont)) {
                    SAVE_REGS();
                    return; /* no more continuations */
                }
                POP_CONT();
                NEXT;
            }
            CASE(SCM_VM_GREF) {
                ScmGloc *gloc;
                FETCH_OPERAND(val0);

                if (!SCM_GLOCP(val0)) {
                    VM_ASSERT(SCM_IDENTIFIERP(val0));
                    gloc = Scm_FindBinding(SCM_IDENTIFIER(val0)->module,
                                           SCM_IDENTIFIER(val0)->name,
                                           FALSE);
                    if (gloc == NULL) {
                        VM_ERR(("unbound variable: %S",
                                SCM_IDENTIFIER(val0)->name));
                    }
                    /* memorize gloc */
                    /* TODO: make it MT safe! */
                    *pc = SCM_OBJ(gloc);
                } else {
                    gloc = SCM_GLOC(val0);
                }
                val0 = SCM_GLOC_GET(gloc);
                if (val0 == SCM_UNBOUND) {
                    VM_ERR(("unbound variable: %S", SCM_OBJ(gloc->name)));
                } else if (SCM_AUTOLOADP(val0)) {
                    SAVE_REGS();
                    val0 = Scm_LoadAutoload(SCM_AUTOLOAD(val0));
                    RESTORE_REGS();
                }
                INCR_PC;
                NEXT;
            }
            CASE(SCM_VM_LREF0) { val0 = ENV_DATA(env, 0); NEXT; }
            CASE(SCM_VM_LREF1) { val0 = ENV_DATA(env, 1); NEXT; }
            CASE(SCM_VM_LREF2) { val0 = ENV_DATA(env, 2); NEXT; }
            CASE(SCM_VM_LREF3) { val0 = ENV_DATA(env, 3); NEXT; }
            CASE(SCM_VM_LREF4) { val0 = ENV_DATA(env, 4); NEXT; }
            CASE(SCM_VM_LREF10) { val0 = ENV_DATA(env->up, 0); NEXT; }
            CASE(SCM_VM_LREF11) { val0 = ENV_DATA(env->up, 1); NEXT; }
            CASE(SCM_VM_LREF12) { val0 = ENV_DATA(env->up, 2); NEXT; }
            CASE(SCM_VM_LREF13) { val0 = ENV_DATA(env->up, 3); NEXT; }
            CASE(SCM_VM_LREF14) { val0 = ENV_DATA(env->up, 4); NEXT; }
            CASE(SCM_VM_LREF) {
                int dep = SCM_VM_INSN_ARG0(code);
                int off = SCM_VM_INSN_ARG1(code);
                ScmEnvFrame *e = env;

                for (; dep > 0; dep--) {
                    VM_ASSERT(e != NULL);
                    e = e->up;
                }
                VM_ASSERT(e != NULL);
                VM_ASSERT(e->size > off);
                val0 = ENV_DATA(e, off);
                NEXT;
            }
            CASE(SCM_VM_LREF0_PUSH) {
                val0 = ENV_DATA(env, 0); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF1_PUSH) {
                val0 = ENV_DATA(env, 1); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF2_PUSH) {
                val0 = ENV_DATA(env, 2); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF3_PUSH) {
                val0 = ENV_DATA(env, 3); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF4_PUSH) {
                val0 = ENV_DATA(env, 4); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF10_PUSH) {
                val0 = ENV_DATA(env->up, 0); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF11_PUSH) {
                val0 = ENV_DATA(env->up, 1); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF12_PUSH) {
                val0 = ENV_DATA(env->up, 2); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF13_PUSH) {
                val0 = ENV_DATA(env->up, 3); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF14_PUSH) {
                val0 = ENV_DATA(env->up, 4); PUSH_ARG(val0); NEXT;
            }
            CASE(SCM_VM_LREF_PUSH) {
                int dep = SCM_VM_INSN_ARG0(code);
                int off = SCM_VM_INSN_ARG1(code);
                ScmEnvFrame *e = env;

                for (; dep > 0; dep--) {
                    VM_ASSERT(e != NULL);
                    e = e->up;
                }
                VM_ASSERT(e != NULL);
                VM_ASSERT(e->size > off);
                val0 = ENV_DATA(e, off);
                PUSH_ARG(val0);
                NEXT;
            }
            CASE(SCM_VM_LET) {
                int nlocals = SCM_VM_INSN_ARG(code);
                int size = CONT_FRAME_SIZE + ENV_SIZE(nlocals);
                ScmObj *next;
                CHECK_STACK(size);
                VM_ASSERT(SCM_INTP(*pc));
                next = vm->base->code + SCM_INT_VALUE(*pc);
                PUSH_CONT(next);
                PUSH_LOCAL_ENV(nlocals, SCM_FALSE);
                INCR_PC;
                NEXT;
            }
            CASE(SCM_VM_TAIL_LET) {
                int nlocals = SCM_VM_INSN_ARG(code);
                int size = ENV_SIZE(nlocals);
                CHECK_STACK(size);
                PUSH_LOCAL_ENV(nlocals, SCM_FALSE);
                NEXT;
            }
            CASE(SCM_VM_GSET) {
                ScmObj loc;
                FETCH_OPERAND(loc);
                if (SCM_GLOCP(loc)) {
                    SCM_GLOC_SET(SCM_GLOC(loc), val0);
                } else {
                    ScmGloc *gloc;
                    ScmIdentifier *id;
                    VM_ASSERT(SCM_IDENTIFIERP(loc));
                    id = SCM_IDENTIFIER(loc);
                    /* If runtime flag LIMIT_MODULE_MUTATION is set,
                       we search only for the id's module, so that set! won't
                       mutate bindings in the other module. */
                    gloc = Scm_FindBinding(id->module, id->name,
                                           SCM_VM_RUNTIME_FLAG_IS_SET(vm, SCM_LIMIT_MODULE_MUTATION));
                    if (gloc == NULL) {
                        /* Do search again for meaningful error message */
                        if (SCM_VM_RUNTIME_FLAG_IS_SET(vm, SCM_LIMIT_MODULE_MUTATION)) {
                            gloc = Scm_FindBinding(id->module, id->name, FALSE);
                            if (gloc != NULL) {
                                VM_ERR(("can't mutate binding of %S, which is in another module",
                                        id->name));
                            }
                            /*FALLTHROUGH*/
                        }
                        VM_ERR(("symbol not defined: %S", loc));
                    }
                    SCM_GLOC_SET(gloc, val0);
                    /* memorize gloc */
                    /* TODO: make it MT safe! */
                    *pc = SCM_OBJ(gloc);
                }
                INCR_PC;
                NEXT;
            }
            CASE(SCM_VM_LSET0) { ENV_DATA(env, 0) = val0; NEXT; }
            CASE(SCM_VM_LSET1) { ENV_DATA(env, 1) = val0; NEXT; }
            CASE(SCM_VM_LSET2) { ENV_DATA(env, 2) = val0; NEXT; }
            CASE(SCM_VM_LSET3) { ENV_DATA(env, 3) = val0; NEXT; }
            CASE(SCM_VM_LSET4) { ENV_DATA(env, 4) = val0; NEXT; }
            CASE(SCM_VM_LSET) {
                int dep = SCM_VM_INSN_ARG0(code);
                int off = SCM_VM_INSN_ARG1(code);
                ScmEnvFrame *e = env;

                for (; dep > 0; dep--) {
                    VM_ASSERT(e != NULL);
                    e = e->up;
                }
                VM_ASSERT(e != NULL);
                VM_ASSERT(e->size > off);
                ENV_DATA(e, off) = val0;
                NEXT;
            }
            CASE(SCM_VM_NOP) {
                NEXT;
            }
            CASE(SCM_VM_MNOP) {
                NEXT;
            }
            CASE(SCM_VM_DEFINE) {
                ScmObj var; ScmSymbol *name;
                FETCH_OPERAND(var);
                VM_ASSERT(SCM_IDENTIFIERP(var));
                INCR_PC;
                Scm_Define(SCM_IDENTIFIER(var)->module,
                           (name = SCM_IDENTIFIER(var)->name), val0);
                val0 = SCM_OBJ(name);
                NEXT;
            }
            CASE(SCM_VM_DEFINE_CONST) {
                ScmObj var; ScmSymbol *name;
                FETCH_OPERAND(var);
                VM_ASSERT(SCM_IDENTIFIERP(var));
                INCR_PC;
                Scm_DefineConst(SCM_IDENTIFIER(var)->module,
                                (name = SCM_IDENTIFIER(var)->name), val0);
                val0 = SCM_OBJ(name);
                NEXT;
            }
            CASE(SCM_VM_IF) {
                if (SCM_FALSEP(val0)) {
                    VM_ASSERT(SCM_INTP(*pc));
                    pc = vm->base->code + SCM_INT_VALUE(*pc);
                } else {
                    INCR_PC;
                }
                NEXT;
            }
            CASE(SCM_VM_LAMBDA) {
                ScmObj body;
                FETCH_OPERAND(body);
                INCR_PC;

                /* preserve environment */
                SAVE_REGS();
                val0 = Scm_MakeClosure(SCM_VM_INSN_ARG0(code),
                                       SCM_VM_INSN_ARG1(code),
                                       body,
                                       get_env(vm));
                RESTORE_REGS();
                NEXT;
            }
            CASE(SCM_VM_RECEIVE) {
                /* TODO: clean up stack management */
                int reqargs = SCM_VM_INSN_ARG0(code);
                int restarg = SCM_VM_INSN_ARG1(code);
                int size = CONT_FRAME_SIZE + ENV_SIZE(reqargs + restarg);
                int i = 0, argsize;
                ScmObj rest = SCM_NIL, tail = SCM_NIL, body;
                ScmObj *nextpc;

                CHECK_STACK(size);
                if (vm->numVals < reqargs) {
                    VM_ERR(("received fewer values than expected"));
                } else if (!restarg && vm->numVals > reqargs) {
                    VM_ERR(("received more values than expected"));
                }
                argsize = reqargs + (restarg? 1 : 0);
                FETCH_INSN(body);

                VM_ASSERT(SCM_INTP(body));
                nextpc = vm->base->code + SCM_INT_VALUE(body);
                PUSH_CONT(nextpc);

                if (reqargs > 0) {
                    PUSH_ARG(val0);
                    i++;
                } else if (restarg && vm->numVals > 0) {
                    SCM_APPEND1(rest, tail, val0);
                    i++;
                }
                for (; i < reqargs; i++) {
                    PUSH_ARG(vm->vals[i-1]);
                }
                if (restarg) {
                    for (; i < vm->numVals; i++) {
                        SCM_APPEND1(rest, tail, vm->vals[i-1]);
                    }
                    PUSH_ARG(rest);
                }
                vm->numVals = 1;
                FINISH_ENV(SCM_FALSE, env);
                NEXT;
            }
            CASE(SCM_VM_QUOTE_INSN) {
                FETCH_OPERAND(val0);
                INCR_PC;
                NEXT;
            }
            /* combined push immediate */
            CASE(SCM_VM_PUSHI) {
                long imm = SCM_VM_INSN_ARG(code);
                val0 = SCM_MAKE_INT(imm);
                PUSH_ARG(val0);
                NEXT;
            }
            CASE(SCM_VM_PUSHNIL) {
                val0 = SCM_NIL;
                PUSH_ARG(val0);
                NEXT;
            }

            /* Inlined procedures */
            CASE(SCM_VM_CONS) {
                ScmObj ca;
                POP_ARG(ca);
                SAVE_REGS();
                val0 = Scm_Cons(ca, val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_CONS_PUSH) {
                ScmObj ca;
                POP_ARG(ca);
                SAVE_REGS();
                val0 = Scm_Cons(ca, val0);
                vm->numVals = 1;
                PUSH_ARG(val0);
                NEXT;
            }
            CASE(SCM_VM_CAR) {
                if (!SCM_PAIRP(val0)) VM_ERR(("pair required, but got %S", val0));
                val0 = SCM_CAR(val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_CAR_PUSH) {
                if (!SCM_PAIRP(val0)) VM_ERR(("pair required, but got %S", val0));
                val0 = SCM_CAR(val0);
                PUSH_ARG(val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_CDR) {
                if (!SCM_PAIRP(val0)) VM_ERR(("pair required, but got %S", val0));
                val0 = SCM_CDR(val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_CDR_PUSH) {
                if (!SCM_PAIRP(val0)) VM_ERR(("pair required, but got %S", val0));
                val0 = SCM_CDR(val0);
                PUSH_ARG(val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_LIST) {
                int nargs = SCM_VM_INSN_ARG(code);
                ScmObj cp = SCM_NIL;
                if (nargs > 0) {
                    ScmObj arg;
                    SAVE_REGS();
                    cp = Scm_Cons(val0, cp);
                    while (--nargs > 0) {
                        POP_ARG(arg);
                        SAVE_REGS();
                        cp = Scm_Cons(arg, cp);
                    }
                }
                val0 = cp;
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_LIST_STAR) {
                int nargs = SCM_VM_INSN_ARG(code);
                ScmObj cp = SCM_NIL;
                if (nargs > 0) {
                    ScmObj arg;
                    cp = val0;
                    while (--nargs > 0) {
                        POP_ARG(arg);
                        SAVE_REGS();
                        cp = Scm_Cons(arg, cp);
                    }
                }
                val0 = cp;
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NOT) {
                val0 = SCM_MAKE_BOOL(SCM_FALSEP(val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NULLP) {
                val0 = SCM_MAKE_BOOL(SCM_NULLP(val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_EQ) {
                ScmObj item;
                POP_ARG(item);
                val0 = SCM_MAKE_BOOL(item == val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_EQV) {
                ScmObj item;
                POP_ARG(item);
                SAVE_REGS();
                val0 = SCM_MAKE_BOOL(Scm_EqvP(item, val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_MEMQ) {
                ScmObj item;
                POP_ARG(item);
                SAVE_REGS();
                val0 = Scm_Memq(item, val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_MEMV) {
                ScmObj item;
                POP_ARG(item);
                SAVE_REGS();
                val0 = Scm_Memv(item, val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_ASSQ) {
                ScmObj item;
                POP_ARG(item);
                SAVE_REGS();
                val0 = Scm_Assq(item, val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_ASSV) {
                ScmObj item;
                POP_ARG(item);
                SAVE_REGS();
                val0 = Scm_Assv(item, val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_PAIRP) {
                val0 = SCM_MAKE_BOOL(SCM_PAIRP(val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_CHARP) {
                val0 = SCM_MAKE_BOOL(SCM_CHARP(val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_EOFP) {
                val0 = SCM_MAKE_BOOL(SCM_EOFP(val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_STRINGP) {
                val0 = SCM_MAKE_BOOL(SCM_STRINGP(val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_SYMBOLP) {
                val0 = SCM_MAKE_BOOL(SCM_SYMBOLP(val0));
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_APPEND) {
                int nargs = SCM_VM_INSN_ARG(code);
                ScmObj cp = SCM_NIL, arg;
                if (nargs > 0) {
                    cp = val0;
                    while (--nargs > 0) {
                        POP_ARG(arg);
                        SAVE_REGS();
                        if (Scm_Length(arg) < 0)
                            VM_ERR(("list required, but got %S\n", arg));
                        cp = Scm_Append2(arg, cp);
                    }
                }
                val0 = cp;
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_REVERSE) {
                SAVE_REGS();
                val0 = Scm_Reverse(val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_APPLY) {
                int nargs = SCM_VM_INSN_ARG(code);
                ScmObj cp;
                while (--nargs > 1) {
                    POP_ARG(cp);
                    SAVE_REGS();
                    val0 = Scm_Cons(cp, val0);
                }
                cp = val0;     /* now cp has arg list */
                POP_ARG(val0); /* get proc */

                /*TODO: tail-apply*/
                CHECK_STACK(CONT_FRAME_SIZE);
                PUSH_CONT(pc);
                pc = PC_TO_RETURN;

                SAVE_REGS();
                val0 = Scm_VMApply(val0, cp);
                RESTORE_REGS();
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_PROMISE) {
                SAVE_REGS();
                val0 = Scm_MakePromise(val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_SETTER) {
                SAVE_REGS();
                val0 = Scm_Setter(val0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_VALUES) {
                int nargs = SCM_VM_INSN_ARG(code), i;
                if (nargs >= SCM_VM_MAX_VALUES)
                    VM_ERR(("values got too many args"));
                VM_ASSERT(nargs -1 <= sp - vm->stackBase);
                if (nargs > 0) {
                    for (i = nargs-1; i>0; i--) {
                        vm->vals[i-1] = val0;
                        POP_ARG(val0);
                    }
                }
                vm->numVals = nargs;
                NEXT;
            }
            CASE(SCM_VM_VEC) {
                int nargs = SCM_VM_INSN_ARG(code), i;
                ScmObj vec;
                SAVE_REGS();
                vec = Scm_MakeVector(nargs, SCM_UNDEFINED);
                if (nargs > 0) {
                    ScmObj arg = val0;
                    for (i=nargs-1; i > 0; i--) {
                        SCM_VECTOR_ELEMENT(vec, i) = arg;
                        POP_ARG(arg);
                    }
                    SCM_VECTOR_ELEMENT(vec, 0) = arg;
                }
                val0 = vec;
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_APP_VEC) {
                int nargs = SCM_VM_INSN_ARG(code);
                ScmObj cp = SCM_NIL, arg;
                if (nargs > 0) {
                    cp = val0;
                    while (--nargs > 0) {
                        POP_ARG(arg);
                        SAVE_REGS();
                        if (Scm_Length(arg) < 0)
                            VM_ERR(("list required, but got %S\n", arg));
                        cp = Scm_Append2(arg, cp);
                    }
                }
                SAVE_REGS();
                val0 = Scm_ListToVector(cp);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_VEC_LEN) {
                int siz;
                if (!SCM_VECTORP(val0))
                    VM_ERR(("vector expected, but got %S\n", val0));
                siz = SCM_VECTOR_SIZE(val0);
                val0 = SCM_MAKE_INT(siz);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_VEC_REF) {
                ScmObj vec;
                int k;
                POP_ARG(vec);
                if (!SCM_VECTORP(vec))
                    VM_ERR(("vector expected, but got %S\n", vec));
                if (!SCM_INTP(val0))
                    VM_ERR(("integer expected, but got %S\n", val0));
                k = SCM_INT_VALUE(val0);
                if (k < 0 || k >= SCM_VECTOR_SIZE(vec))
                    VM_ERR(("index out of range: %d\n", k));
                val0 = SCM_VECTOR_ELEMENT(vec, k);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_VEC_SET) {
                ScmObj vec, ind;
                int k;
                POP_ARG(ind);
                POP_ARG(vec);
                if (!SCM_VECTORP(vec))
                    VM_ERR(("vector expected, but got %S\n", vec));
                if (!SCM_INTP(ind))
                    VM_ERR(("integer expected, but got %S\n", ind));
                k = SCM_INT_VALUE(ind);
                if (k < 0 || k >= SCM_VECTOR_SIZE(vec))
                    VM_ERR(("index out of range: %d\n", k));
                SCM_VECTOR_ELEMENT(vec, k) = val0;
                val0 = SCM_UNDEFINED;
                vm->numVals = 1;
                NEXT;

            }
            CASE(SCM_VM_NUMEQ2) {
                ScmObj arg;
                POP_ARG(arg);
                if (SCM_INTP(val0) && SCM_INTP(arg)) {
                    val0 = SCM_MAKE_BOOL(val0 == arg);
                } else {
                    SAVE_REGS();
                    val0 = SCM_MAKE_BOOL(Scm_NumEq(arg, val0));
                }
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMLT2) {
                ScmObj arg;
                POP_ARG(arg);
                SAVE_REGS();
                val0 = SCM_MAKE_BOOL(Scm_NumCmp(arg, val0) < 0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMLE2) {
                ScmObj arg;
                POP_ARG(arg);
                SAVE_REGS();
                val0 = SCM_MAKE_BOOL(Scm_NumCmp(arg, val0) <= 0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMGT2) {
                ScmObj arg;
                POP_ARG(arg);
                SAVE_REGS();
                val0 = SCM_MAKE_BOOL(Scm_NumCmp(arg, val0) > 0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMGE2) {
                ScmObj arg;
                POP_ARG(arg);
                SAVE_REGS();
                val0 = SCM_MAKE_BOOL(Scm_NumCmp(arg, val0) >= 0);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMADD2) {
                ScmObj arg;
                POP_ARG(arg);
                SAVE_REGS();
                val0 = Scm_Add(arg, val0, SCM_NIL);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMSUB2) {
                ScmObj arg;
                POP_ARG(arg);
                SAVE_REGS();
                val0 = Scm_Subtract(arg, val0, SCM_NIL);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMADDI) {
                long imm = SCM_VM_INSN_ARG(code);
                if (SCM_INTP(val0)) {
                    imm += SCM_INT_VALUE(val0);
                    if (SCM_SMALL_INT_FITS(imm)) {
                        val0 = SCM_MAKE_INT(imm);
                    } else {
                        SAVE_REGS();
                        val0 = Scm_MakeInteger(imm);
                    }
                } else {
                    SAVE_REGS();
                    val0 = Scm_Add(SCM_MAKE_INT(imm), val0, SCM_NIL);
                }
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_NUMSUBI) {
                long imm = SCM_VM_INSN_ARG(code);
                if (SCM_INTP(val0)) {
                    imm -= SCM_INT_VALUE(val0);
                    if (SCM_SMALL_INT_FITS(imm)) {
                        val0 = SCM_MAKE_INT(imm);
                    } else {
                        SAVE_REGS();
                        val0 = Scm_MakeInteger(imm);
                    }
                } else {
                    SAVE_REGS();
                    val0 = Scm_Subtract(SCM_MAKE_INT(imm), val0, SCM_NIL);
                }
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_READ_CHAR) {
                int nargs = SCM_VM_INSN_ARG(code), ch = 0;
                ScmPort *port;
                if (nargs == 1) {
                    if (!SCM_IPORTP(val0))
                        VM_ERR(("read-char: input port required: %S", val0));
                    port = SCM_PORT(val0);
                } else {
                    port = SCM_CURIN;
                }
                SAVE_REGS();
                SCM_GETC(ch, port);
                RESTORE_REGS();
                val0 = (ch < 0)? SCM_EOF : SCM_MAKE_CHAR(ch);
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_WRITE_CHAR) {
                int nargs = SCM_VM_INSN_ARG(code);
                ScmObj ch;
                ScmPort *port;
                if (nargs == 2) {
                    if (!SCM_OPORTP(val0))
                        VM_ERR(("write-char: output port required: %S", val0));
                    port = SCM_PORT(val0);
                    POP_ARG(ch);
                } else {
                    port = SCM_CUROUT;
                    ch = val0;
                }
                if (!SCM_CHARP(ch))
                    VM_ERR(("write-char: character required: %S", ch));
                SAVE_REGS();
                SCM_PUTC(SCM_CHAR_VALUE(ch), port);
                RESTORE_REGS();
                val0 = SCM_UNDEFINED;
                vm->numVals = 1;
                NEXT;
            }
            CASE(SCM_VM_SLOT_REF) {
                ScmObj obj;
                POP_ARG(obj);
                CHECK_STACK(CONT_FRAME_SIZE);
                PUSH_CONT(pc);
                pc = PC_TO_RETURN;
                SAVE_REGS();
                val0 = Scm_VMSlotRef(obj, val0, FALSE);
                vm->numVals = 1;
                RESTORE_REGS();
                NEXT;
            }
            CASE(SCM_VM_SLOT_SET) {
                ScmObj obj, slot;
                POP_ARG(slot);
                POP_ARG(obj);
                CHECK_STACK(CONT_FRAME_SIZE);
                PUSH_CONT(pc);
                pc = PC_TO_RETURN;
                SAVE_REGS();
                val0 = Scm_VMSlotSet(obj, slot, val0);
                vm->numVals = 1;
                RESTORE_REGS();
                NEXT;
            }
            CASE(SCM_VM_HALT) {
                VM_ERR(("HALT instruction called!"));
                NEXT;
            }
#ifndef __GNUC__
            DEFAULT
                Scm_Panic("Illegal vm instruction: %08x",
                          SCM_VM_INSN_CODE(code));
#endif
        }
    }
}
/* End of run_loop */

/*==================================================================
 * Stack management
 */

#define ADJUST_CONT_ENV(c, start)                                       \
    do {                                                                \
      for (c = (start); IN_STACK_P((ScmObj*)c); c = c->prev) {          \
        if (IN_STACK_P((ScmObj*)(c->env)) && c->env->size == -1) {      \
          c->env = c->env->up;                                          \
        }                                                               \
      }                                                                 \
    } while (0)

/* move the current chain of environments from the stack to the heap.
   if there're continuation frames which point to the moved env, those
   pointers are adjusted as well. */
static inline ScmEnvFrame *save_env(ScmVM *vm,
                                    ScmEnvFrame *env_begin,
                                    ScmContFrame *cont_begin)
{
    ScmEnvFrame *e = env_begin, *prev = NULL, *next, *head = env_begin, *saved;
    ScmContFrame *c = cont_begin;
    ScmObj *s;
    ScmCStack *cstk;
    ScmEscapePoint *eh;

    if (!IN_STACK_P((ScmObj*)e)) return e;
    
    /* First pass - move envs in stack to heap.  After env is moved,
       the location of 'up' pointer in the env frame in the stack
       contains the new location of env frame, so that the env pointers
       in continuation frames will be adjusted in the second pass.
       Such forwarded pointer is indicated by env->size == -1. */
    do {
        int esize = e->size;
        int bsize = ENV_SIZE(esize) * sizeof(ScmObj);
        s = SCM_NEW2(ScmObj*, bsize);
        memcpy(s, ENV_FP(e), bsize);
        saved = (ScmEnvFrame*)(s + esize);
        if (prev) prev->up = saved;
        if (e == env_begin) head = saved;
        next = e->up;
        e->up = prev = saved; /* forwarding pointer */
        e->size = -1;         /* indicates forwarded */
        e->info = SCM_FALSE;  /* clear pointer for GC */
        e = next;
    } while (IN_STACK_P((ScmObj*)e));
    
    /* Second pass - scan continuation frames in the stack, and forwards
       env pointers */
    ADJUST_CONT_ENV(c, cont_begin);
    for (cstk = vm->cstack; cstk; cstk = cstk->prev) {
        ADJUST_CONT_ENV(c, cstk->cont);
    }
    for (eh = vm->escapePoint; eh; eh = eh->prev) {
        ADJUST_CONT_ENV(c, eh->cont);
    }
    return head;
}

/* Copy the continuation to the heap. 
   Note that the naive copy of the stack won't work, since
   - Environment frame in it may be moved later, and corresponding
     pointers must be adjusted, and
   - The stack frame contains a pointer value to other frames,
     which will be invalid if the continuation is resumed by
     the different thread.
 */
static void save_cont(ScmVM *vm, ScmContFrame *cont_begin)
{
    ScmContFrame *c = cont_begin, *prev = NULL;
    ScmCStack *cstk;
    ScmEscapePoint *ep;
    
    for (; IN_STACK_P((ScmObj*)c); c = c->prev) {
        ScmEnvFrame *e = save_env(vm, c->env, c);
        int size = (CONT_FRAME_SIZE + c->size) * sizeof(ScmObj);
        ScmContFrame *csave = SCM_NEW2(ScmContFrame*, size);

        if (c->env == vm->env) vm->env = e;
        if (c == vm->cont) vm->cont = csave;
        if (c->argp) {
            memcpy(csave, c, CONT_FRAME_SIZE * sizeof(ScmObj));
            if (c->size) {
                memcpy((void**)csave + CONT_FRAME_SIZE,
                       c->argp, c->size * sizeof(ScmObj));
            }
            csave->argp = ((ScmObj *)csave + CONT_FRAME_SIZE);
        } else {
            /* C continuation */
            memcpy(csave, c, (CONT_FRAME_SIZE + c->size) * sizeof(ScmObj));
        }
        for (cstk = vm->cstack; cstk; cstk = cstk->prev) {
            if (cstk->cont == c) cstk->cont = csave;
        }
        for (ep = vm->escapePoint; ep; ep = ep->prev) {
            if (ep->cont == c) ep->cont = csave;
        }
        if (prev) prev->prev = csave;
        prev = csave;
    }
}

static void save_stack(ScmVM *vm)
{
    ScmObj *p;
    struct timeval t0, t1;
    int stats = SCM_VM_RUNTIME_FLAG_IS_SET(vm, SCM_COLLECT_VM_STATS);

    if (stats) {
        gettimeofday(&t0, NULL);
    }
    
    vm->env = save_env(vm, vm->env, vm->cont);
    save_cont(vm, vm->cont);
    memmove(vm->stackBase, vm->argp,
            (vm->sp - (ScmObj*)vm->argp) * sizeof(ScmObj*));
    vm->sp -= (ScmObj*)vm->argp - vm->stackBase;
    vm->argp = vm->stackBase;
    /* Clear the stack.  This removes bogus pointers and accelerates GC */
    for (p = vm->sp; p < vm->stackEnd; p++) *p = NULL;

    if (stats) {
        gettimeofday(&t1, NULL);
        vm->stat.sovCount++;
        vm->stat.sovTime +=
            (t1.tv_sec - t0.tv_sec)*1000000+(t1.tv_usec - t0.tv_usec);
    }
}

static ScmEnvFrame *get_env(ScmVM *vm)
{
    return vm->env = save_env(vm, vm->env, vm->cont);
}

/*==================================================================
 * Function application from C
 */

/* The Scm_VMApply family is supposed to be called in SUBR.  It doesn't really
   applies the function in it.  Instead, it modifies the VM state so that
   the specified function will be called immediately after this SUBR
   returns to the VM.   The return value of Scm_VMApply is just a PROC,
   but it should be returned as the return value of SUBR, which will be
   used by the VM.
   NB: we don't check proc is a procedure or not.  It can be a non-procedure
   object, because of the object-apply hook. */

/* Static VM instruction arrays.
   Scm_VMApplyN modifies VM's pc to point it. */

static ScmObj apply_calls[][2] = {
    { SCM_VM_INSN1(SCM_VM_TAIL_CALL, 0),
      SCM_VM_INSN(SCM_VM_RET) },
    { SCM_VM_INSN1(SCM_VM_TAIL_CALL, 1),
      SCM_VM_INSN(SCM_VM_RET) },
    { SCM_VM_INSN1(SCM_VM_TAIL_CALL, 2),
      SCM_VM_INSN(SCM_VM_RET) },
    { SCM_VM_INSN1(SCM_VM_TAIL_CALL, 3),
      SCM_VM_INSN(SCM_VM_RET) },
};

ScmObj Scm_VMApply(ScmObj proc, ScmObj args)
{
    DECL_REGS;
    int numargs = Scm_Length(args);
    int reqstack;
    ScmObj cp;

    if (numargs < 0) Scm_Error("improper list not allowed: %S", args);
    reqstack = ENV_SIZE(numargs) + 1;
    if (reqstack >= SCM_VM_STACK_SIZE) {
        /* there's no way we can accept that many arguments */
        Scm_Error("too many arguments (%d) to apply", numargs);
    }
    CHECK_STACK(reqstack);
    SCM_FOR_EACH(cp, args) {
        PUSH_ARG(SCM_CAR(cp));
    }
    if (numargs <= 3) {
        pc = apply_calls[numargs];
    } else {
        pc = SCM_NEW_ARRAY(ScmObj, 2);
        pc[0] = SCM_VM_INSN1(SCM_VM_TAIL_CALL, numargs);
        pc[1] = SCM_VM_INSN(SCM_VM_RET);
    }
    SAVE_REGS();
    return proc;
}

/* shortcuts for common cases */
ScmObj Scm_VMApply0(ScmObj proc)
{
    ScmVM *vm = theVM;
    vm->pc = apply_calls[0];
    return proc;
}

ScmObj Scm_VMApply1(ScmObj proc, ScmObj arg)
{
    DECL_REGS;
    CHECK_STACK(1);
    PUSH_ARG(arg);
    pc = apply_calls[1];
    SAVE_REGS();
    return proc;
}

ScmObj Scm_VMApply2(ScmObj proc, ScmObj arg1, ScmObj arg2)
{
    DECL_REGS;
    CHECK_STACK(2);
    PUSH_ARG(arg1);
    PUSH_ARG(arg2);
    pc = apply_calls[2];
    SAVE_REGS();
    return proc;
}

ScmObj Scm_VMApply3(ScmObj proc, ScmObj arg1, ScmObj arg2, ScmObj arg3)
{
    DECL_REGS;
    CHECK_STACK(3);
    PUSH_ARG(arg1);
    PUSH_ARG(arg2);
    PUSH_ARG(arg3);
    pc = apply_calls[3];
    SAVE_REGS();
    return proc;
}

/* support proc. for eval.  compile expr in the module nmodule,
   ensuring the env is reset to omodule afterwards */
static ScmObj compile_for_eval(ScmObj expr,
                               ScmModule *nmodule,
                               ScmModule *omodule)
{
    ScmObj v = SCM_NIL;
    SCM_UNWIND_PROTECT {
        theVM->module = nmodule;
        v = Scm_Compile(expr, SCM_NIL, SCM_COMPILE_NORMAL);
    }
    SCM_WHEN_ERROR {
        theVM->module = omodule;
        SCM_NEXT_HANDLER;
        /*NOTREACHED*/
    }
    SCM_END_PROTECT;
    theVM->module = omodule;
    return v;
}

static ScmObj eval_restore_env(ScmObj *args, int argc, void *data)
{
    Scm_VM()->module = SCM_MODULE(data);
    return SCM_UNDEFINED;
}

/* For now, we only supports a module as the evaluation environment */
ScmObj Scm_VMEval(ScmObj expr, ScmObj e)
{
    ScmObj v = SCM_NIL;
    ScmVM *vm = Scm_VM();
    int restore_module = FALSE;
    
    /* NB: v is ScmCompiledCode. */
        
    if (SCM_UNBOUNDP(e)) {
        /* if env is not given, just use the current env */
        v = Scm_Compile(expr, SCM_NIL, SCM_COMPILE_NORMAL);
    } else if (!SCM_MODULEP(e)) {
        Scm_Error("module required, but got %S", e);
    } else {
        v = compile_for_eval(expr, SCM_MODULE(e), theVM->module);
        restore_module = TRUE;
    }
    if (SCM_VM_COMPILER_FLAG_IS_SET(theVM, SCM_COMPILE_SHOWRESULT)) {
        Scm_Printf(theVM->curerr, "== %#S\n", v);
    }

    /*Scm_CompiledCodeDump(v);*/

    vm->numVals = 1;
    if (restore_module) {
        /* if we swap the module, we need to make sure it is recovered
           after eval */
        ScmObj body = Scm_MakeClosure(0, 0, v, get_env(vm));
        ScmObj before = Scm_MakeSubr(eval_restore_env, SCM_MODULE(e),
                                     0, 0, SCM_FALSE);
        ScmObj after = Scm_MakeSubr(eval_restore_env, (void*)vm->module,
                                    0, 0, SCM_FALSE);
        return Scm_VMDynamicWind(before, body, after);
    } else {
        /* shortcut */
        SCM_ASSERT(SCM_COMPILED_CODE_P(v));
        vm->base = SCM_COMPILED_CODE(v);
        vm->pc = SCM_COMPILED_CODE(v)->code;
        SCM_ASSERT(vm->pc != NULL);
        return SCM_UNDEFINED;
    }
}

/*-------------------------------------------------------------
 * User level eval and apply.
 *   When the C routine wants the Scheme code to return to the
 *   routine, instead of using C-continuation, the continuation
 *   "cross the border" of C-stack and Scheme-stack.  This
 *   border has peculiar characteristics.   Once the Scheme
 *   returns, continuations saved during the execution of the
 *   Scheme code becomes invalid.
 *
 *   At the implementation level, this boundary is kept in a
 *   structure ScmCStack.
 */

/* Border gate.  All the C->Scheme calls should go through here.
 *
 *   The current C stack information is saved in cstack.  The
 *   current VM stack information is saved (as a continuation
 *   frame pointer) in cstack.cont.
 */
/* program is ScmCompiledCode. */
static ScmObj user_eval_inner(ScmObj program)
{
    DECL_REGS_VOLATILE;
    ScmCStack cstack;
    /* Save prev_pc, for the boundary continuation uses pc slot
       to mark the boundary. */
    ScmObj * volatile prev_pc = pc;

    /* Push extra continuation.  This continuation frame is a 'boundary
       frame' and marked by pc == &boundaryFrameMark.   VM loop knows
       it should return to C frame when it sees a boundary frame.
       A boundary frame also keeps the unfinished argument frame at
       the point when Scm_Eval or Scm_Apply is called. */
    CHECK_STACK(CONT_FRAME_SIZE);
    PUSH_CONT(&boundaryFrameMark);
    SCM_ASSERT(SCM_COMPILED_CODE_P(program));
    vm->base = SCM_COMPILED_CODE(program);
    pc = vm->base->code;
    SCM_ASSERT(pc != NULL);
    SAVE_REGS();

    cstack.prev = vm->cstack;
    cstack.cont = vm->cont;
    vm->cstack = &cstack;
    
  restart:
    vm->escapeReason = SCM_VM_ESCAPE_NONE;
    if (sigsetjmp(cstack.jbuf, TRUE) == 0) {
        run_loop();
        val0 = vm->val0;
        if (vm->cont == cstack.cont) {
            RESTORE_REGS();
            POP_CONT();
            pc = prev_pc;
            SAVE_REGS();
        }
    } else {
        /* An escape situation happened. */
        if (vm->escapeReason == SCM_VM_ESCAPE_CONT) {
             ScmEscapePoint *ep = (ScmEscapePoint*)vm->escapeData[0];
            if (ep->cstack == vm->cstack) {
                ScmObj handlers = throw_cont_calculate_handlers(ep, vm);
                /* force popping continuation when restarted */
                vm->pc = PC_TO_RETURN;
                vm->val0 = throw_cont_body(handlers, ep, vm->escapeData[1]);
                goto restart;
            } else {
                SCM_ASSERT(vm->cstack && vm->cstack->prev);
                vm->cont = cstack.cont;
                val0 = vm->val0;
                RESTORE_REGS();
                POP_CONT();
                SAVE_REGS();
                vm->cstack = vm->cstack->prev;
                siglongjmp(vm->cstack->jbuf, 1);
            }
        } else if (vm->escapeReason == SCM_VM_ESCAPE_ERROR) {
            ScmEscapePoint *ep = (ScmEscapePoint*)vm->escapeData[0];
            if (ep && ep->cstack == vm->cstack) {
                vm->cont = ep->cont;
                vm->pc = PC_TO_RETURN;
                goto restart;
            } else if (vm->cstack->prev == NULL) {
                /* This loop is the outermost C stack, and nobody will
                   capture the error.  Usually this means we're running
                   scripts.  We can safely exit here, for the dynamic
                   stack is already rewound. */
                exit(EX_SOFTWARE);
            } else {
                /* Jump again until C stack is recovered.  We sould pop
                   the extra continuation frame so that the VM stack
                   is consistent. */
                vm->cont = cstack.cont;
                val0 = vm->val0;
                RESTORE_REGS();
                POP_CONT();
                SAVE_REGS();
                vm->cstack = vm->cstack->prev;
                siglongjmp(vm->cstack->jbuf, 1);
            }
        } else {
            Scm_Panic("invalid longjmp");
        }
        /* NOTREACHED */
    }
    vm->cstack = vm->cstack->prev;
    return vm->val0;
}

ScmObj Scm_Eval(ScmObj expr, ScmObj e)
{
    ScmObj v = SCM_NIL;
    if (SCM_UNBOUNDP(e)) {
        /* if env is not given, just use the current env */
        v = Scm_Compile(expr, SCM_NIL, SCM_COMPILE_NORMAL);
    } else if (!SCM_MODULEP(e)) {
        Scm_Error("module required, but got %S", e);
    } else {
        v = compile_for_eval(expr, SCM_MODULE(e), theVM->module);
    }
    if (SCM_VM_COMPILER_FLAG_IS_SET(theVM, SCM_COMPILE_SHOWRESULT)) {
        Scm_Printf(theVM->curerr, "==\n");
        Scm_CompiledCodeDump(v);
    }
    return user_eval_inner(v);
}

ScmObj Scm_Apply(ScmObj proc, ScmObj args)
{
    ScmObj code = SCM_NIL, tail = SCM_NIL, cp;
    int nargs = 0;

    /*TODO: need this?*/
    SCM_APPEND1(code, tail, SCM_VM_INSN1(SCM_VM_CHECK_STACK, Scm_Length(args)));
    SCM_FOR_EACH(cp, args) {
        SCM_APPEND1(code, tail, SCM_CAR(cp));
        SCM_APPEND1(code, tail, SCM_VM_INSN(SCM_VM_PUSH));
        nargs++;
    }
    SCM_APPEND1(code, tail, proc);
    SCM_APPEND1(code, tail, SCM_VM_INSN1(SCM_VM_TAIL_CALL, nargs));
    code = Scm_Cons(SCM_VM_INSN1(SCM_VM_PRE_TAIL, nargs),code);
    return user_eval_inner(Scm_PackCode(code));
}

/* Arrange C function AFTER to be called after the procedure returns.
 * Usually followed by Scm_VMApply* function.
 */
void Scm_VMPushCC(ScmObj (*after)(ScmObj result, void **data),
                  void **data, int datasize)
{
    DECL_REGS;
    int i;
    ScmContFrame *cc;

    CHECK_STACK(CONT_FRAME_SIZE+datasize);
    cc = (ScmContFrame*)sp;
    sp += CONT_FRAME_SIZE;
    cc->prev = cont;
    cc->argp = NULL;
    cc->size = datasize;
    cc->pc = (ScmObj*)after;
    cc->base = vm->base;
    cc->env = env;
    for (i=0; i<datasize; i++) {
        PUSH_ARG(SCM_OBJ(data[i]));
    }
    cont = cc;
    argp = sp;
    SAVE_REGS();
}

/*=================================================================
 * Dynamic handlers
 */

static ScmObj dynwind_before_cc(ScmObj result, void **data);
static ScmObj dynwind_body_cc(ScmObj result, void **data);
static ScmObj dynwind_after_cc(ScmObj result, void **data);

ScmObj Scm_VMDynamicWind(ScmObj before, ScmObj body, ScmObj after)
{
    void *data[3];

#if 0 /* allow object-apply hook for all thunks */
    if (!SCM_PROCEDUREP(before) || SCM_PROCEDURE_REQUIRED(before) != 0)
        Scm_Error("thunk required for BEFORE argument, but got %S", before);
    if (!SCM_PROCEDUREP(body) || SCM_PROCEDURE_REQUIRED(body) != 0)
        Scm_Error("thunk required for BODY argument, but got %S", body);
    if (!SCM_PROCEDUREP(after) || SCM_PROCEDURE_REQUIRED(after) != 0)
        Scm_Error("thunk required for AFTER argument, but got %S", after);
#endif

    data[0] = (void*)before;
    data[1] = (void*)body;
    data[2] = (void*)after;

    Scm_VMPushCC(dynwind_before_cc, data, 3);
    return Scm_VMApply0(before);
}

static ScmObj dynwind_before_cc(ScmObj result, void **data)
{
    ScmObj before  = SCM_OBJ(data[0]);
    ScmObj body = SCM_OBJ(data[1]);
    ScmObj after = SCM_OBJ(data[2]);
    ScmObj prev = theVM->handlers;

    void *d[2];
    d[0] = (void*)after;
    d[1] = (void*)prev;
    theVM->handlers = Scm_Cons(Scm_Cons(before, after), prev);
    Scm_VMPushCC(dynwind_body_cc, d, 2);
    return Scm_VMApply0(body);
}

static ScmObj dynwind_body_cc(ScmObj result, void **data)
{
    ScmVM *vm = theVM;
    ScmObj after = SCM_OBJ(data[0]);
    ScmObj prev  = SCM_OBJ(data[1]);
    void *d[3];

    vm->handlers = prev;
    d[0] = (void*)result;
    d[1] = (void*)vm->numVals;
    if (vm->numVals > 1) {
        ScmObj *array = SCM_NEW_ARRAY(ScmObj, (vm->numVals-1));
        memcpy(array, vm->vals, sizeof(ScmObj)*(vm->numVals-1));
        d[2] = (void*)array;
    }
    Scm_VMPushCC(dynwind_after_cc, d, 3);
    return Scm_VMApply0(after);
}

static ScmObj dynwind_after_cc(ScmObj result, void **data)
{
    ScmObj val0 = SCM_OBJ(data[0]);
    ScmVM *vm = theVM;
    int nvals = (int)data[1];
    vm->numVals = nvals;
    if (nvals > 1) {
        SCM_ASSERT(nvals <= SCM_VM_MAX_VALUES);
        memcpy(vm->vals, data[2], sizeof(ScmObj)*(nvals-1));
    }
    return val0;
}

/* C-friendly wrapper */
ScmObj Scm_VMDynamicWindC(ScmObj (*before)(ScmObj *args, int nargs, void *data),
                          ScmObj (*body)(ScmObj *args, int nargs, void *data),
                          ScmObj (*after)(ScmObj *args, int nargs, void *data),
                          void *data)
{
    ScmObj beforeproc, bodyproc, afterproc;
    beforeproc =
        before ? Scm_MakeSubr(before, data, 0, 0, SCM_FALSE) : Scm_NullProc();
    afterproc =
        after ? Scm_MakeSubr(after, data, 0, 0, SCM_FALSE) : Scm_NullProc();
    bodyproc =
        body ? Scm_MakeSubr(body, data, 0, 0, SCM_FALSE) : Scm_NullProc();
    
    return Scm_VMDynamicWind(beforeproc, bodyproc, afterproc);
}


/*=================================================================
 * Exception handling
 */

/* Conceptually, exception handling is nothing more than a particular
 * combination of dynamic-wind and call/cc.   Gauche implements a parts
 * of it in C so that it will be efficient and safer to use.
 *
 * The most basic layer consists of these two functions:
 *
 *  with-exception-handler
 *  raise
 *
 * There is a slight problem, though.  These two functions are defined
 * both in srfi-18 (multithreads) and srfi-34 (exception handling), and
 * two disagrees in the semantics of raise.
 *
 * Srfi-18 requires an exception handler to be called with the same dynamic
 * environment as the one of the primitive that raises the exception.
 * That means when an exception handler is running, the current
 * exception handler is the running handler itself.  Naturally, calling
 * raise unconditionally within the exception handler causes infinite loop.
 *
 * Srfi-34 says that an exception handler is called with the same dynamic
 * envionment where the exception is raised, _except_ that the current
 * exception handler is "popped", i.e. when an exception handler is running,
 * the current exception handler is the "outer" or "old" one.  Calling
 * raise within an exception handler passes the control to the outer
 * exception handler.
 *
 * At this point I haven't decided which model Gauche should support natively.
 * The current implementation predates srfi-34 and roughly follows srfi-18.
 * It appears that srfi-18's mechanism is more "primitive" or "lightweight"
 * than srfi-34's, so it's likely that Gauche will continue to support
 * srfi-18 model natively, and maybe provides srfi-34's interface by an
 * additional module.
 *
 * The following is a model of the current implementation, sans the messy
 * part of handling C stacks.
 * Suppose a system variable %xh keeps the list of exception handlers.
 *
 *  (define (current-exception-handler) (car %xh))
 *
 *  (define (raise exn)
 *    (receive r ((car %xh) exn)
 *      (when (uncontinuable-exception? exn)
 *        (set! %xh (cdr %xh))
 *        (error "returned from uncontinuable exception"))
 *      (apply values r)))
 *
 *  (define (with-exception-handler handler thunk)
 *    (let ((prev %xh))
 *      (dynamic-wind
 *        (lambda () (set! %xh (cons handler)))
 *        thunk
 *        (lambda () (set! %xh prev)))))
 *
 * In C level, the chain of the handlers are represented in the chain
 * of ScmEscapePoints.
 *
 * Note that this model assumes an exception handler returns unless it
 * explictly invokes continuation captured elsewhere.   In reality,
 * "error" exceptions are not supposed to return (hence it is checked
 * in raise).  Gauche provides another useful exception handling
 * constructs that automates such continuation capturing.  It can be
 * explained by the following code.
 *
 * (define (with-error-handler handler thunk)
 *   (call/cc
 *     (lambda (cont)
 *       (let ((prev-handler (current-exception-handler)))
 *         (with-exception-handler
 *           (lambda (exn)
 *             (if (error? exn)
 *                 (call-with-values (handler exn) cont)
 *                 (prev-handler exn)))
 *           thunk)))))
 *
 * In the actual implementation,
 *
 *  - No "real" continuation procedure is created, but a lightweight
 *    mechanism is used.  The lightweight mechanism is similar to
 *    "one-shot" callback (call/1cc in Chez Scheme).
 *  - The error handler chain is kept in vm->escapePoint
 *  - There are messy lonjmp/setjmp stuff involved to keep C stack sane.
 */

/*
 * Default exception handler
 *  This is what we have as the system default, and also
 *  what with-error-handler installs as an exception handler.
 */

void Scm_VMDefaultExceptionHandler(ScmObj e)
{
    ScmVM *vm = theVM;
    ScmEscapePoint *ep = vm->escapePoint;
    ScmObj hp;

    if (ep) {
        /* There's a escape point defined by with-error-handler */
        ScmObj target, current;
        ScmObj result, rvals[SCM_VM_MAX_VALUES];
        int numVals, i;

        vm->escapePoint = ep->prev;

        /* Call the error handler and save the results. */
        result = Scm_Apply(ep->ehandler, SCM_LIST1(e));
        if ((numVals = vm->numVals) > 1) {
            for (i=0; i<numVals-1; i++) rvals[i] = vm->vals[i];
        }
        target = ep->handlers;
        current = vm->handlers;
        /* Call dynamic handlers */
        for (hp = current; SCM_PAIRP(hp) && hp != target; hp = SCM_CDR(hp)) {
            ScmObj proc = SCM_CDAR(hp);
            vm->handlers = SCM_CDR(hp);
            Scm_Apply(proc, SCM_NIL);
        }
        /* Install the continuation */
        for (i=0; i<numVals; i++) vm->vals[i] = rvals[i];
        vm->numVals = numVals;
        vm->val0 = result;
        vm->cont = ep->cont;
        if (ep->errorReporting) {
            SCM_VM_RUNTIME_FLAG_SET(vm, SCM_ERROR_BEING_REPORTED);
        }
    } else {
        Scm_ReportError(e);
        /* unwind the dynamic handlers */
        SCM_FOR_EACH(hp, vm->handlers) {
            ScmObj proc = SCM_CDAR(hp);
            vm->handlers = SCM_CDR(hp);
            Scm_Apply(proc, SCM_NIL);
        }
    }

    if (vm->cstack) {
        vm->escapeReason = SCM_VM_ESCAPE_ERROR;
        vm->escapeData[0] = ep;
        vm->escapeData[1] = e;
        siglongjmp(vm->cstack->jbuf, 1);
    } else {
        exit(EX_SOFTWARE);
    }
}

static ScmObj default_exception_handler_body(ScmObj *argv, int argc, void *data)
{
    SCM_ASSERT(argc == 1);
    Scm_VMDefaultExceptionHandler(argv[0]);
    return SCM_UNDEFINED;       /*NOTREACHED*/
}

static SCM_DEFINE_STRING_CONST(default_exception_handler_name,
                               "default-exception-handler",
                               25, 25); /* strlen("default-exception-handler") */
static SCM_DEFINE_SUBR(default_exception_handler_rec, 1, 0,
                       SCM_OBJ(&default_exception_handler_name),
                       default_exception_handler_body, NULL, NULL);

/*
 * Entry point of throwing exception.
 *
 *  This function can be called from Scheme function raise,
 *  or C-function Scm_Error families and signal handler. 
 *  So there may be a raw C code in the continuation of this C call.
 *  Thus we can't use Scm_VMApply to call the user-defined exception
 *  handler.
 */
ScmObj Scm_VMThrowException(ScmObj exception)
{
    ScmVM *vm = theVM;
    ScmEscapePoint *ep = vm->escapePoint;

    SCM_VM_RUNTIME_FLAG_CLEAR(vm, SCM_ERROR_BEING_HANDLED);

    if (vm->exceptionHandler != DEFAULT_EXCEPTION_HANDLER) {
        vm->val0 = Scm_Apply(vm->exceptionHandler, SCM_LIST1(exception));
        if (SCM_SERIOUS_CONDITION_P(exception)) {
            /* the user-installed exception handler returned while it
               shouldn't.  In order to prevent infinite loop, we should
               pop the erroneous handler.  For now, we just reset
               the current exception handler. */
            vm->exceptionHandler = DEFAULT_EXCEPTION_HANDLER;
            Scm_Error("user-defined exception handler returned on non-continuable exception %S", exception);
        }
        return vm->val0;
    } else if (!SCM_SERIOUS_CONDITION_P(exception)) {
        /* The system's default handler does't care about
           continuable exception.  See if there's a user-defined
           exception handler in the chain.  */
        for (; ep; ep = ep->prev) {
            if (ep->xhandler != DEFAULT_EXCEPTION_HANDLER) {
                return Scm_Apply(ep->xhandler, SCM_LIST1(exception));
            }
        }
    }
    Scm_VMDefaultExceptionHandler(exception);
    /* this never returns */
    return SCM_UNDEFINED;       /* dummy */
}

/*
 * with-error-handler
 */
static ScmObj install_ehandler(ScmObj *args, int nargs, void *data)
{
    ScmEscapePoint *ep = (ScmEscapePoint*)data;
    ScmVM *vm = theVM;
    vm->exceptionHandler = DEFAULT_EXCEPTION_HANDLER;
    vm->escapePoint = ep;
    SCM_VM_RUNTIME_FLAG_CLEAR(vm, SCM_ERROR_BEING_REPORTED);
    return SCM_UNDEFINED;
}

static ScmObj discard_ehandler(ScmObj *args, int nargs, void *data)
{
    ScmEscapePoint *ep = (ScmEscapePoint *)data;
    ScmVM *vm = theVM;
    vm->escapePoint = ep->prev;
    vm->exceptionHandler = ep->xhandler;
    if (ep->errorReporting) {
        SCM_VM_RUNTIME_FLAG_SET(vm, SCM_ERROR_BEING_REPORTED);
    }
    return SCM_UNDEFINED;
}

ScmObj Scm_VMWithErrorHandler(ScmObj handler, ScmObj thunk)
{
    ScmVM *vm = theVM;
    ScmEscapePoint *ep = SCM_NEW(ScmEscapePoint);
    ScmObj before, after;

    /* NB: we can save pointer to the stack area (vm->cont) to ep->cont,
     * since such ep is always accessible via vm->escapePoint chain and
     * ep->cont is redirected whenever the continuation is captured while
     * ep is valid.
     */
    ep->prev = vm->escapePoint;
    ep->ehandler = handler;
    ep->handlers = vm->handlers;
    ep->cstack = vm->cstack;
    ep->xhandler = vm->exceptionHandler;
    ep->cont = vm->cont;
    ep->errorReporting =
        SCM_VM_RUNTIME_FLAG_IS_SET(vm, SCM_ERROR_BEING_REPORTED);
    
    vm->escapePoint = ep; /* This will be done in install_ehandler, but
                             make sure ep is visible from save_cont
                             to redirect ep->cont */
    before = Scm_MakeSubr(install_ehandler, ep, 0, 0, SCM_FALSE);
    after  = Scm_MakeSubr(discard_ehandler, ep, 0, 0, SCM_FALSE);
    return Scm_VMDynamicWind(before, thunk, after);
}

/* 
 * with-exception-handler
 *
 *   This primitive gives the programmer whole responsibility of
 *   dealing with exceptions.
 */

static ScmObj install_xhandler(ScmObj *args, int nargs, void *data)
{
    theVM->exceptionHandler = SCM_OBJ(data);
    return SCM_UNDEFINED;
}

ScmObj Scm_VMWithExceptionHandler(ScmObj handler, ScmObj thunk)
{
    ScmObj current = theVM->exceptionHandler;
    ScmObj before = Scm_MakeSubr(install_xhandler, handler, 0, 0, SCM_FALSE);
    ScmObj after  = Scm_MakeSubr(install_xhandler, current, 0, 0, SCM_FALSE);
    return Scm_VMDynamicWind(before, thunk, after);
}

/*==============================================================
 * Call With Current Continuation
 */

/* Figure out which before and after thunk should be called. */
static ScmObj throw_cont_calculate_handlers(ScmEscapePoint *ep, /*target*/
                                            ScmVM *vm)
{
    ScmObj target  = Scm_Reverse(ep->handlers);
    ScmObj current = vm->handlers;
    ScmObj h = SCM_NIL, t = SCM_NIL, p;

    SCM_FOR_EACH(p, current) {
        SCM_ASSERT(SCM_PAIRP(SCM_CAR(p)));
        if (!SCM_FALSEP(Scm_Memq(SCM_CAR(p), target))) break;
        /* push 'after' handlers to be called */
        SCM_APPEND1(h, t, SCM_CDAR(p));
    }
    SCM_FOR_EACH(p, target) {
        SCM_ASSERT(SCM_PAIRP(SCM_CAR(p)));
        if (!SCM_FALSEP(Scm_Memq(SCM_CAR(p), current))) continue;
        /* push 'before' handlers to be called */
        SCM_APPEND1(h, t, SCM_CAAR(p));
    }
    return h;
}

static ScmObj throw_cont_cc(ScmObj, void **);

static ScmObj throw_cont_body(ScmObj handlers,    /* after/before thunks
                                                     to be called */
                              ScmEscapePoint *ep, /* target continuation */
                              ScmObj args)        /* args to pass to the
                                                     target continuation */ 
{
    void *data[3];
    int nargs, i;
    ScmObj ap;
    ScmVM *vm = theVM;

    /*
     * first, check to see if we need to evaluate dynamic handlers.
     */
    if (SCM_PAIRP(handlers)) {
        data[0] = (void*)SCM_CDR(handlers);
        data[1] = (void*)ep;
        data[2] = (void*)args;
        Scm_VMPushCC(throw_cont_cc, data, 3);
        return Scm_VMApply0(SCM_CAR(handlers));
    }

    /*
     * now, install the target continuation
     */
    vm->pc = PC_TO_RETURN;
    vm->cont = ep->cont;

    nargs = Scm_Length(args);
    if (nargs == 1) {
        return SCM_CAR(args);
    } else if (nargs < 1) {
        return SCM_UNDEFINED;
    } else if (nargs >= SCM_VM_MAX_VALUES) {
        Scm_Error("too many values passed to the continuation");
    }

    for (i=0, ap=SCM_CDR(args); SCM_PAIRP(ap); i++, ap=SCM_CDR(ap)) {
        vm->vals[i] = SCM_CAR(ap);
    }
    vm->numVals = nargs;
    return SCM_CAR(args);
}

static ScmObj throw_cont_cc(ScmObj result, void **data)
{
    ScmObj handlers = SCM_OBJ(data[0]);
    ScmEscapePoint *ep = (ScmEscapePoint *)data[1];
    ScmObj args = SCM_OBJ(data[2]);
    return throw_cont_body(handlers, ep, args);
}

/* Body of the continuation SUBR */
static ScmObj throw_continuation(ScmObj *argframe, int nargs, void *data)
{
    ScmEscapePoint *ep = (ScmEscapePoint*)data;
    ScmVM *vm = theVM;
    ScmObj args = argframe[0];

    if (vm->cstack != ep->cstack) {
        ScmCStack *cstk;
        for (cstk = vm->cstack; cstk; cstk = cstk->prev) {
            if (ep->cstack == cstk) break;
        }
        if (cstk == NULL) {
            Scm_Error("a continuation is thrown outside of it's extent: %p",
                      ep);
        } else {
            /* Rewind C stack */
            vm->escapeReason = SCM_VM_ESCAPE_CONT;
            vm->escapeData[0] = ep;
            vm->escapeData[1] = args;
            siglongjmp(vm->cstack->jbuf, 1);
        }
    } else {
        ScmObj handlers_to_call = throw_cont_calculate_handlers(ep, vm);
        vm->handlers = ep->handlers;
        return throw_cont_body(handlers_to_call, ep, args);
    }
    return SCM_UNDEFINED; /*dummy*/
}

ScmObj Scm_VMCallCC(ScmObj proc)
{
    ScmObj contproc;
    ScmEscapePoint *ep;
    ScmVM *vm = theVM;

    if (!SCM_PROCEDUREP(proc)
        || (!SCM_PROCEDURE_OPTIONAL(proc) && SCM_PROCEDURE_REQUIRED(proc) != 1)
        || (SCM_PROCEDURE_OPTIONAL(proc) && SCM_PROCEDURE_REQUIRED(proc) > 1))
        Scm_Error("Procedure taking one argument is required, but got: %S",
                  proc);

    save_cont(vm, vm->cont);
    ep = SCM_NEW(ScmEscapePoint);
    ep->prev = NULL;
    ep->ehandler = SCM_FALSE;
    ep->cont = vm->cont;
    ep->handlers = vm->handlers;
    ep->cstack = vm->cstack;

    contproc = Scm_MakeSubr(throw_continuation, ep, 0, 1,
                            SCM_MAKE_STR("continuation"));
    return Scm_VMApply1(proc, contproc);
}

/*==============================================================
 * Values
 */

ScmObj Scm_Values(ScmObj args)
{
    ScmVM *vm = theVM;
    ScmObj cp;
    int nvals;
    
    if (!SCM_PAIRP(args)) {
        vm->numVals = 0;
        return SCM_UNDEFINED;
    }
    nvals = 1;
    SCM_FOR_EACH(cp, SCM_CDR(args)) {
        vm->vals[nvals-1] = SCM_CAR(cp);
        if (nvals++ >= SCM_VM_MAX_VALUES) {
            Scm_Error("too many values: %S", args);
        }
    }
    vm->numVals = nvals;
    return SCM_CAR(args);
}

ScmObj Scm_Values2(ScmObj val0, ScmObj val1)
{
    ScmVM *vm = theVM;
    vm->numVals = 2;
    vm->vals[0] = val1;
    return val0;
}

ScmObj Scm_Values3(ScmObj val0, ScmObj val1, ScmObj val2)
{
    ScmVM *vm = theVM;
    vm->numVals = 3;
    vm->vals[0] = val1;
    vm->vals[1] = val2;
    return val0;
}

ScmObj Scm_Values4(ScmObj val0, ScmObj val1, ScmObj val2, ScmObj val3)
{
    ScmVM *vm = theVM;
    vm->numVals = 4;
    vm->vals[0] = val1;
    vm->vals[1] = val2;
    vm->vals[2] = val3;
    return val0;
}

ScmObj Scm_Values5(ScmObj val0, ScmObj val1, ScmObj val2, ScmObj val3, ScmObj val4)
{
    ScmVM *vm = theVM;
    vm->numVals = 5;
    vm->vals[0] = val1;
    vm->vals[1] = val2;
    vm->vals[2] = val3;
    vm->vals[3] = val4;
    return val0;
}

/*==================================================================
 * Queued handler processing.
 */

/* Signal handlers and finalizers are queued in VM when they
 * are requested, and processed when VM is in consistent state.
 * process_queued_requests() are called near the beginning of
 * VM loop, when the VM checks if there's any queued request.
 *
 * When this procedure is called, VM is in middle of any two
 * VM instructions.  We need to make sure the handlers won't
 * disturb the VM state.
 *
 * Conceptually, this procedure inserts handler invocations before
 * the current continuation.
 */

static ScmObj process_queued_requests_cc(ScmObj result, void **data)
{
    /* restore the saved continuation of normal execution flow of VM */
    int i;
    ScmObj cp;
    ScmVM *vm = theVM;
    vm->numVals = (int)data[0];
    vm->val0 = data[1];
    if (vm->numVals > 1) {
        cp = SCM_OBJ(data[2]);
        for (i=0; i<vm->numVals-1; i++) {
            vm->vals[i] = SCM_CAR(cp);
            cp = SCM_CDR(cp);
        }
    }
    return vm->val0;
}

static void process_queued_requests(ScmVM *vm)
{
    void *data[3];

    /* preserve the current continuation */
    data[0] = (void*)vm->numVals;
    data[1] = vm->val0;
    if (vm->numVals > 1) {
        int i;
        ScmObj h = SCM_NIL, t = SCM_NIL;

        for (i=0; i<vm->numVals-1; i++) {
            SCM_APPEND1(h, t, vm->vals[i]);
        }
        data[2] = h;
    } else {
        data[2] = NULL;
    }
    Scm_VMPushCC(process_queued_requests_cc, data, 3);

    /* Process queued stuff.  Currently they call VM recursively,
       but we'd better to arrange them to be processed in the same
       VM level. */
    if (vm->queueNotEmpty & SCM_VM_SIGQ_MASK) {
        Scm_SigCheck(vm);
    }
    if (vm->queueNotEmpty & SCM_VM_FINQ_MASK) {
        Scm_VMFinalizerRun(vm);
    }
}

/*==============================================================
 * Debug features.
 */

/*
 * Stack trace.
 *
 *   The "lite" version returns a list of source information of
 *   continuation frames.
 *
 *   The full stack trace is consisted by a list of pair of
 *   source information and environment vector.  Environment vector
 *   is a copy of content of env frame, with the first element
 *   be the environment info.   Environment vector may be #f if
 *   the continuation frame doesn't have associated env.
 */

ScmObj Scm_VMGetStackLite(ScmVM *vm)
{
    ScmContFrame *c = vm->cont;
    ScmObj stack = SCM_NIL, tail = SCM_NIL;
    ScmObj info;

    info = Scm_VMGetSourceInfo(vm->base, vm->pc);
    if (!SCM_FALSEP(info)) SCM_APPEND1(stack, tail, info);
    while (c) {
        info = Scm_VMGetSourceInfo(c->base, c->pc);
        if (!SCM_FALSEP(info)) SCM_APPEND1(stack, tail, info);
        c = c->prev;
    }
    return stack;
}

#define DEFAULT_ENV_TABLE_SIZE  64

struct EnvTab {
    struct EnvTabEntry {
        ScmEnvFrame *env;
        ScmObj vec;
    } entries[DEFAULT_ENV_TABLE_SIZE];
    int numEntries;
};

static ScmObj env2vec(ScmEnvFrame *env, struct EnvTab *etab)
{
    int i;
    ScmObj vec;
    
    if (!env) return SCM_FALSE;
    for (i=0; i<etab->numEntries; i++) {
        if (etab->entries[i].env == env) {
            return etab->entries[i].vec;
        }
    }
    vec = Scm_MakeVector(env->size+2, SCM_FALSE);
    SCM_VECTOR_ELEMENT(vec, 0) = env2vec(env->up, etab);
    SCM_VECTOR_ELEMENT(vec, 1) = SCM_NIL; /*Scm_VMGetBindInfo(env->info);*/
    for (i=0; i<env->size; i++) {
        SCM_VECTOR_ELEMENT(vec, i+2) = ENV_DATA(env, (env->size-i-1));
    }
    if (etab->numEntries < DEFAULT_ENV_TABLE_SIZE) {
        etab->entries[etab->numEntries].env = env;
        etab->entries[etab->numEntries].vec = vec;
        etab->numEntries++;
    }
    return vec;
}

ScmObj Scm_VMGetStack(ScmVM *vm)
{
    ScmContFrame *c = vm->cont;
    ScmObj stack = SCM_NIL, tail = SCM_NIL;
    ScmObj info, evec;
    struct EnvTab envTab;

#if 0 /* for now */
    envTab.numEntries = 0;
    if (SCM_PAIRP(vm->pc)) {
        info = Scm_VMGetSourceInfo(vm->pc);
        SCM_APPEND1(stack, tail, Scm_Cons(info, env2vec(vm->env, &envTab)));
    }
    
    for (; c; c = c->prev) {
        if (!SCM_PAIRP(c->info)) continue;
        info = Scm_VMGetSourceInfo(c->info);
        evec = env2vec(c->env, &envTab);
        SCM_APPEND1(stack, tail, Scm_Cons(info, evec));
    }
#endif
    return stack;
}

/*
 * Printer of VM instruction.
 */

static struct insn_info {
    const char *name;
    int nparams;
} insn_table[] = {
#define DEFINSN(sym, nam, np) { nam, np },
#include "gauche/vminsn.h"
#undef DEFINSN
};

void Scm__VMInsnWrite(ScmObj obj, ScmPort *out, ScmWriteContext *ctx)
{
    struct insn_info *info;
    int param0, param1;
    char buf[50];
    int insn = SCM_VM_INSN_CODE(obj);
    SCM_ASSERT(insn >= 0 && insn < SCM_VM_NUM_INSNS);

    info = &insn_table[insn];
    switch (info->nparams) {
    case 0:
        snprintf(buf, 50, "#<%s>", info->name);
        break;
    case 1:
        param0 = SCM_VM_INSN_ARG(obj);
        snprintf(buf, 50, "#<%s %d>", info->name, param0);
        break;
    case 2:
        param0 = SCM_VM_INSN_ARG0(obj);
        param1 = SCM_VM_INSN_ARG1(obj);
        snprintf(buf, 50, "#<%s %d,%d>", info->name, param0, param1);
        break;
    default:
        Scm_Panic("something screwed up");
    }
    SCM_PUTZ(buf, -1, out);
}

/* Returns list of insn name and parameters.  Useful if you want to 
   inspect compiled code from Scheme. */
ScmObj Scm_VMInsnInspect(ScmObj obj)
{
    struct insn_info *info;
    ScmObj r;
    int param0, param1;
    int insn;

    if (!SCM_VM_INSNP(obj))
        Scm_Error("VM instruction expected, but got %S", obj);
    insn = SCM_VM_INSN_CODE(obj);
    SCM_ASSERT(insn >= 0 && insn < SCM_VM_NUM_INSNS);

    info = &insn_table[insn];
    switch (info->nparams) {
    case 0:
        r = SCM_LIST1(SCM_MAKE_STR(info->name));
        break;
    case 1:
        param0 = SCM_VM_INSN_ARG(obj);
        r = SCM_LIST2(SCM_MAKE_STR(info->name), SCM_MAKE_INT(param0));
        break;
    case 2:
        param0 = SCM_VM_INSN_ARG0(obj);
        param1 = SCM_VM_INSN_ARG1(obj);
        r = SCM_LIST3(SCM_MAKE_STR(info->name),
                      SCM_MAKE_INT(param0), SCM_MAKE_INT(param1));
        break;
    default:
        Scm_Panic("something screwed up");
        r = SCM_UNDEFINED;      /* dummy */
    }
    return r;
}

/*
 * Dump VM internal state.
 */
static ScmObj get_debug_info(ScmCompiledCode *base, ScmObj *pc)
{
    int off;
    ScmObj ip;
    if (base == NULL
        || (pc < base->code && pc >= base->code + base->codeSize)) {
        return SCM_FALSE;
    }
    off = pc - base->code - 1;  /* pc is already incremented, so -1. */
    SCM_FOR_EACH(ip, base->info) {
        ScmObj p = SCM_CAR(ip);
        if (!SCM_PAIRP(p) || !SCM_INTP(SCM_CAR(p))) continue;
        if (SCM_INT_VALUE(SCM_CAR(p)) < off) {
            return SCM_CDR(p);
            break;
        }
    }
    return SCM_FALSE;
}

ScmObj Scm_VMGetSourceInfo(ScmCompiledCode *base, ScmObj *pc)
{
    ScmObj info = get_debug_info(base, pc);
    if (SCM_PAIRP(info)) {
        ScmObj p = Scm_Assq(SCM_SYM_SOURCE_INFO, info);
        if (SCM_PAIRP(p)) return SCM_CDR(p);
    }
    return SCM_FALSE;
}

ScmObj Scm_VMGetBindInfo(ScmCompiledCode *base, ScmObj *pc)
{
    ScmObj info = get_debug_info(base, pc);
    if (SCM_PAIRP(info)) {
        ScmObj p = Scm_Assq(SCM_SYM_BIND_INFO, info);
        if (SCM_PAIRP(p)) return SCM_CDR(p);
    }
    return SCM_FALSE;
}

static void dump_env(ScmEnvFrame *env, ScmPort *out)
{
    int i;
    Scm_Printf(out, "   %p %55.1S\n", env, env->info);
    Scm_Printf(out, "       up=%p size=%d\n", env->up, env->size);
    Scm_Printf(out, "       [");
    for (i=0; i<env->size; i++) {
        Scm_Printf(out, " %S", ENV_DATA(env, i));
    }
    Scm_Printf(out, " ]\n");
}

void Scm_VMDump(ScmVM *vm)
{
    ScmPort *out = vm->curerr;
    ScmEnvFrame *env = vm->env;
    ScmContFrame *cont = vm->cont;
    ScmCStack *cstk = vm->cstack;
    ScmEscapePoint *ep = vm->escapePoint;

    Scm_Printf(out, "VM %p -----------------------------------------------------------\n", vm);
    Scm_Printf(out, "   pc: %08x ", vm->pc);
    Scm_Printf(out, "%#65.1S\n", *vm->pc);
    Scm_Printf(out, "   sp: %p  base: %p  [%p-%p]\n", vm->sp, vm->stackBase,
               vm->stack, vm->stackEnd);
    Scm_Printf(out, " argp: %p\n", vm->argp);
    Scm_Printf(out, " val0: %#65.1S\n", vm->val0);

    Scm_Printf(out, " envs:\n");
    while (env) {
        dump_env(env, out);
        env = env->up;
    }
    
    Scm_Printf(out, "conts:\n");
    while (cont) {
        Scm_Printf(out, "   %p\n", cont);
        Scm_Printf(out, "              env = %p\n", cont->env);
        Scm_Printf(out, "             argp = %p[%d]\n", cont->argp, cont->size);
        if (cont->argp) {
            Scm_Printf(out, "               pc = %p ", cont->pc);
            Scm_Printf(out, "%#50.1S\n", *cont->pc);
        } else {
            Scm_Printf(out, "               pc = {cproc %p}\n", cont->pc);
        }
        Scm_Printf(out, "             base = %p\n", cont->base);
        cont = cont->prev;
    }

    Scm_Printf(out, "C stacks:\n");
    while (cstk) {
        Scm_Printf(out, "  %p: prev=%p, cont=%p\n",
                   cstk, cstk->prev, cstk->cont);
        cstk = cstk->prev;
    }
    Scm_Printf(out, "Escape points:\n");
    while (ep) {
        Scm_Printf(out, "  %p: cont=%p, handler=%#20.1S\n",
                   ep, ep->cont, ep->ehandler);
        ep = ep->prev;
    }
    Scm_Printf(out, "dynenv: %S\n", vm->handlers);
}

/*===============================================================
 * NVM related stuff
 */

/* Debug information:
 *
 *  debug info is kept as an assoc-list with insn offset
 *  as a key.
 */

SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_CompiledCodeClass, NULL);

static ScmCompiledCode *make_compiled_code(void)
{
    ScmCompiledCode *cc = SCM_NEW(ScmCompiledCode);
    SCM_SET_CLASS(cc, SCM_CLASS_COMPILED_CODE);
    cc->code = NULL;
    cc->constants = NULL;
    cc->maxstack = -1;
    cc->info = SCM_NIL;
    return cc;
}

/* Quick and dirty version of packcode.
   This is a temporary stub, until the compiler is rewritten
   to generate packed code directly. */

typedef struct pk_data_rec {
    ScmObj code;                /* emitted code pushded here */
    int count;                  /* # of insns pushed */
    ScmObj constants;           /* constant pool */
    ScmObj mergers;             /* alist of (merging point . count) */
    ScmObj info;                /* debug info alist. */
} pk_data;

static void pk_init(pk_data *data)
{
    data->code = SCM_NIL;
    data->count = 0;
    data->constants = SCM_NIL;
    data->mergers = SCM_NIL;
    data->info = SCM_NIL;
}

static inline ScmObj pk_emit(pk_data *data, ScmObj insn)
{
    data->code = Scm_Cons(insn, data->code);
    data->count++;
    return data->code;
}

static inline void pk_constant(pk_data *data, ScmObj constobj)
{
    if (SCM_PTRP(constobj)
        && SCM_FALSEP(Scm_Memq(constobj, data->constants))) {
        data->constants = Scm_Cons(constobj, data->constants);
    }
}

static inline void pk_merger(pk_data *data, ScmObj code)
{
    data->mergers = Scm_Acons(code, SCM_MAKE_INT(data->count), data->mergers);
}

/* Uses data->count, so it must be called before pk_emit. */
static inline void pk_info(pk_data *data, ScmObj info)
{
    data->info = Scm_Acons(SCM_MAKE_INT(data->count), info,
                           data->info);
}

/* Transfer argument info from bind-info attribute to compiled code's info */
static void pk_add_arg_info(ScmCompiledCode *cc, ScmObj attrs)
{
    ScmObj binfo = Scm_Assq(SCM_SYM_BIND_INFO, attrs);
    if (SCM_PAIRP(binfo)) {
        cc->info = Scm_Acons(SCM_SYM_ARG_INFO, SCM_CDR(binfo), cc->info);
    }
}

static inline void pk_rec(pk_data *data, ScmObj code)
{
    ScmObj cp, insn = SCM_FALSE;
    ScmObj save, prev, info;
    
    SCM_FOR_EACH(cp, code) {
        insn = SCM_CAR(cp);
        info = Scm_PairAttr(SCM_PAIR(cp));
        if (!SCM_NULLP(info)) pk_info(data, info);

        if (!SCM_VM_INSNP(insn)) {
            /* constant */
            pk_constant(data, insn);
            pk_emit(data, insn);
            continue;
        }
        switch (SCM_VM_INSN_CODE(insn)) {
        case SCM_VM_PUSH:;
        case SCM_VM_POP:;
        case SCM_VM_DUP:;
            pk_emit(data, insn);
            break;
        case SCM_VM_PRE_CALL:;
            pk_emit(data, insn);
            save = pk_emit(data, SCM_MAKE_INT(0));
            cp = SCM_CDR(cp);
            pk_rec(data, SCM_CAR(cp)); /* prep part */
            SCM_SET_CAR(save, SCM_MAKE_INT(data->count));
            break;
        case SCM_VM_PRE_TAIL:;
        case SCM_VM_CHECK_STACK:;
        case SCM_VM_TAIL_CALL:;
        case SCM_VM_CALL:;
            pk_emit(data, insn);
            break;
        case SCM_VM_JUMP:;
            break;
        case SCM_VM_GREF:;
            pk_emit(data, insn);
            cp = SCM_CDR(cp);
            pk_constant(data, SCM_CAR(cp));
            pk_emit(data, SCM_CAR(cp));
            break;
        case SCM_VM_LREF0:;
        case SCM_VM_LREF1:;
        case SCM_VM_LREF2:;
        case SCM_VM_LREF3:;
        case SCM_VM_LREF4:;
        case SCM_VM_LREF10:;
        case SCM_VM_LREF11:;
        case SCM_VM_LREF12:;
        case SCM_VM_LREF13:;
        case SCM_VM_LREF14:;
        case SCM_VM_LREF:;
        case SCM_VM_LREF0_PUSH:;
        case SCM_VM_LREF1_PUSH:;
        case SCM_VM_LREF2_PUSH:;
        case SCM_VM_LREF3_PUSH:;
        case SCM_VM_LREF4_PUSH:;
        case SCM_VM_LREF10_PUSH:;
        case SCM_VM_LREF11_PUSH:;
        case SCM_VM_LREF12_PUSH:;
        case SCM_VM_LREF13_PUSH:;
        case SCM_VM_LREF14_PUSH:;
        case SCM_VM_LREF_PUSH:;
            pk_emit(data, insn);
            break;
        case SCM_VM_LET:;
            cp = SCM_CDR(cp);
            if (SCM_NULLP(SCM_CDR(cp))) {
                pk_emit(data, SCM_VM_INSN1(SCM_VM_TAIL_LET,
                                           SCM_VM_INSN_ARG(insn)));
                pk_rec(data, SCM_CAR(cp)); /* body */
            } else {
                pk_emit(data, insn);
                save = pk_emit(data, SCM_MAKE_INT(0));
                pk_rec(data, SCM_CAR(cp)); /* body */
                SCM_SET_CAR(save, SCM_MAKE_INT(data->count));
            }
            break;
        case SCM_VM_GSET:;
            pk_emit(data, insn);
            cp = SCM_CDR(cp);
            pk_constant(data, SCM_CAR(cp));
            pk_emit(data, SCM_CAR(cp));
            break;
        case SCM_VM_LSET0:;
        case SCM_VM_LSET1:;
        case SCM_VM_LSET2:;
        case SCM_VM_LSET3:;
        case SCM_VM_LSET4:;
        case SCM_VM_LSET:;
            pk_emit(data, insn);
            break;
        case SCM_VM_NOP:;
            break;
        case SCM_VM_MNOP:;
            /* this may be a merging point. */
            save = Scm_Assq(cp, data->mergers);
            if (SCM_FALSEP(save)) {
                pk_merger(data, cp);
            } else {
                insn = SCM_VM_INSN(SCM_VM_JUMP);
                pk_emit(data, insn);
                pk_emit(data, SCM_CDR(save));
                return;
            }
            break;
        case SCM_VM_DEFINE:;
        case SCM_VM_DEFINE_CONST:;
            pk_emit(data, insn);
            cp = SCM_CDR(cp);
            pk_constant(data, SCM_CAR(cp));
            pk_emit(data, SCM_CAR(cp));
            break;
        case SCM_VM_IF:;
            pk_emit(data, insn);
            cp = SCM_CDR(cp);
            save = pk_emit(data, SCM_MAKE_INT(0));
            pk_rec(data, SCM_CAR(cp)); /* emit then clause first */
            SCM_SET_CAR(save, SCM_MAKE_INT(data->count));
            /* continues to else clause.  jump is taken care of by MNOP. */
            break;
        case SCM_VM_LAMBDA:;
            pk_emit(data, insn);
            save = Scm_PackCode(SCM_CADR(cp));
            pk_add_arg_info(SCM_COMPILED_CODE(save),
                            Scm_PairAttr(SCM_PAIR(cp)));
            cp = SCM_CDR(cp);
            pk_constant(data, save);
            pk_emit(data, save);
            break;
        case SCM_VM_RECEIVE:;
            pk_emit(data, insn);
            cp = SCM_CDR(cp);
            save = pk_emit(data, SCM_MAKE_INT(0));
            pk_rec(data, SCM_CAR(cp)); /* body */
            SCM_SET_CAR(save, SCM_MAKE_INT(data->count));
            break;
        case SCM_VM_QUOTE_INSN:;
            pk_emit(data, insn);
            cp = SCM_CDR(cp);
            pk_emit(data, SCM_CAR(cp));
            break;
        case SCM_VM_PUSHI:;
        case SCM_VM_PUSHNIL:;
        case SCM_VM_CONS:;
        case SCM_VM_CONS_PUSH:;
        case SCM_VM_CAR:;
        case SCM_VM_CAR_PUSH:;
        case SCM_VM_CDR:;
        case SCM_VM_CDR_PUSH:;
        case SCM_VM_LIST:;
        case SCM_VM_LIST_STAR:;
        case SCM_VM_NOT:;
        case SCM_VM_NULLP:;
        case SCM_VM_EQ:;
        case SCM_VM_EQV:;
        case SCM_VM_MEMQ:;
        case SCM_VM_MEMV:;
        case SCM_VM_ASSQ:;
        case SCM_VM_ASSV:;
        case SCM_VM_PAIRP:;
        case SCM_VM_CHARP:;
        case SCM_VM_EOFP:;
        case SCM_VM_STRINGP:;
        case SCM_VM_SYMBOLP:;
        case SCM_VM_APPEND:;
        case SCM_VM_REVERSE:;
        case SCM_VM_APPLY:;
        case SCM_VM_PROMISE:;
        case SCM_VM_SETTER:;
        case SCM_VM_VALUES:;
        case SCM_VM_VEC:;
        case SCM_VM_APP_VEC:;
        case SCM_VM_VEC_LEN:;
        case SCM_VM_VEC_REF:;
        case SCM_VM_VEC_SET:;
        case SCM_VM_NUMEQ2:;
        case SCM_VM_NUMLT2:;
        case SCM_VM_NUMLE2:;
        case SCM_VM_NUMGT2:;
        case SCM_VM_NUMGE2:;
        case SCM_VM_NUMADD2:;
        case SCM_VM_NUMSUB2:;
        case SCM_VM_NUMADDI:;
        case SCM_VM_NUMSUBI:;
        case SCM_VM_READ_CHAR:;
        case SCM_VM_WRITE_CHAR:;
        case SCM_VM_SLOT_REF:;
        case SCM_VM_SLOT_SET:;
            pk_emit(data, insn);
            break;
        default:
            Scm_Error("unknown insn: %S", insn);
        }
    }
    /* emit RET if necessary. */
#if 0
    if (SCM_VM_INSNP(insn)) {
        switch (SCM_VM_INSN_CODE(insn)) {
        case SCM_VM_CALL:;
        case SCM_VM_TAIL_CALL:;
        case SCM_VM_LET:;
        case SCM_VM_JUMP:;
            return;
        }
    }
#endif
    pk_emit(data, SCM_VM_INSN(SCM_VM_RET));
}

ScmObj Scm_PackCode(ScmObj compiled)
{
    pk_data data;
    ScmCompiledCode *cc;
    ScmObj cp;
    int i;

    pk_init(&data);
    pk_rec(&data, compiled);

    cc = make_compiled_code();
    cc->code = SCM_NEW_ATOMIC2(ScmObj*, data.count * sizeof(ScmWord));
    i = 0;
    SCM_FOR_EACH(cp, Scm_ReverseX(data.code)) {
        cc->code[i++] = SCM_CAR(cp);
    }
    cc->codeSize = i;

    cc->constants = SCM_NEW_ARRAY(ScmObj, Scm_Length(data.constants));
    i = 0;
    SCM_FOR_EACH(cp, data.constants) {
        cc->constants[i++] = SCM_CAR(cp);
    }
    cc->constantSize = i;

    cc->info = data.info;

    return SCM_OBJ(cc);
}

void Scm_CompiledCodeDump(ScmObj obj)
{
    int i;
    ScmObj *p;
    ScmCompiledCode *cc;
    ScmObj closures = SCM_NIL, arginfo;
    int clonum = 0;

    if (!SCM_COMPILED_CODE_P(obj)) {
        Scm_Error("compiled-code required, but got %S", obj);
    }
    cc = SCM_COMPILED_CODE(obj);
    Scm_Printf(SCM_CUROUT, "main_code (size=%d, const=%d):\n",
               cc->codeSize, cc->constantSize);
    do {
      loop:
        p = cc->code;
        arginfo = Scm_Assq(SCM_SYM_ARG_INFO, cc->info);
        if (SCM_PAIRP(arginfo)) {
            Scm_Printf(SCM_CUROUT, "args: %S\n", SCM_CDR(arginfo));
        }
        for (i=0; i < cc->codeSize; i++) {
            ScmObj insn = p[i], info, s;
            ScmPort *out = SCM_PORT(Scm_MakeOutputStringPort(TRUE));
            int code;

            info = Scm_Assq(SCM_MAKE_INT(i), cc->info);

            if (!SCM_VM_INSNP(insn)) {
                Scm_Printf(out, "  %4d CONST %S", i, insn);
                goto show_info;
            }
            
            code = SCM_VM_INSN_CODE(insn);
            Scm_Printf(out, "  %4d %A ", i, Scm_VMInsnInspect(insn));
            switch (code) {
            case SCM_VM_PRE_CALL:;
            case SCM_VM_GREF:;
            case SCM_VM_LET:;
            case SCM_VM_GSET:;
            case SCM_VM_JUMP:;
            case SCM_VM_DEFINE:;
            case SCM_VM_DEFINE_CONST:;
            case SCM_VM_IF:;
            case SCM_VM_RECEIVE:;
            case SCM_VM_QUOTE_INSN:;
                Scm_Printf(out, "%S", p[i+1]);
                i++;
                break;
            case SCM_VM_LAMBDA:;
                Scm_Printf(out, "#<lambda %d>", clonum);
                closures = Scm_Acons(p[i+1], SCM_MAKE_INT(clonum),
                                     closures);
                clonum++;
                i++;
                break;
            default:
                /*nothing*/;
            }

          show_info:
            s = Scm_GetOutputStringUnsafe(out);
            if (!SCM_PAIRP(info)) {
                Scm_Puts(SCM_STRING(s), SCM_CUROUT);
                Scm_Putc('\n', SCM_CUROUT);
            } else {
                int len = SCM_STRING_SIZE(s);
                ScmObj srcinfo = Scm_Assq(SCM_SYM_SOURCE_INFO, info);
                ScmObj bindinfo = Scm_Assq(SCM_SYM_BIND_INFO, info);
                Scm_Puts(SCM_STRING(s), SCM_CUROUT);
                Scm_Flush(SCM_CUROUT);
                for (; len<32; len++) {
                    Scm_Putc(' ', SCM_CUROUT);
                }
                if (SCM_FALSEP(srcinfo)) {
                    Scm_Printf(SCM_CUROUT, "; lambda %#40.1S\n",
                               SCM_CDR(bindinfo));
                } else {
                    Scm_Printf(SCM_CUROUT, "; %#40.1S\n", SCM_CDR(srcinfo));
                }
            }
        }
        if (!SCM_NULLP(closures)) {
            cc = SCM_COMPILED_CODE(SCM_CAAR(closures));
            Scm_Printf(SCM_CUROUT, "internal_closure_%S (size=%d const=%d):\n",
                       SCM_CDAR(closures),
                       cc->codeSize, cc->constantSize);
            closures = SCM_CDR(closures);
            goto loop;
        }
    } while (0);
}

ScmObj Scm_CompiledCodeArgInfo(ScmCompiledCode *cc)
{
    ScmObj ainfo = Scm_Assq(SCM_SYM_ARG_INFO, cc->info);
    if (SCM_PAIRP(ainfo)) return SCM_CDR(ainfo);
    else return SCM_FALSE;
}
