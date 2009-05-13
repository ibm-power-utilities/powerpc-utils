AUTOMAKE_OPTIONS = foreign
SUBDIRS = src man scripts

docdir = $(datadir)/doc/packages/@PACKAGE@
doc_DATA = README COPYRIGHT

IN_MAKEFILES = Makefile.in src/Makefile.in src/drmgr/Makefile.in man/Makefile.in \
	       scripts/Makefile.in
AC_CLEAN_FILES = aclocal.m4 depcomp install-sh missing

really-clean: maintainer-clean
	-rm -f powerpc-utils.spec
	-rm -f $(IN_MAKEFILES)
	-rm -f $(AC_CLEAN_FILES)
	
