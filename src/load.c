/*
 * load.c - load a program
 *
 *  Copyright(C) 2000 by Shiro Kawai (shiro@acm.org)
 *
 *  Permission to use, copy, modify, ditribute this software and
 *  accompanying documentation for any purpose is hereby granted,
 *  provided that existing copyright notices are retained in all
 *  copies and that this notice is included verbatim in all
 *  distributions.
 *  This software is provided as is, without express or implied
 *  warranty.  In no circumstances the author(s) shall be liable
 *  for any damages arising out of the use of this software.
 *
 *  $Id: load.c,v 1.1.1.1 2001-01-11 19:26:03 shiro Exp $
 */

#include "gauche.h"

/*
 * Load file.
 */

/*
 * Scm_LoadFromPort
 * 
 *   The most basic function in the load()-family.  Read an expression
 *   from the given port and evaluates it repeatedly, until it reaches
 *   EOF.  Then the port is closed.
 *
 *   The result of the last evaluation remains on VM.
 */

/* C-continuation of the loading */
static void load_cc(ScmObj result, void **data)
{
    ScmObj port = SCM_OBJ(data[0]);
    ScmObj expr = Scm_Read(port);
    if (!SCM_EOFP(expr)) {
        Scm_VMPushCC(load_cc, (void **)&port, 1);
        Scm_Eval(expr, SCM_UNBOUND);
    } else {
        Scm_ClosePort(SCM_PORT(port));
        SCM_RETURN(result);
    }
}

void Scm_LoadFromPort(ScmPort *port)
{
    if (!SCM_IPORTP(port))
        Scm_Error("input port required, but got: %S", port);
    if (SCM_PORT_CLOSED_P(port))
        Scm_Error("port already closed: %S", port);
    Scm_VMPushCC(load_cc, (void **)&port, 1);
}


/* TODO: search path, default suffix, etc... */
void Scm_Load(const char *s)
{
    ScmObj p = Scm_OpenFilePort(s, "r");
    if (SCM_FALSEP(p)) {
        Scm_Error("cannot open file: %s", s);
    }
    Scm_LoadFromPort(SCM_PORT(p));
}


