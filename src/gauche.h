/*
 * gauche.h - Gauche scheme system header
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
 *  $Id: gauche.h,v 1.403.2.1 2004-12-23 06:57:21 shirok Exp $
 */

#ifndef GAUCHE_H
#define GAUCHE_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <setjmp.h>
#include <limits.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <gauche/config.h>  /* read config.h _before_ gc.h */
#include <gauche/int64.h>

#if defined(LIBGAUCHE_BODY)
#define GC_DLL    /* for gc.h to handle Win32 crazyness */
#define GC_BUILD  /* ditto */
#endif 
#include <gc.h>

#ifndef SCM_DECL_BEGIN
#ifdef __cplusplus
#define SCM_DECL_BEGIN  extern "C" {
#define SCM_DECL_END    }
#else  /*! __cplusplus */
#define SCM_DECL_BEGIN
#define SCM_DECL_END
#endif /*! __cplusplus */
#endif /*!defined(SCM_DECL_BEGIN)*/

SCM_DECL_BEGIN

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

/* Ugly cliche for Win32. */
#if defined(__CYGWIN__) || defined(__MINGW32__)
# if defined(LIBGAUCHE_BODY)
#  define SCM_EXTERN extern
# else
#  define SCM_EXTERN extern __declspec(dllimport)
# endif
#else  /*!(__CYGWIN__ || __MINGW32__)*/
# define SCM_EXTERN extern
#endif /*!(__CYGWIN__ || __MINGW32__)*/

/* For Mingw32, we need some tricks */
#if defined(__MINGW32__)
#include <gauche/mingw-compat.h>
#endif /*__MINGW32__*/

/* Some useful macros */

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif

/* This defines several auxiliary routines that are useful for debugging */
#ifndef SCM_DEBUG_HELPER
#define SCM_DEBUG_HELPER      TRUE
#endif

#define SCM_INLINE_MALLOC_PRIMITIVES

#ifdef GAUCHE_USE_PTHREADS
# include <gauche/pthread.h>
#else  /* !GAUCHE_USE_PTHREADS */
# include <gauche/uthread.h>
#endif /* !GAUCHE_USE_PTHREADS */

/*-------------------------------------------------------------
 * BASIC TYPES
 */

/*
 * A word large enough to hold a pointer
 */
typedef unsigned long ScmWord;

/*
 * A byte
 */
typedef unsigned char ScmByte;

/*
 * A character.
 */
typedef long ScmChar;

/*
 * An opaque pointer.  All Scheme objects are represented by
 * this type.
 */
typedef struct ScmHeaderRec *ScmObj;

/*
 * The class structure.  ScmClass is actually a subclass of ScmObj.
 */
typedef struct ScmClassRec ScmClass;

/* TAG STRUCTURE
 *
 * [Pointer]
 *      -------- -------- -------- ------00
 *      Points to a pair or other heap-allocated objects.
 *
 * [Fixnum]
 *      -------- -------- -------- ------01
 *      30-bit signed integer
 *
 * [Character]
 *      -------- -------- -------- -----010
 *      29-bit
 *
 * [Miscellaneous]
 *      -------- -------- -------- ----0110
 *      #f, #t, '(), eof-object, undefined
 *
 * [VM Instructions]
 *      -------- -------- -------- ----1110
 *      Only appears in a compiled code.
 *
 * [Heap object]
 *      -------- -------- -------- ------11
 *      Only appears at the first word of heap-allocated
 *      objects except pairs.   Masking lower 2bits gives
 *      a pointer to ScmClass.  
 */

/* Type coercer */

#define	SCM_OBJ(obj)      ((ScmObj)(obj))
#define	SCM_WORD(obj)     ((ScmWord)(obj))

/*
 * PRIMARY TAG IDENTIFICATION
 */

#define	SCM_TAG(obj)     (SCM_WORD(obj) & 0x03)
#define SCM_PTRP(obj)    (SCM_TAG(obj) == 0)

/*
 * IMMEDIATE OBJECTS
 */

#define SCM_IMMEDIATEP(obj) ((SCM_WORD(obj)&0x0f) == 6)
#define SCM_ITAG(obj)       (SCM_WORD(obj)>>4)

#define SCM__MAKE_ITAG(num)  (((num)<<4) + 6)
#define SCM_FALSE           SCM_OBJ(SCM__MAKE_ITAG(0)) /* #f */
#define SCM_TRUE            SCM_OBJ(SCM__MAKE_ITAG(1)) /* #t  */
#define SCM_NIL             SCM_OBJ(SCM__MAKE_ITAG(2)) /* '() */
#define SCM_EOF             SCM_OBJ(SCM__MAKE_ITAG(3)) /* eof-object */
#define SCM_UNDEFINED       SCM_OBJ(SCM__MAKE_ITAG(4)) /* #undefined */
#define SCM_UNBOUND         SCM_OBJ(SCM__MAKE_ITAG(5)) /* unbound value */

#define SCM_FALSEP(obj)     ((obj) == SCM_FALSE)
#define SCM_TRUEP(obj)      ((obj) == SCM_TRUE)
#define SCM_NULLP(obj)      ((obj) == SCM_NIL)
#define SCM_EOFP(obj)       ((obj) == SCM_EOF)
#define SCM_UNDEFINEDP(obj) ((obj) == SCM_UNDEFINED)
#define SCM_UNBOUNDP(obj)   ((obj) == SCM_UNBOUND)

/*
 * BOOLEAN
 */
#define SCM_BOOLP(obj)       ((obj) == SCM_TRUE || (obj) == SCM_FALSE)
#define SCM_BOOL_VALUE(obj)  (!SCM_FALSEP(obj))
#define	SCM_MAKE_BOOL(obj)   ((obj)? SCM_TRUE:SCM_FALSE)

#define SCM_EQ(x, y)         ((x) == (y))

SCM_EXTERN int Scm_EqP(ScmObj x, ScmObj y);
SCM_EXTERN int Scm_EqvP(ScmObj x, ScmObj y);
SCM_EXTERN int Scm_EqualP(ScmObj x, ScmObj y);

/* comparison mode */
enum {
    SCM_CMP_EQ,
    SCM_CMP_EQV,
    SCM_CMP_EQUAL
};

SCM_EXTERN int Scm_EqualM(ScmObj x, ScmObj y, int mode);

/*
 * FIXNUM
 */

#define SCM_INTP(obj)        (SCM_TAG(obj) == 1)
#define SCM_INT_VALUE(obj)   (((signed long int)(obj)) >> 2)
#define SCM_MAKE_INT(obj)    SCM_OBJ(((long)(obj) << 2) + 1)

#define SCM_UINTP(obj)       (SCM_INTP(obj)&&((signed long int)(obj)>=0))

/*
 * CHARACTERS
 *
 *  A character is represented by (up to) 29-bit integer.  The actual
 *  encoding depends on compile-time flags.
 *
 *  For character cases, I only care about ASCII chars (at least for now)
 */

#define	SCM_CHAR(obj)           ((ScmChar)(obj))
#define	SCM_CHARP(obj)          ((SCM_WORD(obj)&0x07L) == 2)
#define	SCM_CHAR_VALUE(obj)     SCM_CHAR(SCM_WORD(obj) >> 3)
#define	SCM_MAKE_CHAR(ch)       SCM_OBJ((long)((ch) << 3) + 2)

#define SCM_CHAR_INVALID        ((ScmChar)(-1)) /* indicate invalid char */
#define SCM_CHAR_MAX            (0x1fffffff)

#define SCM_CHAR_ASCII_P(ch)    ((ch) < 0x80)
#define SCM_CHAR_UPPER_P(ch)    (('A' <= (ch)) && ((ch) <= 'Z'))
#define SCM_CHAR_LOWER_P(ch)    (('a' <= (ch)) && ((ch) <= 'z'))
#define SCM_CHAR_UPCASE(ch)     (SCM_CHAR_LOWER_P(ch)?((ch)-('a'-'A')):(ch))
#define SCM_CHAR_DOWNCASE(ch)   (SCM_CHAR_UPPER_P(ch)?((ch)+('a'-'A')):(ch))

SCM_EXTERN int Scm_DigitToInt(ScmChar ch, int radix);
SCM_EXTERN ScmChar Scm_IntToDigit(int n, int radix);
SCM_EXTERN int Scm_CharToUcs(ScmChar ch);
SCM_EXTERN ScmChar Scm_UcsToChar(int ucs);
SCM_EXTERN ScmObj Scm_CharEncodingName(void);
SCM_EXTERN const char **Scm_SupportedCharacterEncodings(void);
SCM_EXTERN int Scm_SupportedCharacterEncodingP(const char *encoding);

#if   defined(GAUCHE_CHAR_ENCODING_EUC_JP)
#include "gauche/char_euc_jp.h"
#elif defined(GAUCHE_CHAR_ENCODING_UTF_8)
#include "gauche/char_utf_8.h"
#elif defined(GAUCHE_CHAR_ENCODING_SJIS)
#include "gauche/char_sjis.h"
#else
#include "gauche/char_none.h"
#endif

/*
 * HEAP ALLOCATED OBJECTS
 *
 *  A heap allocated object has its class tag in the first word
 *  (except pairs).  Masking the lower two bits of class tag
 *  gives a pointer to the class object.
 */

#define SCM_HOBJP(obj)  (SCM_PTRP(obj)&&SCM_TAG(SCM_OBJ(obj)->tag)==3)

#define SCM_CPP_CAT(a, b)   a ## b
#define SCM_CPP_CAT3(a, b, c)  a ## b ## c

#define SCM_CLASS_DECL(klass) extern ScmClass klass
#define SCM_CLASS_STATIC_PTR(klass)  (&klass)
#define SCM_CLASS2TAG(klass)  ((ScmByte*)(klass) + 3)

/* A common header for heap-allocated objects */
typedef struct ScmHeaderRec {
    ScmByte *tag;                /* private.  should be accessed
                                    only via macros. */
} ScmHeader;

#define SCM_HEADER       ScmHeader hdr /* for declaration */

/* Extract the class pointer from the tag.
   You can use these only if SCM_HOBJP(obj) != FALSE */
#define SCM_CLASS_OF(obj)      SCM_CLASS((SCM_OBJ(obj)->tag - 3))
#define SCM_SET_CLASS(obj, k)  (SCM_OBJ(obj)->tag = (ScmByte*)(k) + 3)

/* Check if classof(OBJ) equals to an extended class KLASS */
#define SCM_XTYPEP(obj, klass) \
    (SCM_PTRP(obj)&&(SCM_OBJ(obj)->tag == SCM_CLASS2TAG(klass)))

/* Check if classof(OBJ) is a subtype of an extended class KLASS */
#define SCM_ISA(obj, klass) (SCM_XTYPEP(obj,klass)||Scm_TypeP(SCM_OBJ(obj),klass))

/* A common header for objects whose class is defined in Scheme */
typedef struct ScmInstanceRec {
    ScmByte *tag;               /* private */
    ScmObj *slots;              /* private */
} ScmInstance;

#define SCM_INSTANCE_HEADER  ScmInstance hdr  /* for declaration */

#define SCM_INSTANCE(obj)        ((ScmInstance*)(obj))
#define SCM_INSTANCE_SLOTS(obj)  (SCM_INSTANCE(obj)->slots)

/* Fundamental allocators */
#define SCM_MALLOC(size)          GC_MALLOC(size)
#define SCM_MALLOC_ATOMIC(size)   GC_MALLOC_ATOMIC(size)

#define SCM_NEW(type)         ((type*)(SCM_MALLOC(sizeof(type))))
#define SCM_NEW_ARRAY(type, nelts) ((type*)(SCM_MALLOC(sizeof(type)*(nelts))))
#define SCM_NEW2(type, size)  ((type)(SCM_MALLOC(size)))
#define SCM_NEW_ATOMIC(type)  ((type*)(SCM_MALLOC_ATOMIC(sizeof(type))))
#define SCM_NEW_ATOMIC2(type, size) ((type)(SCM_MALLOC_ATOMIC(size)))

typedef void (*ScmFinalizerProc)(ScmObj z, void *data);
SCM_EXTERN void Scm_RegisterFinalizer(ScmObj z, ScmFinalizerProc finalizer,
                                      void *data);
SCM_EXTERN void Scm_UnregisterFinalizer(ScmObj z);

/* Safe coercer */
#define SCM_OBJ_SAFE(obj)     ((obj)?SCM_OBJ(obj):SCM_UNDEFINED)

typedef struct ScmVMRec        ScmVM;
typedef struct ScmPairRec      ScmPair;
typedef struct ScmExtendedPairRec ScmExtendedPair;
typedef struct ScmCharSetRec   ScmCharSet;
typedef struct ScmStringRec    ScmString;
typedef struct ScmDStringRec   ScmDString;
typedef struct ScmVectorRec    ScmVector;
typedef struct ScmBignumRec    ScmBignum;
typedef struct ScmFlonumRec    ScmFlonum;
typedef struct ScmComplexRec   ScmComplex;
typedef struct ScmPortRec      ScmPort;
typedef struct ScmHashTableRec ScmHashTable;
typedef struct ScmModuleRec    ScmModule;
typedef struct ScmSymbolRec    ScmSymbol;
typedef struct ScmGlocRec      ScmGloc;
typedef struct ScmKeywordRec   ScmKeyword;
typedef struct ScmProcedureRec ScmProcedure;
typedef struct ScmClosureRec   ScmClosure;
typedef struct ScmSubrRec      ScmSubr;
typedef struct ScmGenericRec   ScmGeneric;
typedef struct ScmMethodRec    ScmMethod;
typedef struct ScmNextMethodRec ScmNextMethod;
typedef struct ScmSyntaxRec    ScmSyntax;
typedef struct ScmMacroRec     ScmMacro;
typedef struct ScmPromiseRec   ScmPromise;
typedef struct ScmRegexpRec    ScmRegexp;
typedef struct ScmRegMatchRec  ScmRegMatch;
typedef struct ScmWriteContextRec ScmWriteContext;
typedef struct ScmAutoloadRec  ScmAutoload;

/*---------------------------------------------------------
 * VM STUFF
 */

/* Detailed definitions are in vm.h.  Here I expose external interface */

#include <gauche/vm.h>

#define SCM_VM(obj)          ((ScmVM *)(obj))
#define SCM_VMP(obj)         SCM_XTYPEP(obj, SCM_CLASS_VM)

#define SCM_VM_CURRENT_INPUT_PORT(vm)   (SCM_VM(vm)->curin)
#define SCM_VM_CURRENT_OUTPUT_PORT(vm)  (SCM_VM(vm)->curout)
#define SCM_VM_CURRENT_ERROR_PORT(vm)   (SCM_VM(vm)->curerr)

SCM_EXTERN ScmVM *Scm_VM(void);     /* Returns the current VM */

SCM_EXTERN ScmObj Scm_Compile(ScmObj form, ScmObj env, int context);
SCM_EXTERN ScmObj Scm_CompileBody(ScmObj form, ScmObj env, int context);
SCM_EXTERN ScmObj Scm_CompileLookupEnv(ScmObj sym, ScmObj env, int op);

SCM_EXTERN ScmObj Scm_Eval(ScmObj form, ScmObj env);
SCM_EXTERN ScmObj Scm_Apply(ScmObj proc, ScmObj args);
SCM_EXTERN ScmObj Scm_Values(ScmObj args);
SCM_EXTERN ScmObj Scm_Values2(ScmObj val0, ScmObj val1);
SCM_EXTERN ScmObj Scm_Values3(ScmObj val0, ScmObj val1, ScmObj val2);
SCM_EXTERN ScmObj Scm_Values4(ScmObj val0, ScmObj val1, ScmObj val2,
			      ScmObj val3);
SCM_EXTERN ScmObj Scm_Values5(ScmObj val0, ScmObj val1, ScmObj val2,
			      ScmObj val3, ScmObj val4);

SCM_EXTERN ScmObj Scm_MakeMacroTransformer(ScmSymbol *name,
					   ScmProcedure *proc);
SCM_EXTERN ScmObj Scm_MakeMacroAutoload(ScmSymbol *name,
                                        ScmAutoload *al);

SCM_EXTERN ScmObj Scm_UnwrapSyntax(ScmObj form);

SCM_EXTERN ScmObj Scm_VMGetResult(ScmVM *vm);
SCM_EXTERN ScmObj Scm_VMGetStackLite(ScmVM *vm);
SCM_EXTERN ScmObj Scm_VMGetStack(ScmVM *vm);

SCM_EXTERN ScmObj Scm_VMApply(ScmObj proc, ScmObj args);
SCM_EXTERN ScmObj Scm_VMApply0(ScmObj proc);
SCM_EXTERN ScmObj Scm_VMApply1(ScmObj proc, ScmObj arg);
SCM_EXTERN ScmObj Scm_VMApply2(ScmObj proc, ScmObj arg1, ScmObj arg2);
SCM_EXTERN ScmObj Scm_VMApply3(ScmObj proc, ScmObj arg1, ScmObj arg2, ScmObj agr3);
SCM_EXTERN ScmObj Scm_VMEval(ScmObj expr, ScmObj env);
SCM_EXTERN ScmObj Scm_VMCall(ScmObj *args, int argcnt, void *data);

SCM_EXTERN ScmObj Scm_VMCallCC(ScmObj proc);
SCM_EXTERN ScmObj Scm_VMDynamicWind(ScmObj pre, ScmObj body, ScmObj post);
SCM_EXTERN ScmObj Scm_VMDynamicWindC(ScmObj (*before)(ScmObj *, int, void *),
				     ScmObj (*body)(ScmObj *, int, void *),
				     ScmObj (*after)(ScmObj *, int, void *),
				     void *data);

SCM_EXTERN ScmObj Scm_VMWithErrorHandler(ScmObj handler, ScmObj thunk);
SCM_EXTERN ScmObj Scm_VMWithExceptionHandler(ScmObj handler, ScmObj thunk);
SCM_EXTERN ScmObj Scm_VMThrowException(ScmObj exception);

/*---------------------------------------------------------
 * CLASS
 */

/* See class.c for the description of function pointer members.
   There's a lot of voodoo magic in class structure, so don't touch
   those fields casually.  Also, the order of these fields must be
   reflected to the class definition macros below */
struct ScmClassRec {
    SCM_INSTANCE_HEADER;
    void (*print)(ScmObj obj, ScmPort *sink, ScmWriteContext *mode);
    int (*compare)(ScmObj x, ScmObj y, int equalp);
    int (*serialize)(ScmObj obj, ScmPort *sink, ScmObj context);
    ScmObj (*allocate)(ScmClass *klass, ScmObj initargs);
    ScmClass **cpa;             /* class precedence array, NULL terminated */
    int numInstanceSlots;       /* # of instance slots */
    int coreSize;               /* size of core structure; 0 == unknown */
    unsigned int flags;
    ScmObj name;                /* scheme name */
    ScmObj directSupers;        /* list of classes */
    ScmObj cpl;                 /* list of classes */
    ScmObj accessors;           /* alist of slot-name & slot-accessor */
    ScmObj directSlots;         /* alist of slot-name & slot-definition */
    ScmObj slots;               /* alist of slot-name & slot-definition */
    ScmObj directSubclasses;    /* list of direct subclasses */
    ScmObj directMethods;       /* list of methods that has this class in
                                   its specializer */
    ScmObj initargs;            /* saved key-value list for redefinition */
    ScmObj modules;             /* modules where this class is defined */
    ScmObj redefined;           /* if this class is obsoleted by class
                                   redefinition, points to the new class.
                                   if this class is being redefined, points
                                   to a thread that is handling the
                                   redefinition.  (it won't be seen by
                                   Scheme; see class.c)
                                   otherwise #f */
    ScmInternalMutex mutex;     /* to protect from MT hazard */
    ScmInternalCond cv;         /* wait on this while a class being updated */
};

typedef struct ScmClassStaticSlotSpecRec ScmClassStaticSlotSpec;

#define SCM_CLASS(obj)        ((ScmClass*)(obj))
#define SCM_CLASSP(obj)       SCM_ISA(obj, SCM_CLASS_CLASS)

/* Class categories

   In C level, there are four categories of classes.  The category of
   class can be obtained by masking the lower two bits of flags field.

   SCM_CLASS_BUILTIN
       An instance of this class doesn't have "slots" member (thus
       cannot be casted to ScmInstance).   From Scheme level, this
       class cannot be inherited, nor redefined.  In C you can create
       subclasses, by making sure the subclass' instance structure
       to include this class's instance structure.  Such "hard-wired"
       inheritance only forms a tree, i.e. no multiple inheritance.

   SCM_CLASS_ABSTRACT 
       This class is defined in C, but doesn't allowed to create an
       instance by its own.  It is intended to be used as a mixin from
       both C and Scheme-defined class.   This class shouldn't have
       C members other than SCM_HEADER.   This class cannot be redefined.

   SCM_CLASS_BASE
       This class is defined in C, and can be subclassed in Scheme.
       An instance of this class must have "slots" member and be
       able to be casted to ScmInstance.  The instance may have other
       C members.  This class cannot be redefined.

   SCM_CLASS_SCHEME
       A Scheme-defined class.  This class should have at most one
       SCM_CLASS_BASE class in its CPL, except the <object> class,
       which is always in the CPL of Scheme-defined class.  All other
       classes in CPL must be either SCM_CLASS_ABSTRACT or
       SCM_CLASS_SCHEME.  This class can be redefined.
*/

enum {
    SCM_CLASS_BUILTIN  = 0,
    SCM_CLASS_ABSTRACT = 1,
    SCM_CLASS_BASE     = 2,
    SCM_CLASS_SCHEME   = 3,

    /* A special flag that only be used for "natively applicable"
       objects, which basically inherits ScmProcedure. */
    SCM_CLASS_APPLICABLE = 0x04
};

#define SCM_CLASS_FLAGS(obj)     (SCM_CLASS(obj)->flags)
#define SCM_CLASS_APPLICABLE_P(obj) (SCM_CLASS_FLAGS(obj)&SCM_CLASS_APPLICABLE)

#define SCM_CLASS_CATEGORY(obj)  (SCM_CLASS_FLAGS(obj)&3)

SCM_EXTERN void Scm_InitStaticClass(ScmClass *klass, const char *name,
                                    ScmModule *mod,
                                    ScmClassStaticSlotSpec *slots,
                                    int flags);
SCM_EXTERN void Scm_InitStaticClassWithSupers(ScmClass *klass,
                                              const char *name,
                                              ScmModule *mod,
                                              ScmObj supers,
                                              ScmClassStaticSlotSpec *slots,
                                              int flags);
SCM_EXTERN void Scm_InitStaticClassWithMeta(ScmClass *klass,
                                            const char *name,
                                            ScmModule *mod,
                                            ScmClass *meta,
                                            ScmObj supers,
                                            ScmClassStaticSlotSpec *slots,
                                            int flags);

/* OBSOLETE */
SCM_EXTERN void Scm_InitBuiltinClass(ScmClass *c, const char *name,
				     ScmClassStaticSlotSpec *slots,
				     int withMeta,
                                     ScmModule *m);

SCM_EXTERN ScmClass *Scm_ClassOf(ScmObj obj);
SCM_EXTERN int Scm_SubtypeP(ScmClass *sub, ScmClass *type);
SCM_EXTERN int Scm_TypeP(ScmObj obj, ScmClass *type);
SCM_EXTERN ScmClass *Scm_BaseClassOf(ScmClass *klass);

SCM_EXTERN ScmObj Scm_VMSlotRef(ScmObj obj, ScmObj slot, int boundp);
SCM_EXTERN ScmObj Scm_VMSlotSet(ScmObj obj, ScmObj slot, ScmObj value);
SCM_EXTERN ScmObj Scm_VMSlotBoundP(ScmObj obj, ScmObj slot);

/* built-in classes */
SCM_CLASS_DECL(Scm_TopClass);
SCM_CLASS_DECL(Scm_BoolClass);
SCM_CLASS_DECL(Scm_CharClass);
SCM_CLASS_DECL(Scm_ClassClass);
SCM_CLASS_DECL(Scm_UnknownClass);
SCM_CLASS_DECL(Scm_CollectionClass);
SCM_CLASS_DECL(Scm_SequenceClass);
SCM_CLASS_DECL(Scm_ObjectClass); /* base of Scheme-defined objects */

#define SCM_CLASS_TOP          (&Scm_TopClass)
#define SCM_CLASS_BOOL         (&Scm_BoolClass)
#define SCM_CLASS_CHAR         (&Scm_CharClass)
#define SCM_CLASS_CLASS        (&Scm_ClassClass)
#define SCM_CLASS_UNKNOWN      (&Scm_UnknownClass)
#define SCM_CLASS_COLLECTION   (&Scm_CollectionClass)
#define SCM_CLASS_SEQUENCE     (&Scm_SequenceClass)
#define SCM_CLASS_OBJECT       (&Scm_ObjectClass)

SCM_EXTERN ScmClass *Scm_DefaultCPL[];
SCM_EXTERN ScmClass *Scm_CollectionCPL[];
SCM_EXTERN ScmClass *Scm_SequenceCPL[];
SCM_EXTERN ScmClass *Scm_ObjectCPL[];

#define SCM_CLASS_DEFAULT_CPL     (Scm_DefaultCPL)
#define SCM_CLASS_COLLECTION_CPL  (Scm_CollectionCPL)
#define SCM_CLASS_SEQUENCE_CPL    (Scm_SequenceCPL)
#define SCM_CLASS_OBJECT_CPL      (Scm_ObjectCPL)

/* Static definition of classes
 *   SCM_DEFINE_BUILTIN_CLASS
 *   SCM_DEFINE_BUILTIN_CLASS_SIMPLE
 *   SCM_DEFINE_ABSTRACT_CLASS
 *   SCM_DEFINE_BASE_CLASS
 */

#define SCM__DEFINE_CLASS_COMMON(cname, coreSize, flag, printer, compare, serialize, allocate, cpa) \
    ScmClass cname = {                           \
        { SCM_CLASS2TAG(SCM_CLASS_CLASS), NULL },\
        printer,                                 \
        compare,                                 \
        serialize,                               \
        allocate,                                \
        cpa,                                     \
        0,        /*numInstanceSlots*/           \
        coreSize, /*coreSize*/                   \
        flag,     /*flags*/                      \
        SCM_FALSE,/*name*/                       \
        SCM_NIL,  /*directSupers*/               \
        SCM_NIL,  /*cpl*/                        \
        SCM_NIL,  /*accessors*/                  \
        SCM_NIL,  /*directSlots*/                \
        SCM_NIL,  /*slots*/                      \
        SCM_NIL,  /*directSubclasses*/           \
        SCM_NIL,  /*directMethods*/              \
        SCM_NIL,  /*initargs*/                   \
        SCM_NIL,  /*modules*/                    \
        SCM_FALSE /*redefined*/                  \
    }
    
/* Define built-in class statically -- full-featured version */
#define SCM_DEFINE_BUILTIN_CLASS(cname, printer, compare, serialize, allocate, cpa) \
    SCM__DEFINE_CLASS_COMMON(cname, 0,                    \
                             SCM_CLASS_BUILTIN,           \
                             printer, compare, serialize, allocate, cpa)

/* Define built-in class statically -- simpler version */
#define SCM_DEFINE_BUILTIN_CLASS_SIMPLE(cname, printer)         \
    SCM_DEFINE_BUILTIN_CLASS(cname, printer, NULL, NULL, NULL, NULL)

/* define an abstract class */
#define SCM_DEFINE_ABSTRACT_CLASS(cname, cpa)             \
    SCM__DEFINE_CLASS_COMMON(cname, 0,                    \
                             SCM_CLASS_ABSTRACT,          \
                             NULL, NULL, NULL, NULL, cpa)

/* define a class that can be subclassed by Scheme */
#define SCM_DEFINE_BASE_CLASS(cname, ctype, printer, compare, serialize, allocate, cpa) \
    SCM__DEFINE_CLASS_COMMON(cname, sizeof(ctype),        \
                             SCM_CLASS_BASE,              \
                             printer, compare, serialize, allocate, cpa)

/*--------------------------------------------------------
 * PAIR AND LIST
 */

/* Ordinary pair uses two words.  It can be distinguished from
 * other heap allocated objects by checking the first word doesn't
 * have "11" in the lower bits.
 */
struct ScmPairRec {
    ScmObj car;                 /* should be accessed via macros */
    ScmObj cdr;                 /* ditto */
};

/* To keep extra information such as source-code info, some pairs
 * actually have one extra word for attribute assoc-list.  Checking
 * whether a pair is an extended one or not isn't a very lightweight
 * operation, so the use of extended pair should be kept minimal.
 */
struct ScmExtendedPairRec {
    ScmObj car;                 /* should be accessed via macros */
    ScmObj cdr;                 /* ditto */
    ScmObj attributes;          /* should be accessed via API func. */
};

#define SCM_PAIRP(obj)  (SCM_PTRP(obj)&&SCM_TAG(SCM_OBJ(obj)->tag)!=0x03)

#define SCM_PAIR(obj)           ((ScmPair*)(obj))
#define SCM_CAR(obj)            (SCM_PAIR(obj)->car)
#define SCM_CDR(obj)            (SCM_PAIR(obj)->cdr)
#define SCM_CAAR(obj)           (SCM_CAR(SCM_CAR(obj)))
#define SCM_CADR(obj)           (SCM_CAR(SCM_CDR(obj)))
#define SCM_CDAR(obj)           (SCM_CDR(SCM_CAR(obj)))
#define SCM_CDDR(obj)           (SCM_CDR(SCM_CDR(obj)))

#define SCM_SET_CAR(obj, value) (SCM_CAR(obj) = (value))
#define SCM_SET_CDR(obj, value) (SCM_CDR(obj) = (value))

#define SCM_EXTENDED_PAIR_P(obj) \
    (SCM_PAIRP(obj)&&GC_size(obj)>=sizeof(ScmExtendedPair))
#define SCM_EXTENDED_PAIR(obj)  ((ScmExtendedPair*)(obj))


SCM_CLASS_DECL(Scm_ListClass);
SCM_CLASS_DECL(Scm_PairClass);
SCM_CLASS_DECL(Scm_NullClass);
#define SCM_CLASS_LIST          (&Scm_ListClass)
#define SCM_CLASS_PAIR          (&Scm_PairClass)
#define SCM_CLASS_NULL          (&Scm_NullClass)

#define SCM_LISTP(obj)          (SCM_NULLP(obj) || SCM_PAIRP(obj))

/* Useful macros to manipulate lists. */

#define	SCM_FOR_EACH(p, list) \
    for((p) = (list); SCM_PAIRP(p); (p) = SCM_CDR(p))

#define	SCM_APPEND1(start, last, obj)                           \
    do {                                                        \
	if (SCM_NULLP(start)) {                                 \
	    (start) = (last) = Scm_Cons((obj), SCM_NIL);        \
	} else {                                                \
	    SCM_SET_CDR((last), Scm_Cons((obj), SCM_NIL));      \
	    (last) = SCM_CDR(last);                             \
	}                                                       \
    } while (0)

#define	SCM_APPEND(start, last, obj)                    \
    do {                                                \
        ScmObj list_SCM_GLS = (obj);                    \
	if (SCM_NULLP(start)) {                         \
	    (start) = (list_SCM_GLS);                   \
            if (!SCM_NULLP(list_SCM_GLS)) {             \
                (last) = Scm_LastPair(list_SCM_GLS);    \
            }                                           \
        } else {                                        \
	    SCM_SET_CDR((last), (list_SCM_GLS));        \
	    (last) = Scm_LastPair(last);                \
	}                                               \
    } while (0)

#define SCM_LIST1(a)             Scm_Cons(a, SCM_NIL)
#define SCM_LIST2(a,b)           Scm_Cons(a, SCM_LIST1(b))
#define SCM_LIST3(a,b,c)         Scm_Cons(a, SCM_LIST2(b, c))
#define SCM_LIST4(a,b,c,d)       Scm_Cons(a, SCM_LIST3(b, c, d))
#define SCM_LIST5(a,b,c,d,e)     Scm_Cons(a, SCM_LIST4(b, c, d, e))

SCM_EXTERN ScmObj Scm_Cons(ScmObj car, ScmObj cdr);
SCM_EXTERN ScmObj Scm_Acons(ScmObj caar, ScmObj cdar, ScmObj cdr);
SCM_EXTERN ScmObj Scm_List(ScmObj elt, ...);
SCM_EXTERN ScmObj Scm_Conses(ScmObj elt, ...);
SCM_EXTERN ScmObj Scm_VaList(va_list elts);
SCM_EXTERN ScmObj Scm_VaCons(va_list elts);
SCM_EXTERN ScmObj Scm_ArrayToList(ScmObj *elts, int nelts);
SCM_EXTERN ScmObj *Scm_ListToArray(ScmObj list, int *nelts, ScmObj *store,
				   int alloc);

SCM_EXTERN ScmObj Scm_Car(ScmObj obj);
SCM_EXTERN ScmObj Scm_Cdr(ScmObj obj);
SCM_EXTERN ScmObj Scm_Caar(ScmObj obj);
SCM_EXTERN ScmObj Scm_Cadr(ScmObj obj);
SCM_EXTERN ScmObj Scm_Cdar(ScmObj obj);
SCM_EXTERN ScmObj Scm_Cddr(ScmObj obj);

SCM_EXTERN int    Scm_Length(ScmObj obj);
SCM_EXTERN ScmObj Scm_CopyList(ScmObj list);
SCM_EXTERN ScmObj Scm_MakeList(int len, ScmObj fill);
SCM_EXTERN ScmObj Scm_Append2X(ScmObj list, ScmObj obj);
SCM_EXTERN ScmObj Scm_Append2(ScmObj list, ScmObj obj);
SCM_EXTERN ScmObj Scm_Append(ScmObj args);
SCM_EXTERN ScmObj Scm_ReverseX(ScmObj list);
SCM_EXTERN ScmObj Scm_Reverse(ScmObj list);
SCM_EXTERN ScmObj Scm_ListTail(ScmObj list, int i);
SCM_EXTERN ScmObj Scm_ListRef(ScmObj list, int i, ScmObj fallback);
SCM_EXTERN ScmObj Scm_LastPair(ScmObj list);

SCM_EXTERN ScmObj Scm_Memq(ScmObj obj, ScmObj list);
SCM_EXTERN ScmObj Scm_Memv(ScmObj obj, ScmObj list);
SCM_EXTERN ScmObj Scm_Member(ScmObj obj, ScmObj list, int cmpmode);
SCM_EXTERN ScmObj Scm_Assq(ScmObj obj, ScmObj alist);
SCM_EXTERN ScmObj Scm_Assv(ScmObj obj, ScmObj alist);
SCM_EXTERN ScmObj Scm_Assoc(ScmObj obj, ScmObj alist, int cmpmode);

SCM_EXTERN ScmObj Scm_Delete(ScmObj obj, ScmObj list, int cmpmode);
SCM_EXTERN ScmObj Scm_DeleteX(ScmObj obj, ScmObj list, int cmpmode);
SCM_EXTERN ScmObj Scm_AssocDelete(ScmObj elt, ScmObj alist, int cmpmode);
SCM_EXTERN ScmObj Scm_AssocDeleteX(ScmObj elt, ScmObj alist, int cmpmode);

SCM_EXTERN ScmObj Scm_DeleteDuplicates(ScmObj list, int cmpmode);
SCM_EXTERN ScmObj Scm_DeleteDuplicatesX(ScmObj list, int cmpmode);

SCM_EXTERN ScmObj Scm_MonotonicMerge(ScmObj start, ScmObj sequences);
SCM_EXTERN ScmObj Scm_Union(ScmObj list1, ScmObj list2);
SCM_EXTERN ScmObj Scm_Intersection(ScmObj list1, ScmObj list2);

SCM_EXTERN ScmObj Scm_ExtendedCons(ScmObj car, ScmObj cdr);
SCM_EXTERN ScmObj Scm_PairAttr(ScmPair *pair);
SCM_EXTERN ScmObj Scm_PairAttrGet(ScmPair *pair, ScmObj key, ScmObj fallback);
SCM_EXTERN ScmObj Scm_PairAttrSet(ScmPair *pair, ScmObj key, ScmObj value);

SCM_EXTERN ScmObj Scm_NullP(ScmObj obj);
SCM_EXTERN ScmObj Scm_ListP(ScmObj obj);

/*--------------------------------------------------------
 * CHAR and CHAR-SET
 */

SCM_EXTERN ScmChar Scm_ReadXdigitsFromString(const char *, int, const char **);
SCM_EXTERN ScmChar Scm_ReadXdigitsFromPort(ScmPort *port, int ndigits,
                                           char *buf, int *nread);

#define SCM_CHARSET_MASK_CHARS 128
#define SCM_CHARSET_MASK_SIZE  (SCM_CHARSET_MASK_CHARS/(SIZEOF_LONG*8))

struct ScmCharSetRec {
    SCM_HEADER;
    unsigned long mask[SCM_CHARSET_MASK_SIZE];
    struct ScmCharSetRange {
        struct ScmCharSetRange *next;
        ScmChar lo;             /* lower boundary of range (inclusive) */
        ScmChar hi;             /* higher boundary of range (inclusive) */
    } *ranges;
};

SCM_CLASS_DECL(Scm_CharSetClass);
#define SCM_CLASS_CHARSET  (&Scm_CharSetClass)
#define SCM_CHARSET(obj)   ((ScmCharSet*)obj)
#define SCM_CHARSETP(obj)  SCM_XTYPEP(obj, SCM_CLASS_CHARSET)

#define SCM_CHARSET_SMALLP(obj)  (SCM_CHARSET(obj)->ranges == NULL)

SCM_EXTERN ScmObj Scm_MakeEmptyCharSet(void);
SCM_EXTERN ScmObj Scm_CopyCharSet(ScmCharSet *src);
SCM_EXTERN int    Scm_CharSetEq(ScmCharSet *x, ScmCharSet *y);
SCM_EXTERN int    Scm_CharSetLE(ScmCharSet *x, ScmCharSet *y);
SCM_EXTERN ScmObj Scm_CharSetAddRange(ScmCharSet *cs,
				      ScmChar from, ScmChar to);
SCM_EXTERN ScmObj Scm_CharSetAdd(ScmCharSet *dest, ScmCharSet *src);
SCM_EXTERN ScmObj Scm_CharSetComplement(ScmCharSet *cs);
SCM_EXTERN ScmObj Scm_CharSetCaseFold(ScmCharSet *cs);
SCM_EXTERN ScmObj Scm_CharSetRanges(ScmCharSet *cs);
SCM_EXTERN ScmObj Scm_CharSetRead(ScmPort *input, int *complement_p,
				  int error_p, int bracket_syntax);

SCM_EXTERN int    Scm_CharSetContains(ScmCharSet *cs, ScmChar c);

/* predefined character set API */
enum {
    SCM_CHARSET_ALNUM,
    SCM_CHARSET_ALPHA,
    SCM_CHARSET_BLANK,
    SCM_CHARSET_CNTRL,
    SCM_CHARSET_DIGIT,
    SCM_CHARSET_GRAPH,
    SCM_CHARSET_LOWER,
    SCM_CHARSET_PRINT,
    SCM_CHARSET_PUNCT,
    SCM_CHARSET_SPACE,
    SCM_CHARSET_UPPER,
    SCM_CHARSET_XDIGIT,
    SCM_CHARSET_WORD,           /* internal use: word constituent char. */
    SCM_CHARSET_NUM_PREDEFINED_SETS
};
SCM_EXTERN ScmObj Scm_GetStandardCharSet(int id);
    
/*--------------------------------------------------------
 * STRING
 */

/* NB: Conceptually, object immutablility is not specific for strings,
 * so the immutable flag has to be in SCM_HEADER or somewhere else.
 * In practical situations, however, what ususally matters is string
 * immutability (like the return value of symbol->string).  So I keep
 * string specific immutable flag.
 */

struct ScmStringRec {
    SCM_HEADER;
    unsigned int incomplete : 1;
    unsigned int immutable : 1;
    unsigned int length : (SIZEOF_INT*CHAR_BIT-2);
    unsigned int size;
    const char *start;
};

#define SCM_STRINGP(obj)        SCM_XTYPEP(obj, SCM_CLASS_STRING)
#define SCM_STRING(obj)         ((ScmString*)(obj))
#define SCM_STRING_LENGTH(obj)  (SCM_STRING(obj)->length)
#define SCM_STRING_SIZE(obj)    (SCM_STRING(obj)->size)
#define SCM_STRING_START(obj)   ((unsigned char *)SCM_STRING(obj)->start)

#define SCM_STRING_INCOMPLETE_P(obj) (SCM_STRING(obj)->incomplete)
#define SCM_STRING_IMMUTABLE_P(obj)  (SCM_STRING(obj)->immutable)
#define SCM_STRING_SINGLE_BYTE_P(obj) \
    (SCM_STRING_SIZE(obj)==SCM_STRING_LENGTH(obj))

/* constructor flags */
#define SCM_MAKSTR_COPYING     (1L<<0)
#define SCM_MAKSTR_INCOMPLETE  (1L<<1)
#define SCM_MAKSTR_IMMUTABLE   (1L<<2)

#define SCM_MAKE_STR(cstr) \
    Scm_MakeString(cstr, -1, -1, 0)
#define SCM_MAKE_STR_COPYING(cstr) \
    Scm_MakeString(cstr, -1, -1, SCM_MAKSTR_COPYING)
#define SCM_MAKE_STR_IMMUTABLE(cstr) \
    Scm_MakeString(cstr, -1, -1, SCM_MAKSTR_IMMUTABLE)

SCM_CLASS_DECL(Scm_StringClass);
#define SCM_CLASS_STRING        (&Scm_StringClass)

/* grammer spec for StringJoin (see SRFI-13) */
enum {
    SCM_STRING_JOIN_INFIX,
    SCM_STRING_JOIN_STRICT_INFIX,
    SCM_STRING_JOIN_SUFFIX,
    SCM_STRING_JOIN_PREFIX
};

SCM_EXTERN int     Scm_MBLen(const char *str, const char *stop);

SCM_EXTERN ScmObj  Scm_MakeString(const char *str, int size, int len,
				  int flags);
SCM_EXTERN ScmObj  Scm_MakeFillString(int len, ScmChar fill);
SCM_EXTERN ScmObj  Scm_CopyString(ScmString *str);

SCM_EXTERN char*   Scm_GetString(ScmString *str);
SCM_EXTERN const char* Scm_GetStringConst(ScmString *str);

SCM_EXTERN ScmObj  Scm_StringMakeImmutable(ScmString *str);
SCM_EXTERN ScmObj  Scm_StringCompleteToIncompleteX(ScmString *str);
SCM_EXTERN ScmObj  Scm_StringIncompleteToCompleteX(ScmString *str);
SCM_EXTERN ScmObj  Scm_StringCompleteToIncomplete(ScmString *str);
SCM_EXTERN ScmObj  Scm_StringIncompleteToComplete(ScmString *str);

SCM_EXTERN int     Scm_StringEqual(ScmString *x, ScmString *y);
SCM_EXTERN int     Scm_StringCmp(ScmString *x, ScmString *y);
SCM_EXTERN int     Scm_StringCiCmp(ScmString *x, ScmString *y);

SCM_EXTERN const char *Scm_StringPosition(ScmString *str, int k);
SCM_EXTERN ScmChar Scm_StringRef(ScmString *str, int k);
SCM_EXTERN ScmObj  Scm_StringSet(ScmString *str, int k, ScmChar sc);
SCM_EXTERN int     Scm_StringByteRef(ScmString *str, int k);
SCM_EXTERN ScmObj  Scm_StringByteSet(ScmString *str, int k, ScmByte b);
SCM_EXTERN ScmObj  Scm_StringSubstitute(ScmString *target, int start,
					ScmString *str);

SCM_EXTERN ScmObj  Scm_Substring(ScmString *x, int start, int end);
SCM_EXTERN ScmObj  Scm_MaybeSubstring(ScmString *x, ScmObj start, ScmObj end);
SCM_EXTERN ScmObj  Scm_StringTake(ScmString *x, int nchars, int takefirst,
				  int fromright);

SCM_EXTERN ScmObj  Scm_StringAppend2(ScmString *x, ScmString *y);
SCM_EXTERN ScmObj  Scm_StringAppendC(ScmString *x, const char *s, int size,
				     int len);
SCM_EXTERN ScmObj  Scm_StringAppend(ScmObj strs);
SCM_EXTERN ScmObj  Scm_StringJoin(ScmObj strs, ScmString *delim, int grammer);

SCM_EXTERN ScmObj  Scm_StringSplitByChar(ScmString *str, ScmChar ch);
SCM_EXTERN ScmObj  Scm_StringScan(ScmString *s1, ScmString *s2, int retmode);
SCM_EXTERN ScmObj  Scm_StringScanChar(ScmString *s1, ScmChar ch, int retmode);

/* "retmode" argument for string scan */
enum {
    SCM_STRING_SCAN_INDEX,      /* return index */
    SCM_STRING_SCAN_BEFORE,     /* return substring of s1 before s2 */
    SCM_STRING_SCAN_AFTER,      /* return substring of s1 after s2 */
    SCM_STRING_SCAN_BEFORE2,    /* return substr of s1 before s2 and rest */
    SCM_STRING_SCAN_AFTER2,     /* return substr of s1 up to s2 and rest */
    SCM_STRING_SCAN_BOTH        /* return substr of s1 before and after s2 */
};

SCM_EXTERN ScmObj  Scm_StringP(ScmObj obj);
SCM_EXTERN ScmObj  Scm_StringToList(ScmString *str);
SCM_EXTERN ScmObj  Scm_ListToString(ScmObj chars);
SCM_EXTERN ScmObj  Scm_StringFill(ScmString *str, ScmChar c,
				  ScmObj maybeStart, ScmObj maybeEnd);

SCM_EXTERN ScmObj Scm_ConstCStringArrayToList(const char **array, int size);
SCM_EXTERN ScmObj Scm_CStringArrayToList(char **array, int size);

/* You can allocate a constant string statically, if you calculate
   the length by yourself. */
#define SCM_DEFINE_STRING_CONST(name, str, len, siz)            \
    ScmString name = {                                          \
        { SCM_CLASS2TAG(SCM_CLASS_STRING) }, 0, 1,              \
        (len), (siz), (str)                                     \
    }

/* Auxiliary structure to construct a string of unknown length.
   This is not an ScmObj.   See string.c for details. */
#define SCM_DSTRING_INIT_CHUNK_SIZE 32

typedef struct ScmDStringChunkRec {
    int bytes;                  /* actual bytes stored in this chunk.
                                   Note that this is set when the next
                                   chunk is allocated. */
    char data[SCM_DSTRING_INIT_CHUNK_SIZE]; /* variable length, indeed. */
} ScmDStringChunk;

typedef struct ScmDStringChainRec {
    struct ScmDStringChainRec *next;
    ScmDStringChunk *chunk;
} ScmDStringChain;

struct ScmDStringRec {
    ScmDStringChunk init;       /* initial chunk */
    ScmDStringChain *anchor;    /* chain of extra chunks */
    ScmDStringChain *tail;      /* current chunk */
    char *current;              /* current ptr */
    char *end;                  /* end of current chunk */
    int lastChunkSize;          /* size of the last chunk */
    int length;                 /* # of chars written */
};

SCM_EXTERN void        Scm_DStringInit(ScmDString *dstr);
SCM_EXTERN int         Scm_DStringSize(ScmDString *dstr);
SCM_EXTERN ScmObj      Scm_DStringGet(ScmDString *dstr);
SCM_EXTERN const char *Scm_DStringGetz(ScmDString *dstr);
SCM_EXTERN void        Scm_DStringPutz(ScmDString *dstr, const char *str,
				       int siz);
SCM_EXTERN void        Scm_DStringAdd(ScmDString *dstr, ScmString *str);
SCM_EXTERN void        Scm_DStringPutb(ScmDString *dstr, char byte);
SCM_EXTERN void        Scm_DStringPutc(ScmDString *dstr, ScmChar ch);

#define SCM_DSTRING_SIZE(dstr)    Scm_DStringSize(dstr);

#define SCM_DSTRING_PUTB(dstr, byte)                                     \
    do {                                                                 \
        if ((dstr)->current >= (dstr)->end) Scm__DStringRealloc(dstr, 1);\
        *(dstr)->current++ = (char)(byte);                               \
        (dstr)->length = -1;    /* may be incomplete */                  \
    } while (0)

#define SCM_DSTRING_PUTC(dstr, ch)                      \
    do {                                                \
        ScmChar ch_DSTR = (ch);                         \
        ScmDString *d_DSTR = (dstr);                    \
        int siz_DSTR = SCM_CHAR_NBYTES(ch_DSTR);        \
        if (d_DSTR->current + siz_DSTR > d_DSTR->end)   \
            Scm__DStringRealloc(d_DSTR, siz_DSTR);      \
        SCM_CHAR_PUT(d_DSTR->current, ch_DSTR);         \
        d_DSTR->current += siz_DSTR;                    \
        if (d_DSTR->length >= 0) d_DSTR->length++;      \
    } while (0)

SCM_EXTERN void Scm__DStringRealloc(ScmDString *dstr, int min_incr);

/* Efficient way to access string from Scheme */
typedef struct ScmStringPointerRec {
    SCM_HEADER;
    int length;
    int size;
    const char *start;
    int index;
    const char *current;
} ScmStringPointer;

SCM_CLASS_DECL(Scm_StringPointerClass);
#define SCM_CLASS_STRING_POINTER  (&Scm_StringPointerClass)
#define SCM_STRING_POINTERP(obj)  SCM_XTYPEP(obj, SCM_CLASS_STRING_POINTER)
#define SCM_STRING_POINTER(obj)   ((ScmStringPointer*)obj)

SCM_EXTERN ScmObj Scm_MakeStringPointer(ScmString *src, int index,
					int start, int end);
SCM_EXTERN ScmObj Scm_StringPointerRef(ScmStringPointer *sp);
SCM_EXTERN ScmObj Scm_StringPointerNext(ScmStringPointer *sp);
SCM_EXTERN ScmObj Scm_StringPointerPrev(ScmStringPointer *sp);
SCM_EXTERN ScmObj Scm_StringPointerSet(ScmStringPointer *sp, int index);
SCM_EXTERN ScmObj Scm_StringPointerSubstring(ScmStringPointer *sp, int beforep);
SCM_EXTERN ScmObj Scm_StringPointerCopy(ScmStringPointer *sp);

#ifdef SCM_DEBUG_HELPER
SCM_EXTERN void   Scm_StringPointerDump(ScmStringPointer *sp);
#endif

/*--------------------------------------------------------
 * VECTOR
 */

struct ScmVectorRec {
    SCM_HEADER;
    int size;
    ScmObj elements[1];
};

#define SCM_VECTOR(obj)          ((ScmVector*)(obj))
#define SCM_VECTORP(obj)         SCM_XTYPEP(obj, SCM_CLASS_VECTOR)
#define SCM_VECTOR_SIZE(obj)     (SCM_VECTOR(obj)->size)
#define SCM_VECTOR_ELEMENTS(obj) (SCM_VECTOR(obj)->elements)
#define SCM_VECTOR_ELEMENT(obj, i)   (SCM_VECTOR(obj)->elements[i])

SCM_CLASS_DECL(Scm_VectorClass);
#define SCM_CLASS_VECTOR     (&Scm_VectorClass)

/* Utility to check start/end range in string and vector operation */
#define SCM_CHECK_START_END(start, end, len)                            \
    do {                                                                \
        if ((start) < 0 || (start) > (len)) {                           \
            Scm_Error("start argument out of range: %d\n", (start));    \
        }                                                               \
        if ((end) < 0) (end) = (len);                                   \
        else if ((end) > (len)) {                                       \
            Scm_Error("end argument out of range: %d\n", (end));        \
        } else if ((end) < (start)) {                                   \
            Scm_Error("end argument (%d) must be greater than or "      \
                      "equal to the start argument (%d)",               \
                      (end), (start));                                  \
        }                                                               \
    } while (0)

SCM_EXTERN ScmObj Scm_MakeVector(int size, ScmObj fill);
SCM_EXTERN ScmObj Scm_VectorRef(ScmVector *vec, int i, ScmObj fallback);
SCM_EXTERN ScmObj Scm_VectorSet(ScmVector *vec, int i, ScmObj obj);
SCM_EXTERN ScmObj Scm_VectorFill(ScmVector *vec, ScmObj fill, int start, int end);

SCM_EXTERN ScmObj Scm_ListToVector(ScmObj l);
SCM_EXTERN ScmObj Scm_VectorToList(ScmVector *v, int start, int end);
SCM_EXTERN ScmObj Scm_VectorCopy(ScmVector *vec, int start, int end);

#define SCM_VECTOR_FOR_EACH(cnt, obj, vec)           \
    for (cnt = 0, obj = SCM_VECTOR_ELEMENT(vec, 0);  \
         cnt < SCM_VECTOR_SIZE(vec);                 \
         obj = SCM_VECTOR_ELEMENT(vec, ++cnt)) 

/*--------------------------------------------------------
 * WEAK VECTOR
 */

typedef struct ScmWeakVectorRec {
    SCM_HEADER;
    int size;
    void *pointers;  /* opaque */
} ScmWeakVector;

#define SCM_WEAKVECTOR(obj)   ((ScmWeakVector*)(obj))
#define SCM_WEAKVECTORP(obj)  SCM_XTYPEP(obj, SCM_CLASS_WEAKVECTOR)
SCM_CLASS_DECL(Scm_WeakVectorClass);
#define SCM_CLASS_WEAKVECTOR  (&Scm_WeakVectorClass)
    
SCM_EXTERN ScmObj Scm_MakeWeakVector(int size);
SCM_EXTERN ScmObj Scm_WeakVectorRef(ScmWeakVector *v, int index, ScmObj fallback);
SCM_EXTERN ScmObj Scm_WeakVectorSet(ScmWeakVector *v, int index, ScmObj val);

/*--------------------------------------------------------
 * PORT
 */

/* Port is the Scheme way of I/O abstraction.  R5RS's definition of
 * of the port is very simple and straightforward.   Practical
 * applications, however, require far more detailed control over
 * the I/O channel, as well as the reasonable performance.
 *
 * Current implementation is a bit messy, trying to achieve both
 * performance and feature requirements.  In the core API level,
 * ports are categorized in one of three types: file ports, string
 * ports and procedural ports.   A port may be an input port or
 * an output port.   A port may handle byte (binary) streams, as
 * well as character streams.  Some port may interchange byte (binary)
 * I/O versus character I/O, while some may signal an error if you
 * mix those operations.
 *
 * You shouldn't rely on the underlying port implementation, for
 * it is likely to be changed in future.  There are enough macros
 * and API functions provided to use and extend the port mechanism.
 * See also ext/vport for the way to extend the port from Scheme.
 */

/* Substructures */

/* The alternative of FILE* structure, used by buffered (file) port.
   The members are owned by the port, and client shouldn't change the
   elements.  You can create your own custom buffered port by using
   Scm_MakeBufferedPort() --- with it, you pass ScmPortBuffer with
   the function pointers filled in, which is copied to the port's
   internal ScmPortBuffer structure.
   See port.c for the details of function pointers. */
   
typedef struct ScmPortBufferRec {
    char *buffer;       /* ptr to the buffer area */
    char *current;      /* current buffer position */
    char *end;          /* the end of the current valid data */
    int  size;          /* buffer size */
    int  mode;          /* buffering mode (ScmPortBufferMode) */
    int  (*filler)(ScmPort *p, int min);
    int  (*flusher)(ScmPort *p, int cnt, int forcep);
    void (*closer)(ScmPort *p);
    int  (*ready)(ScmPort *p);
    int  (*filenum)(ScmPort *p);
    off_t (*seeker)(ScmPort *p, off_t offset, int whence);
    void *data;
} ScmPortBuffer;

/* For input buffered port, returns the size of room that can be filled
   by the filler */
#define SCM_PORT_BUFFER_ROOM(p) \
    (int)((p)->src.buf.buffer+(p)->src.buf.size-(p)->src.buf.end)

/* For output buffered port, returns the size of available data that can
   be flushed by the flusher */
#define SCM_PORT_BUFFER_AVAIL(p) \
    (int)((p)->src.buf.current-(p)->src.buf.buffer)

/* The funtion table of procedural port. */

typedef struct ScmPortVTableRec {
    int       (*Getb)(ScmPort *p);
    int       (*Getc)(ScmPort *p);
    int       (*Getz)(char *buf, int buflen, ScmPort *p);
    int       (*Ready)(ScmPort *p, int charp);
    void      (*Putb)(ScmByte b, ScmPort *p);
    void      (*Putc)(ScmChar c, ScmPort *p);
    void      (*Putz)(const char *buf, int size, ScmPort *p);
    void      (*Puts)(ScmString *s, ScmPort *p);
    void      (*Flush)(ScmPort *p);
    void      (*Close)(ScmPort *p);
    off_t     (*Seek)(ScmPort *p, off_t off, int whence);
    void      *data;
} ScmPortVTable;

/* The main port structure.
 * Regardless of the port type, the port structure caches at most
 * one character, in order to realize `peek-char' (Scheme) or `Ungetc' (C)
 * operation.   'scratch', 'scrcnt', and 'ungotten' fields are used for
 * that purpose, and outside routine shouldn't touch these fields.
 * See portapi.c for the detailed semantics. 
 */

struct ScmPortRec {
    SCM_INSTANCE_HEADER;
    unsigned int direction : 2; /* SCM_PORT_INPUT or SCM_PORT_OUTPUT.
                                   There may be I/O port in future. */
    unsigned int type      : 2; /* SCM_PORT_{FILE|ISTR|OSTR|PROC} */
    unsigned int scrcnt    : 3; /* # of bytes in the scratch buffer */

    unsigned int ownerp    : 1; /* TRUE if this port owns underlying
                                   file pointer */
    unsigned int closed    : 1; /* TRUE if this port is closed */
    unsigned int error     : 1; /* Error has been occurred */

    unsigned int flags     : 5; /* see ScmPortFlags below */
    
    char scratch[SCM_CHAR_MAX_BYTES]; /* incomplete buffer */

    ScmChar ungotten;           /* ungotten character.
                                   SCM_CHAR_INVALID if empty. */
    ScmObj name;                /* port's name.  Can be any Scheme object. */

    ScmInternalMutex mutex;     /* for port mutex */
    ScmInternalCond  cv;        /* for port mutex */
    ScmVM *lockOwner;           /* for port mutex; owner of the lock */
    int lockCount;              /* for port mutex; # of recursive locks */

    ScmObj data;                /* used internally */

    unsigned int line;          /* line counter */

    union {
        ScmPortBuffer buf;      /* buffered port */
        struct {
            const char *start;
            const char *current;
            const char *end;
        } istr;                 /* input string port */
        ScmDString ostr;        /* output string port */
        ScmPortVTable vt;       /* virtual port */
    } src;
};

/* Port direction.  Bidirectional port is not supported yet. */
enum ScmPortDirection {
    SCM_PORT_INPUT = 1,
    SCM_PORT_OUTPUT = 2
};

/* Port types.  The type is also represented by a port's class, but
   C routine can dispatch quicker using these flags.  */
enum ScmPortType {
    SCM_PORT_FILE,              /* file (buffered) port */
    SCM_PORT_ISTR,              /* input string port */
    SCM_PORT_OSTR,              /* output string port */
    SCM_PORT_PROC               /* virtual port */
};

/* Port buffering mode */
enum ScmPortBufferMode {
    SCM_PORT_BUFFER_FULL,       /* full buffering */
    SCM_PORT_BUFFER_LINE,       /* flush the buffer for each line */
    SCM_PORT_BUFFER_NONE        /* flush the buffer for every output */
};

/* Return value from Scm_FdReady */
enum ScmFdReadyResult {
    SCM_FD_WOULDBLOCK,
    SCM_FD_READY,
    SCM_FD_UNKNOWN
};

/* Other flags used internally */
enum ScmPortFlags {
    SCM_PORT_WRITESS = (1L<<0), /* write/ss on by default? */
    SCM_PORT_WALKING = (1L<<1), /* this port is a special port only used in
                                   the 'walk' phase of write/ss. */
    SCM_PORT_PRIVATE = (1L<<2)  /* this port is for 'private' use within
                                   a thread, so never need to be locked. */
};

#if 0 /* not implemented */
/* Incomplete character handling policy.
   When Scm_Getc encounters a byte sequence that doesn't consist a valid
   multibyte character, it may take one of the following actions,
   according to the port's icpolicy field. */
enum ScmPortICPolicy {
    SCM_PORT_IC_ERROR,          /* signal an error */
    SCM_PORT_IC_IGNORE,         /* ignore bytes until Getc finds a
                                   valid multibyte character */
    SCM_PORT_IC_REPLACE,        /* replace invalid byte to a designated
                                   character. */
};
#endif

/* Predicates & accessors */
#define SCM_PORTP(obj)          (SCM_ISA(obj, SCM_CLASS_PORT))

#define SCM_PORT(obj)           ((ScmPort *)(obj))
#define SCM_PORT_TYPE(obj)      (SCM_PORT(obj)->type)
#define SCM_PORT_DIR(obj)       (SCM_PORT(obj)->direction)
#define SCM_PORT_FLAGS(obj)     (SCM_PORT(obj)->flags)
#define SCM_PORT_ICPOLICY(obj)  (SCM_PORT(obj)->icpolicy)

#define SCM_PORT_CLOSED_P(obj)  (SCM_PORT(obj)->closed)
#define SCM_PORT_OWNER_P(obj)   (SCM_PORT(obj)->ownerp)
#define SCM_PORT_ERROR_OCCURRED_P(obj) (SCM_PORT(obj)->error)

#define SCM_IPORTP(obj)  (SCM_PORTP(obj)&&(SCM_PORT_DIR(obj)&SCM_PORT_INPUT))
#define SCM_OPORTP(obj)  (SCM_PORTP(obj)&&(SCM_PORT_DIR(obj)&SCM_PORT_OUTPUT))

SCM_CLASS_DECL(Scm_PortClass);
#define SCM_CLASS_PORT      (&Scm_PortClass)

SCM_CLASS_DECL(Scm_CodingAwarePortClass);
#define SCM_CLASS_CODING_AWARE_PORT (&Scm_CodingAwarePortClass)

SCM_EXTERN ScmObj Scm_Stdin(void);
SCM_EXTERN ScmObj Scm_Stdout(void);
SCM_EXTERN ScmObj Scm_Stderr(void);

SCM_EXTERN ScmObj Scm_GetBufferingMode(ScmPort *port);
SCM_EXTERN int    Scm_BufferingMode(ScmObj flag, int direction, int fallback);

SCM_EXTERN ScmObj Scm_OpenFilePort(const char *path, int flags,
                                   int buffering, int perm);

SCM_EXTERN void   Scm_FlushAllPorts(int exitting);

SCM_EXTERN ScmObj Scm_MakeInputStringPort(ScmString *str, int privatep);
SCM_EXTERN ScmObj Scm_MakeOutputStringPort(int privatep);
SCM_EXTERN ScmObj Scm_GetOutputString(ScmPort *port);
SCM_EXTERN ScmObj Scm_GetOutputStringUnsafe(ScmPort *port);
SCM_EXTERN ScmObj Scm_GetRemainingInputString(ScmPort *port);

SCM_EXTERN ScmObj Scm_MakeVirtualPort(ScmClass *klass,
                                      int direction,
				      ScmPortVTable *vtable);
SCM_EXTERN ScmObj Scm_MakeBufferedPort(ScmClass *klass,
                                       ScmObj name, int direction,
                                       int ownerp,
                                       ScmPortBuffer *bufrec);
SCM_EXTERN ScmObj Scm_MakePortWithFd(ScmObj name,
				     int direction,
				     int fd,
				     int bufmode,
				     int ownerp);
SCM_EXTERN ScmObj Scm_MakeCodingAwarePort(ScmPort *iport);

SCM_EXTERN ScmObj Scm_PortName(ScmPort *port);
SCM_EXTERN int    Scm_PortLine(ScmPort *port);
SCM_EXTERN ScmObj Scm_PortSeek(ScmPort *port, ScmObj off, int whence);
SCM_EXTERN ScmObj Scm_PortSeekUnsafe(ScmPort *port, ScmObj off, int whence);
SCM_EXTERN int    Scm_PortFileNo(ScmPort *port);
SCM_EXTERN int    Scm_FdReady(int fd, int dir);
SCM_EXTERN int    Scm_ByteReady(ScmPort *port);
SCM_EXTERN int    Scm_ByteReadyUnsafe(ScmPort *port);
SCM_EXTERN int    Scm_CharReady(ScmPort *port);
SCM_EXTERN int    Scm_CharReadyUnsafe(ScmPort *port);

SCM_EXTERN void   Scm_ClosePort(ScmPort *port);

SCM_EXTERN ScmObj Scm_VMWithPortLocking(ScmPort *port,
                                        ScmObj closure);

SCM_EXTERN void Scm_Putb(ScmByte b, ScmPort *port);
SCM_EXTERN void Scm_Putc(ScmChar c, ScmPort *port);
SCM_EXTERN void Scm_Puts(ScmString *s, ScmPort *port);
SCM_EXTERN void Scm_Putz(const char *s, int len, ScmPort *port);
SCM_EXTERN void Scm_Flush(ScmPort *port);

SCM_EXTERN void Scm_PutbUnsafe(ScmByte b, ScmPort *port);
SCM_EXTERN void Scm_PutcUnsafe(ScmChar c, ScmPort *port);
SCM_EXTERN void Scm_PutsUnsafe(ScmString *s, ScmPort *port);
SCM_EXTERN void Scm_PutzUnsafe(const char *s, int len, ScmPort *port);
SCM_EXTERN void Scm_FlushUnsafe(ScmPort *port);

SCM_EXTERN void Scm_Ungetc(ScmChar ch, ScmPort *port);
SCM_EXTERN void Scm_Ungetb(int b, ScmPort *port);
SCM_EXTERN int Scm_Getb(ScmPort *port);
SCM_EXTERN int Scm_Getc(ScmPort *port);
SCM_EXTERN int Scm_Getz(char *buf, int buflen, ScmPort *port);
SCM_EXTERN ScmChar Scm_Peekc(ScmPort *port);
SCM_EXTERN int     Scm_Peekb(ScmPort *port);

SCM_EXTERN void Scm_UngetcUnsafe(ScmChar ch, ScmPort *port);
SCM_EXTERN void Scm_UngetbUnsafe(int b, ScmPort *port);
SCM_EXTERN int Scm_GetbUnsafe(ScmPort *port);
SCM_EXTERN int Scm_GetcUnsafe(ScmPort *port);
SCM_EXTERN int Scm_GetzUnsafe(char *buf, int buflen, ScmPort *port);
SCM_EXTERN ScmChar Scm_PeekcUnsafe(ScmPort *port);
SCM_EXTERN int     Scm_PeekbUnsafe(ScmPort *port);

SCM_EXTERN ScmObj Scm_ReadLine(ScmPort *port);
SCM_EXTERN ScmObj Scm_ReadLineUnsafe(ScmPort *port);

SCM_EXTERN ScmObj Scm_WithPort(ScmPort *port[], ScmObj thunk,
			       int mask, int closep);
#define SCM_PORT_CURIN  (1<<0)
#define SCM_PORT_CUROUT (1<<1)
#define SCM_PORT_CURERR (1<<2)

#define SCM_CURIN    SCM_VM_CURRENT_INPUT_PORT(Scm_VM())
#define SCM_CUROUT   SCM_VM_CURRENT_OUTPUT_PORT(Scm_VM())
#define SCM_CURERR   SCM_VM_CURRENT_ERROR_PORT(Scm_VM())

#define SCM_PUTB(b, p)     Scm_Putb(b, SCM_PORT(p))
#define SCM_PUTC(c, p)     Scm_Putc(c, SCM_PORT(p))
#define SCM_PUTZ(s, l, p)  Scm_Putz(s, l, SCM_PORT(p))
#define SCM_PUTS(s, p)     Scm_Puts(SCM_STRING(s), SCM_PORT(p))
#define SCM_FLUSH(p)       Scm_Flush(SCM_PORT(p))
#define SCM_PUTNL(p)       SCM_PUTC('\n', p)

#define SCM_UNGETC(c, port) Scm_Ungetc(c, SCM_PORT(port))
#define SCM_GETB(b, p)     (b = Scm_Getb(SCM_PORT(p)))
#define SCM_GETC(c, p)     (c = Scm_Getc(SCM_PORT(p)))

/*--------------------------------------------------------
 * WRITE
 */

struct ScmWriteContextRec {
    short mode;                 /* print mode */
    short flags;                /* internal */
    int limit;                  /* internal */
    int ncirc;                  /* internal */
    ScmHashTable *table;        /* internal */
    ScmObj obj;                 /* internal */
};

/* Print mode flags */
enum {
    SCM_WRITE_WRITE = 0,        /* write mode   */
    SCM_WRITE_DISPLAY = 1,      /* display mode */
    SCM_WRITE_SHARED = 2,       /* write/ss mode   */
    SCM_WRITE_WALK = 3,         /* this is a special mode in write/ss */
    SCM_WRITE_MODE_MASK = 0x3,

    SCM_WRITE_CASE_FOLD = 4,    /* case-fold mode.  need to escape capital
                                   letters. */
    SCM_WRITE_CASE_NOFOLD = 8,  /* case-sensitive mode.  no need to escape
                                   capital letters */
    SCM_WRITE_CASE_MASK = 0x0c
};

#define SCM_WRITE_MODE(ctx)   ((ctx)->mode & SCM_WRITE_MODE_MASK)
#define SCM_WRITE_CASE(ctx)   ((ctx)->mode & SCM_WRITE_CASE_MASK)

SCM_EXTERN void Scm_Write(ScmObj obj, ScmObj port, int mode);
SCM_EXTERN int Scm_WriteCircular(ScmObj obj, ScmObj port, int mode, int width);
SCM_EXTERN int Scm_WriteLimited(ScmObj obj, ScmObj port, int mode, int width);
SCM_EXTERN void Scm_Format(ScmPort *port, ScmString *fmt, ScmObj args, int ss);
SCM_EXTERN void Scm_Printf(ScmPort *port, const char *fmt, ...);
SCM_EXTERN void Scm_PrintfShared(ScmPort *port, const char *fmt, ...);
SCM_EXTERN void Scm_Vprintf(ScmPort *port, const char *fmt, va_list args,
                            int sharedp);

/*---------------------------------------------------------
 * READ
 */

typedef struct ScmReadContextRec {
    int flags;                  /* see below */
    ScmHashTable *table;        /* used internally. */
    ScmObj pending;             /* used internally. */
} ScmReadContext;

enum {
    SCM_READ_SOURCE_INFO = (1L<<0),  /* preserving souce file information */
    SCM_READ_CASE_FOLD   = (1L<<1),  /* case-fold read */
    SCM_READ_LITERAL_IMMUTABLE = (1L<<2), /* literal should be read as immutable */
    SCM_READ_RECURSIVELY = (1L<<3)   /* used internally. */
};

#define SCM_READ_CONTEXT_INIT(ctx) \
   do { (ctx)->flags = 0; } while (0)

/* An object to keep unrealized circular reference (e.g. #N=) during
 * 'read'.  It is replaced by the reference value before exitting 'read',
 * and it shouldn't leak out to the normal Scheme program, except the
 * code that handles it explicitly (like read-time constructor).
 */
typedef struct ScmReadReferenceRec {
    SCM_HEADER;
    ScmObj value;               /* realized reference.  initially UNBOUND */
} ScmReadReference;

SCM_CLASS_DECL(Scm_ReadReferenceClass);
#define SCM_CLASS_READ_REFERENCE  (&Scm_ReadReferenceClass)
#define SCM_READ_REFERENCE(obj)   ((ScmReadReference*)(obj))
#define SCM_READ_REFERENCE_P(obj) SCM_XTYPEP(obj, SCM_CLASS_READ_REFERENCE)
#define SCM_READ_REFERENCE_REALIZED(obj) \
   (!SCM_EQ(SCM_READ_REFERENCE(obj)->value, SCM_UNBOUND))

SCM_EXTERN ScmObj Scm_Read(ScmObj port);
SCM_EXTERN ScmObj Scm_ReadWithContext(ScmObj port, ScmReadContext *ctx);
SCM_EXTERN ScmObj Scm_ReadList(ScmObj port, ScmChar closer);
SCM_EXTERN ScmObj Scm_ReadListWithContext(ScmObj port, ScmChar closer,
                                          ScmReadContext *ctx);
SCM_EXTERN ScmObj Scm_ReadFromString(ScmString *string);
SCM_EXTERN ScmObj Scm_ReadFromCString(const char *string);

SCM_EXTERN void   Scm_ReadError(ScmPort *port, const char *fmt, ...);

SCM_EXTERN ScmObj Scm_DefineReaderCtor(ScmObj symbol, ScmObj proc,
                                       ScmObj finisher);
    
/*--------------------------------------------------------
 * HASHTABLE
 */

typedef struct ScmHashEntryRec ScmHashEntry;

typedef ScmHashEntry *(*ScmHashAccessProc)(ScmHashTable*,
                                           ScmObj, int, ScmObj);
typedef unsigned long (*ScmHashProc)(ScmObj);
typedef int (*ScmHashCmpProc)(ScmObj, ScmHashEntry *);

struct ScmHashTableRec {
    SCM_HEADER;
    ScmHashEntry **buckets;
    int numBuckets;
    int numEntries;
    int numBucketsLog2;
    int type;
    ScmHashAccessProc accessfn;
    ScmHashProc hashfn;
    ScmHashCmpProc cmpfn;
};

#define SCM_HASHTABLE(obj)   ((ScmHashTable*)(obj))
#define SCM_HASHTABLEP(obj)  SCM_XTYPEP(obj, SCM_CLASS_HASHTABLE)

SCM_CLASS_DECL(Scm_HashTableClass);
#define SCM_CLASS_HASHTABLE  (&Scm_HashTableClass)

#define SCM_HASH_ADDRESS   (0)  /* eq?-hash */
#define SCM_HASH_EQV       (1)
#define SCM_HASH_EQUAL     (2)
#define SCM_HASH_STRING    (3)
#define SCM_HASH_GENERAL   (4)

/* auxiliary structure; not an ScmObj. */
struct ScmHashEntryRec {
    ScmObj key;
    ScmObj value;
    struct ScmHashEntryRec *next;
};

typedef  struct ScmHashIterRec {
    ScmHashTable *table;
    int currentBucket;
    ScmHashEntry *currentEntry;
} ScmHashIter;

SCM_EXTERN ScmObj Scm_MakeHashTable(ScmHashProc hashfn,
				    ScmHashCmpProc cmpfn,
				    unsigned int initSize);
SCM_EXTERN ScmObj Scm_CopyHashTable(ScmHashTable *tab);

SCM_EXTERN ScmHashEntry *Scm_HashTableGet(ScmHashTable *hash, ScmObj key);
SCM_EXTERN ScmHashEntry *Scm_HashTableAdd(ScmHashTable *hash,
					  ScmObj key, ScmObj value);
SCM_EXTERN ScmHashEntry *Scm_HashTablePut(ScmHashTable *hash,
					  ScmObj key, ScmObj value);
SCM_EXTERN ScmHashEntry *Scm_HashTableDelete(ScmHashTable *hash, ScmObj key);
SCM_EXTERN ScmObj Scm_HashTableKeys(ScmHashTable *table);
SCM_EXTERN ScmObj Scm_HashTableValues(ScmHashTable *table);
SCM_EXTERN ScmObj Scm_HashTableStat(ScmHashTable *table);

SCM_EXTERN void Scm_HashIterInit(ScmHashTable *hash, ScmHashIter *iter);
SCM_EXTERN ScmHashEntry *Scm_HashIterNext(ScmHashIter *iter);

SCM_EXTERN unsigned long Scm_EqHash(ScmObj obj);
SCM_EXTERN unsigned long Scm_EqvHash(ScmObj obj);
SCM_EXTERN unsigned long Scm_Hash(ScmObj obj);
SCM_EXTERN unsigned long Scm_HashString(ScmString *str, unsigned long bound);

/*--------------------------------------------------------
 * MODULE
 */

struct ScmModuleRec {
    SCM_HEADER;
    ScmSymbol *name;
    ScmObj imported;
    ScmObj exported;
    ScmObj parents;             /* direct parent modules */
    ScmObj mpl;                 /* module precedence list */
    ScmHashTable *table;
};

#define SCM_MODULE(obj)       ((ScmModule*)(obj))
#define SCM_MODULEP(obj)      SCM_XTYPEP(obj, SCM_CLASS_MODULE)

SCM_CLASS_DECL(Scm_ModuleClass);
#define SCM_CLASS_MODULE     (&Scm_ModuleClass)

SCM_EXTERN ScmGloc *Scm_FindBinding(ScmModule *module, ScmSymbol *symbol,
				    int stay_in_module);
SCM_EXTERN ScmObj Scm_MakeModule(ScmSymbol *name, int error_if_exists);
SCM_EXTERN ScmObj Scm_SymbolValue(ScmModule *module, ScmSymbol *symbol);
SCM_EXTERN ScmObj Scm_Define(ScmModule *module, ScmSymbol *symbol,
			     ScmObj value);
SCM_EXTERN ScmObj Scm_DefineConst(ScmModule *module, ScmSymbol *symbol,
                                  ScmObj value);

SCM_EXTERN ScmObj Scm_ExtendModule(ScmModule *module, ScmObj supers);
SCM_EXTERN ScmObj Scm_ImportModules(ScmModule *module, ScmObj list);
SCM_EXTERN ScmObj Scm_ExportSymbols(ScmModule *module, ScmObj list);
SCM_EXTERN ScmObj Scm_ExportAll(ScmModule *module);
SCM_EXTERN ScmObj Scm_FindModule(ScmSymbol *name, int createp);
SCM_EXTERN ScmObj Scm_AllModules(void);
SCM_EXTERN void   Scm_SelectModule(ScmModule *mod);

#define SCM_FIND_MODULE(name, createp) \
    Scm_FindModule(SCM_SYMBOL(SCM_INTERN(name)), createp)

SCM_EXTERN ScmObj Scm_ModuleNameToPath(ScmSymbol *name);
SCM_EXTERN ScmObj Scm_PathToModuleName(ScmString *path);

SCM_EXTERN ScmModule *Scm_NullModule(void);
SCM_EXTERN ScmModule *Scm_SchemeModule(void);
SCM_EXTERN ScmModule *Scm_GaucheModule(void);
SCM_EXTERN ScmModule *Scm_UserModule(void);
SCM_EXTERN ScmModule *Scm_CurrentModule(void);

#define SCM_DEFINE(module, cstr, val)           \
    Scm_Define(SCM_MODULE(module),              \
               SCM_SYMBOL(SCM_INTERN(cstr)),    \
               SCM_OBJ(val))

/*--------------------------------------------------------
 * SYMBOL
 */

struct ScmSymbolRec {
    SCM_HEADER;
    ScmString *name;
};

#define SCM_SYMBOL(obj)        ((ScmSymbol*)(obj))
#define SCM_SYMBOLP(obj)       SCM_XTYPEP(obj, SCM_CLASS_SYMBOL)
#define SCM_SYMBOL_NAME(obj)   (SCM_SYMBOL(obj)->name)

SCM_EXTERN ScmObj Scm_Intern(ScmString *name);
#define SCM_INTERN(cstr)  Scm_Intern(SCM_STRING(SCM_MAKE_STR_IMMUTABLE(cstr)))
SCM_EXTERN ScmObj Scm_Gensym(ScmString *prefix);

SCM_CLASS_DECL(Scm_SymbolClass);
#define SCM_CLASS_SYMBOL       (&Scm_SymbolClass)

/* Gloc (global location) */
struct ScmGlocRec {
    SCM_HEADER;
    ScmSymbol *name;
    ScmModule *module;
    ScmObj value;
    ScmObj (*getter)(ScmGloc *);
    ScmObj (*setter)(ScmGloc *, ScmObj);
};

#define SCM_GLOC(obj)            ((ScmGloc*)(obj))
#define SCM_GLOCP(obj)           SCM_XTYPEP(obj, SCM_CLASS_GLOC)
SCM_CLASS_DECL(Scm_GlocClass);
#define SCM_CLASS_GLOC          (&Scm_GlocClass)

#define SCM_GLOC_GET(gloc) \
    ((gloc)->getter? (gloc)->getter(gloc) : (gloc)->value)
#define SCM_GLOC_SET(gloc, val) \
    ((gloc)->setter? (gloc)->setter((gloc), (val)) : ((gloc)->value = (val)))

SCM_EXTERN ScmObj Scm_MakeGloc(ScmSymbol *sym, ScmModule *module);
SCM_EXTERN ScmObj Scm_MakeConstGloc(ScmSymbol *sym, ScmModule *module);
SCM_EXTERN ScmObj Scm_GlocConstSetter(ScmGloc *g, ScmObj val);

#define SCM_GLOC_CONST_P(gloc) \
    ((gloc)->setter == Scm_GlocConstSetter)

/*--------------------------------------------------------
 * KEYWORD
 */

struct ScmKeywordRec {
    SCM_HEADER;
    ScmString *name;
};

SCM_CLASS_DECL(Scm_KeywordClass);
#define SCM_CLASS_KEYWORD       (&Scm_KeywordClass)

#define SCM_KEYWORD(obj)        ((ScmKeyword*)(obj))
#define SCM_KEYWORDP(obj)       SCM_XTYPEP(obj, SCM_CLASS_KEYWORD)
#define SCM_KEYWORD_NAME(obj)   (SCM_KEYWORD(obj)->name)

SCM_EXTERN ScmObj Scm_MakeKeyword(ScmString *name);
SCM_EXTERN ScmObj Scm_GetKeyword(ScmObj key, ScmObj list, ScmObj fallback);
SCM_EXTERN ScmObj Scm_DeleteKeyword(ScmObj key, ScmObj list);
SCM_EXTERN ScmObj Scm_DeleteKeywordX(ScmObj key, ScmObj list);

#define SCM_MAKE_KEYWORD(cstr) \
    Scm_MakeKeyword(SCM_STRING(SCM_MAKE_STR_IMMUTABLE(cstr)))
#define SCM_GET_KEYWORD(cstr, list, fallback) \
    Scm_GetKeyword(SCM_MAKE_KEYWORD(cstr), list, fallback)

/*--------------------------------------------------------
 * NUMBER
 */

/* "Normalized" numbers
 *
 * In Scheme world, numbers should be always in normalized form.
 *
 *  - Exact integers that can be representable in fixnum should be in
 *    the fixnum form, not in the bignum form.
 *  - Complex numbers whose imaginary part is 0.0 should be in the flonum
 *    form, not in the complexnum form.
 *
 * Some C API returns anormalized numbers to avoid unnecessary
 * conversion overhead.  These anormalized numbers shuold be used
 * strictly in the intermediate form within C world.  Anything that
 * is passed back to Scheme world must be normalized.
 */

#define SCM_SMALL_INT_SIZE         (SIZEOF_LONG*8-3)
#define SCM_SMALL_INT_MAX          ((1L << SCM_SMALL_INT_SIZE) - 1)
#define SCM_SMALL_INT_MIN          (-SCM_SMALL_INT_MAX-1)
#define SCM_SMALL_INT_FITS(k) \
    (((k)<=SCM_SMALL_INT_MAX)&&((k)>=SCM_SMALL_INT_MIN))

#define SCM_RADIX_MAX              36

#define SCM_INTEGERP(obj)          (SCM_INTP(obj) || SCM_BIGNUMP(obj))
#define SCM_REALP(obj)             (SCM_INTEGERP(obj)||SCM_FLONUMP(obj))
#define SCM_NUMBERP(obj)           (SCM_REALP(obj)||SCM_COMPLEXP(obj))
#define SCM_EXACTP(obj)            SCM_INTEGERP(obj)
#define SCM_INEXACTP(obj)          (SCM_FLONUMP(obj)||SCM_COMPLEXP(obj))

#define SCM_UINTEGERP(obj) \
    (SCM_UINTP(obj) || (SCM_BIGNUMP(obj)&&SCM_BIGNUM_SIGN(obj)>=0))

SCM_CLASS_DECL(Scm_NumberClass);
SCM_CLASS_DECL(Scm_ComplexClass);
SCM_CLASS_DECL(Scm_RealClass);
SCM_CLASS_DECL(Scm_IntegerClass);

#define SCM_CLASS_NUMBER        (&Scm_NumberClass)
#define SCM_CLASS_COMPLEX       (&Scm_ComplexClass)
#define SCM_CLASS_REAL          (&Scm_RealClass)
#define SCM_CLASS_INTEGER       (&Scm_IntegerClass)

struct ScmBignumRec {
    SCM_HEADER;
    int sign : 2;
    unsigned int size : (SIZEOF_INT*CHAR_BIT-2);
    unsigned long values[1];           /* variable length */
};

#define SCM_BIGNUM(obj)        ((ScmBignum*)(obj))
#define SCM_BIGNUMP(obj)       SCM_XTYPEP(obj, SCM_CLASS_INTEGER)
#define SCM_BIGNUM_SIZE(obj)   SCM_BIGNUM(obj)->size
#define SCM_BIGNUM_SIGN(obj)   SCM_BIGNUM(obj)->sign

#define SCM_BIGNUM_MAX_DIGITS  ((1UL<<(SIZEOF_INT*CHAR_BIT-2))-1)

/* Converting a Scheme number to a C number:
 *
 * It's a tricky business.  It's always possible that the Scheme number
 * you got may not fit into the desired C variable.  There are several
 * options you can choose.
 *
 *  - Error.  Throws an error.
 *  - Clamping.  If the Scheme value falls out of the supported range
 *    of C variable, use the closest representable value.
 *  - Convert only when possible.  If conversion is not possible, use
 *    the Scheme value as-is.  It is useful to provide a shortcut path
 *    to improve performance.
 *
 * Some APIs take 'clamp' argument to specify the behavior.  The value
 * can be one of the SCM_CLAMP_* enums.  If an API supports SCM_CLAMP_NONE,
 * it also takes an output argument to return a flag whether the argument
 * is out of range or not.  This output argument can be NULL if the caller 
 * doesn't specify SCM_CLAMP_NONE flag.
 */

enum {
    SCM_CLAMP_ERROR = 0,       /* throws an error when out-of-range */
    SCM_CLAMP_HI = 1,
    SCM_CLAMP_LO = 2,
    SCM_CLAMP_BOTH = 3,
    SCM_CLAMP_NONE = 4         /* do not convert when out-of-range */
};

SCM_EXTERN ScmObj Scm_MakeBignumFromSI(long val);
SCM_EXTERN ScmObj Scm_MakeBignumFromUI(u_long val);
SCM_EXTERN ScmObj Scm_MakeBignumFromUIArray(int sign, u_long *values, int size);
SCM_EXTERN ScmObj Scm_MakeBignumFromDouble(double val);
SCM_EXTERN ScmObj Scm_BignumCopy(ScmBignum *b);
SCM_EXTERN ScmObj Scm_BignumToString(ScmBignum *b, int radix, int use_upper);

SCM_EXTERN long   Scm_BignumToSI(ScmBignum *b, int clamp, int* oor);
SCM_EXTERN u_long Scm_BignumToUI(ScmBignum *b, int clamp, int* oor);
#if SIZEOF_LONG == 4
SCM_EXTERN ScmInt64  Scm_BignumToSI64(ScmBignum *b, int clamp, int *oor);
SCM_EXTERN ScmUInt64 Scm_BignumToUI64(ScmBignum *b, int clamp, int *oor);
#else  /* SIZEOF_LONG >= 8 */
#define Scm_BignumToSI64       Scm_BignumToSI
#define Scm_BignumToUI64       Scm_BignumToUI
#endif /* SIZEOF_LONG >= 8 */
SCM_EXTERN double Scm_BignumToDouble(ScmBignum *b);
SCM_EXTERN ScmObj Scm_NormalizeBignum(ScmBignum *b);
SCM_EXTERN ScmObj Scm_BignumNegate(ScmBignum *b);
SCM_EXTERN int    Scm_BignumCmp(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN int    Scm_BignumAbsCmp(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN int    Scm_BignumCmp3U(ScmBignum *bx, ScmBignum *off, ScmBignum *by);
SCM_EXTERN ScmObj Scm_BignumComplement(ScmBignum *bx);

SCM_EXTERN ScmObj Scm_BignumAdd(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN ScmObj Scm_BignumAddSI(ScmBignum *bx, long y);
SCM_EXTERN ScmObj Scm_BignumAddN(ScmBignum *bx, ScmObj args);
SCM_EXTERN ScmObj Scm_BignumSub(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN ScmObj Scm_BignumSubSI(ScmBignum *bx, long y);
SCM_EXTERN ScmObj Scm_BignumSubN(ScmBignum *bx, ScmObj args);
SCM_EXTERN ScmObj Scm_BignumMul(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN ScmObj Scm_BignumMulSI(ScmBignum *bx, long y);
SCM_EXTERN ScmObj Scm_BignumMulN(ScmBignum *bx, ScmObj args);
SCM_EXTERN ScmObj Scm_BignumDivSI(ScmBignum *bx, long y, long *r);
SCM_EXTERN ScmObj Scm_BignumDivRem(ScmBignum *bx, ScmBignum *by);

SCM_EXTERN ScmObj Scm_BignumLogAndSI(ScmBignum *bx, long y);
SCM_EXTERN ScmObj Scm_BignumLogAnd(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN ScmObj Scm_BignumLogIor(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN ScmObj Scm_BignumLogXor(ScmBignum *bx, ScmBignum *by);
SCM_EXTERN ScmObj Scm_BignumLogNot(ScmBignum *bx);
SCM_EXTERN ScmObj Scm_BignumLogBit(ScmBignum *bx, int bit);
SCM_EXTERN ScmObj Scm_BignumAsh(ScmBignum *bx, int cnt);

SCM_EXTERN ScmBignum *Scm_MakeBignumWithSize(int size, u_long init);
SCM_EXTERN ScmBignum *Scm_BignumAccMultAddUI(ScmBignum *acc, 
                                             u_long coef, u_long c);

struct ScmFlonumRec {
    SCM_HEADER;
    double value;
};

#define SCM_FLONUM(obj)            ((ScmFlonum*)(obj))
#define SCM_FLONUMP(obj)           SCM_XTYPEP(obj, SCM_CLASS_REAL)
#define SCM_FLONUM_VALUE(obj)      (SCM_FLONUM(obj)->value)

struct ScmComplexRec {
    SCM_HEADER;
    double real;
    double imag;
};

#define SCM_COMPLEX(obj)           ((ScmComplex*)(obj))
#define SCM_COMPLEXP(obj)          SCM_XTYPEP(obj, SCM_CLASS_COMPLEX)
#define SCM_COMPLEX_REAL(obj)      SCM_COMPLEX(obj)->real
#define SCM_COMPLEX_IMAG(obj)      SCM_COMPLEX(obj)->imag

SCM_EXTERN ScmObj Scm_MakeInteger(long i);
SCM_EXTERN ScmObj Scm_MakeIntegerU(u_long i);

SCM_EXTERN long   Scm_GetIntegerClamp(ScmObj obj, int clamp, int *oor);
SCM_EXTERN u_long Scm_GetIntegerUClamp(ScmObj obj, int clamp, int *oor);
#define Scm_GetInteger(x)  Scm_GetIntegerClamp(x, SCM_CLAMP_BOTH, NULL)
#define Scm_GetIntegerU(x) Scm_GetIntegerUClamp(x, SCM_CLAMP_BOTH, NULL)

SCM_EXTERN ScmInt32  Scm_GetInteger32Clamp(ScmObj obj, int clamp, int *oor);
SCM_EXTERN ScmUInt32 Scm_GetIntegerU32Clamp(ScmObj obj, int clamp, int *oor);

/* 64bit integer stuff */
#if SIZEOF_LONG == 4
SCM_EXTERN ScmObj Scm_MakeInteger64(ScmInt64 i);
SCM_EXTERN ScmObj Scm_MakeIntegerU64(ScmUInt64 i);
SCM_EXTERN ScmInt64  Scm_GetInteger64Clamp(ScmObj obj, int clamp, int *oor);
SCM_EXTERN ScmUInt64 Scm_GetIntegerU64Clamp(ScmObj obj, int clamp, int *oor);
#else  /* SIZEOF_LONG >= 8 */
#define Scm_MakeInteger64      Scm_MakeInteger
#define Scm_MakeIntegerU64     Scm_MakeIntegerU
#define Scm_GetInteger64Clamp  Scm_GetIntegerClamp
#define Scm_GetIntegerU64Clamp Scm_GetIntegerUClamp
#endif /* SIZEOF_LONG >= 8 */
#define Scm_GetInteger64(x)    Scm_GetInteger64Clamp(x, SCM_CLAMP_BOTH, NULL)
#define Scm_GetIntegerU64(x)   Scm_GetIntegerU64Clamp(x, SCM_CLAMP_BOTH, NULL)

/* for backward compatibility -- will be gone soon */
#define Scm_MakeIntegerFromUI Scm_MakeIntegerU
#define Scm_GetUInteger       Scm_GetIntegerU

SCM_EXTERN ScmObj Scm_MakeFlonum(double d);
SCM_EXTERN double Scm_GetDouble(ScmObj obj);
SCM_EXTERN ScmObj Scm_DecodeFlonum(double d, int *exp, int *sign);
SCM_EXTERN ScmObj Scm_MakeFlonumToNumber(double d, int exactp);

SCM_EXTERN ScmObj Scm_MakeComplex(double real, double imag);
SCM_EXTERN ScmObj Scm_MakeComplexPolar(double magnitude, double angle);
SCM_EXTERN ScmObj Scm_MakeComplexNormalized(double real, double imag);

SCM_EXTERN ScmObj Scm_PromoteToBignum(ScmObj obj);
SCM_EXTERN ScmObj Scm_PromoteToComplex(ScmObj obj);
SCM_EXTERN ScmObj Scm_PromoteToFlonum(ScmObj obj);

SCM_EXTERN int    Scm_IntegerP(ScmObj obj);
SCM_EXTERN int    Scm_OddP(ScmObj obj);
SCM_EXTERN ScmObj Scm_Abs(ScmObj obj);
SCM_EXTERN int    Scm_Sign(ScmObj obj);
SCM_EXTERN ScmObj Scm_Negate(ScmObj obj);
SCM_EXTERN ScmObj Scm_Reciprocal(ScmObj obj);
SCM_EXTERN ScmObj Scm_ExactToInexact(ScmObj obj);
SCM_EXTERN ScmObj Scm_InexactToExact(ScmObj obj);

SCM_EXTERN ScmObj Scm_Add(ScmObj arg1, ScmObj arg2, ScmObj args);
SCM_EXTERN ScmObj Scm_Subtract(ScmObj arg1, ScmObj arg2, ScmObj args);
SCM_EXTERN ScmObj Scm_Multiply(ScmObj arg1, ScmObj arg2, ScmObj args);
SCM_EXTERN ScmObj Scm_Divide(ScmObj arg1, ScmObj arg2, ScmObj args);

#define Scm_Add2(a, b)       Scm_Add((a), (b), SCM_NIL)
#define Scm_Subtract2(a, b)  Scm_Subtract((a), (b), SCM_NIL)
#define Scm_Multiply2(a, b)  Scm_Multiply((a), (b), SCM_NIL)
#define Scm_Divide2(a, b)    Scm_Divide((a), (b), SCM_NIL)

SCM_EXTERN ScmObj Scm_Quotient(ScmObj arg1, ScmObj arg2, ScmObj *rem);
SCM_EXTERN ScmObj Scm_Modulo(ScmObj arg1, ScmObj arg2, int remainder);

SCM_EXTERN ScmObj Scm_Expt(ScmObj x, ScmObj y);

SCM_EXTERN int    Scm_NumEq(ScmObj x, ScmObj y);
SCM_EXTERN int    Scm_NumCmp(ScmObj x, ScmObj y);
SCM_EXTERN void   Scm_MinMax(ScmObj arg0, ScmObj args, ScmObj *min, ScmObj *max);

SCM_EXTERN ScmObj Scm_LogAnd(ScmObj x, ScmObj y);
SCM_EXTERN ScmObj Scm_LogIor(ScmObj x, ScmObj y);
SCM_EXTERN ScmObj Scm_LogXor(ScmObj x, ScmObj y);
SCM_EXTERN ScmObj Scm_LogNot(ScmObj x);
SCM_EXTERN int    Scm_LogTest(ScmObj x, ScmObj y);
SCM_EXTERN int    Scm_LogBit(ScmObj x, int bit);
SCM_EXTERN ScmObj Scm_Ash(ScmObj x, int cnt);
    
enum {
    SCM_ROUND_FLOOR,
    SCM_ROUND_CEIL,
    SCM_ROUND_TRUNC,
    SCM_ROUND_ROUND
};
SCM_EXTERN ScmObj Scm_Round(ScmObj num, int mode);

SCM_EXTERN ScmObj Scm_Magnitude(ScmObj z);
SCM_EXTERN ScmObj Scm_Angle(ScmObj z);

SCM_EXTERN ScmObj Scm_NumberToString(ScmObj num, int radix, int use_upper);
SCM_EXTERN ScmObj Scm_StringToNumber(ScmString *str, int radix, int strict);

SCM_EXTERN void   Scm_PrintDouble(ScmPort *port, double d, int flags);

/*--------------------------------------------------------
 * PROCEDURE (APPLICABLE OBJECT)
 */


typedef ScmObj (*ScmTransformerProc)(ScmObj self, ScmObj form, ScmObj env,
                                     void *data);

/* Packet for inliner */
typedef struct ScmInlinerRec {
    ScmTransformerProc proc;
    void *data;
} ScmInliner;

#define SCM_DEFINE_INLINER(name, proc, data)     \
    ScmInliner name = { (proc), (data) }

/* Base structure */
struct ScmProcedureRec {
    SCM_INSTANCE_HEADER;
    unsigned char required;     /* # of required args */
    unsigned char optional;     /* 1 if it takes rest args */
    unsigned char type;         /* procedure type  */
    unsigned char locked;       /* setter locked? */
    ScmObj info;                /* source code info */
    ScmObj setter;              /* setter, if exists. */
    ScmInliner *inliner;        /* inliner */
};

/* procedure type */
enum {
    SCM_PROC_SUBR,
    SCM_PROC_CLOSURE,
    SCM_PROC_GENERIC,
    SCM_PROC_METHOD,
    SCM_PROC_NEXT_METHOD
};

#define SCM_PROCEDURE(obj)          ((ScmProcedure*)(obj))
#define SCM_PROCEDURE_REQUIRED(obj) SCM_PROCEDURE(obj)->required
#define SCM_PROCEDURE_OPTIONAL(obj) SCM_PROCEDURE(obj)->optional
#define SCM_PROCEDURE_TYPE(obj)     SCM_PROCEDURE(obj)->type
#define SCM_PROCEDURE_INFO(obj)     SCM_PROCEDURE(obj)->info
#define SCM_PROCEDURE_SETTER(obj)   SCM_PROCEDURE(obj)->setter
#define SCM_PROCEDURE_INLINER(obj)  SCM_PROCEDURE(obj)->inliner

SCM_CLASS_DECL(Scm_ProcedureClass);
#define SCM_CLASS_PROCEDURE    (&Scm_ProcedureClass)
#define SCM_PROCEDUREP(obj) \
    (SCM_HOBJP(obj) && SCM_CLASS_APPLICABLE_P(SCM_CLASS_OF(obj)))
#define SCM_PROCEDURE_TAKE_NARG_P(obj, narg) \
    (SCM_PROCEDUREP(obj)&& \
     (  (!SCM_PROCEDURE_OPTIONAL(obj)&&SCM_PROCEDURE_REQUIRED(obj)==(narg)) \
      ||(SCM_PROCEDURE_OPTIONAL(obj)&&SCM_PROCEDURE_REQUIRED(obj)<=(narg))))
#define SCM_PROCEDURE_THUNK_P(obj) \
    (SCM_PROCEDUREP(obj)&& \
     (  (!SCM_PROCEDURE_OPTIONAL(obj)&&SCM_PROCEDURE_REQUIRED(obj)==0) \
      ||(SCM_PROCEDURE_OPTIONAL(obj))))
#define SCM_PROCEDURE_INIT(obj, req, opt, typ, inf)     \
    SCM_PROCEDURE(obj)->required = req,                 \
    SCM_PROCEDURE(obj)->optional = opt,                 \
    SCM_PROCEDURE(obj)->type = typ,                     \
    SCM_PROCEDURE(obj)->info = inf,                     \
    SCM_PROCEDURE(obj)->setter = SCM_FALSE,             \
    SCM_PROCEDURE(obj)->inliner = NULL

#define SCM__PROCEDURE_INITIALIZER(klass, req, opt, typ, inf, inl)  \
    { { klass }, (req), (opt), (typ), FALSE, (inf), SCM_FALSE, (inl) }

/* Closure - Scheme defined procedure */
struct ScmClosureRec {
    ScmProcedure common;
    ScmObj code;                /* compiled code */
    ScmEnvFrame *env;           /* environment */
};

#define SCM_CLOSUREP(obj) \
    (SCM_PROCEDUREP(obj)&&(SCM_PROCEDURE_TYPE(obj)==SCM_PROC_CLOSURE))
#define SCM_CLOSURE(obj)           ((ScmClosure*)(obj))

SCM_EXTERN ScmObj Scm_MakeClosure(int required, int optional,
				  ScmObj code, ScmEnvFrame *env);

/* Subr - C defined procedure */
struct ScmSubrRec {
    ScmProcedure common;
    ScmObj (*func)(ScmObj *, int, void*);
    void *data;
};

#define SCM_SUBRP(obj) \
    (SCM_PROCEDUREP(obj)&&(SCM_PROCEDURE_TYPE(obj)==SCM_PROC_SUBR))
#define SCM_SUBR(obj)              ((ScmSubr*)(obj))
#define SCM_SUBR_FUNC(obj)         SCM_SUBR(obj)->func
#define SCM_SUBR_DATA(obj)         SCM_SUBR(obj)->data

#define SCM_DEFINE_SUBR(cvar, req, opt, inf, func, inliner, data)           \
    ScmSubr cvar = {                                                        \
        SCM__PROCEDURE_INITIALIZER(SCM_CLASS2TAG(SCM_CLASS_PROCEDURE),      \
                                   req, opt, SCM_PROC_SUBR, inf, inliner),  \
        (func), (data)                                                      \
    }

SCM_EXTERN ScmObj Scm_MakeSubr(ScmObj (*func)(ScmObj*, int, void*),
			       void *data,
			       int required, int optional,
			       ScmObj info);
SCM_EXTERN ScmObj Scm_NullProc(void);

SCM_EXTERN ScmObj Scm_SetterSet(ScmProcedure *proc, ScmProcedure *setter,
				int lock);
SCM_EXTERN ScmObj Scm_Setter(ScmObj proc);
SCM_EXTERN int    Scm_HasSetter(ScmObj proc);

/* Generic - Generic function */
struct ScmGenericRec {
    ScmProcedure common;
    ScmObj methods;
    ScmObj (*fallback)(ScmObj *args, int nargs, ScmGeneric *gf);
    void *data;
    ScmInternalMutex lock;
};

SCM_CLASS_DECL(Scm_GenericClass);
#define SCM_CLASS_GENERIC          (&Scm_GenericClass)
#define SCM_GENERICP(obj)          SCM_XTYPEP(obj, SCM_CLASS_GENERIC)
#define SCM_GENERIC(obj)           ((ScmGeneric*)obj)
#define SCM_GENERIC_DATA(obj)      (SCM_GENERIC(obj)->data)

#define SCM_DEFINE_GENERIC(cvar, cfunc, data)                           \
    ScmGeneric cvar = {                                                 \
        SCM__PROCEDURE_INITIALIZER(SCM_CLASS2TAG(SCM_CLASS_GENERIC),    \
                                   0, 0, SCM_PROC_GENERIC, SCM_FALSE,   \
                                   NULL),                               \
        SCM_NIL, cfunc, data                                            \
    }

SCM_EXTERN void Scm_InitBuiltinGeneric(ScmGeneric *gf, const char *name,
				       ScmModule *mod);
SCM_EXTERN ScmObj Scm_MakeBaseGeneric(ScmObj name,
				      ScmObj (*fallback)(ScmObj *, int, ScmGeneric*),
				      void *data);
SCM_EXTERN ScmObj Scm_NoNextMethod(ScmObj *args, int nargs, ScmGeneric *gf);
SCM_EXTERN ScmObj Scm_NoOperation(ScmObj *args, int nargs, ScmGeneric *gf);
SCM_EXTERN ScmObj Scm_InvalidApply(ScmObj *args, int nargs, ScmGeneric *gf);

/* Method - method
   A method can be defined either by C or by Scheme.  C-defined method
   have func ptr, with optional data.   Scheme-define method has NULL
   in func, code in data, and optional environment in env. */
struct ScmMethodRec {
    ScmProcedure common;
    ScmGeneric *generic;
    ScmClass **specializers;    /* array of specializers, size==required */
    ScmObj (*func)(ScmNextMethod *nm, ScmObj *args, int nargs, void * data);
    void *data;                 /* closure, or code */
    ScmEnvFrame *env;           /* environment (for Scheme created method) */
};

SCM_CLASS_DECL(Scm_MethodClass);
#define SCM_CLASS_METHOD           (&Scm_MethodClass)
#define SCM_METHODP(obj)           SCM_ISA(obj, SCM_CLASS_METHOD)
#define SCM_METHOD(obj)            ((ScmMethod*)obj)

#define SCM_DEFINE_METHOD(cvar, gf, req, opt, specs, func, data)        \
    ScmMethod cvar = {                                                  \
        SCM__PROCEDURE_INITIALIZER(SCM_CLASS2TAG(SCM_CLASS_METHOD),     \
                                   req, opt, SCM_PROC_METHOD,           \
                                   SCM_FALSE, NULL),                    \
        gf, specs, func, data, NULL                                     \
    }

SCM_EXTERN void Scm_InitBuiltinMethod(ScmMethod *m);

/* Next method object
   Next method is just another callable entity, with memoizing
   the arguments. */
struct ScmNextMethodRec {
    ScmProcedure common;
    ScmGeneric *generic;
    ScmObj methods;             /* list of applicable methods */
    ScmObj *args;               /* original arguments */
    int nargs;                  /* # of original arguments */
};

SCM_CLASS_DECL(Scm_NextMethodClass);
#define SCM_CLASS_NEXT_METHOD      (&Scm_NextMethodClass)
#define SCM_NEXT_METHODP(obj)      SCM_XTYPEP(obj, SCM_CLASS_NEXT_METHOD)
#define SCM_NEXT_METHOD(obj)       ((ScmNextMethod*)obj)

/* Other APIs */
SCM_EXTERN ScmObj Scm_ForEach1(ScmObj proc, ScmObj args);
SCM_EXTERN ScmObj Scm_ForEach(ScmObj proc, ScmObj arg1, ScmObj args);
SCM_EXTERN ScmObj Scm_Map1(ScmObj proc, ScmObj args);
SCM_EXTERN ScmObj Scm_Map(ScmObj proc, ScmObj arg1, ScmObj args);

/*--------------------------------------------------------
 * MACROS AND SYNTAX
 */

/* Syntax is a built-in procedure to compile given form. */
typedef ScmObj (*ScmCompileProc)(ScmObj form,
                                 ScmObj env,
                                 int context,
                                 void *data);

struct ScmSyntaxRec {
    SCM_HEADER;
    ScmSymbol *name;            /* for debug */
    ScmCompileProc compiler;    /* takes Sexpr and returns compiled insns */
    void *data;
};

#define SCM_SYNTAX(obj)             ((ScmSyntax*)(obj))
#define SCM_SYNTAXP(obj)            SCM_XTYPEP(obj, SCM_CLASS_SYNTAX)

SCM_CLASS_DECL(Scm_SyntaxClass);
#define SCM_CLASS_SYNTAX            (&Scm_SyntaxClass)

SCM_EXTERN ScmObj Scm_MakeSyntax(ScmSymbol *name,
				 ScmCompileProc compiler, void *data);

/* Macro */
struct ScmMacroRec {
    SCM_HEADER;
    ScmSymbol *name;            /* for debug */
    ScmTransformerProc transformer; /* (Self, Sexpr, Env) -> Sexpr */
    void *data;
};

#define SCM_MACRO(obj)             ((ScmMacro*)(obj))
#define SCM_MACROP(obj)            SCM_XTYPEP(obj, SCM_CLASS_MACRO)

SCM_CLASS_DECL(Scm_MacroClass);
#define SCM_CLASS_MACRO            (&Scm_MacroClass)

SCM_EXTERN ScmObj Scm_MakeMacro(ScmSymbol *name,
                                ScmTransformerProc transformer,
                                void *data);

SCM_EXTERN ScmObj Scm_MacroExpand(ScmObj expr, ScmObj env, int oncep);

/*--------------------------------------------------------
 * PROMISE
 */

struct ScmPromiseRec {
    SCM_HEADER;
    int forced;
    ScmObj code;
};

SCM_CLASS_DECL(Scm_PromiseClass);
#define SCM_CLASS_PROMISE           (&Scm_PromiseClass)

SCM_EXTERN ScmObj Scm_MakePromise(ScmObj code);
SCM_EXTERN ScmObj Scm_Force(ScmObj p);

/*--------------------------------------------------------
 * CONDITION
 */

/* Condition classes are defined in a separate file */
#include <gauche/exception.h>

/* 'reason' flag for Scm_PortError */
enum {
    SCM_PORT_ERROR_INPUT,
    SCM_PORT_ERROR_OUTPUT,
    SCM_PORT_ERROR_CLOSED,
    SCM_PORT_ERROR_UNIT,
    SCM_PORT_ERROR_OTHER
};

/* Throwing error */
SCM_EXTERN void Scm_Error(const char *msg, ...);
SCM_EXTERN void Scm_SysError(const char *msg, ...);
SCM_EXTERN void Scm_PortError(ScmPort *port, int reason, const char *msg, ...);

SCM_EXTERN ScmObj Scm_SError(ScmObj reason, ScmObj args);
SCM_EXTERN ScmObj Scm_FError(ScmObj fmt, ScmObj args);

SCM_EXTERN void Scm_Warn(const char *msg, ...);
SCM_EXTERN void Scm_FWarn(ScmString *fmt, ScmObj args);

SCM_EXTERN int    Scm_ConditionHasType(ScmObj c, ScmObj k);
SCM_EXTERN ScmObj Scm_ConditionMessage(ScmObj c);
SCM_EXTERN ScmObj Scm_ConditionTypeName(ScmObj c);

enum {
    /* predefined stack trace formats.  EXPERIMENTAL. */
    SCM_STACK_TRACE_FORMAT_ORIGINAL, /* original format */
    SCM_STACK_TRACE_FORMAT_CC        /* compiler-message-like format */
};

SCM_EXTERN void Scm_ShowStackTrace(ScmPort *out, ScmObj stacklite,
                                   int maxdepth, int skip, int offset,
                                   int format);

SCM_EXTERN void Scm_ReportError(ScmObj e);

/*--------------------------------------------------------
 * REGEXP
 */

struct ScmRegexpRec {
    SCM_HEADER;
    ScmString *pattern;
    const unsigned char *code;
    int numGroups;
    int numCodes;
    ScmCharSet **sets;
    int numSets;
    int flags;
    ScmString *mustMatch;
};

SCM_CLASS_DECL(Scm_RegexpClass);
#define SCM_CLASS_REGEXP          (&Scm_RegexpClass)
#define SCM_REGEXP(obj)           ((ScmRegexp*)obj)
#define SCM_REGEXPP(obj)          SCM_XTYPEP(obj, SCM_CLASS_REGEXP)

/* flags */
#define SCM_REGEXP_CASE_FOLD      (1L<<0)
#define SCM_REGEXP_PARSE_ONLY     (1L<<1)

SCM_EXTERN ScmObj Scm_RegComp(ScmString *pattern, int flags);
SCM_EXTERN ScmObj Scm_RegCompFromAST(ScmObj ast);
SCM_EXTERN ScmObj Scm_RegOptimizeAST(ScmObj ast);
SCM_EXTERN ScmObj Scm_RegExec(ScmRegexp *rx, ScmString *input);
SCM_EXTERN void Scm_RegDump(ScmRegexp *rx);

struct ScmRegMatchRec {
    SCM_HEADER;
    const char *input;
    int inputSize;
    int inputLen;
    int numMatches;
    struct ScmRegMatchSub {
        int start;
        int length;
        const char *startp;
        const char *endp;
    } *matches;
};

SCM_CLASS_DECL(Scm_RegMatchClass);
#define SCM_CLASS_REGMATCH        (&Scm_RegMatchClass)
#define SCM_REGMATCH(obj)         ((ScmRegMatch*)obj)
#define SCM_REGMATCHP(obj)        SCM_XTYPEP(obj, SCM_CLASS_REGMATCH)

SCM_EXTERN ScmObj Scm_RegMatchSubstr(ScmRegMatch *rm, int i);
SCM_EXTERN ScmObj Scm_RegMatchStart(ScmRegMatch *rm, int i);
SCM_EXTERN ScmObj Scm_RegMatchEnd(ScmRegMatch *rm, int i);
SCM_EXTERN ScmObj Scm_RegMatchAfter(ScmRegMatch *rm, int i);
SCM_EXTERN ScmObj Scm_RegMatchBefore(ScmRegMatch *rm, int i);
SCM_EXTERN void Scm_RegMatchDump(ScmRegMatch *match);

/*-------------------------------------------------------
 * STUB MACROS
 */
#define SCM_ENTER_SUBR(name)

#define SCM_ARGREF(count)           (SCM_FP[count])
#define SCM_RETURN(value)           return value
#define SCM_CURRENT_MODULE()        (Scm_VM()->module)
#define SCM_VOID_RETURN_VALUE(expr) ((void)(expr), SCM_UNDEFINED)

/*---------------------------------------------------
 * SIGNAL
 */

typedef struct ScmSysSigsetRec {
    SCM_HEADER;
    sigset_t set;
} ScmSysSigset;

SCM_CLASS_DECL(Scm_SysSigsetClass);
#define SCM_CLASS_SYS_SIGSET   (&Scm_SysSigsetClass)
#define SCM_SYS_SIGSET(obj)    ((ScmSysSigset*)(obj))
#define SCM_SYS_SIGSET_P(obj)  SCM_XTYPEP(obj, SCM_CLASS_SYS_SIGSET)

SCM_EXTERN ScmObj Scm_SysSigsetOp(ScmSysSigset*, ScmObj, int);
SCM_EXTERN ScmObj Scm_SysSigsetFill(ScmSysSigset*, int);
SCM_EXTERN ScmObj Scm_GetSignalHandler(int);
SCM_EXTERN ScmObj Scm_GetSignalHandlers(void);
SCM_EXTERN ScmObj Scm_SetSignalHandler(ScmObj, ScmObj);
SCM_EXTERN ScmObj Scm_SysSigmask(int how, ScmSysSigset *newmask);
SCM_EXTERN ScmObj Scm_Pause(void);
SCM_EXTERN ScmObj Scm_SigSuspend(ScmSysSigset *mask);
SCM_EXTERN sigset_t Scm_GetMasterSigmask(void);
SCM_EXTERN void   Scm_SetMasterSigmask(sigset_t *set);
SCM_EXTERN ScmObj Scm_SignalName(int signum);

/*---------------------------------------------------
 * SYSTEM
 */

SCM_EXTERN off_t  Scm_IntegerToOffset(ScmObj i);
SCM_EXTERN ScmObj Scm_OffsetToInteger(off_t o);

/* System call wrapper */
#define SCM_SYSCALL3(result, expr, check)       \
  do {                                          \
    (result) = (expr);                          \
    if ((check) && errno == EINTR) {            \
      ScmVM *vm__ = Scm_VM();                   \
      errno = 0;                                \
      SCM_SIGCHECK(vm__);                       \
    } else {                                    \
      break;                                    \
    }                                           \
  } while (1)

#define SCM_SYSCALL(result, expr) \
  SCM_SYSCALL3(result, expr, (result < 0))

/* Obsoleted 
SCM_EXTERN int Scm_SysCall(int r);
SCM_EXTERN void *Scm_PtrSysCall(void *r);
*/

SCM_EXTERN int Scm_GetPortFd(ScmObj port_or_fd, int needfd);

SCM_EXTERN ScmObj Scm_ReadDirectory(ScmString *pathname);
SCM_EXTERN ScmObj Scm_GlobDirectory(ScmString *pattern);

#define SCM_PATH_ABSOLUTE       (1L<<0)
#define SCM_PATH_EXPAND         (1L<<1)
#define SCM_PATH_CANONICALIZE   (1L<<2)
#define SCM_PATH_FOLLOWLINK     (1L<<3) /* not supported yet */
SCM_EXTERN ScmObj Scm_NormalizePathname(ScmString *pathname, int flags);
SCM_EXTERN ScmObj Scm_DirName(ScmString *filename);
SCM_EXTERN ScmObj Scm_BaseName(ScmString *filename);

/* struct stat */
typedef struct ScmSysStatRec {
    SCM_HEADER;
    struct stat statrec;
} ScmSysStat;
    
SCM_CLASS_DECL(Scm_SysStatClass);
#define SCM_CLASS_SYS_STAT    (&Scm_SysStatClass)
#define SCM_SYS_STAT(obj)     ((ScmSysStat*)(obj))
#define SCM_SYS_STAT_P(obj)   (SCM_XTYPEP(obj, SCM_CLASS_SYS_STAT))

SCM_EXTERN ScmObj Scm_MakeSysStat(void); /* returns empty SysStat */

/* time_t
 * NB: POSIX defines time_t to be a type to represent number of seconds
 * since Epoch.  It may be a structure.  In Gauche we just convert it
 * to a number.
 */
SCM_EXTERN ScmObj Scm_MakeSysTime(time_t time);
SCM_EXTERN time_t Scm_GetSysTime(ScmObj val);

/* Gauche also has a <time> object, as specified in SRFI-18, SRFI-19
 * and SRFI-21.  It can be constructed from the basic system interface
 * such as sys-time or sys-gettimeofday. 
 */
typedef struct ScmTimeRec {
    SCM_HEADER;
    ScmObj type;       /* 'time-utc by default.  see SRFI-19 */
    long sec;          /* seconds */
    long nsec;         /* nanoseconds */
} ScmTime;

SCM_CLASS_DECL(Scm_TimeClass);
#define SCM_CLASS_TIME        (&Scm_TimeClass)
#define SCM_TIME(obj)         ((ScmTime*)obj)
#define SCM_TIMEP(obj)        SCM_XTYPEP(obj, SCM_CLASS_TIME)

SCM_EXTERN ScmObj Scm_CurrentTime(void);
SCM_EXTERN ScmObj Scm_MakeTime(ScmObj type, long sec, long nsec);
SCM_EXTERN ScmObj Scm_IntSecondsToTime(long sec);
SCM_EXTERN ScmObj Scm_RealSecondsToTime(double sec);
SCM_EXTERN ScmObj Scm_TimeToSeconds(ScmTime *t);
#if defined(HAVE_STRUCT_TIMESPEC) || defined(GAUCHE_USE_PTHREADS)
SCM_EXTERN struct timespec *Scm_GetTimeSpec(ScmObj t, struct timespec *spec);
#endif /*HAVE_STRUCT_TIMESPEC||GAUCHE_USE_PTHREADS*/

/* struct tm */
typedef struct ScmSysTmRec {
    SCM_HEADER;
    struct tm tm;
} ScmSysTm;
    
SCM_CLASS_DECL(Scm_SysTmClass);
#define SCM_CLASS_SYS_TM      (&Scm_SysTmClass)
#define SCM_SYS_TM(obj)       ((ScmSysTm*)(obj))
#define SCM_SYS_TM_P(obj)     (SCM_XTYPEP(obj, SCM_CLASS_SYS_TM))
#define SCM_SYS_TM_TM(obj)    SCM_SYS_TM(obj)->tm

SCM_EXTERN ScmObj Scm_MakeSysTm(struct tm *);
    
/* struct group */
typedef struct ScmSysGroupRec {
    SCM_HEADER;
    ScmObj name;
    ScmObj gid;
    ScmObj passwd;
    ScmObj mem;
} ScmSysGroup;

SCM_CLASS_DECL(Scm_SysGroupClass);
#define SCM_CLASS_SYS_GROUP    (&Scm_SysGroupClass)
#define SCM_SYS_GROUP(obj)     ((ScmSysGroup*)(obj))
#define SCM_SYS_GROUP_P(obj)   (SCM_XTYPEP(obj, SCM_CLASS_SYS_GROUP))

SCM_EXTERN ScmObj Scm_GetGroupById(gid_t gid);
SCM_EXTERN ScmObj Scm_GetGroupByName(ScmString *name);

/* struct passwd */
typedef struct ScmSysPasswdRec {
    SCM_HEADER;
    ScmObj name;
    ScmObj passwd;
    ScmObj uid;
    ScmObj gid;
    ScmObj gecos;
    ScmObj dir;
    ScmObj shell;
    ScmObj pwclass;
} ScmSysPasswd;

SCM_CLASS_DECL(Scm_SysPasswdClass);
#define SCM_CLASS_SYS_PASSWD    (&Scm_SysPasswdClass)
#define SCM_SYS_PASSWD(obj)     ((ScmSysPasswd*)(obj))
#define SCM_SYS_PASSWD_P(obj)   (SCM_XTYPEP(obj, SCM_CLASS_SYS_PASSWD))

SCM_EXTERN ScmObj Scm_GetPasswdById(uid_t uid);
SCM_EXTERN ScmObj Scm_GetPasswdByName(ScmString *name);

SCM_EXTERN int    Scm_IsSugid(void);

SCM_EXTERN ScmObj Scm_SysExec(ScmString *file, ScmObj args,
                              ScmObj iomap, int forkp);

/* select */
#ifdef HAVE_SELECT
typedef struct ScmSysFdsetRec {
    SCM_HEADER;
    int maxfd;
    fd_set fdset;
} ScmSysFdset;

SCM_CLASS_DECL(Scm_SysFdsetClass);
#define SCM_CLASS_SYS_FDSET     (&Scm_SysFdsetClass)
#define SCM_SYS_FDSET(obj)      ((ScmSysFdset*)(obj))
#define SCM_SYS_FDSET_P(obj)    (SCM_XTYPEP(obj, SCM_CLASS_SYS_FDSET))

SCM_EXTERN ScmObj Scm_SysSelect(ScmObj rfds, ScmObj wfds, ScmObj efds,
				ScmObj timeout);
SCM_EXTERN ScmObj Scm_SysSelectX(ScmObj rfds, ScmObj wfds, ScmObj efds,
				 ScmObj timeout);
#else  /*!HAVE_SELECT*/
/* dummy definitions */
typedef struct ScmHeaderRec ScmSysFdset;
#define SCM_SYS_FDSET(obj)      (obj)
#define SCM_SYS_FDSET_P(obj)    (FALSE)
#endif /*!HAVE_SELECT*/

/* other stuff */
SCM_EXTERN ScmObj Scm_SysMkstemp(ScmString *tmpl);

/*---------------------------------------------------
 * LOAD AND DYNAMIC LINK
 */

/* Flags for Scm_VMLoad and Scm_Load. (not for Scm_VMLoadPort) */
enum ScmLoadFlags {
    SCM_LOAD_QUIET_NOFILE = (1L<<0),  /* do not signal an error if the file
                                         does not exist; just return #f. */
    SCM_LOAD_IGNORE_CODING = (1L<<1)  /* do not use coding-aware port to honor
                                         'coding' magic comment */
};

SCM_EXTERN ScmObj Scm_VMLoadFromPort(ScmPort *port, ScmObj next_paths,
                                     ScmObj env, int flags);
SCM_EXTERN ScmObj Scm_VMLoad(ScmString *file, ScmObj paths, ScmObj env,
			     int flags);
SCM_EXTERN void Scm_LoadFromPort(ScmPort *port, int flags);
SCM_EXTERN int  Scm_Load(const char *file, int flags);

SCM_EXTERN ScmObj Scm_GetLoadPath(void);
SCM_EXTERN ScmObj Scm_AddLoadPath(const char *cpath, int afterp);

SCM_EXTERN ScmObj Scm_DynLoad(ScmString *path, ScmObj initfn, int export_);

SCM_EXTERN ScmObj Scm_Require(ScmObj feature);
SCM_EXTERN ScmObj Scm_Provide(ScmObj feature);
SCM_EXTERN int    Scm_ProvidedP(ScmObj feature);

struct ScmAutoloadRec {
    SCM_HEADER;
    ScmSymbol *name;            /* variable to be autoloaded */
    ScmModule *module;          /* where the binding should be inserted.
                                   this is where autoload is defined. */
    ScmString *path;            /* file to load */
    ScmSymbol *import_from;     /* module to be imported after loading */
    ScmModule *import_to;       /* module to where import_from should be
                                   imported */
                                /* The fields above will be set up when
                                   the autoload object is created, and never
                                   be modified. */

    int loaded;                 /* The flag that indicates this autoload
                                   is resolved, and value field contains
                                   the resolved value.  Once the autoload
                                   goes into "loaded" status, no field
                                   should be changed. */
    ScmObj value;               /* The resolved value */
    ScmInternalMutex mutex;     /* mutex to resolve this autoload */
    ScmInternalCond cv;         /* ... and condition variable. */
    ScmVM *locker;              /* The thread that is resolving the autoload.*/
};

SCM_CLASS_DECL(Scm_AutoloadClass);
#define SCM_CLASS_AUTOLOAD      (&Scm_AutoloadClass)
#define SCM_AUTOLOADP(obj)      SCM_XTYPEP(obj, SCM_CLASS_AUTOLOAD)
#define SCM_AUTOLOAD(obj)       ((ScmAutoload*)(obj))

SCM_EXTERN ScmObj Scm_MakeAutoload(ScmModule *where,
                                   ScmSymbol *name, ScmString *path,
				   ScmSymbol *import_from);
SCM_EXTERN void   Scm_DefineAutoload(ScmModule *where, ScmObj file_or_module,
                                     ScmObj list);
SCM_EXTERN ScmObj Scm_LoadAutoload(ScmAutoload *autoload);

/*---------------------------------------------------
 * UTILITY STUFF
 */

/* Program start and termination */

SCM_EXTERN void Scm_Init(const char *signature);
SCM_EXTERN void Scm_Exit(int code);
SCM_EXTERN void Scm_Abort(const char *msg);
SCM_EXTERN void Scm_Panic(const char *msg, ...);

SCM_EXTERN void Scm_RegisterDL(void *data_start, void *data_end,
                               void *bss_start, void *bss_end);
SCM_EXTERN void Scm_GCSentinel(void *obj, const char *name);

/* repl */
SCM_EXTERN void Scm_Repl(ScmObj reader, ScmObj evaluator, ScmObj printer,
                         ScmObj prompter);

/* Inspect the configuration */
SCM_EXTERN const char *Scm_HostArchitecture(void);

SCM_EXTERN ScmObj Scm_LibraryDirectory(void);
SCM_EXTERN ScmObj Scm_ArchitectureDirectory(void);
SCM_EXTERN ScmObj Scm_SiteLibraryDirectory(void);
SCM_EXTERN ScmObj Scm_SiteArchitectureDirectory(void);

/* Compare and Sort */
SCM_EXTERN int Scm_Compare(ScmObj x, ScmObj y);
SCM_EXTERN void Scm_SortArray(ScmObj *elts, int nelts, ScmObj cmpfn);
SCM_EXTERN ScmObj Scm_SortList(ScmObj objs, ScmObj fn);
SCM_EXTERN ScmObj Scm_SortListX(ScmObj objs, ScmObj fn);

/* Assertion */

#ifdef GAUCHE_RECKLESS
#define SCM_ASSERT(expr)   /* nothing */
#else

#ifdef __GNUC__

#define SCM_ASSERT(expr)                                                \
    do {                                                                \
        if (!(expr))                                                    \
            Scm_Panic("\"%s\", line %d (%s): Assertion failed: %s",     \
                      __FILE__, __LINE__, __PRETTY_FUNCTION__, #expr);  \
    } while (0)

#else

#define SCM_ASSERT(expr)                                        \
    do {                                                        \
        if (!(expr))                                            \
            Scm_Panic("\"%s\", line %d: Assertion failed: %s",  \
                      __FILE__, __LINE__, #expr);               \
    } while (0)

#endif /* !__GNUC__ */

#endif /* !GAUCHE_RECKLESS */


SCM_DECL_END

#endif /* GAUCHE_H */
