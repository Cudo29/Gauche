/*
 * macro.h - structures used internally in macro expander
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
 *  $Id: macro.h,v 1.7.2.1 2005-01-12 23:38:59 shirok Exp $
 */

#ifndef GAUCHE_MACRO_H
#define GAUCHE_MACRO_H

/*
 * SyntaxPattern object is an internal object used to expand r5rs macro.
 */

typedef struct ScmSyntaxPatternRec {
    SCM_HEADER;
    ScmObj pattern;             /* subpattern */
    ScmObj vars;                /* pattern variables in this subpattern */
    short level;                /* level of this subpattern */
    short repeat;               /* does this subpattern repeat? */
} ScmSyntaxPattern;

SCM_CLASS_DECL(Scm_SyntaxPatternClass);
#define SCM_CLASS_SYNTAX_PATTERN  (&Scm_SyntaxPatternClass)

#define SCM_SYNTAX_PATTERN(obj)   ((ScmSyntaxPattern*)(obj))
#define SCM_SYNTAX_PATTERN_P(obj) SCM_XTYPEP(obj, SCM_CLASS_SYNTAX_PATTERN)

/*
 * SyntaxRules keeps a compiled rules of macro transformation.
 */

typedef struct ScmSyntaxRuleBranchRec {
    ScmObj pattern;             /* pattern to match */
    ScmObj template;            /* template to be expanded */
    int numPvars;               /* # of pattern variables */
    int maxLevel;               /* maximum # of nested subpatterns */
} ScmSyntaxRuleBranch;

typedef struct ScmSyntaxRules {
    SCM_HEADER;
    ScmObj name;                  /* name of the macro (for debug) */
    int numRules;                 /* # of rules */
    int maxNumPvars;              /* max # of pattern variables */
    ScmSyntaxRuleBranch rules[1]; /* variable length */
} ScmSyntaxRules;

SCM_CLASS_DECL(Scm_SyntaxRulesClass);
#define SCM_CLASS_SYNTAX_RULES   (&Scm_SyntaxRulesClass)

#define SCM_SYNTAX_RULES(obj)    ((ScmSyntaxRules*)(obj))
#define SCM_SYNTAX_RULES_P(obj)  SCM_XTYPEP(obj, SCM_CLASS_SYNTAX_RULES)

SCM_EXTERN ScmObj Scm_CompileSyntaxRules(ScmObj name, ScmObj lietrals,
                                         ScmObj rules, ScmObj env);

#endif /* GAUCHE_MACRO_H */
