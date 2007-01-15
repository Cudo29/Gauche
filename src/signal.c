/*
 * signal.c - signal handling
 *
 *   Copyright (c) 2000-2007 Shiro Kawai, All rights reserved.
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
 *  $Id: signal.c,v 1.48 2007-01-15 02:01:05 shirok Exp $
 */

#include <stdlib.h>
#include <signal.h>
#define LIBGAUCHE_BODY
#include "gauche.h"
#include "gauche/vm.h"
#include "gauche/class.h"

/* Signals
 *
 *  C-application that embeds Gauche can specify a set of signals
 *  that Gauche can handle.
 *
 *  The Scheme program can specify which signal it wants to handle
 *  by setting a Scheme signal handler.  Gauche registers the internal
 *  signal handler for the specified signal.  What the internal signal
 *  handler does is just queue the signal in the VM's signal queue.
 *  VM calls Scm_SigCheck() at the "safe" point, which flushes
 *  the signal queue and make a list of handlers to be called.
 *
 *  Scheme signal handler vector is shared by all threads.  Each
 *  thread can set a signal mask.  By default, only the primordial
 *  thread handles signals.
 *
 *  For most signals, Gauche installs the default signal handler that
 *  raises 'unhandled signal exception'.   For other signals, Gauche lets
 *  the system to handle the signal unless the Scheme program installs
 *  the handler.   Such signals are the ones that can't be caught, or
 *  are ignored by default.  SIGPWR and SIGXCPU are also left to the system
 *  since GC uses it in the Linux/pthread environment.
 */

#ifndef __MINGW32__
  #ifdef GAUCHE_USE_PTHREADS
  #define SIGPROCMASK pthread_sigmask
  #else
  #define SIGPROCMASK sigprocmask
  #endif
#else  /* __MINGW32__ */
  /* This isn't correct (we need some mechanism to block the signal),
     but just for the time being ... */
  #define SIGPROCMASK(mode, set, omask)  (0)
#endif /* __MINGW32__ */

/* Master signal handler vector. */
static struct sigHandlersRec {
    ScmObj handlers[NSIG];      /* Scheme signal handlers.  This is #f on
                                   signals to which Gauche does not install
                                   C-level signal handler (sig_handle). */
    ScmSysSigset *masks[NSIG];  /* Signal masks during executing Scheme
                                   handlers.  Can be NULL, which means
                                   the handling signal(s) are blocked. */
    sigset_t masterSigset;      /* The signals Gauche is _allowed_ to handle.
                                   set by Scm_SetMasterSigset.
                                   For some signals in this set Gauche sets
                                   the default signal handlers; for other
                                   signals in this set Gauche leaves them
                                   for the system to handle.  These can be
                                   overridden by Scm_SetSignalHandler. */
    ScmInternalMutex mutex;
} sigHandlers = {{NULL}};

/* Maximum # of the same signals before it is processed by the VM loop.
   If any one of signals exceeds this count, Gauche exits with Scm_Abort.
   It is useful to terminate unresponsive program that are executing
   long-running C-routine and do not returns to VM.
   The actual limit can be changed at runtime by Scm_SetSignalPendingLimit().
   If signalPendingLimit is 0, the number of pending signals is unlimited. */
#define SIGNAL_PENDING_LIMIT_DEFALT 3
#define SIGNAL_PENDING_LIMIT_MAX 255

static unsigned int signalPendingLimit = SIGNAL_PENDING_LIMIT_DEFALT;


/* Table of signals and its initial behavior. */
#define SIGDEF_NOHANDLE 0       /* Gauche doesn't install a signal handler,
                                   leaving it to the application. */
#define SIGDEF_DFL      1       /* Gauche resets the singal handler to
                                   SIG_DFL. */
#define SIGDEF_ERROR    2       /* Gauche installs a default signal handler
                                   that raises an error. */
#define SIGDEF_EXIT     3       /* Gauche installs a handler that calls
                                   Scm_Exit(). */

#define SIGDEF(x, flag)  { #x, x, flag }

static struct sigdesc {
    const char *name;
    int num;
    int defaultHandle;
} sigDesc[] = {
#ifdef SIGHUP
    SIGDEF(SIGHUP,  SIGDEF_EXIT),     /* Hangup (POSIX) */
#endif
    SIGDEF(SIGINT,  SIGDEF_ERROR),    /* Interrupt (ANSI) */
#ifdef SIGQUIT
    SIGDEF(SIGQUIT, SIGDEF_EXIT),     /* Quit (POSIX) */
#endif
    SIGDEF(SIGILL,  SIGDEF_NOHANDLE), /* Illegal instruction (ANSI) */
#ifdef SIGTRAP
    SIGDEF(SIGTRAP, SIGDEF_ERROR),    /* Trace trap */
#endif
    SIGDEF(SIGABRT, SIGDEF_NOHANDLE), /* Abort (ANSI) */
#ifdef SIGIOT
    SIGDEF(SIGIOT,  SIGDEF_ERROR),    /* IOT trap (4.2 BSD) */
#endif
#ifdef SIGBUS
    SIGDEF(SIGBUS,  SIGDEF_NOHANDLE), /* BUS error (4.2 BSD) */
#endif
    SIGDEF(SIGFPE,  SIGDEF_ERROR),    /* Floating-point exception (ANSI) */
#ifdef SIGKILL
    SIGDEF(SIGKILL, SIGDEF_NOHANDLE), /* Kill, unblockable (POSIX) */
#endif
#ifdef SIGUSR1
    SIGDEF(SIGUSR1, SIGDEF_ERROR),    /* User-defined signal 1 (POSIX) */
#endif
    SIGDEF(SIGSEGV, SIGDEF_NOHANDLE), /* Segmentation violation (ANSI) */
#ifdef SIGUSR2
    SIGDEF(SIGUSR2, SIGDEF_ERROR),    /* User-defined signal 2 (POSIX) */
#endif
#ifdef SIGPIPE
    SIGDEF(SIGPIPE, SIGDEF_ERROR),    /* Broken pipe (POSIX) */
#endif
#ifdef SIGALRM
    SIGDEF(SIGALRM, SIGDEF_ERROR),    /* Alarm clock (POSIX) */
#endif
    SIGDEF(SIGTERM, SIGDEF_EXIT),     /* Termination (ANSI) */
#ifdef SIGSTKFLT
    SIGDEF(SIGSTKFLT, SIGDEF_ERROR),  /* Stack fault */
#endif
#ifdef SIGCHLD
    SIGDEF(SIGCHLD, SIGDEF_DFL),      /* Child status has changed (POSIX) */
#endif
#ifdef SIGCONT
    SIGDEF(SIGCONT, SIGDEF_NOHANDLE), /* Continue (POSIX) */
#endif
#ifdef SIGSTOP
    SIGDEF(SIGSTOP, SIGDEF_NOHANDLE), /* Stop, unblockable (POSIX) */
#endif
#ifdef SIGTSTP
    SIGDEF(SIGTSTP, SIGDEF_NOHANDLE), /* Keyboard stop (POSIX) */
#endif
#ifdef SIGTTIN
    SIGDEF(SIGTTIN, SIGDEF_NOHANDLE), /* Background read from tty (POSIX) */
#endif
#ifdef SIGTTOU
    SIGDEF(SIGTTOU, SIGDEF_NOHANDLE), /* Background write to tty (POSIX) */
#endif
#ifdef SIGURG
    SIGDEF(SIGURG,  SIGDEF_NOHANDLE), /* Urgent condition on socket (4.2 BSD) */
#endif
#ifdef SIGXCPU
    SIGDEF(SIGXCPU, SIGDEF_NOHANDLE), /* CPU limit exceeded (4.2 BSD) */
#endif
#ifdef SIGXFSZ
    SIGDEF(SIGXFSZ, SIGDEF_ERROR),    /* File size limit exceeded (4.2 BSD) */
#endif
#ifdef SIGVTALRM
    SIGDEF(SIGVTALRM, SIGDEF_ERROR),  /* Virtual alarm clock (4.2 BSD) */
#endif
#ifdef SIGPROF
    SIGDEF(SIGPROF, SIGDEF_ERROR),    /* Profiling alarm clock (4.2 BSD) */
#endif
#ifdef SIGWINCH
    SIGDEF(SIGWINCH, SIGDEF_NOHANDLE),/* Window size change (4.3 BSD, Sun) */
#endif
#ifdef SIGPOLL
    SIGDEF(SIGPOLL, SIGDEF_ERROR),    /* Pollable event occurred (System V) */
#endif
#ifdef SIGIO
    SIGDEF(SIGIO,   SIGDEF_ERROR),    /* I/O now possible (4.2 BSD) */
#endif
#ifdef SIGPWR
    SIGDEF(SIGPWR,  SIGDEF_NOHANDLE), /* Power failure restart (System V) */
#endif
    { NULL, -1 }
};

/*===============================================================
 * Signal set operations
 */

/*
 * utilities for sigset
 */
static void display_sigset(sigset_t *set, ScmPort *port)
{
    struct sigdesc *desc = sigDesc;
    int cnt = 0;
    for (; desc->name; desc++) {
        if (sigismember(set, desc->num)) {
            if (cnt++) Scm_Putc('|', port);
            Scm_Putz(desc->name+3, -1, port);
        }
    }
}

static int validsigp(int signum)
{
    if (signum > 0) {
        struct sigdesc *desc = sigDesc;
        for (; desc->name; desc++) {
            if (desc->num == signum) return TRUE;
        }
    }
    return FALSE;
}

static void sigset_op(sigset_t *s1, sigset_t *s2, int delp)
{
    struct sigdesc *desc = sigDesc;
    for (; desc->name; desc++) {
        if (sigismember(s2, desc->num)) {
            if (!delp) sigaddset(s1, desc->num);
            else       sigdelset(s1, desc->num);
        }
    }
}

ScmObj Scm_SignalName(int signum)
{
    struct sigdesc *desc = sigDesc;
    for (; desc->name; desc++) {
        if (desc->num == signum) {
            return SCM_MAKE_STR_IMMUTABLE(desc->name);
        }
    }
    return SCM_FALSE;
}

/*
 * sigset class
 */

static void sigset_print(ScmObj obj, ScmPort *out, ScmWriteContext *ctx);
static ScmObj sigset_allocate(ScmClass *klass, ScmObj initargs);

SCM_DEFINE_BUILTIN_CLASS(Scm_SysSigsetClass, sigset_print,
                         NULL, NULL, sigset_allocate, SCM_CLASS_DEFAULT_CPL);

void sigset_print(ScmObj obj, ScmPort *out, ScmWriteContext *ctx)
{
    Scm_Printf(out, "#<sys-sigset [");
    display_sigset(&SCM_SYS_SIGSET(obj)->set, out);
    Scm_Printf(out, "]>");
}

ScmObj sigset_allocate(ScmClass *klass, ScmObj initargs)
{
    ScmSysSigset *s = SCM_ALLOCATE(ScmSysSigset, klass);
    SCM_SET_CLASS(s, klass);
    sigemptyset(&s->set);
    return SCM_OBJ(s);
}

ScmSysSigset *make_sigset(void)
{
    return SCM_SYS_SIGSET(sigset_allocate(SCM_CLASS_SYS_SIGSET, SCM_NIL));
}

/* multifunction on sigset
    if delp == FALSE, signals are added to set.
    else, signals are removed from set.
    signals is a list of either integer or #t (all signals), or other sigset.
*/
ScmObj Scm_SysSigsetOp(ScmSysSigset *set, ScmObj signals, int delp)
{
    ScmObj cp;
    
    if (!SCM_PAIRP(signals)) {
        Scm_Error("list of signals required, but got %S", signals);
    }
    SCM_FOR_EACH(cp, signals) {
        ScmObj s = SCM_CAR(cp);
        if (SCM_TRUEP(s)) {
            if (!delp) sigfillset(&set->set);
            else       sigemptyset(&set->set);
            break;
        }
        if (SCM_SYS_SIGSET_P(s)) {
            sigset_op(&set->set, &SCM_SYS_SIGSET(s)->set, delp);
            continue;
        }
        if (!SCM_INTP(s) || !validsigp(SCM_INT_VALUE(s))) {
            Scm_Error("bad signal number %S", s);
        }
        if (!delp) sigaddset(&set->set, SCM_INT_VALUE(s));
        else       sigdelset(&set->set, SCM_INT_VALUE(s));
    }
    return SCM_OBJ(set);
}

/* fill or empty sigset. */
ScmObj Scm_SysSigsetFill(ScmSysSigset *set, int emptyp)
{
    if (emptyp) sigemptyset(&(set->set));
    else        sigfillset(&(set->set));
    return SCM_OBJ(set);
}

/*=============================================================
 * C-level signal handling
 */


/*-------------------------------------------------------------------
 * C-level signal handler - just records the signal delivery.
 */

static void sig_handle(int signum)
{
    ScmVM *vm = Scm_VM();
    /* It is possible that vm == NULL at this point, if the thread is
       terminating and in the cleanup phase. */
    if (vm == NULL) return;

    vm->sigq.sigcounts[signum]++;
    if (signalPendingLimit > 0
        && vm->sigq.sigcounts[signum] >= signalPendingLimit) {
        Scm_Abort("Received too many signals before processing it.  Exitting for the emergency...\n");
    }
    vm->queueNotEmpty |= SCM_VM_SIGQ_MASK;
}

/*-------------------------------------------------------------------
 * Signal queue operations
 */

/*
 * Clear the signal queue
 */
void Scm_SignalQueueClear(ScmSignalQueue* q)
{
    int i;
    for (i=0; i<NSIG; i++) q->sigcounts[i] = 0;
}

/*
 * Initializes signal queue
 */
void Scm_SignalQueueInit(ScmSignalQueue* q)
{
    Scm_SignalQueueClear(q);
    q->pending = SCM_NIL;
}

/*
 * Get/Set signal pending limit
 */
int Scm_GetSignalPendingLimit(void)
{
    return signalPendingLimit;
}

void Scm_SetSignalPendingLimit(int num)
{
    if (num < 0 || num >= SIGNAL_PENDING_LIMIT_MAX) {
        Scm_Error("signal-pending-limit argument out of range: %d", num);
    }
    signalPendingLimit = num;
}

/*
 * Called from VM's safe point to flush the queued signals.
 * VM already checks there's a pending signal in the queue.
 */
void Scm_SigCheck(ScmVM *vm)
{
    ScmObj tail, cell, sp;
    ScmSignalQueue *q = &vm->sigq;
    sigset_t omask;
    int i;
    unsigned char sigcounts[NSIG]; /* copy of signal counter */

    /* Copy VM's signal counter to local storage, for we can't call
       storage allocation during blocking signals. */
    SIGPROCMASK(SIG_BLOCK, &sigHandlers.masterSigset, &omask);
    memcpy(sigcounts, vm->sigq.sigcounts, NSIG * sizeof(unsigned char));
    Scm_SignalQueueClear(&vm->sigq);
    vm->queueNotEmpty &= ~SCM_VM_SIGQ_MASK;
    SIGPROCMASK(SIG_SETMASK, &omask, NULL);

    /* Now, prepare queued signal handlers
       If an error is thrown in this loop, the queued signals will be
       lost---it doesn't look like so, but I may overlook something. */
    tail = q->pending;
    if (!SCM_NULLP(tail)) tail = Scm_LastPair(tail);
    for (i=0; i<NSIG; i++) {
        if (sigcounts[i] == 0) continue;
        if (SCM_PROCEDUREP(sigHandlers.handlers[i])) {
            cell = Scm_Cons(SCM_LIST3(sigHandlers.handlers[i],
                                      SCM_MAKE_INT(i),
                                      SCM_OBJ_SAFE(sigHandlers.masks[i])),
                            SCM_NIL);
            if (SCM_NULLP(tail)) {
                q->pending = tail = cell;
            } else {
                SCM_SET_CDR(tail, cell);
                tail = SCM_CDR(tail);
            }
        }
    }
    
    /* Call the queued signal handlers.  If an error is thrown in one
       of those handlers, the rest of handlers remain in the queue. */
    /* TODO: if VM is active, it'd be better to make the active VM to handle
       those handler procs, instead of calling Scm_Eval. */
    SCM_FOR_EACH(sp, q->pending) {
        ScmObj e = SCM_CAR(sp), handler, num, mask;
        q->pending = SCM_CDR(sp);
        handler = SCM_CAR(e);
        num = SCM_CADR(e);
        mask = SCM_CAR(SCM_CDDR(e));
        if (SCM_SYS_SIGSET_P(mask)) {
            sigset_t omask;
            SCM_UNWIND_PROTECT {
                SIGPROCMASK(SIG_BLOCK, &SCM_SYS_SIGSET(mask)->set, &omask);
                Scm_ApplyRec(handler, SCM_LIST1(num));
            }
            SCM_WHEN_ERROR {
                SIGPROCMASK(SIG_SETMASK, &omask, NULL);
                SCM_NEXT_HANDLER;
            }
            SCM_END_PROTECT;
            SIGPROCMASK(SIG_SETMASK, &omask, NULL);
        } else {
            Scm_ApplyRec(handler, SCM_LIST1(num));
        }
    }
}

/*=============================================================
 * Scheme-level signal handling
 */

/*-------------------------------------------------------------
 * Default Scheme-level handlers
 */
/* For most signals, default handler raises an error. */
static ScmObj default_sighandler(ScmObj *args, int nargs, void *data)
{
    int signum;
    struct sigdesc *desc;
    const char *name = NULL;
    
    SCM_ASSERT(nargs == 1 && SCM_INTP(args[0]));
    signum = SCM_INT_VALUE(args[0]);

    for (desc = sigDesc; desc->name; desc++) {
        if (desc->num == signum) {
            name = desc->name;
            break;
        }
    }
    if (name) {
        Scm_RaiseCondition(SCM_OBJ(SCM_CLASS_UNHANDLED_SIGNAL_ERROR),
                           "signal", SCM_MAKE_INT(signum),
                           SCM_RAISE_CONDITION_MESSAGE,
                           "unhandled signal %d (%s)", signum, name);
    } else {
        Scm_RaiseCondition(SCM_OBJ(SCM_CLASS_UNHANDLED_SIGNAL_ERROR),
                           "signal", SCM_MAKE_INT(signum),
                           SCM_RAISE_CONDITION_MESSAGE,
                           "unhandled signal %d (unknown signal)", signum);
    }
    return SCM_UNDEFINED;       /* dummy */
}

static SCM_DEFINE_STRING_CONST(default_sighandler_name,
                               "%default-signal-handler", 23, 23);
static SCM_DEFINE_SUBR(default_sighandler_stub, 1, 0,
                       SCM_OBJ(&default_sighandler_name),
                       default_sighandler,
                       NULL, NULL);

#define DEFAULT_SIGHANDLER    SCM_OBJ(&default_sighandler_stub)

/* For some signals, exits. */
static ScmObj exit_sighandler(ScmObj *args, int nargs, void *data)
{
    Scm_Exit(0);
    return SCM_UNDEFINED;       /* dummy */
}

static SCM_DEFINE_STRING_CONST(exit_sighandler_name,
                               "%exit-signal-handler", 20, 20);
static SCM_DEFINE_SUBR(exit_sighandler_stub, 1, 0,
                       SCM_OBJ(&exit_sighandler_name),
                       exit_sighandler,
                       NULL, NULL);

#define EXIT_SIGHANDLER    SCM_OBJ(&exit_sighandler_stub)

#if 0                           /* not used for now */
/* For some signals, gauche does nothing */
static ScmObj through_sighandler(ScmObj *args, int nargs, void *data)
{
    return SCM_UNDEFINED;
}

static SCM_DEFINE_STRING_CONST(through_sighandler_name,
                               "%through-signal-handler", 20, 20);
static SCM_DEFINE_SUBR(through_sighandler_stub, 1, 0,
                       SCM_OBJ(&through_sighandler_name),
                       through_sighandler,
                       NULL, NULL);

#define THROUGH_SIGHANDLER    SCM_OBJ(&through_sighandler_stub)
#endif

/*
 * An emulation stub for Windows/MinGW
 */
#ifdef __MINGW32__
int sigaction(int signum, const struct sigaction *act,
	      struct sigaction *oact)
{
    if (oact != NULL) {
	Scm_Panic("sigaction() with oldact != NULL isn't supported on MinGW port");
    }
    if (signal(signum, act->sa_handler) == SIG_ERR) {
	return -1;
    } else {
	return 0;
    }
}
#endif /* __MINGW32__ */

/*
 * set-signal-handler!
 */
ScmObj Scm_SetSignalHandler(ScmObj sigs, ScmObj handler, ScmSysSigset *mask)
{
    struct sigaction act;
    struct sigdesc *desc;
    sigset_t sigset;
    int badproc = FALSE, sigactionfailed = FALSE;

    if (SCM_INTP(sigs)) {
        int signum = SCM_INT_VALUE(sigs);
        if (signum < 0 || signum >= NSIG) {
            Scm_Error("bad signal number: %d", signum);
        }
        sigemptyset(&sigset);
        sigaddset(&sigset, signum);
    } else if (SCM_SYS_SIGSET_P(sigs)) {
        sigset = SCM_SYS_SIGSET(sigs)->set;
    } else {
        Scm_Error("bad signal number: must be an integer signal number or a <sys-sigset> object, but got %S", sigs);
    }

    if (mask == NULL) {
        /* If no mask is specified, block singals in SIGS. */
        mask = make_sigset();
        mask->set = sigset;
    }
    
    (void)SCM_INTERNAL_MUTEX_LOCK(sigHandlers.mutex);
    if (SCM_TRUEP(handler)) {
        act.sa_handler = SIG_DFL;
    } else if (SCM_FALSEP(handler)) {
        act.sa_handler = SIG_IGN;
    } else if (SCM_PROCEDUREP(handler)
               && SCM_PROCEDURE_TAKE_NARG_P(handler, 1)) {
        act.sa_handler = sig_handle;
    } else {
        badproc = TRUE;
    }
    if (!badproc) {
        sigfillset(&act.sa_mask); /* we should block all the signals */
        act.sa_flags = 0;
        for (desc=sigDesc; desc->name; desc++) {
            if (!sigismember(&sigset, desc->num)) continue;
            if (!sigismember(&sigHandlers.masterSigset, desc->num)) continue;
            if (sigaction(desc->num, &act, NULL) != 0) {
                sigactionfailed = desc->num;
            } else {
                sigHandlers.handlers[desc->num] = handler;
                sigHandlers.masks[desc->num] = mask;
            }
        }
    }
    (void)SCM_INTERNAL_MUTEX_UNLOCK(sigHandlers.mutex);
    if (badproc) Scm_Error("bad signal handling procedure: must be either a procedure that takes at least one argument, #t, or #f, but got %S", handler);
    if (sigactionfailed) Scm_Error("sigaction failed when setting a sighandler for signal %d", sigactionfailed);
    return SCM_UNDEFINED;
}

ScmObj Scm_GetSignalHandler(int signum)
{
    if (signum < 0 || signum >= NSIG) {
        Scm_Error("bad signal number: %d", signum);
    }
    /* No lock; atomic pointer access */
    return sigHandlers.handlers[signum];
}

ScmObj Scm_GetSignalHandlerMask(int signum)
{
    ScmSysSigset *r;
    if (signum < 0 || signum >= NSIG) {
        Scm_Error("bad signal number: %d", signum);
    }
    /* No lock; atomic pointer access */
    r = sigHandlers.masks[signum];
    return r? SCM_OBJ(r) : SCM_FALSE;
}

ScmObj Scm_GetSignalHandlers(void)
{
    ScmObj h = SCM_NIL, hp;
    ScmObj handlers[NSIG];
    struct sigdesc *desc;
    sigset_t masterSet;
    int i;

    /* copy handler vector and master sig set locally, so that we won't
       grab the lock for extensive time */
    (void)SCM_INTERNAL_MUTEX_LOCK(sigHandlers.mutex);
    for (i=0; i<NSIG; i++) handlers[i] = sigHandlers.handlers[i];
    masterSet = sigHandlers.masterSigset;
    (void)SCM_INTERNAL_MUTEX_UNLOCK(sigHandlers.mutex);
        
    for (desc=sigDesc; desc->name; desc++) {
        if (!sigismember(&masterSet, desc->num)) continue;
        SCM_FOR_EACH(hp, h) {
            if (SCM_EQ(SCM_CDAR(hp), handlers[desc->num])) {
                sigaddset(&(SCM_SYS_SIGSET(SCM_CAAR(hp))->set), desc->num);
                break;
            }
        }
        if (SCM_NULLP(hp)) {
            ScmSysSigset *set = make_sigset();
            sigaddset(&(set->set), desc->num);
            h = Scm_Acons(SCM_OBJ(set), handlers[desc->num], h);
        }
    }
    return h;
}

/*
 * set/get master signal
 */
sigset_t Scm_GetMasterSigmask(void)
{
    return sigHandlers.masterSigset;
}

/* this should be called before any thread is created. */
void Scm_SetMasterSigmask(sigset_t *set)
{
    struct sigdesc *desc = sigDesc;
    struct sigaction acton, actoff;

    acton.sa_handler = (void(*)())sig_handle;
    acton.sa_mask = *set;
    acton.sa_flags = 0;
    actoff.sa_handler = SIG_DFL;
    sigemptyset(&actoff.sa_mask);
    actoff.sa_flags = 0;
    
    for (; desc->name; desc++) {
        if (sigismember(&sigHandlers.masterSigset, desc->num)
            && !sigismember(set, desc->num)) {
            /* remove sighandler */
            if (sigaction(desc->num, &actoff, NULL) != 0) {
                Scm_SysError("sigaction on %d failed", desc->num);
            }
            sigHandlers.handlers[desc->num] = SCM_TRUE;
        } else if (!sigismember(&sigHandlers.masterSigset, desc->num)
                   && sigismember(set, desc->num)) {
            /* add sighandler if necessary */
            if (desc->defaultHandle == SIGDEF_DFL) {
                if (sigaction(desc->num, &actoff, NULL) != 0) {
                    Scm_SysError("sigaction on %d failed", desc->num);
                }
                sigHandlers.handlers[desc->num] = SCM_TRUE;
            } else if (desc->defaultHandle != SIGDEF_NOHANDLE) {
                if (sigaction(desc->num, &acton, NULL) != 0) {
                    Scm_SysError("sigaction on %d failed", desc->num);
                }
                switch (desc->defaultHandle) {
                case SIGDEF_ERROR:
                    sigHandlers.handlers[desc->num] = DEFAULT_SIGHANDLER;
                    break;
                case SIGDEF_EXIT:
                    sigHandlers.handlers[desc->num] = EXIT_SIGHANDLER;
                    break;
                default:
                    Scm_Panic("Scm_SetMasterSigmask: can't be here");
                }
            }
        }
    }
    sigHandlers.masterSigset = *set;
    Scm_VM()->sigMask = sigHandlers.masterSigset;
}

/*============================================================
 * Other signal-related operations
 */

/*
 * set signal mask
 */

ScmObj Scm_SysSigmask(int how, ScmSysSigset *newmask)
{
    ScmSysSigset *oldmask = make_sigset();
    sigset_t *newset = NULL;

    if (newmask) {
        newset = &(newmask->set);
        if (how != SIG_SETMASK && how != SIG_BLOCK && how != SIG_UNBLOCK) {
            Scm_Error("bad 'how' argument for signal mask action: %d", how);
        }
    }
    if (SIGPROCMASK(how, newset, &(oldmask->set)) != 0) {
        Scm_Error("sigprocmask failed");
    }
    return SCM_OBJ(oldmask);
}

/*
 * Reset signal handlers except the masked ones.
 * This is called just before we change the signal mask and call exec(2),
 * so that we can avoid the hazard that the signal handler is called
 * between sigsetmask and exec.
 */
void Scm_ResetSignalHandlers(sigset_t *mask)
{
    struct sigdesc *desc = sigDesc;
    struct sigaction act;

    for (; desc->name; desc++) {
        if (!sigismember(&sigHandlers.masterSigset, desc->num)
            && (!mask || !sigismember(mask, desc->num))) {
            act.sa_flags = 0;
            act.sa_handler = SIG_IGN;
            // NB: we tolerate failure of this
            sigaction(desc->num, &act, NULL);
        }
    }
}

/*
 * sigsuspend
 */
static void scm_sigsuspend(sigset_t *mask)
{
#ifndef __MINGW32__
    sigset_t omask;
    ScmVM *vm = Scm_VM();
    for (;;) {
        SIGPROCMASK(SIG_BLOCK, &sigHandlers.masterSigset, &omask);
        if (vm->queueNotEmpty & SCM_VM_SIGQ_MASK) {
            SIGPROCMASK(SIG_SETMASK, &omask, NULL);
            Scm_SigCheck(vm);
            continue;
        }
        break;
    }
    sigsuspend(mask);
    SIGPROCMASK(SIG_SETMASK, &omask, NULL);
    SCM_SIGCHECK(vm);
#else  /*__MINGW32__*/
    Scm_Error("sigsuspend not supported on MinGW port");
#endif /*__MINGW32__*/
}

ScmObj Scm_SigSuspend(ScmSysSigset *mask)
{
    scm_sigsuspend(&(mask->set));
    return SCM_UNDEFINED;
}

/*
 * Alternative of 'pause()'
 * we can't use pause() reliably, since the process may miss a signal
 * if it is delivered after the last call of Scm_SigCheck before pause();
 * the signal is queued, but will never be processed until pause() returns
 * by another signal.
 */
ScmObj Scm_Pause(void)
{
    sigset_t omask;
    SIGPROCMASK(SIG_SETMASK, NULL, &omask);
    scm_sigsuspend(&omask);
    return SCM_UNDEFINED;
}

/*
 * Sigwait wrapper
 *
 * The behavior of sigwait is undefined if a signal handler is set to
 * the waiting signal.  On Cygwin, for example, using both signal handler
 * and sigwait makes havoc.  Since Gauche installs sig_handle()
 * implicitly to some signals, a casual user may be confused by the
 * unpredictable behavior when he doesn't reset signal handlers explicitly.
 * So we take care of them here. 
 *
 * We remove the signal handlers for the signals to be waited before calling
 * sigwait(), and restore them after its return.  We assume those signals
 * are blocked at this moment (if not, the behavior of sigwait() is
 * undefined), so we don't need to care about race condition.  If another
 * thread replaces signal handlers during this thread's waiting for a
 * signal, it would be reverted upon returning from this function, but 
 * such operation is inherently unsafe anyway, so we don't care.
 */
int Scm_SigWait(ScmSysSigset *mask)
{
#if defined(HAVE_SIGWAIT)
    int i, r = 0, sig = 0;
    int failed_sig = -1;
    int sigwait_called = FALSE;
    int errno_save = 0;
    struct sigaction act, oact;
    sigset_t to_wait;        /* real set of signals to wait */
    sigset_t saved;
    void (*c_handlers[NSIG])(int);
    sigset_t c_masks[NSIG];

    (void)SCM_INTERNAL_MUTEX_LOCK(sigHandlers.mutex);
    /* we can't wait for the signals Gauche doesn't handle. */
    to_wait = mask->set;
    for (i=0; i<NSIG; i++) {
        if (!sigismember(&sigHandlers.masterSigset, i)) {
            sigdelset(&to_wait, i);
        }
    }

    /* Remove C-level handlers */
    sigemptyset(&saved);
    act.sa_handler = SIG_DFL;
    for (i=1; i<NSIG; i++) {
        c_handlers[i] = SIG_DFL;
        if (!sigismember(&to_wait, i)) continue;
        if (sigaction(i, NULL, &oact) < 0) {
            failed_sig = i;
            errno_save = errno;
            continue;
        }
        sigaddset(&saved, i);
        c_handlers[i] = oact.sa_handler;
        c_masks[i]    = oact.sa_mask;
    }
    
    if (failed_sig < 0) {
        (void)SCM_INTERNAL_MUTEX_UNLOCK(sigHandlers.mutex);
        sigwait_called = TRUE;
        r = sigwait(&to_wait, &sig);
        (void)SCM_INTERNAL_MUTEX_LOCK(sigHandlers.mutex);
    }

    /* Restore C-level handlers */
    for (i=1; i<NSIG; i++) {
        if (!sigismember(&saved, i)) continue;
        act.sa_handler = c_handlers[i];
        act.sa_mask    = c_masks[i];
        if (sigaction(i, &act, NULL) < 0) {
            failed_sig = i;
            errno_save = errno;
        }
    }
    (void)SCM_INTERNAL_MUTEX_UNLOCK(sigHandlers.mutex);

    /* error handling */
    if (failed_sig >= 0) {
        Scm_SysError("sigaction(2) call failed on signal %d"
                     " %s sigwait call",
                     failed_sig,
                     sigwait_called? "after" : "before");
    }
    if (r < 0) {
        Scm_SysError("sigwait failed");
    }
    return sig;
#else  /* !HAVE_SIGWAIT */
    Scm_Error("sigwait not supported on this platform");
    return 0;
#endif
}


/*================================================================
 * Initialize
 */

void Scm__InitSignal(void)
{
    ScmModule *mod = Scm_GaucheModule();
    ScmObj defsigh_sym = Scm_Intern(&default_sighandler_name);
    struct sigdesc *desc;
    int i;

    (void)SCM_INTERNAL_MUTEX_INIT(sigHandlers.mutex);
    sigemptyset(&sigHandlers.masterSigset);
    for (i=0; i<NSIG; i++) sigHandlers.handlers[i] = SCM_FALSE;
    
    Scm_InitStaticClass(&Scm_SysSigsetClass, "<sys-sigset>",
                        mod, NULL, 0);

    for (desc = sigDesc; desc->name; desc++) {
        SCM_DEFINE(mod, desc->name, SCM_MAKE_INT(desc->num));
    }
    Scm_Define(mod, SCM_SYMBOL(defsigh_sym), DEFAULT_SIGHANDLER);
}
