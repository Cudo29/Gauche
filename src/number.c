/*
 * number.c - numeric functions
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
 *  $Id: number.c,v 1.16 2001-03-17 09:17:51 shiro Exp $
 */

#include <math.h>
#include <limits.h>
#include <string.h>
#include "gauche.h"

#ifndef M_PI
#define M_PI 3.14159265358979323
#endif

/* Linux gcc have those, but the declarations aren't included unless
   __USE_ISOC9X is defined.  Just in case. */
#ifdef HAVE_TRUNC
extern double trunc(double);
#endif
#ifdef HAVE_RINT
extern double rint(double);
#endif

/*
 * Classes of Numeric Tower
 */

static ScmClass *numeric_cpl[] = {
    SCM_CLASS_REAL,
    SCM_CLASS_COMPLEX,
    SCM_CLASS_NUMBER,
    SCM_CLASS_TOP,
    NULL
};

static int number_print(ScmObj obj, ScmPort *port, int mode);

SCM_DEFINE_BUILTIN_CLASS(Scm_NumberClass,
                         number_print, NULL, NULL, NULL,
                         numeric_cpl+4);
SCM_DEFINE_BUILTIN_CLASS(Scm_ComplexClass,
                         number_print, NULL, NULL, NULL,
                         numeric_cpl+3);
SCM_DEFINE_BUILTIN_CLASS(Scm_RealClass,
                         number_print, NULL, NULL, NULL,
                         numeric_cpl+2);
SCM_DEFINE_BUILTIN_CLASS(Scm_IntegerClass,
                         number_print, NULL, NULL, NULL,
                         numeric_cpl+1);

/*=====================================================================
 *  Flonums
 */

ScmObj Scm_MakeFlonum(double d)
{
    ScmFlonum *f = SCM_NEW(ScmFlonum);
    SCM_SET_CLASS(f, SCM_CLASS_REAL);
    f->value = d;
    return SCM_OBJ(f);
}

ScmObj Scm_MakeFlonumToNumber(double d, int exact)
{
    if (exact) {
        /* see if d can be demoted to integer */
        double i, f;
        f = modf(d, &i);
        if (f == 0.0) {
            /* TODO: range check */
            return SCM_MAKE_INT((int)i);
        }
    }
    return Scm_MakeFlonum(d);
}

/*=======================================================================
 *  Complex numbers
 */

ScmObj Scm_MakeComplex(double r, double i)
{
    ScmComplex *c = SCM_NEW_ATOMIC(ScmComplex);
    SCM_SET_CLASS(c, SCM_CLASS_COMPLEX);
    c->real = r;
    c->imag = i;
    return SCM_OBJ(c);
}

ScmObj Scm_Magnitude(ScmObj z)
{
    double m;
    if (SCM_REALP(z)) {
        m = fabs(Scm_GetDouble(z));
    } else if (!SCM_COMPLEXP(z)) {
        Scm_Error("number required, but got %S", z);
        m = 0.0;                /* dummy */
    } else {
        double r = SCM_COMPLEX_REAL(z);
        double i = SCM_COMPLEX_IMAG(z);
        m = sqrt(r*r+i*i);
    }
    return Scm_MakeFlonum(m);
}

ScmObj Scm_Angle(ScmObj z)
{
    double a;
    if (SCM_REALP(z)) {
        a = (Scm_Sign(z) < 0)? M_PI : 0.0;
    } else if (!SCM_COMPLEXP(z)) {
        Scm_Error("number required, but got %S", z);
        a = 0.0;                /* dummy */
    } else {
        double r = SCM_COMPLEX_REAL(z);
        double i = SCM_COMPLEX_IMAG(z);
        a = atan2(i, r);
    }
    return Scm_MakeFlonum(a);
}

/*=======================================================================
 *  Coertion
 */

ScmObj Scm_MakeInteger(long i)
{
    if (i >= SCM_SMALL_INT_MIN && i <= SCM_SMALL_INT_MAX) {
        return SCM_MAKE_INT(i);
    } else {
        return Scm_MakeBignumFromSI(i);
    }
}

ScmObj Scm_MakeIntegerFromUI(u_long i)
{
    if (i <= SCM_SMALL_INT_MAX) return SCM_MAKE_INT(i);
    else return Scm_MakeBignumFromUI(i);
}

/* Convert scheme integer to C integer. Overflow is neglected. */
long Scm_GetInteger(ScmObj obj)
{
    if (SCM_INTP(obj)) return SCM_INT_VALUE(obj);
    else if (SCM_BIGNUMP(obj)) return Scm_BignumToSI(SCM_BIGNUM(obj));
    else if (SCM_FLONUMP(obj)) return (long)SCM_FLONUM_VALUE(obj);
    else return 0;
}

u_long Scm_GetUInteger(ScmObj obj)
{
    if (SCM_INTP(obj)) return SCM_INT_VALUE(obj);
    else if (SCM_BIGNUMP(obj)) return Scm_BignumToUI(SCM_BIGNUM(obj));
    else if (SCM_FLONUMP(obj)) return (u_long)SCM_FLONUM_VALUE(obj);
    else return 0;
}

double Scm_GetDouble(ScmObj obj)
{
    if (SCM_FLONUMP(obj)) return SCM_FLONUM_VALUE(obj);
    else if (SCM_INTP(obj)) return (double)SCM_INT_VALUE(obj);
    else if (SCM_BIGNUMP(obj)) return Scm_BignumToDouble(SCM_BIGNUM(obj));
    else return 0.0;
}

/*
 *   Generic Methods
 */

/* Predicates */

ScmObj Scm_NumberP(ScmObj obj)
{
    return SCM_NUMBERP(obj)? SCM_TRUE : SCM_FALSE;
}

ScmObj Scm_IntegerP(ScmObj obj)
{
    if (SCM_INTP(obj) || SCM_BIGNUMP(obj)) return SCM_TRUE;
    if (SCM_FLONUMP(obj)) {
        double d = SCM_FLONUM_VALUE(obj);
        double f, i;
        if ((f = modf(d, &i)) == 0.0) return SCM_TRUE;
        return SCM_FALSE;
    }
    if (SCM_COMPLEXP(obj)) {
        if (SCM_COMPLEX_IMAG(obj) == 0.0) {
            double d = SCM_FLONUM_VALUE(obj);
            double f, i;
            if ((f = modf(d, &i)) == 0.0) return SCM_TRUE;
        }
        return SCM_FALSE;
    }
    Scm_Error("number required, but got %S", obj);
    return SCM_FALSE;           /* dummy */
}

/* Unary Operator */

ScmObj Scm_Abs(ScmObj obj)
{
    if (SCM_INTP(obj)) {
        int v = SCM_INT_VALUE(obj);
        if (v < 0) obj = SCM_MAKE_INT(-v);
    } else if (SCM_BIGNUMP(obj)) {
        if (SCM_BIGNUM_SIGN(obj) < 0) {
            obj = Scm_BignumCopy(SCM_BIGNUM(obj));
            SCM_BIGNUM_SIGN(obj) = 1;
        }
    } else if (SCM_FLONUMP(obj)) {
        double v = SCM_FLONUM_VALUE(obj);
        if (v < 0) obj = Scm_MakeFlonum(-v);
    } else if (SCM_COMPLEXP(obj)) {
        double r = SCM_COMPLEX_REAL(obj);
        double i = SCM_COMPLEX_IMAG(obj);
        double a = sqrt(r*r+i*i);
        return Scm_MakeFlonum(a);
    } else {
        Scm_Error("number required: %S", obj);
    }
    return obj;
}

/* Return -1, 0 or 1 when arg is minus, zero or plus, respectively.
   used to implement zero?, positive? and negative? */
int Scm_Sign(ScmObj obj)
{
    int r = 0;
    
    if (SCM_INTP(obj)) {
        r = SCM_INT_VALUE(obj);
    } else if (SCM_BIGNUMP(obj)) {
        r = SCM_BIGNUM_SIGN(obj);
    } else if (SCM_FLONUMP(obj)) {
        double v = SCM_FLONUM_VALUE(obj);
        if (v != 0.0) {
            r = (v > 0.0)? 1 : -1;
        }
    } else {
        Scm_Error("real number required: %S", obj);
    }
    return r;
}

ScmObj Scm_Negate(ScmObj obj)
{
    if (SCM_INTP(obj)) {
        int v = SCM_INT_VALUE(obj);
        if (v == SCM_SMALL_INT_MIN) {
            obj = Scm_MakeBignumFromSI(-v);
        } else {
            obj = SCM_MAKE_INT(-v);
        }
    } else if (SCM_BIGNUMP(obj)) {
        obj = Scm_BignumNegate(SCM_BIGNUM(obj));
    } else if (SCM_FLONUMP(obj)) {
        obj = Scm_MakeFlonum(-SCM_FLONUM_VALUE(obj));
    } else if (SCM_COMPLEXP(obj)) {
        obj = Scm_MakeComplex(-SCM_COMPLEX_REAL(obj),
                              -SCM_COMPLEX_IMAG(obj));
    } else {
        Scm_Error("number required: %S", obj);
    }
    return obj;
}

ScmObj Scm_Reciprocal(ScmObj obj)
{
    if (SCM_INTP(obj)) {
        int val = SCM_INT_VALUE(obj);
        if (val == 0) Scm_Error("divide by zero");
        obj = Scm_MakeFlonum(1.0/(double)val);
    } else if (SCM_BIGNUMP(obj)) {
        double val = Scm_BignumToDouble(SCM_BIGNUM(obj));
        if (val == 0.0) Scm_Error("divide by zero");
        obj = Scm_MakeFlonum(1.0/val);
    } else if (SCM_FLONUMP(obj)) {
        double val = SCM_FLONUM_VALUE(obj);
        if (val == 0.0) Scm_Error("divide by zero");
        obj = Scm_MakeFlonum(1.0/val);
    } else if (SCM_COMPLEXP(obj)) {
        double r = SCM_COMPLEX_REAL(obj), r1;
        double i = SCM_COMPLEX_IMAG(obj), i1;
        double d;
        if (r == 0.0 && i == 0.0) Scm_Error("divie by zero");
        d = r*r + i*i;
        r1 = r/d;
        i1 = -i/d;
        obj = Scm_MakeComplex(r1, i1);
    } else {
        Scm_Error("number required: %S", obj);
    }
    return obj;
}

/*
 * Conversion operators
 */

ScmObj Scm_ExactToInexact(ScmObj obj)
{
    if (SCM_INTP(obj)) {
        obj = Scm_MakeFlonum((double)SCM_INT_VALUE(obj));
    } else if (SCM_BIGNUMP(obj)) {
        obj = Scm_MakeFlonum(Scm_BignumToDouble(SCM_BIGNUM(obj)));
    } else if (!SCM_FLONUMP(obj) || !SCM_COMPLEXP(obj)) {
        Scm_Error("number required: %S", obj);
    }
    return obj;
}

ScmObj Scm_InexactToExact(ScmObj obj)
{
    if (SCM_FLONUMP(obj)) {
        double d = SCM_FLONUM_VALUE(obj);
        if (d < SCM_SMALL_INT_MIN || d > SCM_SMALL_INT_MAX) {
            obj = Scm_MakeBignumFromDouble(d);
        } else {
            obj = SCM_MAKE_INT((int)d);
        }
    } else if (SCM_COMPLEXP(obj)) {
        Scm_Error("exact complex is not supported: %S", obj);
    } if (!SCM_INTP(obj) && !SCM_BIGNUMP(obj)) {
        Scm_Error("number required: %S", obj);
    }
    return obj;
}

enum NumberClass {
    FIXNUM,
    BIGNUM,
    FLONUM,
    COMPLEX,
    NONUMBER
};

#define NUMBER_CLASS(obj)                       \
    (SCM_INTP(obj)? FIXNUM :                    \
       SCM_BIGNUMP(obj)? BIGNUM :               \
         SCM_FLONUMP(obj)? FLONUM :             \
           SCM_COMPLEXP(obj)? COMPLEX: NONUMBER)

/* Type conversion:
 *   `promote' means a conversion from lower number class to higher,
 *      e.g. fixnum -> bignum -> flonum -> complex.
 *   `demote' means a conversion from higher number class to lower,
 *      e.g. complex -> flonum -> bignum -> fixnum.
 */

ScmObj Scm_PromoteToBignum(ScmObj obj)
{
    if (SCM_INTP(obj)) return Scm_MakeBignumFromSI(SCM_INT_VALUE(obj));
    if (SCM_BIGNUMP(obj)) return obj;
    Scm_Panic("Scm_PromoteToBignum: can't be here");
    return SCM_UNDEFINED;       /* dummy */
}

ScmObj Scm_PromoteToFlonum(ScmObj obj)
{
    if (SCM_INTP(obj)) return Scm_MakeFlonum(SCM_INT_VALUE(obj));
    if (SCM_BIGNUMP(obj))
        return Scm_MakeFlonum(Scm_BignumToDouble(SCM_BIGNUM(obj)));
    if (SCM_FLONUMP(obj)) return obj;
    Scm_Panic("Scm_PromoteToFlonum: can't be here");
    return SCM_UNDEFINED;       /* dummy */
}

ScmObj Scm_PromoteToComplex(ScmObj obj)
{
    if (SCM_INTP(obj))
        return Scm_MakeComplex((double)SCM_INT_VALUE(obj), 0.0);
    if (SCM_BIGNUMP(obj))
        return Scm_MakeComplex(Scm_BignumToDouble(SCM_BIGNUM(obj)), 0.0);
    if (SCM_FLONUMP(obj))
        return Scm_MakeComplex(SCM_FLONUM_VALUE(obj), 0.0);
    Scm_Panic("Scm_PromoteToComplex: can't be here");
    return SCM_UNDEFINED;       /* dummy */
}

/*===============================================================
 * Arithmetics
 */

/*
 * Addition and subtraction
 */

ScmObj Scm_Add(ScmObj args)
{
    ScmObj v;
    int result_int = 0;
    double result_real, result_imag;

    if (!SCM_PAIRP(args)) return SCM_MAKE_INT(0);

    v = SCM_CAR(args);
    args = SCM_CDR(args);

    if (SCM_INTP(v)) {
        if (!SCM_PAIRP(args)) return v;
        result_int = SCM_INT_VALUE(v);
        v = SCM_CAR(args);
        args = SCM_CDR(args);
        for (;;) {
            if (SCM_INTP(v)) {
                result_int += SCM_INT_VALUE(v);
                if (result_int > SCM_SMALL_INT_MAX 
                    || result_int < SCM_SMALL_INT_MIN) {
                    ScmObj big = Scm_MakeBignumFromSI(result_int);
                    return Scm_BignumAddN(SCM_BIGNUM(big), args);
                }
            } else if (SCM_BIGNUMP(v)) {
                return Scm_BignumAddN(SCM_BIGNUM(v), args);
            } else if (SCM_FLONUMP(v)) {
                result_real = (double)result_int;
                goto DO_FLONUM;
            } else if (SCM_COMPLEXP(v)) {
                result_real = (double)result_int;
                result_imag = 0.0;
                goto DO_COMPLEX;
            } else {
                Scm_Error("number required, but got: %S", v);
            }
            if (SCM_NULLP(args)) return Scm_MakeInteger(result_int);
            v = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    if (SCM_BIGNUMP(v)) {
        return Scm_BignumAddN(SCM_BIGNUM(v), args);
    }
    if (SCM_FLONUMP(v)) {
        if (!SCM_PAIRP(args)) return v;
        result_real = SCM_FLONUM_VALUE(v);
        v = SCM_CAR(args);
        args = SCM_CDR(args);
      DO_FLONUM:
        for (;;) {
            if (SCM_INTP(v)) {
                result_real += (double)SCM_INT_VALUE(v);
            } else if (SCM_BIGNUMP(v)) {
                result_real += Scm_BignumToDouble(SCM_BIGNUM(v));
                fprintf(stderr, "%f\n", result_real);
            } else if (SCM_FLONUMP(v)) {
                result_real += SCM_FLONUM_VALUE(v);
            } else if (SCM_COMPLEXP(v)) {
                result_imag = 0.0;
                goto DO_COMPLEX;
            } else {
                Scm_Error("number required, but got: %S", v);
            }
            if (SCM_NULLP(args)) return Scm_MakeFlonum(result_real);
            v = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    if (SCM_COMPLEXP(v)) {
        if (!SCM_PAIRP(args)) return v;
        result_real = SCM_COMPLEX_REAL(v);
        result_imag = SCM_COMPLEX_IMAG(v);
        v = SCM_CAR(args);
        args = SCM_CDR(args);
      DO_COMPLEX:
        for (;;) {
            if (SCM_INTP(v)) {
                result_real += (double)SCM_INT_VALUE(v);
            } else if (SCM_BIGNUMP(v)) {
                result_real += Scm_BignumToDouble(SCM_BIGNUM(v));
            } else if (SCM_FLONUMP(v)) {
                result_real += SCM_FLONUM_VALUE(v);
            } else if (SCM_COMPLEXP(v)) {
                result_real += SCM_COMPLEX_REAL(v);
                result_imag += SCM_COMPLEX_IMAG(v);
            } else {
                Scm_Error("number required, but got: %S", v);
            }
            if (!SCM_PAIRP(args)) {
                if (result_imag == 0.0)
                    return Scm_MakeFlonum(result_real);
                else
                    return Scm_MakeComplex(result_real, result_imag);
            }
            v = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    Scm_Error("number required: %S", v);
    return SCM_UNDEFINED;       /* NOTREACHED */
}

ScmObj Scm_Subtract(ScmObj arg0, ScmObj arg1, ScmObj args)
{
    int result_int = 0;
    double result_real = 0.0, result_imag = 0.0;
    int nc0, nc1;

    if (SCM_INTP(arg0)) {
        result_int = SCM_INT_VALUE(arg0);
        for (;;) {
            if (SCM_INTP(arg1)) {
                result_int -= SCM_INT_VALUE(arg1);
                if (result_int < SCM_SMALL_INT_MIN
                    || result_int > SCM_SMALL_INT_MAX) {
                    ScmObj big = Scm_MakeBignumFromSI(result_int);
                    return Scm_BignumSubN(SCM_BIGNUM(big), args);
                }
            } else if (SCM_BIGNUMP(arg1)) {
                ScmObj big = Scm_MakeBignumFromSI(result_int);
                return Scm_BignumSubN(SCM_BIGNUM(big), Scm_Cons(arg1, args));
            } else if (SCM_FLONUMP(arg1)) {
                result_real = (double)result_int;
                goto DO_FLONUM;
            } else if (SCM_COMPLEXP(arg1)) {
                result_real = (double)result_int;
                goto DO_COMPLEX;
            } else {
                Scm_Error("number required, but got %S", arg1);
            }
            if (SCM_NULLP(args))
                return SCM_MAKE_INT(result_int);
            arg1 = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    if (SCM_BIGNUMP(arg0)) {
        return Scm_BignumSubN(SCM_BIGNUM(arg0), Scm_Cons(arg1, args));
    }
    if (SCM_FLONUMP(arg0)) {
        result_real = SCM_FLONUM_VALUE(arg0);
      DO_FLONUM:
        for (;;) {
            if (SCM_INTP(arg1)) {
                result_real -= (double)SCM_INT_VALUE(arg1);
            } else if (SCM_BIGNUMP(arg1)) {
                result_real -= Scm_BignumToDouble(SCM_BIGNUM(arg1));
            } else if (SCM_FLONUMP(arg1)) {
                result_real -= SCM_FLONUM_VALUE(arg1);
            } else if (SCM_COMPLEXP(arg1)) {
                goto DO_COMPLEX;
            } else {
                Scm_Error("number required, but got %S", arg1);
            }
            if (SCM_NULLP(args))
                return Scm_MakeFlonum(result_real);
            arg1 = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    if (SCM_COMPLEXP(arg0)) {
        result_real = SCM_COMPLEX_REAL(arg0);
        result_imag = SCM_COMPLEX_IMAG(arg0);
      DO_COMPLEX:
        for (;;) {
            if (SCM_INTP(arg1)) {
                result_real -= (double)SCM_INT_VALUE(arg1);
            } else if (SCM_BIGNUMP(arg1)) {
                result_real -= Scm_BignumToDouble(SCM_BIGNUM(arg1));
            } else if (SCM_FLONUMP(arg1)) {
                result_real -= SCM_FLONUM_VALUE(arg1);
            } else if (SCM_COMPLEXP(arg1)) {
                result_real -= SCM_COMPLEX_REAL(arg1);
                result_imag -= SCM_COMPLEX_IMAG(arg1);
            } else {
                Scm_Error("number required, but got %S", arg1);
            }
            if (SCM_NULLP(args))
                return Scm_MakeComplex(result_real, result_imag);
            arg1 = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    Scm_Error("number required: %S", arg1);
    return SCM_UNDEFINED;       /* NOTREACHED */
}

/*
 * Multiplication
 */

ScmObj Scm_Multiply(ScmObj args)
{
    ScmObj v;
    long result_int;
    double result_real, result_imag;

    if (!SCM_PAIRP(args)) return SCM_MAKE_INT(1);

    v = SCM_CAR(args);
    args = SCM_CDR(args);

    if (SCM_INTP(v)) {
        if (!SCM_PAIRP(args)) return v;
        result_int = SCM_INT_VALUE(v);
        v = SCM_CAR(args);
        args = SCM_CDR(args);
        for (;;) {
            if (SCM_INTP(v)) {
                long vv = SCM_INT_VALUE(v);
                long k = result_int * vv;
                /* TODO: need a better way to check overflow */
                if ((vv != 0 && k/vv != result_int)
                    || k < SCM_SMALL_INT_MIN
                    || k > SCM_SMALL_INT_MAX) {
                    ScmObj big = Scm_MakeBignumFromSI(result_int);
                    big = Scm_BignumMulSI(SCM_BIGNUM(big), vv);
                    return Scm_BignumMulN(SCM_BIGNUM(big), args);
                }
                result_int = k;
            } else if (SCM_BIGNUMP(v)) {
                ScmObj big = Scm_BignumMulSI(SCM_BIGNUM(v), result_int);
                return Scm_BignumMulN(SCM_BIGNUM(big), args);
            } else if (SCM_FLONUMP(v)) {
                result_real = (double)result_int;
                goto DO_FLONUM;
            } else if (SCM_COMPLEXP(v)) {
                result_real = (double)result_int;
                result_imag = 0.0;
                goto DO_COMPLEX;
            } else {
                Scm_Error("number required, but got: %S", v);
            }
            if (SCM_NULLP(args)) return Scm_MakeInteger(result_int);
            v = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    if (SCM_BIGNUMP(v)) {
        return Scm_BignumMulN(SCM_BIGNUM(v), args);
    }
    if (SCM_FLONUMP(v)) {
        if (!SCM_PAIRP(args)) return v;
        result_real = SCM_FLONUM_VALUE(v);
        v = SCM_CAR(args);
        args = SCM_CDR(args);
      DO_FLONUM:
        for (;;) {
            if (SCM_INTP(v)) {
                result_real *= (double)SCM_INT_VALUE(v);
            } else if (SCM_BIGNUMP(v)) {
                result_real *= Scm_BignumToDouble(SCM_BIGNUM(v));
            } else if (SCM_FLONUMP(v)) {
                result_real *= SCM_FLONUM_VALUE(v);
            } else if (SCM_COMPLEXP(v)) {
                result_imag = 0.0;
                goto DO_COMPLEX;
            } else {
                Scm_Error("number required, but got: %S", v);
            }
            if (SCM_NULLP(args)) return Scm_MakeFlonum(result_real);
            v = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    if (SCM_COMPLEXP(v)) {
        if (!SCM_PAIRP(args)) return v;
        result_real = SCM_COMPLEX_REAL(v);
        result_imag = SCM_COMPLEX_IMAG(v);
        v = SCM_CAR(args);
        args = SCM_CDR(args);
      DO_COMPLEX:
        for (;;) {
            if (SCM_INTP(v)) {
                result_real *= (double)SCM_INT_VALUE(v);
                result_imag *= (double)SCM_INT_VALUE(v);
            } else if (SCM_BIGNUMP(v)) {
                double dd = Scm_BignumToDouble(SCM_BIGNUM(v));
                result_real *= dd;
                result_imag *= dd;
            } else if (SCM_FLONUMP(v)) {
                result_real *= SCM_FLONUM_VALUE(v);
                result_imag *= SCM_FLONUM_VALUE(v);
            } else if (SCM_COMPLEXP(v)) {
                double r = SCM_COMPLEX_REAL(v);
                double i = SCM_COMPLEX_IMAG(v);
                double t = result_real * r - result_imag * i;
                result_imag   = result_real * i + result_imag * r;
                result_real = t;
            } else {
                Scm_Error("number required, but got: %S", v);
            }
            if (!SCM_PAIRP(args)) {
                if (result_imag == 0.0)
                    return Scm_MakeFlonum(result_real);
                else
                    return Scm_MakeComplex(result_real, result_imag);
            }
            v = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    Scm_Error("number required: %S", v);
    return SCM_UNDEFINED;       /* NOTREACHED */
}

/*
 * Division
 */

ScmObj Scm_Divide(ScmObj arg0, ScmObj arg1, ScmObj args)
{
    double result_real = 0.0, result_imag = 0.0, div_real, div_imag;
    int exact = 1;

    if (SCM_INTP(arg0)) {
        result_real = (double)SCM_INT_VALUE(arg0);
        goto DO_FLONUM;
    }
    if (SCM_FLONUMP(arg0)) {
        result_real = SCM_FLONUM_VALUE(arg0);
        exact = 0;
      DO_FLONUM:
        for (;;) {
            if (SCM_INTP(arg1)) {
                div_real = (double)SCM_INT_VALUE(arg1);
            } else if (SCM_FLONUMP(arg1)) {
                div_real = SCM_FLONUM_VALUE(arg1);
                exact = 0;
            } else if (SCM_COMPLEXP(arg1)) {
                goto DO_COMPLEX;
            } else {
                Scm_Error("number required, but got %S", arg1);
            }
            if (div_real == 0) 
                Scm_Error("divide by zero");
            result_real /= div_real;
            if (SCM_NULLP(args))
                return Scm_MakeFlonumToNumber(result_real, exact);
            arg1 = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    if (SCM_COMPLEXP(arg0)) {
        double d, r, i;
        result_real = SCM_COMPLEX_REAL(arg0);
        result_imag = SCM_COMPLEX_IMAG(arg0);
        div_imag = 0.0;
      DO_COMPLEX:
        for (;;) {
            if (SCM_INTP(arg1)) {
                div_real = (double)SCM_INT_VALUE(arg1);
            } else if (SCM_FLONUMP(arg1)) {
                div_real = SCM_FLONUM_VALUE(arg1);
            } else if (SCM_COMPLEXP(arg1)) {
                div_real = SCM_COMPLEX_REAL(arg1);
                div_imag = SCM_COMPLEX_IMAG(arg1);
            } else {
                Scm_Error("number required, but got %S", arg1);
            }
            d = div_real*div_real + div_imag*div_imag;
            if (d == 0.0)
                Scm_Error("divide by zero");
            r = (result_real*div_real + result_imag*div_imag)/d;
            i = (result_imag*div_real - result_real*div_imag)/d;
            result_real = r;
            result_imag = i;
            if (SCM_NULLP(args))
                return Scm_MakeComplex(result_real, result_imag);
            arg1 = SCM_CAR(args);
            args = SCM_CDR(args);
        }
    }
    Scm_Error("number required: %S", arg0);
    return SCM_UNDEFINED;       /* NOTREACHED */
}

/*
 * Integer division
 */
ScmObj Scm_Quotient(ScmObj x, ScmObj y)
{
    double rx, ry, f, i;
    if (SCM_INTP(x)) {
        if (SCM_INTP(y)) {
            int r;
            if (SCM_INT_VALUE(y) == 0) goto DIVBYZERO;
            r = SCM_INT_VALUE(x)/SCM_INT_VALUE(y);
            return SCM_MAKE_INT(r);
        }
        rx = (double)SCM_INT_VALUE(x);
        if (SCM_FLONUMP(y)) {
            ry = SCM_FLONUM_VALUE(y);
            if (ry != floor(ry)) goto BADARGY;
            goto DO_FLONUM;
        }
        goto BADARGY;
    } else if (SCM_FLONUMP(x)) {
        rx = SCM_FLONUM_VALUE(x);
        if (rx != floor(rx)) goto BADARG;
        if (SCM_INTP(y)) {
            ry = (double)SCM_INT_VALUE(y);
        } else if (SCM_FLONUMP(y)) {
            ry = SCM_FLONUM_VALUE(y);
            if (ry != floor(ry)) goto BADARGY;
        } else {
            goto BADARGY;
        }
      DO_FLONUM:
        if (ry == 0.0) goto DIVBYZERO;
        f = modf(rx/ry, &i);
        return Scm_MakeFlonum(i);
    }
  DIVBYZERO:
    Scm_Error("divide by zero");
  BADARGY:
    x = y;
  BADARG:
    Scm_Error("integer required, but got %S", x);
    return SCM_UNDEFINED;       /* dummy */
}

/* Modulo and Reminder.
   TODO: on gcc, % works like reminder.  I'm not sure the exact behavior
   of % is defined in ANSI C.  Need to check it later. */
ScmObj Scm_Modulo(ScmObj x, ScmObj y, int reminder)
{
    double rx, ry, div, rem;
    if (SCM_INTP(x)) {
        if (SCM_INTP(y)) {
            int r;
            if (SCM_INT_VALUE(y) == 0) goto DIVBYZERO;
            r = SCM_INT_VALUE(x)%SCM_INT_VALUE(y);
            if (!reminder) {
                if ((SCM_INT_VALUE(x) > 0 && SCM_INT_VALUE(y) < 0)
                    || (SCM_INT_VALUE(x) < 0 && SCM_INT_VALUE(y) > 0)) {
                    r += SCM_INT_VALUE(y);
                }
            }
            return SCM_MAKE_INT(r);
        }
        rx = (double)SCM_INT_VALUE(x);
        if (SCM_FLONUMP(y)) {
            ry = SCM_FLONUM_VALUE(y);
            if (ry != floor(ry)) goto BADARGY;
            goto DO_FLONUM;
        }
        goto BADARGY;
    } else if (SCM_FLONUMP(x)) {
        rx = SCM_FLONUM_VALUE(x);
        if (rx != floor(rx)) goto BADARG;
        if (SCM_INTP(y)) {
            ry = (double)SCM_INT_VALUE(y);
        } else if (SCM_FLONUMP(y)) {
            ry = SCM_FLONUM_VALUE(y);
            if (ry != floor(ry)) goto BADARGY;
        } else {
            goto BADARGY;
        }
      DO_FLONUM:
        if (ry == 0.0) goto DIVBYZERO;
        div = floor(rx/ry);
        rem = rx - (div * ry);
        if (!reminder) {
            if ((rx > 0 && ry < 0) || (rx < 0 && ry > 0)) {
                rem += ry;
            }
        }
        return Scm_MakeFlonum(rem);
    }
  DIVBYZERO:
    Scm_Error("divide by zero");
  BADARGY:
    x = y;
  BADARG:
    Scm_Error("integer required, but got %S", x);
    return SCM_UNDEFINED;       /* dummy */
}

/*===============================================================
 * Comparison
 */

/* We support more than two args, but optmize for two-arg case.
 * Expecially, the third and after args are not checked until 
 * they are actually used, so (= 3 2 #f) doesn't report an error,
 * for example.
 */
ScmObj Scm_NumEq(ScmObj arg0, ScmObj arg1, ScmObj args)
{
    int nc0 = NUMBER_CLASS(arg0);
    int nc1 = NUMBER_CLASS(arg1);

    /* deal with the most common case first. */
    if (nc0 == nc1) {
        if (nc0 == FIXNUM) {
            if (arg0 != arg1)   /* we can use EQ here */
                return SCM_FALSE;
            if (SCM_PAIRP(args))
                return Scm_NumEq(arg1, SCM_CAR(args), SCM_CDR(args));
            else
                return SCM_TRUE;
        }
        if (nc0 == BIGNUM) {
            if (Scm_BignumCmp(SCM_BIGNUM(arg0), SCM_BIGNUM(arg1)) != 0)
                return SCM_FALSE;
            if (SCM_PAIRP(args))
                return Scm_NumEq(arg1, SCM_CAR(args), SCM_CDR(args));
            else
                return SCM_TRUE;
        }
        if (nc0 == FLONUM) {
            if (SCM_FLONUM_VALUE(arg0) != SCM_FLONUM_VALUE(arg1))
                return SCM_FALSE;
            if (SCM_PAIRP(args))
                return Scm_NumEq(arg1, SCM_CAR(args), SCM_CDR(args));
            else
                return SCM_TRUE;
        }
        if (nc0 == COMPLEX) {
            if (SCM_COMPLEX_REAL(arg0) != SCM_COMPLEX_REAL(arg1)
                || SCM_COMPLEX_IMAG(arg0) != SCM_COMPLEX_IMAG(arg1))
                return SCM_FALSE;
            if (SCM_PAIRP(args))
                return Scm_NumEq(arg1, SCM_CAR(args), SCM_CDR(args));
            else
                return SCM_TRUE;
        }
        Scm_Error("number required: %S", arg0);
    }

    /* Need type coertion.  I assume this is less common case,
       so forgetaboutspeed. */
    if (nc0 < nc1) {            /* let arg0 be higher class */
        int tmpi; ScmObj tmps;
        tmpi = nc0;  nc0 = nc1;   nc1 = tmpi;
        tmps = arg0; arg0 = arg1; arg1 = tmps;
    }
    if (nc0 == BIGNUM) {
        return Scm_NumEq(arg0, Scm_PromoteToBignum(arg1), args);
    }
    if (nc0 == FLONUM) {
        return Scm_NumEq(arg0, Scm_PromoteToFlonum(arg1), args);
    }
    if (nc0 == COMPLEX) {
        return Scm_NumEq(arg0, Scm_PromoteToComplex(arg1), args);
    }
    Scm_Error("number required: %S", arg0);
    return SCM_UNDEFINED;       /* NOTREACHED */
}

#define NUMCMP(FN, OP)                                                  \
ScmObj FN(ScmObj arg0, ScmObj arg1, ScmObj args)                        \
{                                                                       \
    int nc0 = NUMBER_CLASS(arg0);                                       \
    int nc1 = NUMBER_CLASS(arg1);                                       \
                                                                        \
    if (nc0 == nc1) {                                                   \
        if (nc0 == FIXNUM) {                                            \
            if (SCM_INT_VALUE(arg0) OP SCM_INT_VALUE(arg1))             \
                return SCM_FALSE;                                       \
            if (SCM_PAIRP(args))                                        \
                return FN(arg1, SCM_CAR(args), SCM_CDR(args));          \
            else return SCM_TRUE;                                       \
        }                                                               \
        if (nc0 == BIGNUM) {                                            \
            if (Scm_BignumCmp(SCM_BIGNUM(arg0), SCM_BIGNUM(arg1)) OP 0) \
                return SCM_FALSE;                                       \
            if (SCM_PAIRP(args))                                        \
                return FN(arg1, SCM_CAR(args), SCM_CDR(args));          \
            else return SCM_TRUE;                                       \
        }                                                               \
        if (nc0 == FLONUM) {                                            \
            if (SCM_FLONUM_VALUE(arg0) OP SCM_FLONUM_VALUE(arg1))       \
                return SCM_FALSE;                                       \
            if (SCM_PAIRP(args))                                        \
                return FN(arg1, SCM_CAR(args), SCM_CDR(args));          \
            else return SCM_TRUE;                                       \
        }                                                               \
        Scm_Error("real number required: %S", arg0);                    \
    }                                                                   \
                                                                        \
    if (nc0 < nc1) {                                                    \
        if (nc1 == BIGNUM)                                              \
            return FN(Scm_PromoteToBignum(arg0), arg1, args);           \
        if (nc1 == FLONUM)                                              \
            return FN(Scm_PromoteToFlonum(arg0), arg1, args);           \
        else                                                            \
            Scm_Error("real number required: %S", arg1);                \
    } else {                                                            \
        if (nc0 == BIGNUM)                                              \
            return FN(arg0, Scm_PromoteToBignum(arg1), args);           \
        if (nc0 == FLONUM)                                              \
            return FN(arg0, Scm_PromoteToFlonum(arg1), args);           \
        else                                                            \
            Scm_Error("real number required: %S", arg0);                \
    }                                                                   \
    /*NOTREACHED*/                                                      \
    return SCM_UNDEFINED;                                               \
}

NUMCMP(Scm_NumLt, >=)
NUMCMP(Scm_NumLe, >)
NUMCMP(Scm_NumGt, <=)
NUMCMP(Scm_NumGe, <)

ScmObj Scm_Max(ScmObj arg0, ScmObj args)
{
    int nc0 = NUMBER_CLASS(arg0), nc1;
    ScmObj arg1;

    if (nc0 > FLONUM) Scm_Error("real number required, but got %S", arg0);

    for (;;) {
        if (SCM_NULLP(args)) return arg0;

        arg1 = SCM_CAR(args);
        args = SCM_CDR(args);
        nc1 = NUMBER_CLASS(arg1);

        if (nc0 < nc1) {
            if (nc1 == FLONUM) {
                arg0 = Scm_PromoteToFlonum(arg0);
                nc0 = FLONUM;
            } else {
                Scm_Error("real number required, but got %S", arg1);
            }
        } else if (nc0 > nc1) {
            arg1 = Scm_PromoteToFlonum(arg1);
        }

        if (nc0 == FIXNUM) {
            if (SCM_INT_VALUE(arg0) < SCM_INT_VALUE(arg1)) {
                arg0 = arg1;
            }
        } else {
            if (SCM_FLONUM_VALUE(arg0) < SCM_FLONUM_VALUE(arg1)) {
                arg0 = arg1;
            }
        }
    }
}

ScmObj Scm_Min(ScmObj arg0, ScmObj args)
{
    int nc0 = NUMBER_CLASS(arg0), nc1;
    ScmObj arg1;

    if (nc0 > FLONUM) Scm_Error("real number required, but got %S", arg0);

    for (;;) {
        if (SCM_NULLP(args)) return arg0;

        arg1 = SCM_CAR(args);
        args = SCM_CDR(args);
        nc1 = NUMBER_CLASS(arg1);

        if (nc0 < nc1) {
            if (nc1 == FLONUM) {
                arg0 = Scm_PromoteToFlonum(arg0);
                nc0 = FLONUM;
            } else {
                Scm_Error("real number required, but got %S", arg1);
            }
        } else if (nc0 > nc1) {
            arg1 = Scm_PromoteToFlonum(arg1);
        }

        if (nc0 == FIXNUM) {
            if (SCM_INT_VALUE(arg0) > SCM_INT_VALUE(arg1)) {
                arg0 = arg1;
            }
        } else {
            if (SCM_FLONUM_VALUE(arg0) > SCM_FLONUM_VALUE(arg1)) {
                arg0 = arg1;
            }
        }
    }
}

/*===============================================================
 * ROUNDING
 */

ScmObj Scm_Round(ScmObj num, int mode)
{
    double r, v;
    
    if (SCM_EXACTP(num)) return num;
    if (!SCM_FLONUMP(num))
        Scm_Error("real number required, but got %S", num);
    v = SCM_FLONUM_VALUE(num);
    switch (mode) {
    case SCM_ROUND_FLOOR: r = floor(v); break;
    case SCM_ROUND_CEIL:  r = ceil(v); break;
    /* trunc and round is neither in ANSI nor in POSIX. */
#ifdef HAVE_TRUNC
    case SCM_ROUND_TRUNC: r = trunc(v); break;
#else
    case SCM_ROUND_TRUNC: r = (v < 0.0)? ceil(v) : floor(v); break;
#endif
#ifdef HAVE_RINT
    case SCM_ROUND_ROUND: r = rint(v); break;
#else
    case SCM_ROUND_ROUND: {
        double frac = modf(v, &r);
        if (v > 0.0) {
            if (frac > 0.5) r += 1.0;
            else if (frac == 0.5) {
                if (r/2.0 != 0.0) r += 1.0;
            }
        } else {
            if (frac < -0.5) r -= 1.0;
            else if (frac == 0.5) {
                if (r/2.0 != 0.0) r -= 1.0;
            }
        }
        break;
    }
#endif
    default: Scm_Panic("something screwed up");
    }
    printf("%lf\n", r);
    return Scm_MakeFlonum(r);
}

/*===============================================================
 * TRANSCEDENTAL FUNCTIONS
 *   for now, we have functions only for real numbers.
 */

#define TRANS(sfn, fn)                                                   \
ScmObj sfn(ScmObj z)                                                     \
{                                                                        \
    double r;                                                            \
    if (!SCM_REALP(z)) Scm_Error("real number required, but got %S", z); \
    r = fn(Scm_GetDouble(z));                                            \
    return Scm_MakeFlonum(r);                                            \
}

/* TODO: check domain error! */
TRANS(Scm_Exp, exp)
TRANS(Scm_Log, log)
TRANS(Scm_Sin, sin)
TRANS(Scm_Cos, cos)
TRANS(Scm_Tan, tan)
TRANS(Scm_Asin, asin)
TRANS(Scm_Acos, acos)
TRANS(Scm_Atan, atan)

ScmObj Scm_Atan2(ScmObj y, ScmObj x)
{
    if (!SCM_REALP(x)) Scm_Error("real number required, but got %S", x);
    if (!SCM_REALP(y)) Scm_Error("real number required, but got %S", y);
    {
        double vx = Scm_GetDouble(x);
        double vy = Scm_GetDouble(y);
        double r = atan2(vy, vx);
        return Scm_MakeFlonum(r);
    }
}
     
ScmObj Scm_Expt(ScmObj x, ScmObj y)
{
    if (!SCM_REALP(x)) Scm_Error("real number required, but got %S", x);
    if (!SCM_REALP(y)) Scm_Error("real number required, but got %S", y);
    
    /* TODO: optimize the case where x and y is integer, y>0 */
    {
        double vx = Scm_GetDouble(x);
        double vy = Scm_GetDouble(y);
        double r = pow(vx, vy);
        return Scm_MakeFlonum(r);
    }
}

ScmObj Scm_Sqrt(ScmObj z)
{
    if (!SCM_REALP(z)) Scm_Error("real number required, but got %S", z);
    {
        double vz = Scm_GetDouble(z);
        if (vz < 0){
            return Scm_MakeComplex(0.0, sqrt(-vz));
        } else {
            return Scm_MakeFlonum(sqrt(vz));
        }
    }
}

/*===============================================================
 * Number I/O
 */

/*
 * Printer
 */

static int number_print(ScmObj obj, ScmPort *port, int mode)
{
    ScmObj s = Scm_NumberToString(obj, 10);
    SCM_PUTS(SCM_STRING(s), port);
    return SCM_STRING_LENGTH(s);
}

ScmObj Scm_NumberToString(ScmObj obj, int radix)
{
    ScmObj r;
    
    if (SCM_INTP(obj)) {
        char buf[50];
        if (radix == 10) {
            snprintf(buf, 50, "%d", SCM_INT_VALUE(obj));
        } else if (radix == 16) {
            snprintf(buf, 50, "%x", SCM_INT_VALUE(obj));
        } else if (radix == 8) {
            snprintf(buf, 50, "%o", SCM_INT_VALUE(obj));
        } else {
            /* TODO: implement this! */
            buf[0] = '?';
            buf[1] = '\0';
        }
        r = Scm_MakeString(buf, -1, -1);
    } else if (SCM_BIGNUMP(obj)) {
        /* TODO: write proper printer! */
        ScmObj p = Scm_MakeOutputStringPort();
        Scm_DumpBignum(SCM_BIGNUM(obj), p);
        r = Scm_GetOutputString(SCM_PORT(p));
    } else if (SCM_FLONUMP(obj)) {
        char buf[50];
        snprintf(buf, 47, "%.15g", SCM_FLONUM_VALUE(obj));
        if (strchr(buf, '.') == NULL && strchr(buf, 'e') == NULL)
            strcat(buf, ".0");
        r = Scm_MakeString(buf, -1, -1);
    } else if (SCM_COMPLEXP(obj)) {
        ScmObj p = Scm_MakeOutputStringPort();
        double real = SCM_COMPLEX_REAL(obj), imag = SCM_COMPLEX_IMAG(obj);
        Scm_Printf(SCM_PORT(p), "%lg%+lgi", real, imag);
        r = Scm_GetOutputString(SCM_PORT(p));
    } else {
        Scm_Error("number required: %S", obj);
    }
    return r;
}

/*
 * Number Parser
 */

static ScmObj read_integer(const char *str, int len, int radix)
{
    long value_int = 0, t;
    ScmObj value_big = SCM_FALSE;
    int minusp = 0;
    char c;
    static const char tab[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    const char *ptab;

    if (*str == '+')      { minusp = 0; str++; len--; }
    else if (*str == '-') { minusp = 1; str++; len--; }
    if (len == 0) return SCM_FALSE;
    
    while (len--) {
        c = tolower(*str++);
        for (ptab = tab; ptab < tab+radix; ptab++) {
            if (c == *ptab) {
                if (SCM_FALSEP(value_big)) {
                    t = value_int*radix + (ptab-tab);
                    if (t >= 0) {
                        value_int = t;
                        break;
                    } else {    /* overflow */
                        value_big = Scm_MakeBignumFromSI(value_int);
                    }
                }
                value_big = Scm_BignumMulSI(SCM_BIGNUM(value_big), radix);
                value_big = Scm_BignumAddSI(SCM_BIGNUM(value_big), ptab-tab);
                break;
            }
        }
        if (ptab >= tab+radix) return SCM_FALSE;
    }
    if (SCM_FALSEP(value_big)) {
        if (minusp) return Scm_MakeInteger(-value_int);
        else        return Scm_MakeInteger(value_int);
    } else {
        if (minusp) return Scm_BignumNegate(SCM_BIGNUM(value_big));
        else        return value_big;
    }
}

static double read_real(const char *str, int len, const char **next)
{
    char c;
    ScmDString ds;
    double value;
    int point_seen = 0, exp_seen = 0, digits = 0;
    
    Scm_DStringInit(&ds);

    if (*str == '+' || *str == '-') {
        SCM_DSTRING_PUTB(&ds, *str);
        str++;
        len--;
    }
    if (len == 0) { *next = NULL; return 0.0; }

    for (; len > 0; len--, str++) {
        switch (c = *str) {
        case '0':; case '1':; case '2':; case '3':; case '4':;
        case '5':; case '6':; case '7':; case '8':; case '9':
            digits++;
            SCM_DSTRING_PUTB(&ds, c);
            continue;
        case '.':
            if (point_seen) { *next = NULL; return 0.0; }
            SCM_DSTRING_PUTB(&ds, c);
            point_seen = 1;
            continue;
        case 'e':; case 'E':;
        case 's':; case 'S':; case 'f':; case 'F':;
        case 'd':; case 'D':; case 'l':; case 'L':;
            if (digits == 0 || exp_seen) { *next = NULL; return 0.0; }
            point_seen = exp_seen = 1;
            SCM_DSTRING_PUTB(&ds, 'e');
            if (len > 0 && (str[1] == '+' || str[1] == '-')) {
                SCM_DSTRING_PUTB(&ds, *++str);
                len--;
            }
            continue;
        case '+':; case '-':; case 'i':
            break;
        default:
            *next == NULL;
            return 0.0;
        }
        break;
    }
    if (digits == 0) SCM_DSTRING_PUTB(&ds, '1');
    sscanf(Scm_DStringGetCstr(&ds), "%lf", &value);
    *next = str;
    return value;
}

static ScmObj read_complex(const char *str, int len)
{
    double real, imag;
    const char *next;
    char sign = 0;

    if (*str == '.' && len == 1) return SCM_FALSE;
    if (*str == '+' || *str == '-') sign = *str;

    real = read_real(str, len, &next);
    if (next == NULL) return SCM_FALSE;

    if (next == str+len) return Scm_MakeFlonum(real);

    if (*next == 'i') {
        if (sign && next == str+len-1) return Scm_MakeComplex(0, real);
        else return SCM_FALSE;
    }

    if (*next == '+' || *next == '-') {
        imag = read_real(next, len-(next-str), &next);
        if (next == NULL || next != str+len-1) return SCM_FALSE;
        if (*next != 'i') return SCM_FALSE;
        return Scm_MakeComplex(real, imag);
    }
    return SCM_FALSE;
}

static ScmObj read_number(const char *str, int len, int radix)
{
    int radix_seen = 0;
    int exactness = 0, exactness_seen = 0;
    int i;

    if (radix <= 1 || radix > 36) return SCM_FALSE; /* XXX:should be error? */
    
    /* start from prefix part */
    for (; len >= 0; len-=2) {
        if (*str != '#') break;
        str++;
        switch (*str++) {
        case 'x':; case 'X':;
            if (radix_seen) return SCM_FALSE;
            radix = 16; radix_seen++;
            continue;
        case 'o':; case 'O':;
            if (radix_seen) return SCM_FALSE;
            radix = 8; radix_seen++;
            continue;
        case 'b':; case 'B':;
            if (radix_seen) return SCM_FALSE;
            radix = 2; radix_seen++;
            continue;
        case 'd':; case 'D':;
            if (radix_seen) return SCM_FALSE;
            radix = 10; radix_seen++;
            continue;
        case 'e':; case 'E':;
            if (exactness_seen) return SCM_FALSE;
            exactness = 1; exactness_seen++;
            continue;
        case 'i':; case 'I':;
            if (exactness_seen) return SCM_FALSE;
            exactness = 0; exactness_seen++;
            continue;
        }
        return SCM_FALSE;
    }
    if (len <= 0) return SCM_FALSE;

    /* number body */
    if (exactness || radix != 10)  return read_integer(str, len, radix);

    i = (*str == '+' || *str == '-')? 1 : 0;
    for (; i<len; i++) {
        if (!isdigit(str[i])) return read_complex(str, len);
    }
    return read_integer(str, len, 10);
}


ScmObj Scm_StringToNumber(ScmString *str, int radix)
{
    if (SCM_STRING_LENGTH(str) != SCM_STRING_SIZE(str)) {
        /* This can't be a proper number. */
        return SCM_FALSE;
    } else {
        return read_number(SCM_STRING_START(str), SCM_STRING_SIZE(str), radix);
    }
}
