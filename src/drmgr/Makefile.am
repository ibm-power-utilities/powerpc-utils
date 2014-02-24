COM_DIR = $(top_srcdir)/src/common

AM_CFLAGS = -Wall -g -I $(COM_DIR)

if WITH_LIBRTAS
sbin_PROGRAMS = drmgr lsslot
endif

pseries_platform_SOURCES = $(COM_DIR)/pseries_platform.c $(COM_DIR)/pseries_platform.h

drmgr_SOURCES = common.c common_cpu.c common_ofdt.c common_pci.c	\
		drcpu.h dr.h drmem.h drmgr.c drmig_chrp_pmig.c drpci.h	\
		drslot_chrp_cpu.c drslot_chrp_hea.c drslot_chrp_mem.c	\
		drslot_chrp_pci.c drslot_chrp_phb.c drslot_chrp_slot.c	\
		ofdt.h rtas_calls.c rtas_calls.h $(pseries_platform_SOURCES)
drmgr_LDADD = -lrtas

lsslot_SOURCES = lsslot.c lsslot.h lsslot_chrp_cpu.c common.c		\
		 common_cpu.c common_pci.c common_ofdt.c ofdt.h		\
		 rtas_calls.c rtas_calls.h drslot_chrp_mem.c $(pseries_platform_SOURCES)
lsslot_LDADD = -lrtas
