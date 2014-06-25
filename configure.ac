#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
m4_define([ppu_version], 1.2.21)

AC_PREREQ([2.63])
AC_INIT([powerpc-utils], ppu_version, [nfont@linux.vnet.ibm.com])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.10 -Wall subdir-objects -Werror foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_CONFIG_SRCDIR([src/rtas_ibm_get_vpd.c])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h locale.h memory.h netinet/in.h nl_types.h stdint.h stdlib.h string.h sys/ioctl.h syslog.h unistd.h linux/perf_event.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT8_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_CHECK_FUNCS([memset strchr strcspn strdup strerror strrchr strstr strtol strtoul strtoull gettimeofday])

# check for librtas
AC_ARG_WITH([librtas],
    [AS_HELP_STRING([--without-librtas],
        [disable building utilities that require librtas])],
    [],
    [with_librtas=yes]
)

AS_IF([test "x$with_librtas" != "xno"],
	[AC_CHECK_HEADER([librtas.h],
        [with_libtras=yes],
        [AC_MSG_FAILURE(
                [librtas test failed (--without-librtas to disable)])]
    )]
)

AM_CONDITIONAL([WITH_LIBRTAS], [test "x$with_librtas" = "xyes"])

AC_DEFUN([LOCAL_CHECK_FLAGS],[
  AC_REQUIRE([AX_CHECK_LINK_FLAG])
  AC_REQUIRE([AX_APPEND_COMPILE_FLAGS])
  AC_LANG_PUSH([C])
  AX_APPEND_COMPILE_FLAGS([-Wall])
  AX_APPEND_COMPILE_FLAGS([-D_FORTIFY_SOURCE=2 -fstack-protector-all])
  AX_APPEND_COMPILE_FLAGS([-fwrapv -fPIE -Wstack-protector])
  AX_APPEND_COMPILE_FLAGS([--param=ssp-buffer-size=1])
  AX_CHECK_LINK_FLAG([-z relro -z now])
  AX_CHECK_LINK_FLAG([-pie])
  AC_LANG_POP
])
LOCAL_CHECK_FLAGS

AC_CONFIG_FILES([Makefile powerpc-utils.spec])
AC_OUTPUT
