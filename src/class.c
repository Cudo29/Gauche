/*
 * class.c - class metaobject implementation
 *
 *  Copyright(C) 2000-2001 by Shiro Kawai (shiro@acm.org)
 *
 *  Permission to use, copy, modify, distribute this software and
 *  accompanying documentation for any purpose is hereby granted,
 *  provided that existing copyright notices are retained in all
 *  copies and that this notice is included verbatim in all
 *  distributions.
 *  This software is provided as is, without express or implied
 *  warranty.  In no circumstances the author(s) shall be liable
 *  for any damages arising out of the use of this software.
 *
 *  $Id: class.c,v 1.30 2001-03-26 10:03:49 shiro Exp $
 */

#include "gauche.h"
#include "gauche/macro.h"
#include "gauche/class.h"

/*===================================================================
 * Built-in classes
 */

static int class_print(ScmObj, ScmPort *, int);
static int generic_print(ScmObj, ScmPort *, int);
static int method_print(ScmObj, ScmPort *, int);
static int slot_accessor_print(ScmObj, ScmPort *, int);

static ScmObj object_allocate(ScmClass *k, ScmObj initargs);
static void scheme_slot_default(ScmObj obj);

ScmClass *Scm_DefaultCPL[] = { SCM_CLASS_TOP, NULL };
ScmClass *Scm_CollectionCPL[] = {
    SCM_CLASS_COLLECTION, SCM_CLASS_TOP, NULL
};
ScmClass *Scm_SequenceCPL[] = {
    SCM_CLASS_SEQUENCE, SCM_CLASS_COLLECTION, SCM_CLASS_TOP, NULL
};
ScmClass *Scm_ObjectCPL[] = {
    SCM_CLASS_OBJECT, SCM_CLASS_TOP, NULL
};

SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_TopClass, NULL);
SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_BoolClass, NULL);
SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_CharClass, NULL);
SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_UnknownClass, NULL);

/* Intercessory classes */
SCM_DEFINE_BUILTIN_CLASS(Scm_CollectionClass,
                         NULL, NULL, NULL, NULL,
                         SCM_CLASS_DEFAULT_CPL);
SCM_DEFINE_BUILTIN_CLASS(Scm_SequenceClass,
                         NULL, NULL, NULL, NULL,
                         SCM_CLASS_COLLECTION_CPL);
SCM_DEFINE_BASE_CLASS(Scm_ObjectClass, ScmObj,
                      NULL, NULL, NULL, NULL,
                      SCM_CLASS_DEFAULT_CPL);

/* Those basic metaobjects will be initialized further in Scm__InitClass */
SCM_DEFINE_BASE_CLASS(Scm_ClassClass, ScmClass,
                      class_print, NULL, NULL, NULL,
                      SCM_CLASS_OBJECT_CPL);
SCM_DEFINE_BASE_CLASS(Scm_GenericClass, ScmGeneric,
                      generic_print, NULL, NULL, NULL,
                      SCM_CLASS_OBJECT_CPL);
SCM_DEFINE_BASE_CLASS(Scm_MethodClass, ScmMethod,
                      method_print, NULL, NULL, NULL,
                      SCM_CLASS_OBJECT_CPL);

/* Internally used classes */
SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_SlotAccessorClass, slot_accessor_print);
SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_NextMethodClass, NULL);

/* Builtin generic functions */
SCM_DEFINE_GENERIC(Scm_GenericMake, Scm_NoNextMethod, NULL);
SCM_DEFINE_GENERIC(Scm_GenericAllocate, Scm_NoNextMethod, NULL);
SCM_DEFINE_GENERIC(Scm_GenericInitialize, Scm_NoOperation, NULL);
SCM_DEFINE_GENERIC(Scm_GenericAddMethod, Scm_NoNextMethod, NULL);
SCM_DEFINE_GENERIC(Scm_GenericComputeCPL, Scm_NoNextMethod, NULL);
SCM_DEFINE_GENERIC(Scm_GenericComputeSlots, Scm_NoNextMethod, NULL);
SCM_DEFINE_GENERIC(Scm_GenericComputeGetNSet, Scm_NoNextMethod, NULL);
SCM_DEFINE_GENERIC(Scm_GenericSlotMissing, Scm_NoNextMethod, NULL);
SCM_DEFINE_GENERIC(Scm_GenericSlotUnbound, Scm_NoNextMethod, NULL);

/* Some frequently-used pointers */
static ScmObj key_allocation;
static ScmObj key_instance;
static ScmObj key_accessor;
static ScmObj key_slot_accessor;
static ScmObj key_builtin;
static ScmObj key_name;
static ScmObj key_supers;
static ScmObj key_slots;
static ScmObj key_metaclass;
static ScmObj key_lambda_list;
static ScmObj key_generic;
static ScmObj key_specializers;
static ScmObj key_body;
static ScmObj key_init_keyword;
static ScmObj key_init_thunk;
static ScmObj key_init_value;
static ScmObj key_slot_num;
static ScmObj key_slot_set;
static ScmObj key_slot_ref;

/*=====================================================================
 * Auxiliary utilities
 */

static ScmClass **class_list_to_array(ScmObj classes, int len)
{
    ScmObj cp;
    ScmClass **v, **vp;
    v = vp = SCM_NEW2(ScmClass**, sizeof(ScmClass*)*(len+1));
    SCM_FOR_EACH(cp, classes) {
        if (!Scm_TypeP(SCM_CAR(cp), SCM_CLASS_CLASS))
            Scm_Error("list of classes required, but found non-class object"
                      " %S in %S", SCM_CAR(cp), classes);
        *vp++ = SCM_CLASS(SCM_CAR(cp));
    }
    *vp = NULL;
    return v;
}

static ScmObj class_array_to_list(ScmClass **array, int len)
{
    ScmObj h = SCM_NIL, t;
    if (array) while (len-- > 0) SCM_APPEND1(h, t, SCM_OBJ(*array++));
    return h;
}

static ScmObj class_array_to_names(ScmClass **array, int len)
{
    ScmObj h = SCM_NIL, t;
    int i;
    for (i=0; i<len; i++, array++) SCM_APPEND1(h, t, (*array)->name);
    return h;
}

/*=====================================================================
 * Class metaobject
 */

/* One of the design goals of Gauche object system is to make Scheme-defined
 * class easily accessible from C code, and vice versa.
 *
 * Classes in Gauche fall into four categories: core final class, core
 * base class, core abstract class and Scheme class.  A C-defined class
 * may belong to one of the first three, while a Scheme-defined class is
 * always a Scheme class.
 *
 * Core final classes are the ones that represents basic objects of the
 * language, such as <integer> or <port>.   Those classes are just
 * the way to reify the basic object, and don't follow object protorol;
 * for example, you can't overload "initialize" method specialized to
 * <integer> to customize initialization of integers, nor subclass <integer>,
 * although you can use them to specialize methods you write.  "Make" methods
 * for these objects are dispatched to the appropriate C functions.
 *
 * Core base classes are the ones from which you can derive Scheme classes.
 * <class>, <generic-method> and <method> are in this category.  The instance
 * of those classes have extra slots that contains C-specific data, such
 * as function pointers.  You can subclass them, but there is one restriction:
 * There can't be more than one core base class in the class' superclasses.
 * Because of this fact, C routines can take the pointer to the instance
 * of subclasses and safely cast it to oen of the core base classes.
 *
 * Core abstract classes are just for method specialization.  They can't
 * create instances directly, and they shouldn't have any direct slots.
 * <top>, <object> and <sequence> are in this category, among others.
 *
 * Since a class must be <class> itself or its descendants, C code can
 * treat them as ScmClass*, and can determine the category of the class.
 *
 * Depending on its category, a class must or may provide those function
 * pointers:
 *
 *   Category:  core final     core base     core abstract 
 *   -----------------------------------------------------
 *    allocate   required       optional        NULL
 *    print      optional       optional        ignored
 *    equal      optional       optional        ignored
 *    compare    optional       optional        ignored
 *    serialize  optional       optional        ignored
 *
 *  (*1: required for applicable classes, must be NULL otherwise)
 *
 * If the function is optional, you can set NULL there and the system
 * uses default function.  For Scheme class the system sets appropriate
 * functions.   See the following section for details of these function
 * potiners.
 */

/*
 * Built-in protocols
 *
 *  ScmObj klass->allocate(ScmClass *klass, ScmObj initargs)
 *     Called at the bottom of the chain of allocate-instance method.
 *     Besides allocating the required space, it must initialize
 *     members of the C-specific part of the instance, including SCM_HEADER.
 *     This protocol can be NULL for core base classes; if so, attempt
 *     to "make" such class reports an error.
 *
 *  int klass->print(ScmObj obj, ScmPort *sink, int mode)
 *     OBJ is an instance of klass (you can safely assume it).  This
 *     function should print OBJ into SINK, and returns number of characters
 *     output to SINK.   MODE can be SCM_PRINT_DISPLAY for display(),
 *     SCM_PRINT_WRITE for write(), or SCM_PRINT_DEBUG for more precise
 *     debug information.
 *     If this function pointer is not set, a default print method
 *     is used.
 *
 *  int klass->equal(ScmObj x, ScmObj y)
 *     X and Y are instances of klass.  This function should return TRUE iff
 *     X equals Y, FALSE otherwise.   If this function pointer is not set,
 *     Gauche uses pointer comparison to see their equality.
 *
 *  int klass->compare(ScmObj x, ScmObj y)
 *     X and Y are instances of klass or its descendants.  If the objects
 *     are fully orderable, this function returns either -1, 0 or 1, depending
 *     X preceding Y, X being equal to Y, or X following Y, respectively.
 *     If the objects are not fully orderable, just returns 0.
 *     If this function pointer is not set, Gauche assumes objects are
 *     not orderable.
 *
 *  int klass->serialize(ScmObj obj, ScmPort *sink, ScmObj table)
 *     OBJ is an instance of klass.  This method is only called when OBJ
 *     has not been output in the current serializing session.
 */

/*
 * Class metaobject protocol implementation
 */

/* Allocate class structure.  klass is a metaclass. */
static ScmObj class_allocate(ScmClass *klass, ScmObj initargs)
{
    ScmClass *instance;
    int i, nslots = klass->numInstanceSlots;
    instance = SCM_NEW2(ScmClass*,
                        sizeof(ScmClass) + sizeof(ScmObj)*nslots);
    SCM_SET_CLASS(instance, klass);
    instance->print = NULL;
    instance->equal = NULL;
    instance->compare = NULL;
    instance->serialize = NULL; /* class_serialize? */
    instance->allocate = object_allocate; /* default allocation */
    instance->cpa = NULL;
    instance->numInstanceSlots = nslots;
    instance->instanceSlotOffset = 1; /* default */
    instance->flags = 0;        /* ?? */
    instance->name = SCM_FALSE;
    instance->directSupers = SCM_NIL;
    instance->accessors = SCM_NIL;
    instance->cpl = SCM_NIL;
    instance->directSlots = SCM_NIL;
    instance->slots = SCM_NIL;
    instance->directSubclasses = SCM_NIL;
    instance->directMethods = SCM_NIL;
    scheme_slot_default(SCM_OBJ(instance));
    return SCM_OBJ(instance);
}

static int class_print(ScmObj obj, ScmPort *port, int mode) 
{
    ScmClass *c = (ScmClass*)obj;
    return Scm_Printf(port, "#<class %A>", Scm_ClassName(c));
}

/*
 * (make <class> ...)   - default method to make a class instance.
 */

/* defined in Scheme */

/*
 * (allocate-instance <class> initargs)
 */
static ScmObj allocate(ScmNextMethod *nm, ScmObj *args, int nargs, void *d)
{
    ScmClass *c = SCM_CLASS(args[0]);
    if (c->allocate == NULL) {
        Scm_Error("built-in class can't be allocated via allocate-instance: %S",
                  SCM_OBJ(c));
    }
    return c->allocate(c, args[1]);
}

static ScmClass *class_allocate_SPEC[] = { SCM_CLASS_CLASS, SCM_CLASS_LIST };
static SCM_DEFINE_METHOD(class_allocate_rec, &Scm_GenericAllocate,
                         2, 0, class_allocate_SPEC, allocate, NULL);

/*
 * (compute-cpl <class>)
 */
static ScmObj class_compute_cpl(ScmNextMethod *nm, ScmObj *args, int nargs,
                                void *d)
{
    ScmClass *c = SCM_CLASS(args[0]);
    return Scm_ComputeCPL(c);
}

static ScmClass *class_compute_cpl_SPEC[] = { SCM_CLASS_CLASS };
static SCM_DEFINE_METHOD(class_compute_cpl_rec, &Scm_GenericComputeCPL,
                         1, 0, class_compute_cpl_SPEC,
                         class_compute_cpl, NULL);

/*
 * Get class
 */

ScmClass *Scm_ClassOf(ScmObj obj)
{
    if (!SCM_PTRP(obj)) {
        if (SCM_TRUEP(obj) || SCM_FALSEP(obj)) return SCM_CLASS_BOOL;
        if (SCM_NULLP(obj)) return SCM_CLASS_NULL;
        if (SCM_CHARP(obj)) return SCM_CLASS_CHAR;
        if (SCM_INTP(obj))  return SCM_CLASS_INTEGER;
        else return SCM_CLASS_UNKNOWN;
    } else {
        return obj->klass;
    }
}

/*--------------------------------------------------------------
 * Metainformation accessors
 */
/* TODO: disable modification of system-builtin classes */

ScmObj Scm_ClassName(ScmClass *klass)
{
    return klass->name;
}

static void class_name_set(ScmClass *klass, ScmObj val)
{
    klass->name = val;
}

ScmObj Scm_ClassCPL(ScmClass *klass)
{
    /* TODO: MT safeness */
    if (!SCM_PAIRP(klass->cpl)) {
        /* This is the case of builtin class.  The second head of cpl is
           always the only direct superclass. */
        ScmClass **p = klass->cpa;
        ScmObj h = SCM_NIL, t;
        SCM_APPEND1(h, t, SCM_OBJ(klass));
        while (*p) {
            SCM_APPEND1(h, t, SCM_OBJ(*p));
            p++;
        }
        klass->cpl = h;
        if (SCM_PAIRP(SCM_CDR(h))) {
            klass->directSupers = SCM_LIST1(SCM_CADR(h));
        } else {
            klass->directSupers = SCM_NIL;
        }
    }
    return klass->cpl;
}

static void class_cpl_set(ScmClass *klass, ScmObj val)
{
    ScmObj cp;
    int len;
    if (!SCM_PAIRP(val)) goto err;
    if (SCM_CAR(val) != SCM_OBJ(klass)) goto err;
    if ((len = Scm_Length(val)) < 0) goto err;
    klass->cpa = class_list_to_array(val, len);
    if (klass->cpa[len-1] != SCM_CLASS_TOP) goto err;
    klass->cpl = Scm_CopyList(val);
    return;
  err:
    Scm_Error("class precedence list must be a proper list of class "
              "metaobject, begenning from the class itself owing the list, "
              "and ending by the class <top>: %S", val);
}

ScmObj Scm_ClassDirectSupers(ScmClass *klass)
{
    if (!SCM_PAIRP(klass->directSupers)) {
        Scm_ClassCPL(klass);    /* set directSupers */
    }
    return klass->directSupers;
}

static void class_direct_supers_set(ScmClass *klass, ScmObj val)
{
    /* TODO: check argument vailidity */
    klass->directSupers = val;
}

ScmObj Scm_ClassDirectSlots(ScmClass *klass)
{
    return klass->directSlots;
}

static void class_direct_slots_set(ScmClass *klass, ScmObj val)
{
    /* TODO: check argument vailidity */
    klass->directSlots = val;
}

ScmObj Scm_ClassSlots(ScmClass *klass)
{
    return klass->slots;
}

static void class_slots_set(ScmClass *klass, ScmObj val)
{
    /* TODO: check argument vailidity */
    klass->slots = val;
}

ScmObj Scm_SlotAccessors(ScmClass *klass)
{
    return klass->accessors;
}

static void class_accessors_set(ScmClass *klass, ScmObj val)
{
    /* TODO: check argument vailidity */
    klass->accessors = val;
}

ScmObj Scm_ClassDirectSubclasses(ScmClass *klass)
{
    return klass->directSubclasses;
}

static void class_direct_subclasses_set(ScmClass *klass, ScmObj val)
{
    /* TODO: check argument vailidity */
    klass->directSubclasses = val;
}

static ScmObj class_numislots(ScmClass *klass)
{
    Scm_MakeInteger(klass->numInstanceSlots);
}

static void class_numislots_set(ScmClass *klass, ScmObj snf)
{
    int nf;
    if (!SCM_INTP(snf) || (nf = SCM_INT_VALUE(snf)) < 0) {
        Scm_Error("invalid argument: %S", snf);
    }
    klass->numInstanceSlots = nf;
}

/*--------------------------------------------------------------
 * External interface
 */

int Scm_SubtypeP(ScmClass *sub, ScmClass *type)
{
    ScmClass **p;
    if (sub == type) return TRUE;

    p = sub->cpa;
    while (*p) {
        if (*p++ == type) return TRUE;
    }
    return FALSE;
}

int Scm_TypeP(ScmObj obj, ScmClass *type)
{
    return Scm_SubtypeP(Scm_ClassOf(obj), type);
}

/*
 * compute-cpl
 */
static ScmObj compute_cpl_cb(ScmObj k, void *dummy)
{
    return SCM_CLASS(k)->directSupers;
}

ScmObj Scm_ComputeCPL(ScmClass *klass)
{
    ScmObj seqh = SCM_NIL, seqt, ds, dp, result;

    /* a trick to ensure we have <object> <top> at the end of CPL. */
    ds = Scm_Delete(SCM_OBJ(SCM_CLASS_OBJECT), klass->directSupers,
                    SCM_CMP_EQ);
    ds = Scm_Delete(SCM_OBJ(SCM_CLASS_TOP), ds, SCM_CMP_EQ);
    ds = Scm_Append2(ds, SCM_LIST1(SCM_OBJ(SCM_CLASS_OBJECT)));
    SCM_APPEND1(seqh, seqt, ds);

    SCM_FOR_EACH(dp, klass->directSupers) {
        if (!SCM_CLASSP(SCM_CAR(dp)))
            Scm_Error("non-class found in direct superclass list: %S",
                      klass->directSupers);
        if (SCM_CAR(dp) == SCM_OBJ(SCM_CLASS_OBJECT)
            || SCM_CAR(dp) == SCM_OBJ(SCM_CLASS_TOP))
            continue;
        SCM_APPEND1(seqh, seqt, Scm_ClassCPL(SCM_CLASS(SCM_CAR(dp))));
    }
    SCM_APPEND1(seqh, seqt, Scm_ClassCPL(SCM_CLASS_OBJECT));
    
    result = Scm_MonotonicMerge(SCM_OBJ(klass), seqh, compute_cpl_cb, NULL);
    if (SCM_FALSEP(result))
        Scm_Error("discrepancy found in class precedence lists of the superclasses: %S",
                  klass->directSupers);
    return result;
}

/*=====================================================================
 * Scheme slot access
 */

/* Scheme slots are simply stored in ScmObj array.  What complicates
 * the matter is that we allow any C structure to be a base class of
 * Scheme class, so there may be an offset for such slots.
 */

/* dummy structure to access Scheme slots */
typedef struct ScmInstanceRec {
    SCM_HEADER;
    ScmObj slots[1];
} ScmInstance;

#define SCM_INSTANCE(obj)      ((ScmInstance *)obj)

static inline int scheme_slot_index(ScmObj obj, int number)
{
    ScmClass *k = SCM_CLASS_OF(obj);
    int offset = k->instanceSlotOffset;
    if (offset == 0)
        Scm_Error("scheme slot accessor called with C-defined object %S.  implementation error?",
                  obj);
    if (number < 0 || number > k->numInstanceSlots)
        Scm_Error("instance slot index %d out of bounds for %S",
                  number, obj);
    return number-offset+1;
}


static inline ScmObj scheme_slot_ref(ScmObj obj, int number)
{
    int index = scheme_slot_index(obj, number);
    return SCM_INSTANCE(obj)->slots[index];
}

static inline void scheme_slot_set(ScmObj obj, int number, ScmObj val)
{
    int index = scheme_slot_index(obj, number);
    SCM_INSTANCE(obj)->slots[index] = val;
}

static void scheme_slot_default(ScmObj obj)
{
    int index = scheme_slot_index(obj, 0);
    int count = SCM_CLASS_OF(obj)->numInstanceSlots;
    int i;
    for (i=0; i<count; i++, index++)
        SCM_INSTANCE(obj)->slots[index] = SCM_UNBOUND;
}

/* initialize slot according to its accessor spec */
static ScmObj slot_initialize_cc(ScmObj result, void **data)
{
    ScmObj obj = data[0];
    ScmObj slot = data[1];
    return Scm_VMSlotSet(obj, slot, result);
}

static ScmObj slot_initialize(ScmObj obj, ScmObj acc, ScmObj initargs)
{
    ScmClass *klass = SCM_CLASS_OF(obj);
    ScmObj slot = SCM_CAR(acc);
    ScmSlotAccessor *ca = SCM_SLOT_ACCESSOR(SCM_CDR(acc));
    if (SCM_KEYWORDP(ca->initKeyword)) {
        ScmObj v = Scm_GetKeyword(ca->initKeyword, initargs, SCM_UNDEFINED);
        if (!SCM_UNDEFINEDP(v)) return Scm_VMSlotSet(obj, slot, v);
        v = SCM_UNBOUND;
    }
    if (!SCM_UNBOUNDP(ca->initValue))
        return Scm_VMSlotSet(obj, slot, ca->initValue);
    if (SCM_PROCEDUREP(ca->initThunk)) {
        void *data[2];
        data[0] = (void*)obj;
        data[1] = (void*)slot;
        Scm_VMPushCC(slot_initialize_cc, data, 2);
        return Scm_VMApply(ca->initThunk, SCM_NIL);
    }
    return SCM_UNDEFINED;
}

/*-------------------------------------------------------------------
 * slot-ref, slot-set! and families
 */
inline ScmSlotAccessor *Scm_GetSlotAccessor(ScmClass *klass, ScmObj slot)
{
    ScmObj p = Scm_Assq(slot, klass->accessors);
    if (!SCM_PAIRP(p)) return NULL;
    if (!SCM_XTYPEP(SCM_CDR(p), SCM_CLASS_SLOT_ACCESSOR))
        Scm_Error("slot accessor information of class %S, slot %S is screwed up.",
                  SCM_OBJ(klass), slot);
    return SCM_SLOT_ACCESSOR(SCM_CDR(p));
}

#define SLOT_UNBOUND(klass, obj, slot)                  \
    Scm_VMApply(SCM_OBJ(&Scm_GenericSlotUnbound),       \
                SCM_LIST3(SCM_OBJ(klass), obj, slot))

static ScmObj slot_ref_cc(ScmObj result, void **data)
{
    ScmObj obj = data[0];
    ScmObj slot = data[1];
    if (SCM_UNBOUNDP(result))
        return SLOT_UNBOUND(Scm_ClassOf(obj), obj, slot);
    else
        return result;
}

ScmObj Scm_VMSlotRef(ScmObj obj, ScmObj slot)
{
    ScmClass *klass = Scm_ClassOf(obj);
    ScmSlotAccessor *ca = Scm_GetSlotAccessor(klass, slot);
    ScmObj val = SCM_UNBOUND;

    if (ca == NULL) {
        return Scm_VMApply(SCM_OBJ(&Scm_GenericSlotMissing),
                           SCM_LIST3(SCM_OBJ(klass), obj, slot));
    }
    if (ca->getter) {
        val = ca->getter(obj);
    } else if (ca->slotNumber >= 0) {
        val = scheme_slot_ref(obj, ca->slotNumber);
    } else if (SCM_PAIRP(ca->schemeAccessor)
               && SCM_PROCEDUREP(SCM_CAR(ca->schemeAccessor))) {
        void *data[2];
        data[0] = obj;
        data[1] = slot;
        Scm_VMPushCC(slot_ref_cc, data, 2);
        return Scm_VMApply(SCM_CAR(ca->schemeAccessor), SCM_LIST1(obj));
    } else {
        Scm_Error("don't know how to retrieve value of slot %S of object %S (MOP error?)",
                  slot, obj);
    }
    if (SCM_UNBOUNDP(val)) return SLOT_UNBOUND(klass, obj, slot);
    return val;
}

ScmObj Scm_VMSlotSet(ScmObj obj, ScmObj slot, ScmObj val)
{
    ScmClass *klass = Scm_ClassOf(obj);
    ScmSlotAccessor *ca = Scm_GetSlotAccessor(klass, slot);
    
    if (ca == NULL) {
        return Scm_VMApply(SCM_OBJ(&Scm_GenericSlotMissing),
                           SCM_LIST4(SCM_OBJ(klass), obj, slot, val));
    }
    if (ca->setter) {
        ca->setter(obj, val);
    } else if (ca->slotNumber >= 0) {
        scheme_slot_set(obj, ca->slotNumber, val);
    } else if (SCM_PAIRP(ca->schemeAccessor)
               && SCM_PROCEDUREP(SCM_CDR(ca->schemeAccessor))) {
        return Scm_VMApply(SCM_CDR(ca->schemeAccessor), SCM_LIST2(obj, val));
    } else {
        Scm_Error("slot %S of class %S is read-only", slot, SCM_OBJ(klass));
    }
    return SCM_UNDEFINED;
}

#if 0
/* NB: Scm_GetSlotRefProc(CLASS, SLOT) and Scm_GetSlotSetProc(CLASS, SLOT)
 *   return subrs that can be used in place of (slot-ref OBJ SLOT) and
 *   and (slot-set! OBJ SLOT), where OBJ's class is CLASS.   The intention
 *   is to pre-calculate slot lookup and type dispatch according to
 *   the slot implementation.  The rudimental benchmark showed it is
 *   faster than applying slot-ref/slot-set!.
 *
 *   However, I found that inlining slot-ref/slot-set! makes
 *   the code even faster, so you don't need to do all these precalculation
 *   for speed.   I leave the code inside #if 0 -- #endif, for in future
 *   I may find it useful...
 */
struct slot_acc_packet {
    union {
        ScmNativeGetterProc cgetter;
        ScmNativeSetterProc csetter;
        int slotNum;
        ScmObj sgetter;
        ScmObj ssetter;
    } method;
    ScmObj slot;
};

static ScmObj slot_ref_native(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    ScmObj val = p->method.cgetter(args[0]);
    if (SCM_UNBOUNDP(val)) 
        return SLOT_UNBOUND(Scm_ClassOf(args[0]), args[0], p->slot);
    else
        return val;
}

static ScmObj slot_ref_instance(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    ScmObj val = scheme_slot_ref(args[0], p->method.slotNum);
    if (SCM_UNBOUNDP(val)) 
        return SLOT_UNBOUND(Scm_ClassOf(args[0]), args[0], p->slot);
    else
        return val;
}

static ScmObj slot_ref_procedural(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    void *next[2];
    next[0] = args[0];
    next[1] = p->slot;
    Scm_VMPushCC(slot_ref_cc, next, 2);
    return Scm_VMApply(p->method.sgetter, SCM_LIST1(args[0]));
}

static ScmObj slot_ref_missing(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    return Scm_VMApply(SCM_OBJ(&Scm_GenericSlotMissing),
                       SCM_LIST3(SCM_OBJ(Scm_ClassOf(args[0])), args[0],
                                         p->slot));

}

static ScmObj slot_set_native(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    p->method.csetter(args[0], args[1]);
    return SCM_UNDEFINED;
}

static ScmObj slot_set_instance(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    scheme_slot_set(args[0], p->method.slotNum, args[1]);
    return SCM_UNDEFINED;
}

static ScmObj slot_set_procedural(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    return Scm_VMApply(p->method.ssetter, SCM_LIST2(args[0], args[1]));
}

static ScmObj slot_set_missing(ScmObj *args, int nargs, void *data)
{
    struct slot_acc_packet *p = (struct slot_acc_packet *)data;
    return Scm_VMApply(SCM_OBJ(&Scm_GenericSlotMissing),
                       SCM_LIST4(SCM_OBJ(Scm_ClassOf(args[0])), args[0],
                                         p->slot, args[1]));

}

ScmObj Scm_GetSlotRefProc(ScmClass *klass, ScmObj slot)
{
    ScmSlotAccessor *ca = Scm_GetSlotAccessor(klass, slot);
    struct slot_acc_packet *p = SCM_NEW(struct slot_acc_packet);
    ScmObj outp = Scm_MakeOutputStringPort(), name;
    p->slot = slot;

    Scm_Printf(SCM_PORT(outp), "slot-ref %S %S", klass->name, slot);
    name = Scm_GetOutputString(SCM_PORT(outp));

    if (ca == NULL) {
        return Scm_MakeSubr(slot_ref_missing, p, 1, 0, name);
    }
    if (ca->getter) {
        p->method.cgetter = ca->getter;
        return Scm_MakeSubr(slot_ref_native, p, 1, 0, name);
    }
    if (ca->slotNumber >= 0) {
        p->method.slotNum = ca->slotNumber;
        return Scm_MakeSubr(slot_ref_instance, p, 1, 0, name);
    }
    if (SCM_PAIRP(ca->schemeAccessor)
        && SCM_PROCEDUREP(SCM_CAR(ca->schemeAccessor))) {
        p->method.sgetter = SCM_CAR(ca->schemeAccessor);
        return Scm_MakeSubr(slot_ref_procedural, p, 1, 0, name);
    } else {
        Scm_Error("don't know how to make slot referencer of slot %S of class %S (MOP error?)",
                  slot, klass);
        return SCM_UNDEFINED;
    }
}

ScmObj Scm_GetSlotSetProc(ScmClass *klass, ScmObj slot)
{
    ScmSlotAccessor *ca = Scm_GetSlotAccessor(klass, slot);
    struct slot_acc_packet *p = SCM_NEW(struct slot_acc_packet);
    ScmObj outp = Scm_MakeOutputStringPort(), name;
    p->slot = slot;

    Scm_Printf(SCM_PORT(outp), "slot-set %S %S", klass->name, slot);
    name = Scm_GetOutputString(SCM_PORT(outp));
    
    if (ca == NULL) {
        return Scm_MakeSubr(slot_set_missing, p, 2, 0, name);
    }
    if (ca->setter) {
        p->method.csetter = ca->setter;
        return Scm_MakeSubr(slot_set_native, p, 2, 0, name);
    }
    if (ca->slotNumber >= 0) {
        p->method.slotNum = ca->slotNumber;
        return Scm_MakeSubr(slot_set_instance, p, 2, 0, name);
    }
    if (SCM_PAIRP(ca->schemeAccessor)
        && SCM_PROCEDUREP(SCM_CDR(ca->schemeAccessor))) {
        p->method.ssetter = SCM_CDR(ca->schemeAccessor);
        return Scm_MakeSubr(slot_set_procedural, p, 2, 0, name);
    } else {
        Scm_Error("slot %S of class %S is read-only", slot, SCM_OBJ(klass));
        return SCM_UNDEFINED;
    }
}
#endif

/*--------------------------------------------------------------
 * Slot accessor object
 */

/* we initialize fields appropriately here. */
static ScmObj slot_accessor_allocate(ScmClass *klass, ScmObj initargs)
{
    ScmSlotAccessor *sa = SCM_NEW(ScmSlotAccessor);
    ScmObj slotnum, slotget, slotset;

    SCM_SET_CLASS(sa, klass);
    sa->getter = NULL;
    sa->setter = NULL;
    sa->initValue =   Scm_GetKeyword(key_init_value, initargs, SCM_UNDEFINED);
    if (sa->initValue == SCM_UNDEFINED) sa->initValue = SCM_UNBOUND;
    sa->initKeyword = Scm_GetKeyword(key_init_keyword, initargs, SCM_FALSE);
    sa->initThunk =   Scm_GetKeyword(key_init_thunk, initargs, SCM_FALSE);

    slotnum = Scm_GetKeyword(key_slot_num, initargs, SCM_FALSE);
    if (SCM_INTP(slotnum) && SCM_INT_VALUE(slotnum) >= 0) {
        sa->slotNumber = SCM_INT_VALUE(slotnum);
    } else {
        sa->slotNumber = -1;
    }
    slotget = Scm_GetKeyword(key_slot_ref, initargs, SCM_FALSE);
    slotset = Scm_GetKeyword(key_slot_set, initargs, SCM_FALSE);
    if (SCM_PROCEDUREP(slotget) && SCM_PROCEDUREP(slotset)) {
        sa->schemeAccessor = Scm_Cons(slotget, slotset);
    } else {
        sa->schemeAccessor = SCM_FALSE;
    }
    return SCM_OBJ(sa);
}

static int slot_accessor_print(ScmObj obj, ScmPort *out, int mode)
{
    int nc = 0;
    ScmSlotAccessor *sa = SCM_SLOT_ACCESSOR(obj);
    
    nc += Scm_Printf(out, "#<slot-accessor ");
    if (sa->getter) nc += Scm_Printf(out, "native");
    else if (SCM_PAIRP(sa->schemeAccessor)) nc += Scm_Printf(out, "proc");
    else if (sa->slotNumber >= 0) nc += Scm_Printf(out, "%d", sa->slotNumber);
    else nc += Scm_Printf(out, "unknown");
    if (!SCM_FALSEP(sa->initKeyword))
        nc += Scm_Printf(out, " %S", sa->initKeyword);
    nc += Scm_Printf(out, ">");
    return nc;
}

/* some information is visible from Scheme world */
static ScmObj slot_accessor_init_value(ScmSlotAccessor *sa)
{
    return sa->initValue;
}

static ScmObj slot_accessor_init_keyword(ScmSlotAccessor *sa)
{
    return sa->initKeyword;
}

static ScmObj slot_accessor_init_thunk(ScmSlotAccessor *sa)
{
    return sa->initThunk;
}

static ScmObj slot_accessor_slot_number(ScmSlotAccessor *sa)
{
    return SCM_MAKE_INT(sa->slotNumber);
}

static void slot_accessor_slot_number_set(ScmSlotAccessor *sa, ScmObj val)
{
    int n;
    if (!SCM_INTP(val) || ((n = SCM_INT_VALUE(val)) < 0))
        Scm_Error("small positive integer required, but got %S", val);
    sa->slotNumber = n;
}

static ScmObj slot_accessor_scheme_accessor(ScmSlotAccessor *sa)
{
    return sa->schemeAccessor;
}

static void slot_accessor_scheme_accessor_set(ScmSlotAccessor *sa, ScmObj p)
{
    /* TODO: check */
    sa->schemeAccessor = p;
}

/*=====================================================================
 * <object> class initialization
 */

static ScmObj object_allocate(ScmClass *klass, ScmObj initargs)
{
    int size = sizeof(ScmObj)*(klass->numInstanceSlots+1);
    int i;
    ScmObj obj = SCM_NEW2(ScmObj, size);
    SCM_SET_CLASS(obj, klass);
    scheme_slot_default(obj);
    return SCM_OBJ(obj);
}

/* (initialize <object> initargs) */
static ScmObj object_initialize_cc(ScmObj result, void **data)
{
    ScmObj obj = SCM_OBJ(data[0]);
    ScmObj accs = SCM_OBJ(data[1]);
    ScmObj initargs = SCM_OBJ(data[2]);
    void *next[3];
    if (SCM_NULLP(accs)) return obj;
    next[0] = obj;
    next[1] = SCM_CDR(accs);
    next[2] = initargs;
    Scm_VMPushCC(object_initialize_cc, next, 3);
    return slot_initialize(obj, SCM_CAR(accs), initargs);
}

static ScmObj object_initialize(ScmNextMethod *nm, ScmObj *args, int nargs,
                                void *data)
{
    ScmObj obj = args[0];
    ScmObj initargs = args[1];
    ScmObj accs = Scm_ClassOf(obj)->accessors;
    void *next[3];
    if (SCM_NULLP(accs)) return obj;
    next[0] = obj;
    next[1] = SCM_CDR(accs);
    next[2] = initargs;
    Scm_VMPushCC(object_initialize_cc, next, 3);
    return slot_initialize(obj, SCM_CAR(accs), initargs);
}

static ScmClass *object_initialize_SPEC[] = {
    SCM_CLASS_OBJECT, SCM_CLASS_LIST
};
static SCM_DEFINE_METHOD(object_initialize_rec,
                         &Scm_GenericInitialize,
                         2, 0,
                         object_initialize_SPEC,
                         object_initialize, NULL);

/*=====================================================================
 * Generic function
 */

static ScmObj generic_allocate(ScmClass *klass, ScmObj initargs)
{
    ScmGeneric *instance;
    int nslots = klass->numInstanceSlots, i;
    instance = SCM_NEW2(ScmGeneric*,
                        sizeof(ScmGeneric) + sizeof(ScmObj)*nslots);
    SCM_SET_CLASS(instance, klass);
    SCM_PROCEDURE_INIT(instance, 0, 0, SCM_PROC_GENERIC, SCM_FALSE);
    instance->methods = SCM_NIL;
    instance->fallback = Scm_NoNextMethod;
    instance->data = NULL;
    scheme_slot_default(SCM_OBJ(instance));
    return SCM_OBJ(instance);
}

static int generic_print(ScmObj obj, ScmPort *port, int mode)
{
    return Scm_Printf(port, "#<generic %S (%d)>",
                      SCM_GENERIC(obj)->common.info,
                      Scm_Length(SCM_GENERIC(obj)->methods));
}

/*
 * (initialize <generic> &key name)  - default initialize function for gf
 */
static ScmObj generic_initialize(ScmNextMethod *nm, ScmObj *args, int nargs,
                                 void *data)
{
    ScmGeneric *g = SCM_GENERIC(args[0]);
    ScmObj initargs = args[1], name;
    name = Scm_GetKeyword(key_name, initargs, SCM_FALSE);
    g->common.info = name;
    return SCM_OBJ(g);
}

static ScmClass *generic_initialize_SPEC[] = {
    SCM_CLASS_GENERIC, SCM_CLASS_LIST
};
static SCM_DEFINE_METHOD(generic_initialize_rec,
                         &Scm_GenericInitialize,
                         2, 0,
                         generic_initialize_SPEC,
                         generic_initialize, NULL);

/*
 * Accessors
 */
static ScmObj generic_name(ScmGeneric *gf)
{
    return gf->common.info;
}

static void generic_name_set(ScmGeneric *gf, ScmObj val)
{
    gf->common.info = val;
}

static ScmObj generic_methods(ScmGeneric *gf)
{
    return gf->methods;
}

static void generic_methods_set(ScmGeneric *gf, ScmObj val)
{
    gf->methods = val;
}

/* Make base generic function from C */
ScmObj Scm_MakeBaseGeneric(ScmObj name,
                           ScmObj (*fallback)(ScmObj *, int, ScmGeneric*),
                           void *data)
{
    ScmGeneric *gf = SCM_GENERIC(generic_allocate(SCM_CLASS_GENERIC, SCM_NIL));
    gf->common.info = name;
    if (fallback) {
        gf->fallback = fallback;
        gf->data = data;
    }
    return SCM_OBJ(gf);
}

/* default "default method" */
ScmObj Scm_NoNextMethod(ScmObj *args, int nargs, ScmGeneric *gf)
{
    Scm_Error("no applicable method for %S with arguments %S",
              SCM_OBJ(gf), Scm_ArrayToList(args, nargs));
    return SCM_UNDEFINED;       /* dummy */
}

/* another handy "default method", which does nothing. */
ScmObj Scm_NoOperation(ScmObj *arg, int nargs, ScmGeneric *gf)
{
    return SCM_UNDEFINED;
}

ScmObj Scm_ComputeApplicableMethods(ScmGeneric *gf, ScmObj *args, int nargs)
{
    ScmObj methods = gf->methods, mp;
    ScmObj h = SCM_NIL, t;

    SCM_FOR_EACH(mp, methods) {
        ScmMethod *m = SCM_METHOD(SCM_CAR(mp));
        ScmObj *ap;
        ScmClass **sp;
        int n;
        
        if (nargs < m->common.required) continue;
        if (!m->common.optional && nargs > m->common.required) continue;
        for (ap = args, sp = m->specializers, n = 0;
             n < m->common.required;
             ap++, sp++, n++) {
            if (!Scm_SubtypeP(Scm_ClassOf(*ap), *sp)) break;
        }
        if (n == m->common.required) SCM_APPEND1(h, t, SCM_OBJ(m));
    }
    return h;
}

/* sort-methods
 *  This is a naive implementation just to make things work.
 * TODO: can't we carry around the method list in array
 * instead of list, at least internally?
 */
static inline int method_more_specific(ScmMethod *x, ScmMethod *y,
                                       ScmObj *args, int nargs)
{
    ScmClass **xs = x->specializers;
    ScmClass **ys = y->specializers;
    ScmClass *ac, **acpl;
    int i;
    SCM_ASSERT(SCM_PROCEDURE_REQUIRED(x) == SCM_PROCEDURE_REQUIRED(y));
    for (i=0; i < SCM_PROCEDURE_REQUIRED(x); i++) {
        if (xs[i] != ys[i]) {
            ac = Scm_ClassOf(args[i]);
            if (xs[i] == ac) return TRUE;
            if (ys[i] == ac) return TRUE;
            for (acpl = ac->cpa; *acpl; acpl++) {
                if (xs[i] == *acpl) return TRUE;
                if (ys[i] == *acpl) return FALSE;
            }
            Scm_Panic("internal error: couldn't determine more specific method.");
        }
    }
    /* all specializers match.  the one without optional arg is more special.*/
    if (SCM_PROCEDURE_OPTIONAL(x)) return TRUE;
    else return FALSE;
}

#define STATIC_SORT_ARRAY_SIZE  32

ScmObj Scm_SortMethods(ScmObj methods, ScmObj *args, int nargs)
{
    ScmObj starray[STATIC_SORT_ARRAY_SIZE], *array = starray;
    int cnt = 0, len = Scm_Length(methods), step, i, j, k;
    ScmObj mp;

    if (len >= STATIC_SORT_ARRAY_SIZE)
        array = SCM_NEW2(ScmObj*, sizeof(ScmObj)*len);

    SCM_FOR_EACH(mp, methods) {
        if (!Scm_TypeP(SCM_CAR(mp), SCM_CLASS_METHOD))
            Scm_Error("bad method in applicable method list: %S", SCM_CAR(mp));
        array[cnt] = SCM_CAR(mp);
        cnt++;
    }

    for (step = len/2; step > 0; step /= 2) {
        for (i=step; i<len; i++) {
            for (j=i-step; j >= 0; j -= step) {
                if (method_more_specific(SCM_METHOD(array[j]),
                                         SCM_METHOD(array[j+step]),
                                         args, nargs)) {
                    break;
                } else {
                    ScmObj tmp = array[j+step];
                    array[j+step] = array[j];
                    array[j] = tmp;
                }
            }
        }
    }
    return Scm_ArrayToList(array, len);
}

/*=====================================================================
 * Method
 */

static ScmObj method_allocate(ScmClass *klass, ScmObj initargs)
{
    ScmMethod *instance;
    int nslots = klass->numInstanceSlots, i;
    instance = SCM_NEW2(ScmMethod*,
                        sizeof(ScmMethod) + sizeof(ScmObj)*nslots);
    SCM_SET_CLASS(instance, klass);
    SCM_PROCEDURE_INIT(instance, 0, 0, SCM_PROC_METHOD, SCM_FALSE);
    instance->generic = NULL;
    instance->specializers = NULL;
    instance->func = NULL;
    scheme_slot_default(SCM_OBJ(instance));
    return SCM_OBJ(instance);
}

static int method_print(ScmObj obj, ScmPort *port, int mode)
{
    return Scm_Printf(port, "#<method %S>",
                      SCM_METHOD(obj)->common.info);
}

/*
 * (initialize <method> (&key lamdba-list generic specializers body))
 *    Method initialization.   This needs to be hardcoded, since
 *    we can't call Scheme verison of initialize to initialize the
 *    "initialize" method (chicken-and-egg circularity).
 */
static ScmObj method_initialize(ScmNextMethod *nm, ScmObj *args, int nargs,
                                void *data)
{
    ScmMethod *m = SCM_METHOD(args[0]);
    ScmGeneric *g;
    ScmObj initargs = args[1];
    ScmObj llist = Scm_GetKeyword(key_lambda_list, initargs, SCM_FALSE);
    ScmObj generic = Scm_GetKeyword(key_generic, initargs, SCM_FALSE);
    ScmObj specs = Scm_GetKeyword(key_specializers, initargs, SCM_FALSE);
    ScmObj body = Scm_GetKeyword(key_body, initargs, SCM_FALSE);
    ScmClass **specarray;
    ScmObj lp;
    int speclen, req = 0, opt = 0;

    if (!Scm_TypeP(generic, SCM_CLASS_GENERIC))
        Scm_Error("generic function required for :generic argument: %S",
                  generic);
    g = SCM_GENERIC(generic);
    if (!SCM_CLOSUREP(body))
        Scm_Error("closure required for :body argument: %S", body);
    if (!SCM_PAIRP(specs) ||(speclen = Scm_Length(specs)) < 0)
        Scm_Error("invalid specializers list: %S", specs);
    specarray = class_list_to_array(specs, speclen);

    /* find out # of args from lambda list */
    SCM_FOR_EACH(lp, llist) req++;
    if (!SCM_NULLP(lp)) opt++;

    if (SCM_PROCEDURE_REQUIRED(body) != req + opt + 1)
        Scm_Error("body doesn't match with lambda list: %S", body);
    if (speclen != req)
        Scm_Error("specializer list doesn't match with lambda list: %S",specs);
    
    m->common.required = req;
    m->common.optional = opt;
    m->common.info = Scm_Cons(g->common.info,
                              class_array_to_names(specarray, speclen));
    m->generic = g;
    m->specializers = specarray;
    m->func = NULL;
    m->data = SCM_CLOSURE(body)->code;
    m->env = SCM_CLOSURE(body)->env;
    return SCM_OBJ(m);
}

static ScmClass *method_initialize_SPEC[] = {
    SCM_CLASS_METHOD, SCM_CLASS_LIST
};
static SCM_DEFINE_METHOD(method_initialize_rec,
                         &Scm_GenericInitialize,
                         2, 0,
                         method_initialize_SPEC,
                         method_initialize, NULL);

/*
 * Accessors
 */
static ScmObj method_generic(ScmMethod *m)
{
    return m->generic ? SCM_OBJ(m->generic) : SCM_FALSE;
}

static void method_generic_set(ScmMethod *m, ScmObj val)
{
    if (SCM_GENERICP(val))
        m->generic = SCM_GENERIC(val);
    else
        Scm_Error("generic function required, but got %S", val);
}

static ScmObj method_specializers(ScmMethod *m)
{
    if (m->specializers) {
        return class_array_to_list(m->specializers, m->common.required);
    } else {
        return SCM_NIL;
    }
}

static void method_specializers_set(ScmMethod *m, ScmObj val)
{
    int len = Scm_Length(val);
    if (len != m->common.required)
        Scm_Error("specializer list doesn't match body's lambda list:", val);
    if (len == 0) 
        m->specializers = NULL;
    else 
        m->specializers = class_list_to_array(val, len);
}

/*
 * ADD-METHOD, and it's default method version.
 */
ScmObj Scm_AddMethod(ScmGeneric *gf, ScmMethod *method)
{
    if (method->generic && method->generic != gf)
        Scm_Error("method %S already added to a generic function %S",
                  method, method->generic);
    if (!SCM_FALSEP(Scm_Memq(SCM_OBJ(method), gf->methods)))
        Scm_Error("method %S already appears in a method list of generic %S"
                  " something wrong in MOP implementation?",
                  method, gf);
    method->generic = gf;
    gf->methods = Scm_Cons(SCM_OBJ(method), gf->methods);
    return SCM_UNDEFINED;
}

static ScmObj generic_addmethod(ScmNextMethod *nm, ScmObj *args, int nargs,
                                void *data)
{
    return Scm_AddMethod(SCM_GENERIC(args[0]), SCM_METHOD(args[1]));
}

static ScmClass *generic_addmethod_SPEC[] = {
    SCM_CLASS_GENERIC, SCM_CLASS_METHOD
};
static SCM_DEFINE_METHOD(generic_addmethod_rec, &Scm_GenericAddMethod, 2, 0,
                         generic_addmethod_SPEC, generic_addmethod, NULL);

/*=====================================================================
 * Next Method
 */

ScmObj Scm_MakeNextMethod(ScmGeneric *gf, ScmObj methods,
                          ScmObj *args, int nargs)
{
    ScmNextMethod *nm = SCM_NEW(ScmNextMethod);
    SCM_SET_CLASS(nm, SCM_CLASS_NEXT_METHOD);
    SCM_PROCEDURE_INIT(nm, 0, 0, SCM_PROC_NEXT_METHOD, SCM_FALSE);
    nm->generic = gf;
    nm->methods = methods;
    nm->args = args;
    nm->nargs = nargs;
    return SCM_OBJ(nm);
}

/*=====================================================================
 * Class initialization
 */

/* TODO: need a cleaner way! */
/* static declaration of some structures */

static ScmClassStaticSlotSpec class_slots[] = {
    SCM_CLASS_SLOT_SPEC("name",
                        Scm_ClassName, class_name_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("cpl",
                        Scm_ClassCPL, class_cpl_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("direct-supers", 
                        Scm_ClassDirectSupers, class_direct_supers_set,
                        SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("accessors",
                        Scm_SlotAccessors, class_accessors_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("slots",
                        Scm_ClassSlots, class_slots_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("direct-slots",
                        Scm_ClassDirectSlots, class_direct_slots_set,
                        SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("direct-subclasses", 
                        Scm_ClassDirectSubclasses,
                        class_direct_subclasses_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("num-instance-slots",
                        class_numislots, class_numislots_set, SCM_FALSE),
    { NULL }
};

static ScmClassStaticSlotSpec generic_slots[] = {
    SCM_CLASS_SLOT_SPEC("name",
                        generic_name, generic_name_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("methods",
                        generic_methods, generic_methods_set, SCM_FALSE),
    { NULL }
};

static ScmClassStaticSlotSpec method_slots[] = {
    SCM_CLASS_SLOT_SPEC("generic",
                        method_generic, method_generic_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("specializers",
                        method_specializers, method_specializers_set,
                        SCM_FALSE),
    { NULL }
};

static ScmClassStaticSlotSpec slot_accessor_slots[] = {
    SCM_CLASS_SLOT_SPEC("init-value",
                        slot_accessor_init_value, NULL, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("init-keyword",
                        slot_accessor_init_keyword, NULL, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("init-thunk",
                        slot_accessor_init_thunk, NULL, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("slot-number",
                        slot_accessor_slot_number,
                        slot_accessor_slot_number_set, SCM_FALSE),
    SCM_CLASS_SLOT_SPEC("getter-n-setter",
                        slot_accessor_scheme_accessor,
                        slot_accessor_scheme_accessor_set, SCM_FALSE),
    { NULL }
};

/* booting class metaobject */
void bootstrap_class(ScmClass *k,
                     ScmClassStaticSlotSpec *specs,
                     ScmObj (*allocate)(ScmClass*, ScmObj initargs))
{
    ScmObj slots = SCM_NIL, t;
    ScmObj acc = SCM_NIL;

    k->allocate = allocate;
    if (specs) {
        for (;specs->name; specs++) {
            ScmObj snam = SCM_INTERN(specs->name);
            acc = Scm_Acons(snam, SCM_OBJ(&specs->accessor), acc);
            SCM_APPEND1(slots, t,
                        Scm_List(snam,
                                 key_allocation, key_builtin,
                                 key_slot_accessor, SCM_OBJ(&specs->accessor),
                                 NULL));
        }
    }
    k->accessors = acc;
    k->directSlots = k->slots = slots;
}

void Scm_InitBuiltinClass(ScmClass *klass, const char *name, ScmModule *mod)
{
    ScmObj s = SCM_INTERN(name);
    klass->name = s;
    Scm_Define(mod, SCM_SYMBOL(s), SCM_OBJ(klass));
}

void Scm_InitBuiltinGeneric(ScmGeneric *gf, const char *name, ScmModule *mod)
{
    ScmObj s = SCM_INTERN(name);
    gf->common.info = s;
    Scm_Define(mod, SCM_SYMBOL(s), SCM_OBJ(gf));
}

void Scm_InitBuiltinMethod(ScmMethod *m)
{
    m->common.info = Scm_Cons(m->generic->common.info,
                              class_array_to_names(m->specializers,
                                                   m->common.required));
    Scm_AddMethod(m->generic, m);
}

void Scm__InitClass(void)
{
    ScmModule *mod = Scm_SchemeModule();
    ScmClass *nullcpa[] = { NULL }; /* for <top> */

    key_allocation = SCM_MAKE_KEYWORD("allocation");
    key_instance = SCM_MAKE_KEYWORD("instance");
    key_builtin = SCM_MAKE_KEYWORD("builtin");
    key_accessor = SCM_MAKE_KEYWORD("accessor");
    key_slot_accessor = SCM_MAKE_KEYWORD("slot-accessor");
    key_name = SCM_MAKE_KEYWORD("name");
    key_supers = SCM_MAKE_KEYWORD("supers");
    key_slots = SCM_MAKE_KEYWORD("slots");
    key_metaclass = SCM_MAKE_KEYWORD("metaclass");
    key_lambda_list = SCM_MAKE_KEYWORD("lambda-list");
    key_generic = SCM_MAKE_KEYWORD("generic");
    key_specializers = SCM_MAKE_KEYWORD("specializers");
    key_body = SCM_MAKE_KEYWORD("body");
    key_init_keyword = SCM_MAKE_KEYWORD("init-keyword");
    key_init_thunk = SCM_MAKE_KEYWORD("init-thunk");
    key_init_value = SCM_MAKE_KEYWORD("init-value");
    key_slot_num = SCM_MAKE_KEYWORD("slot-number");
    key_slot_ref = SCM_MAKE_KEYWORD("slot-ref");
    key_slot_set = SCM_MAKE_KEYWORD("slot-set!");

    /* booting class metaobject */
    Scm_TopClass.cpa = nullcpa;
    Scm_ObjectClass.allocate = object_allocate;
    bootstrap_class(&Scm_ClassClass, class_slots, class_allocate);
    bootstrap_class(&Scm_GenericClass, generic_slots, generic_allocate);
    Scm_GenericClass.flags |= SCM_CLASS_APPLICABLE;
    bootstrap_class(&Scm_MethodClass, method_slots, method_allocate);
    Scm_MethodClass.flags |= SCM_CLASS_APPLICABLE;
    Scm_NextMethodClass.flags |= SCM_CLASS_APPLICABLE;
    bootstrap_class(&Scm_SlotAccessorClass, slot_accessor_slots,
                    slot_accessor_allocate);

#define CINIT(cl, nam) \
    Scm_InitBuiltinClass(cl, nam, mod)
    
    /* class.c */
    CINIT(SCM_CLASS_TOP,              "<top>");
    CINIT(SCM_CLASS_BOOL,             "<boolean>");
    CINIT(SCM_CLASS_CHAR,             "<char>");
    CINIT(SCM_CLASS_UNKNOWN,          "<unknown>");
    CINIT(SCM_CLASS_OBJECT,           "<object>");
    CINIT(SCM_CLASS_CLASS,            "<class>");
    CINIT(SCM_CLASS_GENERIC,          "<generic>");
    CINIT(SCM_CLASS_METHOD,           "<method>");
    CINIT(SCM_CLASS_NEXT_METHOD,      "<next-method>");
    CINIT(SCM_CLASS_SLOT_ACCESSOR,    "<slot-accessor>");
    CINIT(SCM_CLASS_COLLECTION,       "<collection>");
    CINIT(SCM_CLASS_SEQUENCE,         "<sequence>");

    /* compile.c */
    CINIT(SCM_CLASS_IDENTIFIER,       "<identifier>");
    CINIT(SCM_CLASS_SOURCE_INFO,      "<source-info>");

    /* error.c */
    CINIT(SCM_CLASS_EXCEPTION,        "<exception>");

    /* hash.c */
    CINIT(SCM_CLASS_HASHTABLE,        "<hash-table>");

    /* keyword.c */
    CINIT(SCM_CLASS_KEYWORD,          "<keyword>");

    /* list.c */
    CINIT(SCM_CLASS_LIST,             "<list>");
    CINIT(SCM_CLASS_PAIR,             "<pair>");
    CINIT(SCM_CLASS_NULL,             "<null>");

    /* macro.c */
    CINIT(SCM_CLASS_SYNTAX,           "<syntax>");
    CINIT(SCM_CLASS_SYNTAX_PATTERN,   "<syntax-pattern>");
    CINIT(SCM_CLASS_SYNTAX_RULES,     "<syntax-rules>");

    /* module.c */
    CINIT(SCM_CLASS_MODULE,           "<module>");

    /* number.c */
    CINIT(SCM_CLASS_NUMBER,           "<number>");
    CINIT(SCM_CLASS_COMPLEX,          "<complex>");
    CINIT(SCM_CLASS_REAL,             "<real>");
    CINIT(SCM_CLASS_INTEGER,          "<integer>");

    /* port.c */
    CINIT(SCM_CLASS_PORT,             "<port>");

    /* proc.c */
    CINIT(SCM_CLASS_PROCEDURE,        "<procedure>");

    /* promise.c */
    CINIT(SCM_CLASS_PROMISE,          "<promise>");

    /* string.c */
    CINIT(SCM_CLASS_STRING,           "<string>");

    /* symbol.c */
    CINIT(SCM_CLASS_SYMBOL,           "<symbol>");
    CINIT(SCM_CLASS_GLOC,             "<gloc>");

    /* system.c */
    CINIT(SCM_CLASS_SYS_STAT,         "<sys-stat>");
    CINIT(SCM_CLASS_SYS_TIME,         "<sys-time>");
    CINIT(SCM_CLASS_SYS_TM,           "<sys-tm>");
    CINIT(SCM_CLASS_SYS_GROUP,        "<sys-group>");
    CINIT(SCM_CLASS_SYS_PASSWD,       "<sys-passwd>");
    
    /* vector.c */
    CINIT(SCM_CLASS_VECTOR,           "<vector>");
    
    /* vm.c */
    CINIT(SCM_CLASS_VM,               "<vm>");

#define GINIT(gf, nam) \
    Scm_InitBuiltinGeneric(gf, nam, mod);

    GINIT(&Scm_GenericMake, "make");
    GINIT(&Scm_GenericAllocate, "allocate-instance");
    GINIT(&Scm_GenericInitialize, "initialize");
    GINIT(&Scm_GenericAddMethod, "add-method!");
    GINIT(&Scm_GenericComputeCPL, "compute-cpl");
    GINIT(&Scm_GenericComputeSlots, "compute-slots");
    GINIT(&Scm_GenericComputeGetNSet, "compute-get-n-set");
    GINIT(&Scm_GenericSlotMissing, "slot-missing");
    GINIT(&Scm_GenericSlotUnbound, "slot-unbound");

    Scm_InitBuiltinMethod(&class_allocate_rec);
    Scm_InitBuiltinMethod(&class_compute_cpl_rec);
    Scm_InitBuiltinMethod(&object_initialize_rec);
    Scm_InitBuiltinMethod(&generic_initialize_rec);
    Scm_InitBuiltinMethod(&generic_addmethod_rec);
    Scm_InitBuiltinMethod(&method_initialize_rec);
}
