#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([powerpc-utils], [1.2.0], [nfont@austin.ibm.com])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/rtas_ibm_get_vpd.c])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries.
AC_CHECK_LIB([servicelog], [servicelog_open])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h limits.h locale.h memory.h netinet/in.h nl_types.h stdint.h stdlib.h string.h sys/ioctl.h syslog.h unistd.h servicelog-1/servicelog.h])

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
AC_CHECK_FUNCS([memset strchr strcspn strdup strerror strrchr strstr strtol strtoul strtoull])

echo "configure: creating ./powerpc-utils.spec"
tfile=/tmp/powerpc-utils.spec$$

# See if the .egg-info file is to be created.
PY_VERSION=`python -c "from platform import python_version; print python_version()" 2>/dev/null`

if [[ -z $PY_VERSION ]]; then
    sed "|\@PY_VERSION\@|d" powerpc-utils.spec.in > $tfile
else
    PY_VERSION=${PY_VERSION%.*}
    sed "s|\@PY_VERSION\@|$PY_VERSION|g" powerpc-utils.spec.in > $tfile
fi

# Update the spec file with the correct python paths
PY_INST_DIR=`python -c "from distutils.sysconfig import get_python_lib; print get_python_lib()"`
sed "s|\@PYTHON_SITE_PKGS\@|$PY_INST_DIR|g" $tfile > powerpc-utils.spec
rm $tfile

# Update the prefix used in the scripts/amsvis/Makefile
echo "configure: creating ./scripts/amsvis/Makefile"
sed "s|\@PREFIX\@|$prefix|g" scripts/amsvis/Makefile.in > scripts/amsvis/Makefile

# Update the release in scripts/amsvis/setup.py
echo "configure: creating ./scripts/amsvis/setup.py"
sed "s|\@VERSION\@|$PACKAGE_VERSION|g" scripts/amsvis/setup.py.in > scripts/amsvis/setup.py

AC_CONFIG_FILES(Makefile src/Makefile src/drmgr/Makefile scripts/Makefile man/Makefile)
AC_OUTPUT
