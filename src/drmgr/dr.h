/**
 * @file dr.h
 *
 * Copyright (C) IBM Corporation 2006
 */

#ifndef _H_DR
#define _H_DR

#include <syslog.h>
#include <signal.h>
#include <nl_types.h>
#include <unistd.h>
#include <stdarg.h>
#include "rtas_calls.h"
#include "drpci.h"

extern int output_level;
extern int log_fd;

/* Error Exit Codes */
#define RC_IN_USE		1
#define RC_NONEXISTENT 		3
#define RC_DONT_OWN		4
#define RC_ALREADY_OWN		5
#define RC_LINUX_SLOT		6 /* Special case for ConcMaint */

/* Online/Offline */
#define OFFLINE		0
#define ONLINE		1

static inline int is_dot_dir(char * _p)
{
	return (_p[0] == '.');
}

void * __zalloc(size_t, const char *, int);
#define zalloc(x)	__zalloc((x), __func__, __LINE__);

#define DR_LOCK_FILE    	"/var/lock/dr_config_lock"
#define PLATFORMPATH    	"/proc/device-tree/device_type"
#define OFDTPATH    		"/proc/ppc64/ofdt"
#define DR_COMMAND		"drslot_chrp_%s"
#define DRMIG_COMMAND		"drmig_chrp_%s"

struct options {
	int     action;	      /* remove, add, REPLACE, IDENTIFY ...           */
#define NONE		0
#define ADD 	     	1
#define REMOVE       	2
#define QUERY		3
#define REPLACE		4
#define IDENTIFY	5
#define MIGRATE		6
#define HIBERNATE	7

	int     no_ident;     /* used in drslot_chrp_pci                      */
	int 	timeout;      /* time (in seconds) to try operation           */
	char   *usr_drc_name; /* pointer to user-specified drc-name
	                       * of resource                                  */
	uint32_t	usr_drc_index;
	int     noprompt;     /* 1 = do not prompt user for input, assume yes */
	unsigned int quantity;  /* number of resources                        */
	char	*ctype;
	char	*p_option;
};

enum say_level { ERROR = 1, WARN, INFO, DEBUG};

/* The follwing are defined in common.c */
int say(enum say_level, char *, ...);
void report_unknown_error(char *, int);
int dr_init(void);
void dr_fini(void);
void set_timeout(int);
int drmgr_timed_out(void);
int dr_lock(void);
int dr_unlock(void);
int valid_platform(const char *);

void free_of_node(struct of_node *);
int add_device_tree_nodes(char *, struct of_node *);
int remove_device_tree_nodes(char *);

int update_property(const char *, size_t);
int get_property(const char *, const char *, void *, size_t);
int get_int_attribute(const char *, const char *, void *, size_t);
int get_str_attribute(const char *, const char *, void *, size_t);
int get_property_size(const char *, const char *);
int signal_handler(int, int, struct sigcontext *);
int sig_setup(void);
char *node_type(struct dr_node *);

struct dr_node *alloc_dr_node(struct dr_connector *, int, const char *);
int update_sysparm(struct options *);

int cpu_dlpar_capable(void);
int mem_dlpar_capable(void);
int slot_dlpar_capable(void);
int phb_dlpar_capable(void);
int pmig_capable(void);
int phib_capable(void);
int hea_dlpar_capable(void);
int cpu_entitlement_capable(void);
int mem_entitlement_capable(void);
void print_dlpar_capabilities(void);

void set_output_level(int);

#define DR_BUF_SZ	256

int drslot_chrp_slot(struct options *);
int valid_slot_options(struct options *);
void slot_usage(char **);

int drslot_chrp_cpu(struct options *);
int valid_cpu_options(struct options *);
void cpu_usage(char **);

int drslot_chrp_pci(struct options *);
int valid_pci_options(struct options *);
void pci_usage(char **);

int drslot_chrp_phb(struct options *);
int valid_phb_options(struct options *);
void phb_usage(char **);

int drslot_chrp_mem(struct options *);
int valid_mem_options(struct options *);
void mem_usage(char **);

int drslot_chrp_hea(struct options *);
int valid_hea_options(struct options *);
void hea_usage(char **);

int drmig_chrp_pmig(struct options *);
int valid_pmig_options(struct options *);
void pmig_usage(char **);
void phib_usage(char **);

int ams_balloon_active(void);

#endif
