AM_CFLAGS = -Wall -g 
AM_LDFLAGS =

SUBDIRS = drmgr

sbin_PROGRAMS = activate_firmware usysident usysattn set_poweron_time \
       rtas_ibm_get_vpd serv_config uesensor rtas_event_decode sys_ident \
       nvram lsprop ppc64_cpu lparstat

activate_firmware_SOURCES = activate_fw.c
activate_firmware_LDFLAGS = -lrtas

usysident_SOURCES = usysident.c librtas_error.c librtas_error.h
usysident_LDFLAGS = -lrtas

usysattn_SOURCES = usysident.c librtas_error.c librtas_error.h
usysattn_LDFLAGS = -lrtas

set_poweron_time_SOURCES = set_poweron_time.c librtas_error.c librtas_error.h
set_poweron_time_LDFLAGS = -lrtas

rtas_ibm_get_vpd_SOURCES = rtas_ibm_get_vpd.c librtas_error.c librtas_error.h
rtas_ibm_get_vpd_LDFLAGS = -lrtas

serv_config_SOURCES = serv_config.c librtas_error.c librtas_error.h
serv_config_LDFLAGS = -lrtas

uesensor_SOURCES = uesensor.c librtas_error.c librtas_error.h
uesensor_LDFLAGS = -lrtas

rtas_event_decode_SOURCES = rtas_event_decode.c
rtas_event_decode_LDFLAGS = -lrtasevent

sys_ident_SOURCES = sys_ident.c
sys_ident_LDFLAGS = -lrtas

nvram_SOURCES = nvram.c nvram.h
nvram_LDFLAGS = -ldl

lsprop_SOURCES = lsprop.c

ppc64_cpu_SOURCES = ppc64_cpu.c librtas_error.c librtas_error.h
ppc64_cpu_LDFLAGS = -lrtas -lpthread

lparstat_SOURCES = lparstat.c lparstat.h
