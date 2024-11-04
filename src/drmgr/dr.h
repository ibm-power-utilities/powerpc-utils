/**
 * @file dr.h
 *
 * Copyright (C) IBM Corporation 2006
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef _H_DR
#define _H_DR

#include <syslog.h>
#include <signal.h>
#include <nl_types.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>
#include "rtas_calls.h"
#include "drpci.h"

extern unsigned output_level;
extern int log_fd;

extern int read_dynamic_memory_v2;

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

#define MAX(x,y)	(((x) > (y)) ? (x) : (y))

/* Global User Specifications */
enum drmgr_action {NONE, ADD, REMOVE, QUERY, REPLACE, IDENTIFY,
		   MIGRATE, HIBERNATE};

enum drc_type {DRC_TYPE_NONE, DRC_TYPE_PCI, DRC_TYPE_SLOT, DRC_TYPE_PHB,
	       DRC_TYPE_CPU, DRC_TYPE_MEM, DRC_TYPE_PORT,
	       DRC_TYPE_HIBERNATE, DRC_TYPE_MIGRATION, DRC_TYPE_ACC};

enum hook_phase {HOOK_CHECK, HOOK_UNDOCHECK, HOOK_PRE, HOOK_POST};

extern enum drmgr_action usr_action;
extern int display_capabilities;
extern int usr_slot_identification;
extern int usr_timeout;
extern char *usr_drc_name;
extern uint32_t usr_drc_index;
extern int usr_prompt;
extern unsigned usr_drc_count;
extern enum drc_type usr_drc_type;
extern char *usr_p_option;
extern char *usr_t_option;
extern int pci_virtio;     /* qemu virtio device (legacy guest workaround) */
extern char *prrn_filename;
extern int show_available_slots;
extern int show_cpus_and_caches;
extern int show_occupied_slots;
extern int show_caches;
extern char *usr_delimiter;
extern int pci_hotplug_only;

enum say_level { ERROR = 1, WARN, INFO, DEBUG, EXTRA_DEBUG};

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
int remove_device_tree_nodes(const char *path);

int update_property(const char *, size_t);
int get_property(const char *, const char *, void *, size_t);
int get_int_attribute(const char *, const char *, void *, size_t);
int get_str_attribute(const char *, const char *, void *, size_t);
int get_ofdt_uint_property(const char *, const char *, uint *);
int get_property_size(const char *, const char *);
int signal_handler(int, int, struct sigcontext *);
int sig_setup(void);
char *node_type(struct dr_node *);

struct dr_node *alloc_dr_node(struct dr_connector *, int, const char *);
int update_sysparm(void);

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

void set_output_level(unsigned);

int run_hooks(enum drc_type drc_type, enum drmgr_action, enum hook_phase phase,
	      int drc_count);

#define DR_BUF_SZ	256

int drslot_chrp_slot(void);
int valid_slot_options(void);
void slot_usage(char **);

int drslot_chrp_cpu(void);
int valid_cpu_options(void);
void cpu_usage(char **);

int drslot_chrp_pci(void);
int valid_pci_options(void);
void pci_usage(char **);

int drslot_chrp_phb(void);
int valid_phb_options(void);
void phb_usage(char **);

int drslot_chrp_mem(void);
int valid_mem_options(void);
void mem_usage(char **);

int drslot_chrp_hea(void);
int valid_hea_options(void);
void hea_usage(char **);

int drmig_chrp_pmig(void);
int valid_pmig_options(void);
void pmig_usage(char **);
void phib_usage(char **);

int dracc_chrp_acc(void);
int valid_acc_options(void);
void acc_usage(char **);

int ams_balloon_active(void);

int is_display_adapter(struct dr_node *);

enum drc_type to_drc_type(const char *);

#define PRRN_TIMEOUT 30
int handle_prrn(void);

int kernel_dlpar_exists(void);
int do_kernel_dlpar_common(const char *, int, int);
static inline int do_kernel_dlpar(const char *cmd, int len)
{
	return do_kernel_dlpar_common(cmd, len, 0);
}
int do_dt_kernel_dlpar(uint32_t, int);
#endif
