#
# Gauche-specific aucotonf macros.
# $Id: aclocal.m4,v 1.3 2002-02-10 05:03:31 shirok Exp $

# AC_GAUCHE_INIT_EXT
#   Sets some parameters about installed Gauche package.  This macro checks
#   if you're configuring within Gauche source tree or as an individual
#   extension package, and sets up the following macros appropriately.
#
#    GAUCHE_CONFIG   - Name of gauche-config script
#    GAUCHE_TOP      - Directory prefix where Gauche is installed, or
#                      empty if this is an individual extension.
#    GAUCHE_INC      - '-I' macros required to compile extensions.
#    GAUCHE_LIB      - '-L' macros required to link extensions.
#    GOSH            - Name of gosh executable, possibly including
#                      options to run it from the extension directory.
AC_DEFUN([AC_GAUCHE_INIT_EXT],
         [
if test -f ../../src/gauche.h; then
  GAUCHE_CONFIG="sh ../../src/gauche-config"
  GAUCHE_TOP='../../'
  GAUCHE_INC="-I../../src -I../../gc/include `$GAUCHE_CONFIG --local-incdir`"
  GAUCHE_LIB="-L../../src"
  GOSH="../../src/gosh -I../../src -I../../lib"
else
  GAUCHE_CONFIG=gauche-config
  GAUCHE_TOP=
  GAUCHE_INC="`gauche-config -I`"
  GAUCHE_LIB="`gauche-config -L`"
  GOSH=gosh
fi
AC_SUBST(GAUCHE_CONFIG)
AC_SUBST(GAUCHE_TOP)
AC_SUBST(GAUCHE_INC)
AC_SUBST(GOSH)
])

# AC_GAUCHE_INSTALL_TYPE(TYPE)
#   Sets the default value of INSTALL_TYPE macro.  TYPE must be either
#   sys or site.
AC_DEFUN([AC_GAUCHE_INSTALL_TYPE],
         [
: ${INSTALL_TYPE=$1}
if test "X$INSTALL_TYPE" != "Xsys" -a "X$INSTALL_TYPE" != "Xsite"; then
  AC_MSG_ERROR([INSTALL_TYPE must be either 'sys' or 'site'])
fi
AC_SUBST(INSTALL_TYPE)
])

# AC_GAUCHE_CC
#   Gets compiler parameters which Gauche has been compiled with.
AC_DEFUN([AC_GAUCHE_CC],
         [
CC="`$GAUCHE_CONFIG --cc`"
AC_SUBST(CC)
])

# AC_GAUCHE_FLAGS
#   Sets CFLAGS, CPPFLAGS and LDFLAGS appropriate for furthre testing.
#   This should come before any testings that requires those flags to be set.
AC_DEFUN([AC_GAUCHE_FLAGS],
         [
CFLAGS="$CFLAGS $GAUCHE_INC `$GAUCHE_CONFIG --so-cflags`"
AC_SUBST(CFLAGS)
CPPFLAGS="$GAUCHE_INC"       # some test requires this
LDFLAGS="$LDFLAGS `$GAUCHE_CONFIG --local-libdir`"
])

# AC_GAUCHE_FIX_LIBS
#   Sets LDFLAGS and LIBS to generate shared library.
#   This has to come all the tests that requre linking, or those test
#   will fail because they can't generate stand-alone executable.
AC_DEFUN([AC_GAUCHE_FIX_LIBS],
         [
LDFLAGS="$LDFLAGS `$GAUCHE_CONFIG --so-ldflags`"
LIBS="$GAUCHE_LIB `$GAUCHE_CONFIG -l` $LIBS"
AC_SUBST(LDFLAGS)
])

# AC_GAUCHE_EXT_FIXUP(name)
#   Sets the shell command to generate 'head.c' and 'tail.c', needed by
#   some platforms for GC.   NAME must be the extension module's name.
AC_DEFUN([AC_GAUCHE_EXT_FIXUP],
         [AC_CONFIG_COMMANDS("$1_head_n_tail",
                             [
AC_MSG_NOTICE(generating $1_head.c and $1_tail.c);
rm -f $1_head.c
echo "void *Scm__datastart_$1 = (void*)&Scm__datastart_$1;" > $1_head.c
rm -f $1_tail.c
echo "void *Scm__dataend_$1 = (void*)&Scm__dataend_$1;" > $1_tail.c
])])


