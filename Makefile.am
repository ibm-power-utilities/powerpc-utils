AUTOMAKE_OPTIONS = foreign
SUBDIRS = src man scripts

docdir = $(datadir)/doc/packages/@PACKAGE@
doc_DATA = README COPYRIGHT

EXTRA_DIST = Changelog powerpc-utils.spec.in doc/activate_firmware.doxycfg doc/nvram.doxycfg      \
	     doc/rtas_ibm_get_vpd.doxycfg doc/serv_config.doxycfg doc/set_poweron_time.doxycfg \
	     doc/uesensor.doxycfg

AC_CLEAN_FILES = aclocal.m4

really-clean: maintainer-clean
	-rm -f powerpc-utils.spec
	-rm -f $(AC_CLEAN_FILES)
	
