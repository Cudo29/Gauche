/*
 * repl.c - repl
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
 *  $Id: repl.c,v 1.8 2001-03-16 07:11:06 shiro Exp $
 */

#include "gauche.h"

void Scm_Repl(ScmObj prompt, ScmPort *in, ScmPort *out)
{
    ScmObj c, v;
    int eofread = FALSE;
    
    for (;;) {
        ScmObj p = Scm_Eval(prompt, SCM_NIL);
        SCM_PUSH_ERROR_HANDLER {
            Scm_Write(p, SCM_OBJ(out), SCM_PRINT_DISPLAY);
            SCM_FLUSH(out);
            v = Scm_Read(SCM_OBJ(in));
            if (SCM_EOFP(v)) { eofread = TRUE; break; }
            Scm_Eval(v, SCM_NIL);
            SCM_FOR_EACH(c, Scm_VMGetResult(Scm_VM())) {
                Scm_Printf(out, "%S\n", SCM_CAR(c));
            }
        }
        SCM_POP_ERROR_HANDLER;
        if (eofread) return;
    }
}

