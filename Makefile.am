ACLOCAL_AMFLAGS = -I m4

noinst_HEADERS =
sbin_PROGRAMS =
EXTRA_DIST =

docdir = $(datadir)/doc/packages/@PACKAGE@
doc_DATA = README COPYRIGHT

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
	man/lsslot.8

EXTRA_DIST += $(bin_SCRIPTS) $(sbin_SCRIPTS) $(man_MANS)


EXTRA_DIST += COPYRIGHT Changelog powerpc-utils.spec.in doc/activate_firmware.doxycfg \
	     doc/nvram.doxycfg doc/rtas_ibm_get_vpd.doxycfg doc/serv_config.doxycfg \
	     doc/set_poweron_time.doxycfg doc/uesensor.doxycfg

sbin_PROGRAMS += src/nvram src/lsprop src/lparstat

pseries_platform_SOURCES = src/common/pseries_platform.c src/common/pseries_platform.h

AM_CFLAGS = -Wall -g -I $(top_srcdir)/src/common/

if WITH_LIBRTAS
sbin_PROGRAMS += \
	src/activate_firmware \
	src/set_poweron_time \
	src/rtas_ibm_get_vpd \
	src/serv_config \
	src/uesensor \
	src/rtas_event_decode \
	src/sys_ident \
	src/ppc64_cpu

src_activate_firmware_SOURCES = src/activate_fw.c $(pseries_platform_SOURCES)
src_activate_firmware_LDADD = -lrtas

src_set_poweron_time_SOURCES = src/set_poweron_time.c src/librtas_error.c src/librtas_error.h $(pseries_platform_SOURCES)
src_set_poweron_time_LDADD = -lrtas

src_rtas_ibm_get_vpd_SOURCES = src/rtas_ibm_get_vpd.c src/librtas_error.c src/librtas_error.h $(pseries_platform_SOURCES)
src_rtas_ibm_get_vpd_LDADD = -lrtas

src_serv_config_SOURCES = src/serv_config.c src/librtas_error.c src/librtas_error.h $(pseries_platform_SOURCES)
src_serv_config_LDADD = -lrtas

src_uesensor_SOURCES = src/uesensor.c src/librtas_error.c src/librtas_error.h $(pseries_platform_SOURCES)
src_uesensor_LDADD = -lrtas

src_rtas_event_decode_SOURCES = src/rtas_event_decode.c $(pseries_platform_SOURCES)
src_rtas_event_decode_LDADD = -lrtasevent

src_sys_ident_SOURCES = src/sys_ident.c $(pseries_platform_SOURCES)
src_sys_ident_LDADD = -lrtas

src_ppc64_cpu_SOURCES = src/ppc64_cpu.c src/librtas_error.c src/librtas_error.h $(pseries_platform_SOURCES)
src_ppc64_cpu_LDADD = -lrtas -lpthread

endif

src_nvram_SOURCES = src/nvram.c src/nvram.h $(pseries_platform_SOURCES)
src_nvram_LDADD = -ldl -lz

src_lsprop_SOURCES = src/lsprop.c $(pseries_platform_SOURCES)

src_lparstat_SOURCES = src/lparstat.c src/lparstat.h $(pseries_platform_SOURCES)

if WITH_LIBRTAS
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
	$(pseries_platform_SOURCES)

noinst_HEADERS += \
	src/drmgr/drcpu.h \
	src/drmgr/dr.h \
	src/drmgr/drmem.h \
	src/drmgr/drpci.h \
	src/drmgr/rtas_calls.h \
	src/drmgr/ofdt.h \
	src/drmgr/lsslot.h \
	src/drmgr/rtas_calls.h

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

src_drmgr_lsslot_LDADD = -lrtas
