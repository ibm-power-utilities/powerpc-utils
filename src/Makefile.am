AM_CFLAGS = -Wall -g 
AM_LDFLAGS =

SUBDIRS = drmgr

sbin_PROGRAMS = activate_firmware usysident usysattn set_poweron_time \
       rtas_ibm_get_vpd serv_config uesensor rtas_event_decode sys_ident \
       nvram lsprop ppc64_cpu lparstat

activate_firmware_SOURCES = activate_fw.c
activate_firmware_LDADD = -lrtas

usysident_SOURCES = usysident.c librtas_error.c librtas_error.h
usysident_LDADD = -lrtas

usysattn_SOURCES = usysident.c librtas_error.c librtas_error.h
usysattn_LDADD = -lrtas

set_poweron_time_SOURCES = set_poweron_time.c librtas_error.c librtas_error.h
set_poweron_time_LDADD = -lrtas

rtas_ibm_get_vpd_SOURCES = rtas_ibm_get_vpd.c librtas_error.c librtas_error.h
rtas_ibm_get_vpd_LDADD = -lrtas

serv_config_SOURCES = serv_config.c librtas_error.c librtas_error.h
serv_config_LDADD = -lrtas

uesensor_SOURCES = uesensor.c librtas_error.c librtas_error.h
uesensor_LDADD = -lrtas

rtas_event_decode_SOURCES = rtas_event_decode.c
rtas_event_decode_LDADD = -lrtasevent

sys_ident_SOURCES = sys_ident.c
sys_ident_LDADD = -lrtas

nvram_SOURCES = nvram.c nvram.h
nvram_LDADD = -ldl -lz

lsprop_SOURCES = lsprop.c

ppc64_cpu_SOURCES = ppc64_cpu.c librtas_error.c librtas_error.h
ppc64_cpu_LDADD = -lrtas -lpthread

lparstat_SOURCES = lparstat.c lparstat.h
