ACLOCAL_AMFLAGS = -I m4

noinst_HEADERS =
sbin_PROGRAMS =
EXTRA_DIST =

docdir = $(datadir)/doc/packages/@PACKAGE@
doc_DATA = README COPYING

bin_SCRIPTS = scripts/amsstat

sbin_SCRIPTS = \
	scripts/update_flash \
	scripts/update_flash_nv \
	scripts/hvcsadmin \
	scripts/rtas_dump \
	scripts/snap \
	scripts/bootlist \
	scripts/ofpathname \
	scripts/lsdevinfo \
	scripts/ls-veth \
	scripts/ls-vscsi \
	scripts/ls-vdev \
	scripts/nvsetenv \
	scripts/pseries_platform

man_MANS = \
	man/activate_firmware.8 \
	man/rtas_ibm_get_vpd.8 \
	man/uesensor.8 \
	man/amsstat.1 \
	man/serv_config.8 \
	man/update_flash.8 \
	man/nvram.8 \
	man/set_poweron_time.8 \
	man/bootlist.8 \
	man/ofpathname.8 \
	man/snap.8 \
	man/hvcsadmin.8 \
	man/rtas_dump.8 \
	man/sys_ident.8 \
	man/lparcfg.5 \
	man/lparstat.8 \
	man/lsslot.8 \
	man/ppc64_cpu.8 \
	man/errinjct.8 \
	man/vcpustat.8 \
	man/rtas_dbg.8

EXTRA_DIST += $(bin_SCRIPTS) $(sbin_SCRIPTS) $(man_MANS)


EXTRA_DIST += COPYING Changelog powerpc-utils.spec.in doc/activate_firmware.doxycfg \
	     doc/nvram.doxycfg doc/rtas_ibm_get_vpd.doxycfg doc/serv_config.doxycfg \
	     doc/set_poweron_time.doxycfg doc/uesensor.doxycfg

if WITH_SYSTEMD
sbin_SCRIPTS += scripts/smtstate
man_MANS += man/smtstate.8
EXTRA_DIST += systemd/smt_off.service.in \
	systemd/smtstate.service.in \
	scripts/smtstate.in \
	var/lib/powerpc-utils/smt.state \
	man/smtstate.8
endif

sbin_PROGRAMS += src/nvram src/lsprop src/lparstat src/ppc64_cpu src/vcpustat

pseries_platform_SOURCES = src/common/pseries_platform.c src/common/pseries_platform.h

librtas_error_SOURCES = src/common/librtas_error.c src/common/librtas_error.h

cpu_info_helpers_SOURCES = src/common/cpu_info_helpers.c src/common/cpu_info_helpers.h

src_nvram_SOURCES = src/nvram.c src/nvram.h $(pseries_platform_SOURCES)
src_nvram_LDADD = -lz @LIBDL@

src_lsprop_SOURCES = src/lsprop.c $(pseries_platform_SOURCES)

src_lparstat_SOURCES = src/lparstat.c src/lparstat.h $(pseries_platform_SOURCES) \
		       $(cpu_info_helpers_SOURCES)

src_ppc64_cpu_SOURCES = src/ppc64_cpu.c $(pseries_platform_SOURCES) $(cpu_info_helpers_SOURCES)
src_ppc64_cpu_LDADD = -lpthread

src_vcpustat_SOURCES = src/vcpustat.c $(pseries_platform_SOURCES)


AM_CFLAGS = -Wall -g
AM_CPPFLAGS = -I $(top_srcdir)/src/common/

if WITH_LIBRTAS
sbin_PROGRAMS += \
	src/activate_firmware \
	src/set_poweron_time \
	src/rtas_ibm_get_vpd \
	src/serv_config \
	src/uesensor \
	src/rtas_event_decode \
	src/sys_ident

src_activate_firmware_SOURCES = src/activate_fw.c $(librtas_error_SOURCES) $(pseries_platform_SOURCES)
src_activate_firmware_LDADD = -lrtas -lm

src_set_poweron_time_SOURCES = src/set_poweron_time.c $(librtas_error_SOURCES) $(pseries_platform_SOURCES)
src_set_poweron_time_LDADD = -lrtas

src_rtas_ibm_get_vpd_SOURCES = src/rtas_ibm_get_vpd.c $(librtas_error_SOURCES) $(pseries_platform_SOURCES)
src_rtas_ibm_get_vpd_LDADD = -lrtas

src_serv_config_SOURCES = src/serv_config.c $(librtas_error_SOURCES) $(pseries_platform_SOURCES)
src_serv_config_LDADD = -lrtas

src_uesensor_SOURCES = src/uesensor.c $(librtas_error_SOURCES) $(pseries_platform_SOURCES)
src_uesensor_LDADD = -lrtas

src_rtas_event_decode_SOURCES = src/rtas_event_decode.c $(pseries_platform_SOURCES)
src_rtas_event_decode_LDADD = -lrtasevent

src_sys_ident_SOURCES = src/sys_ident.c $(pseries_platform_SOURCES)
src_sys_ident_LDADD = -lrtas

src_ppc64_cpu_SOURCES += $(librtas_error_SOURCES)
src_ppc64_cpu_LDADD += -lrtas

sbin_PROGRAMS += src/errinjct/errinjct src/rtas_dbg

src_errinjct_errinjct_SOURCES = \
	src/errinjct/dcache.c \
	src/errinjct/errinjct.c \
	src/errinjct/icache.c \
	src/errinjct/ioa_bus_error.c \
	src/errinjct/open_close.c \
	src/errinjct/platform.c \
	src/errinjct/slb.c \
	src/errinjct/tlb.c \
	$(pseries_platform_SOURCES)

noinst_HEADERS += src/errinjct/errinjct.h

src_errinjct_errinjct_LDADD = -lrtas

src_rtas_dbg_SOURCES = src/rtas_dbg.c $(pseries_platform_SOURCES)

src_rtas_dbg_LDADD = -lrtas

sbin_PROGRAMS += src/drmgr/drmgr src/drmgr/lsslot
endif

src_drmgr_drmgr_SOURCES = \
	src/drmgr/common.c \
	src/drmgr/common_cpu.c \
	src/drmgr/common_ofdt.c \
	src/drmgr/common_pci.c \
	src/drmgr/drmgr.c \
	src/drmgr/drmig_chrp_pmig.c \
	src/drmgr/drslot_chrp_cpu.c \
	src/drmgr/drslot_chrp_hea.c \
	src/drmgr/drslot_chrp_mem.c \
	src/drmgr/drslot_chrp_pci.c \
	src/drmgr/drslot_chrp_phb.c \
	src/drmgr/drslot_chrp_slot.c \
	src/drmgr/rtas_calls.c \
	src/drmgr/prrn.c \
	$(pseries_platform_SOURCES)

noinst_HEADERS += \
	src/drmgr/drcpu.h \
	src/drmgr/dr.h \
	src/drmgr/drmem.h \
	src/drmgr/drpci.h \
	src/drmgr/rtas_calls.h \
	src/drmgr/ofdt.h \
	src/drmgr/rtas_calls.h \
	src/drmgr/options.c

src_drmgr_drmgr_LDADD = -lrtas

src_drmgr_lsslot_SOURCES = \
	src/drmgr/lsslot.c \
	src/drmgr/lsslot_chrp_cpu.c \
	src/drmgr/common.c \
	src/drmgr/common_cpu.c \
	src/drmgr/common_pci.c \
	src/drmgr/common_ofdt.c \
	src/drmgr/rtas_calls.c \
	src/drmgr/drslot_chrp_mem.c \
	$(pseries_platform_SOURCES)

noinst_HEADERS += \
	src/drmgr/options.c

src_drmgr_lsslot_LDADD = -lrtas

if WITH_SYSTEMD
install-data-hook:
	$(MKDIR_P) $(DESTDIR)${systemd_unit_dir}
	$(MKDIR_P) $(DESTDIR)/var/lib/@PACKAGE@
	$(INSTALL_DATA) systemd/smt_off.service $(DESTDIR)${systemd_unit_dir}/
	$(INSTALL_DATA) systemd/smtstate.service $(DESTDIR)${systemd_unit_dir}/
	$(INSTALL_DATA) var/lib/powerpc-utils/smt.state $(DESTDIR)/var/lib/@PACKAGE@/
	$(INSTALL_SCRIPT) scripts/smtstate $(DESTDIR)@sbindir@
	sed -i -e 's,$${exec_prefix},@prefix@,g' $(DESTDIR)${systemd_unit_dir}/smt_off.service
	sed -i -e 's,$${exec_prefix},@prefix@,g' $(DESTDIR)${systemd_unit_dir}/smtstate.service
	sed -i -e 's,$${exec_prefix},@prefix@,g' $(DESTDIR)@sbindir@/smtstate
endif
