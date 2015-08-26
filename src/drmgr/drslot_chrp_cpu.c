/**
 * @file drslot_chrp_cpu.c
 *
 *
 * Copyright (C) IBM Corporation 2006
 *
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
#include "dr.h"
#include "drcpu.h"
#include "drpci.h"
#include "ofdt.h"

struct cpu_operation;
typedef int (cpu_op_func_t) (struct options *);

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

/**
 * get_available_cpu
 *
 * Find an available cpu to that we can add or remove, depending
 * on the request.
 *
 * @param opts options passed in to drmgr
 * @param dr_info cpu drc information
 * @returns pointer to cpu on success, NULL on failure
 */
struct dr_node *
get_available_cpu(struct options *opts, struct dr_info *dr_info)
{
	struct dr_node *cpu = NULL;
	struct dr_node *survivor = NULL;
	struct thread *t;

	if (opts->usr_drc_name) {
		cpu = get_cpu_by_name(dr_info, opts->usr_drc_name);
		if (!cpu)
			say(ERROR, "Could not locate cpu %s\n",
			    opts->usr_drc_name);

		return cpu;
	} else if (opts->usr_drc_index) {
		cpu = get_cpu_by_index(dr_info, opts->usr_drc_index);
		if (!cpu)
			say(ERROR, "Could not locate cpu %x\n",
			    opts->usr_drc_index);

		return cpu;
	}

	switch (opts->action) {
	    case ADD:
		for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
			if (cpu->unusable)
				continue;
			if (!cpu->is_owned)
				survivor = cpu;
		}

		cpu = survivor;
		break;

	    case REMOVE:
		/* Find the first cpu with an online thread */
		for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
			if (cpu->unusable)
				continue;

			for (t = cpu->cpu_threads; t; t = t->next) {
				if (get_thread_state(t) == ONLINE)
					return cpu;
			}
		}
		break;
	}

	if (!cpu)
		say(ERROR, "Could not find available cpu.\n");

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
static int
add_cpus(struct options *opts, struct dr_info *dr_info)
{
	int rc = -1;
	uint count;
	struct dr_node *cpu = NULL;

	count = 0;
	while (count < opts->quantity) {
		if (drmgr_timed_out())
			break;

		cpu = get_available_cpu(opts, dr_info);
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
		count++;
	}

	say(DEBUG, "Acquired %d of %d requested cpu(s).\n", count,
	    opts->quantity);
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
static int
remove_cpus(struct options *opts, struct dr_info *dr_info)
{
	int rc = 0;
	uint count = 0;
	struct dr_node *cpu;

	while (count < opts->quantity) {
		if (drmgr_timed_out())
			break;

		if (cpu_count(dr_info) == 1) {
			say(WARN, "Cannot remove the last CPU\n");
			rc = -1;
			break;
		}

		cpu = get_available_cpu(opts, dr_info);
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
		count++;
	}

	say(DEBUG, "Removed %d of %d requested cpu(s)\n", count,
	    opts->quantity);
	return rc;
}

/**
 * smt_thread_func
 * @brief Act upon logical cpus/threads
 *
 * @param opts
 * @returns 0 on success, !0 otherwise
 */
static int
smt_threads_func(struct options *opts, struct dr_info *dr_info)
{
	int rc;
	struct dr_node *cpu;

	if (opts->quantity != 1) {
		say(ERROR, "Quantity option '-q' may not be specified with "
		    "the '-p smt_threads' option\n");
		return -1;
	}

	if (! smt_enabled(dr_info)) {
		say(ERROR, "SMT functions not available on this system.\n");
		return -1;
	}

	if (opts->usr_drc_name) {
		cpu = get_cpu_by_name(dr_info, opts->usr_drc_name);
		if (cpu == NULL) {
			say(ERROR, "Could not find cpu %s\n",
			    opts->usr_drc_name);
			return -1;
		}

		if (opts->action == ADD)
			rc = cpu_enable_smt(cpu, dr_info);
		else if (opts->action == REMOVE)
			rc = cpu_disable_smt(cpu);

	} else if (opts->usr_drc_index) {
		cpu = get_cpu_by_index(dr_info, opts->usr_drc_index);
		if (cpu == NULL) {
			say(ERROR, "Could not find cpu %x\n",
			    opts->usr_drc_index);
			return -1;
		}

		if (opts->action == ADD)
			rc = cpu_enable_smt(cpu, dr_info);
		else if (opts->action == REMOVE)
			rc = cpu_disable_smt(cpu);

	} else { /* no drc name given, action is system-wide */
		if (opts->action == ADD)
			rc = system_enable_smt(dr_info);
		if (opts->action == REMOVE)
			rc = system_disable_smt(dr_info);
	}

	return rc;
}

int
valid_cpu_options(struct options *opts)
{
	/* default to a quantity of 1 */
	if ((opts->quantity == 0))
		opts->quantity = 1;

	if ((opts->action != ADD) && (opts->action != REMOVE)) {
		say(ERROR, "The '-r' or '-a' option must be specified for "
		    "CPU operations.\n");
		return -1;
	}

	/* The -s option can specify a drc name or drc index */
	if (opts->usr_drc_name && !strncmp(opts->usr_drc_name, "0x", 2)) {
		opts->usr_drc_index = strtoul(opts->usr_drc_name, NULL, 16);
		opts->usr_drc_name = NULL;
	}

	return 0;
}

int
drslot_chrp_cpu(struct options *opts)
{
	struct dr_info dr_info;
	int rc = -1;

	if (! cpu_dlpar_capable()) {
		say(ERROR, "CPU DLPAR capability is not enabled on this "
		    "platform.\n");
		return -1;
	}

	if (opts->p_option) {
		if ((strcmp(opts->p_option, "ent_capacity") == 0) ||
		    (strcmp(opts->p_option, "variable_weight") == 0)) {
			rc = update_sysparm(opts);
			if (rc)
				say(ERROR, "Could not update system parameter "
				    "%s\n", opts->p_option);
			return rc;
		}
	}

	if (init_cpu_drc_info(&dr_info)) {
		say(ERROR, "Could not initialize Dynamic Reconfiguration "
		    "information.\n");
		return -1;
	}

	/* If a user specifies a drc name, the quantity to add/remove is
	 * one. Enforce that here so the loops in add/remove code behave
	 * accordingly.
	 */
	if (opts->usr_drc_name)
		opts->quantity = 1;

	if (opts->p_option && (strcmp(opts->p_option, "smt_threads") == 0)) {
		rc = smt_threads_func(opts, &dr_info);
	} else {
		switch (opts->action) {
		    case ADD:
			rc = add_cpus(opts, &dr_info);
			break;
		    case REMOVE:
			rc = remove_cpus(opts, &dr_info);
			break;
		}
	}

	free_cpu_drc_info(&dr_info);
	return rc;
}
