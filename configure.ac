#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
m4_define([ppu_version], 1.3.4)

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

# check for zlib
AC_CHECK_HEADER(zlib.h,
		[AC_CHECK_LIB(z, inflate, [], [AC_MSG_FAILURE([zlib library is required for compilation])])],
		[AC_MSG_FAILURE([zlib.h is required for compiliation])])

# check for librtas
AC_ARG_WITH([librtas],
    [AS_HELP_STRING([--without-librtas],
        [disable building utilities that require librtas])],
    [],
    [with_librtas=yes]
)

AS_IF([test "x$with_librtas" != "xno"],
	[AC_CHECK_HEADER(librtas.h, [], [AC_MSG_FAILURE([librtas.h is required (--without-librtas to disable)])])]
	[AC_CHECK_LIB(rtas, rtas_errinjct_open, [], [AC_MSG_FAILURE([librtas library is missing (--without-librtas to disable)])])]
	[AC_CHECK_HEADER(librtasevent.h, [], [AC_MSG_FAILURE([librtasevent.h is required (--without-librtas to disable)])])]
	[AC_CHECK_LIB(rtasevent, parse_rtas_event, [], [AC_MSG_FAILURE([librtasevent library is missing (--without-librtas to disable)])])]

	AC_DEFINE(WITH_LIBRTAS)
)

AM_CONDITIONAL([WITH_LIBRTAS], [test "x$with_librtas" = "xyes"])

# Check what OS you are running
AC_CANONICAL_HOST
case $host_os in
	linux*)
		LIBDL="-ldl"
	;;
	*freebsd*)
		LIBDL=""
	;;
	*)
	#Default Case
		AC_MSG_ERROR([Your platform is not currently supported])
	;;
esac
AC_SUBST(LIBDL)

# check for systemd
systemd_unit_dir=/usr/lib/systemd/system
AC_ARG_WITH([systemd],
	[AC_HELP_STRING([--with-systemd@<:@=DIR@:>@],
                    [install systemd unit files (not default and unit dir is /usr/lib/systemd/system)])],
	[if test "$withval" = "no"; then
		with_systemd=0
	else
		with_systemd=1
		test $withval != "yes" && systemd_unit_dir=$withval
	fi],
	with_systemd=0
	)
AM_CONDITIONAL(WITH_SYSTEMD, [test "$with_systemd" = 1])
AC_SUBST(systemd_unit_dir)

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

AC_CONFIG_FILES([Makefile powerpc-utils.spec systemd/smt_off.service])
AC_OUTPUT
