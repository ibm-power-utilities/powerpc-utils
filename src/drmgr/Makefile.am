AM_CFLAGS = -Wall -g

if WITH_LIBRTAS
sbin_PROGRAMS = drmgr lsslot
endif

drmgr_SOURCES = common.c common_cpu.c common_ofdt.c common_pci.c	\
		drcpu.h dr.h drmem.h drmgr.c drmig_chrp_pmig.c drpci.h	\
		drslot_chrp_cpu.c drslot_chrp_hea.c drslot_chrp_mem.c	\
		drslot_chrp_pci.c drslot_chrp_phb.c drslot_chrp_slot.c	\
		ofdt.h rtas_calls.c rtas_calls.h
drmgr_LDADD = -lrtas

lsslot_SOURCES = lsslot.c lsslot.h lsslot_chrp_cpu.c common.c		\
		 common_cpu.c common_pci.c common_ofdt.c ofdt.h		\
		 rtas_calls.c rtas_calls.h drslot_chrp_mem.c
lsslot_LDADD = -lrtas
