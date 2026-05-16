/**
 * @file drslot_chrp_cpu.c
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <librtas.h>
#include <numa.h>
#include "dr.h"
#include "drcpu.h"
#include "drpci.h"
#include "ofdt.h"
#include "common_numa.h"

#define	DEFAULT_LMB_SIZE	0x10000000	/* 256MB */

struct cpu_operation;
typedef int (cpu_op_func_t) (void);

struct parm_to_func {
	char	*parmname;
	cpu_op_func_t *func;
};


static char *usagestr = "-c cpu {-a | -r} {-q <quantity> -p {variable_weight | ent_capacity} [-s drc_name | drc_index]";

/**
 * cpu_usage
 *
 */
void
cpu_usage(char **pusage)
{
	*pusage = usagestr;
}

static struct dr_node *
get_cpu_by_name(struct dr_info *drinfo, const char *name)
{
	struct dr_node *cpu;

	for (cpu = drinfo->all_cpus; cpu; cpu = cpu->next) {
		if (strcmp(cpu->drc_name, name) == 0) {
			break;
		}
	}

	return cpu;
}

static struct dr_node *
get_cpu_by_index(struct dr_info *drinfo, uint32_t index)
{
	struct dr_node *cpu;

	for (cpu = drinfo->all_cpus; cpu; cpu = cpu->next) {
		if (cpu->drc_index == index) {
			break;
		}
	}

	return cpu;
}

/**
 * cpu_count
 *
 * Count the number of CPUs currently on the system
 *
 * @param dr_info cpu drc information
 * @return number of cpus
 */
static int cpu_count(struct dr_info *dr_info)
{
	struct dr_node *cpu;
	int cpu_count = 0;

	for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
		if (cpu->is_owned)
			cpu_count++;
	}

	say(DEBUG, "Number of CPUs = %d\n", cpu_count);
	return cpu_count;
}

static struct dr_node *get_available_cpu_by_name(struct dr_info *dr_info)
{
	struct dr_node *cpu;

	cpu = get_cpu_by_name(dr_info, usr_drc_name);
	if (!cpu) {
		say(ERROR, "Could not locate CPU \"%s\"\n", usr_drc_name);
		return NULL;
	} 

	if (cpu->unusable) {
		say(ERROR, "Requested CPU \"%s\" is unusable\n", usr_drc_name);
		return NULL;
	}

	if (usr_action == ADD && cpu->is_owned) {
		say(ERROR, "Requested CPU \"%s\" is already present.\n",
		    usr_drc_name); 
		return NULL;
	} else if (usr_action == REMOVE && !cpu->is_owned) {
		say(ERROR, "Requested CPU \"%s\" is not present.\n",
		    usr_drc_name); 
		return NULL;
	}

	return cpu;
}
	
static struct dr_node *get_available_cpu_by_index(struct dr_info *dr_info)
{
	struct dr_node *cpu;

	cpu = get_cpu_by_index(dr_info, usr_drc_index);
	if (!cpu) {
		say(ERROR, "Could not locate CPU with drc index %x\n",
		    usr_drc_index);
		return NULL;
	} 

	if (cpu->unusable) {
		say(ERROR, "Requested CPU with drc index %x is unusable\n",
		    usr_drc_index);
		return NULL;
	}

	if (usr_action == ADD && cpu->is_owned) {
		say(ERROR, "Requested CPU with drc index %x is "
		    "already present.\n", usr_drc_index); 
		return NULL;
	} else if (usr_action == REMOVE && !cpu->is_owned) {
		say(ERROR, "Requested CPU with drc index %x is "
		    "not present.\n", usr_drc_index); 
		return NULL;
	}

	return cpu;
}

/*
 * Return node if CPU ID matches in node CPU bitmap.
 */
static struct ppcnuma_node *match_cpu_node(struct ppcnuma_node *node,
					struct dr_node *cpu)
{
	int nid;

	if (cpu->cpu_threads) {
		nid = numa_node_of_cpu(cpu->cpu_threads->id);
		if (nid == node->node_id) {
			if (numa_bitmask_isbitset(node->cpus,
						cpu->cpu_threads->id))
				return node;
		}
	}

	return NULL;
}

/*
 * Return node if CPU belongs to any memoryless NUMA node.
 */
static struct ppcnuma_node *find_cpu_memless_node(struct dr_node *cpu)
{
	struct ppcnuma_node *node = NULL;
	int nid;

	ppcnuma_foreach_node(&numa, nid, node) {
		if (node->n_lmbs)
			continue;

		if (match_cpu_node(node, cpu))
			return node;
	}

	return NULL;
}

/*
 * The node list is sorted by node ratio (less memory per CPU).
 * So consider the first node
 * Return node if CPU belongs to the first NUMA node which
 * has memory.
 */
static struct ppcnuma_node *find_cpu_numa_node(struct dr_node *cpu)
{
	struct ppcnuma_node *node = NULL;
	int found = 0;

	ppcnuma_foreach_node_by_ratio(&numa, node) {
		if (node->n_cpus && node->n_lmbs) {
			found = 1;
			break;
		}
	}

	if (found && match_cpu_node(node, cpu))
		return node;

	return NULL;
}

/*
 * Calculate node ratio based on amount of memory per CPU and sort
 * the node ratio list.
 */
static void cpu_update_node_ratio(void)
{
	struct ppcnuma_node *node;
	int nid;

	ppcnuma_foreach_node(&numa, nid, node) {
		if (!node->n_lmbs || !node->n_cpus)
			continue;

		/*
		 * Node ratio = n_lmbs per CPU
		 */
		node->ratio = (node->n_lmbs * 100) / node->n_cpus;
	}

	order_numa_node_ratio_list();
}

/*
 * Scan CPUs from the last one in the list and select the first CPU
 * based on:
 * - CPU from memory less node
 * - If no CPUs are available in memory less nodes, CPU belongs to
 *   the first node from node ratio list.
 */
static struct dr_node *numa_get_next_cpu(struct dr_info *dr_info)
{
	struct ppcnuma_node *node;
	struct dr_node *cpu = NULL;
	struct thread *t;
	int i, found = 0;

	/*
	 * Update node ratio for each CPU removal request
	 */
	cpu_update_node_ratio();

	/* Find the first cpu with an online thread */
	for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
		if (cpu->unusable)
			continue;

		if (numa.memless_cpu_count)
			node = find_cpu_memless_node(cpu);
		else
			node = find_cpu_numa_node(cpu);

		if (!node)
			continue;

		t = cpu->cpu_threads;
		for (i = 0; i < cpu->cpu_nthreads && t; i++, t = t->next) {
			if (get_thread_state(t) == ONLINE)
				found = 1;
			numa_bitmask_clearbit(node->cpus, t->id);
		}
		if (found) {
			node->n_cpus -= cpu->cpu_nthreads;
			numa.cpu_count -= cpu->cpu_nthreads;
			if (!node->n_lmbs)
				numa.memless_cpu_count -= cpu->cpu_nthreads;
			return cpu;
		}
	}

	return NULL;
}

/*
 * Scan all CPUs from the last one for the next available CPU.
 * Used only for non-NUMA based CPU removal.
 */
static struct dr_node *get_next_cpu(struct dr_info *dr_info)
{
	struct dr_node *cpu = NULL;
	struct thread *t;

	/* Find the first cpu with an online thread */
	for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
		if (cpu->unusable)
			continue;

		for (t = cpu->cpu_threads; t; t = t->next) {
			if (get_thread_state(t) == ONLINE)
				return cpu;
		}
	}

	return NULL;
}

static struct dr_node *get_next_available_cpu(struct dr_info *dr_info)
{
	struct dr_node *cpu = NULL;
	struct dr_node *survivor = NULL;
	
	if (usr_action == ADD) {
		for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
			if (cpu->unusable)
				continue;
			if (!cpu->is_owned)
				survivor = cpu;
		}

		cpu = survivor;
	} else if (usr_action == REMOVE) {
		if (numa_enabled)
			/* Find the first CPU from NUMA nodes */
			cpu = numa_get_next_cpu(dr_info);
		else
			/* Find the first cpu with an online thread */
			cpu = get_next_cpu(dr_info);
	}

	if (!cpu)
		say(ERROR, "Could not find available cpu.\n");

	return cpu;
}

/**
 * get_available_cpu
 *
 * Find an available cpu to that we can add or remove, depending
 * on the request.
 *
 * @param dr_info cpu drc information
 * @returns pointer to cpu on success, NULL on failure
 */
struct dr_node *get_available_cpu(struct dr_info *dr_info)
{
	struct dr_node *cpu = NULL;

	if (usr_drc_name)
		cpu = get_available_cpu_by_name(dr_info);
	else if (usr_drc_index)
		cpu = get_available_cpu_by_index(dr_info);
	else
		cpu = get_next_available_cpu(dr_info);

	return cpu;
}

/**
 * add_cpus
 *
 * Attempt to acquire and online the given number of cpus.
 * This function calls itself recursively to simplify recovery
 * actions in case of an error.  This is intended only for the case
 * where the user does not specify a drc-name.
 *
 * The final steps are to display the drc-names value to stdout and
 * return with 0.
 *
 * @param nr_cpus
 * @returns 0 on success, !0 otherwise
 */
static int add_cpus(struct dr_info *dr_info, int *count)
{
	int rc = -1;
	struct dr_node *cpu = NULL;

	*count = 0;
	while (*count < usr_drc_count) {
		if (drmgr_timed_out())
			break;

		cpu = get_available_cpu(dr_info);
		if (!cpu)
			break;

		rc = probe_cpu(cpu, dr_info);
		if (rc) {
			say(DEBUG, "Unable to acquire CPU with drc index %x\n",
			    cpu->drc_index);
			cpu->unusable = 1;
			continue;
		}

		fprintf(stdout, "%s\n", cpu->drc_name);
		(*count)++;
	}

	say(DEBUG, "Acquired %d of %d requested cpu(s).\n", *count,
	    usr_drc_count);
	return rc ? 1 : 0;
}

/**
 * remove_cpus
 *
 * Attempt to offline and release to the hypervisor the given number of
 * cpus.  This functions calls itself recursively to simplify recovery
 * actions in the case of an error.  This is intended only for the case
 * where the user does not specify a drc-name.
 *
 * From "Design Specification for AIX Configuration Support of
 * Dynamic Reconfiguration including the drmgr command and drslot for
 * memory, processors, and PCI slots" Version 1.2:
 *
 * Section V. Part B. Item 2. "drslot_chrp_cpu -r -c cpu"
 * "Once the resource has been released by the kernel, all the following
 * steps are taken.  Errors are ignored.  The code continues releasing the
 * resource by using RTAS services. ..."
 * "If successful, the code displays the drc-names value to stdout and
 * returns with 0, else displays an error message to stderr and returns with
 * non-zero."
 *
 * @param nr_cpus
 * @returns 0 on success, !0 otherwise
 */
static int remove_cpus(struct dr_info *dr_info, int *count)
{
	int rc = 0;
	struct dr_node *cpu;

	*count = 0;
	while (*count < usr_drc_count) {
		if (drmgr_timed_out())
			break;

		if (cpu_count(dr_info) == 1) {
			say(WARN, "Cannot remove the last CPU\n");
			rc = -1;
			break;
		}

		cpu = get_available_cpu(dr_info);
		if (!cpu)
			break;

		/* cpu is invalid after release_cpu, so no recovery
		 * steps seem feasible.  We could copy the cpu name
		 * and look it up again if the operation fails.
		 */
		rc = release_cpu(cpu, dr_info);
		if (rc) {
			online_cpu(cpu, dr_info);
			cpu->unusable = 1;
			continue;
		}

		fprintf(stdout, "%s\n", cpu->drc_name);
		(*count)++;
	}

	say(DEBUG, "Removed %d of %d requested cpu(s)\n", *count,
	    usr_drc_count);
	return rc;
}

/**
 * smt_thread_func
 * @brief Act upon logical cpus/threads
 *
 * @returns 0 on success, !0 otherwise
 */
static int smt_threads_func(struct dr_info *dr_info)
{
	int rc;
	struct dr_node *cpu;

	if (usr_drc_count != 1) {
		say(ERROR, "Quantity option '-q' may not be specified with "
		    "the '-p smt_threads' option\n");
		return -1;
	}

	if (! smt_enabled(dr_info)) {
		say(ERROR, "SMT functions not available on this system.\n");
		return -1;
	}

	if (usr_drc_name) {
		cpu = get_cpu_by_name(dr_info, usr_drc_name);
		if (cpu == NULL) {
			say(ERROR, "Could not find cpu %s\n", usr_drc_name);
			return -1;
		}

		if (usr_action == ADD)
			rc = cpu_enable_smt(cpu, dr_info);
		else if (usr_action == REMOVE)
			rc = cpu_disable_smt(cpu);

	} else if (usr_drc_index) {
		cpu = get_cpu_by_index(dr_info, usr_drc_index);
		if (cpu == NULL) {
			say(ERROR, "Could not find cpu %x\n", usr_drc_index);
			return -1;
		}

		if (usr_action == ADD)
			rc = cpu_enable_smt(cpu, dr_info);
		else if (usr_action == REMOVE)
			rc = cpu_disable_smt(cpu);

	} else { /* no drc name given, action is system-wide */
		if (usr_action == ADD)
			rc = system_enable_smt(dr_info);
		if (usr_action == REMOVE)
			rc = system_disable_smt(dr_info);
	}

	return rc;
}

/*
 * Per node CPUs are defined as part of build_numa_topology().
 * This function calculates number of LMBs per node based on
 * node mememory / lmb-size.
 * n_cpus and n_lmbs are used to determine node ratio.
 */
static int cpu_update_numa_config(void)
{
	struct ppcnuma_node *node;
	unsigned long long node_size;
	int rc, nid;
	uint64_t lmb_sz;

	rc = get_dynamic_lmb_size(&lmb_sz);
	/*
	 * Use the default value if lmb-size property is not available.
	 * For CPU removal, node ratio will be calculated based on
	 * total n_lmbs per CPU.
	 */
	if (rc)
		lmb_sz = DEFAULT_LMB_SIZE;

	ppcnuma_foreach_node(&numa, nid, node) {
		node_size = numa_node_size(nid, 0);
		/*
		 * Node has memory
		 * n_lmbs = Total memory / lmb-size
		 */
		if (node_size) {
			node->n_lmbs = node_size / lmb_sz;
		} else
			numa.memless_cpu_count += node->n_cpus;
	}

	return 0;
}

int valid_cpu_options(void)
{
	/* default to a quantity of 1 */
	if (usr_drc_count == 0)
		usr_drc_count = 1;

	if ((usr_action != ADD) && (usr_action != REMOVE)) {
		say(ERROR, "The '-r' or '-a' option must be specified for "
		    "CPU operations.\n");
		return -1;
	}

	/* The -s option can specify a drc name or drc index */
	if (usr_drc_name && !strncmp(usr_drc_name, "0x", 2)) {
		usr_drc_index = strtoul(usr_drc_name, NULL, 16);
		usr_drc_name = NULL;
	}

	return 0;
}

int drslot_chrp_cpu(void)
{
	struct dr_info dr_info;
	int rc, count = 0;

	if (! cpu_dlpar_capable()) {
		say(ERROR, "CPU DLPAR capability is not enabled on this "
		    "platform.\n");
		return -1;
	}

	if (usr_p_option && (!strcmp(usr_p_option, "ent_capacity") ||
	    !strcmp(usr_p_option, "variable_weight"))) {
		rc = update_sysparm();
		if (rc)
			say(ERROR, "Could not update system parameter "
			    "%s\n", usr_p_option);
		return rc;
	}

	if (init_cpu_drc_info(&dr_info)) {
		say(ERROR, "Could not initialize Dynamic Reconfiguration "
		    "information.\n");
		return -1;
	}

	/*
	 * Maintain NUMA aware hotplug only for remove and with count request.
	 */
	if (usr_drc_count && (usr_action == REMOVE)) {
		build_numa_topology();
		if (numa_enabled)
			cpu_update_numa_config();
	}

	/* If a user specifies a drc name, the quantity to add/remove is
	 * one. Enforce that here so the loops in add/remove code behave
	 * accordingly.
	 */
	if (usr_drc_name)
		usr_drc_count = 1;

	if (usr_p_option && !strcmp(usr_p_option, "smt_threads")) {
		rc = smt_threads_func(&dr_info);
		free_cpu_drc_info(&dr_info);
		return rc;
	}

	if (usr_action == ADD || usr_action == REMOVE)
		run_hooks(DRC_TYPE_CPU, usr_action, HOOK_PRE, usr_drc_count);

	switch (usr_action) {
	case ADD:
		rc = add_cpus(&dr_info, &count);
		break;
	case REMOVE:
		rc = remove_cpus(&dr_info, &count);
		break;
	default:
		rc = -1;
		break;
	}

	if (usr_action == ADD || usr_action == REMOVE)
		run_hooks(DRC_TYPE_CPU, usr_action, HOOK_POST, count);

	if ((usr_action == REMOVE) && numa_enabled)
		free_numa_topology();

	free_cpu_drc_info(&dr_info);
	return rc;
}
