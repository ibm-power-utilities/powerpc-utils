/**
 * @file lsslot_chrp_cpu.c
 * @brief routines for lsslot_chrp_cpu command
 *
 * Copyright (C) IBM Corporation 2006
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "dr.h"
#include "drcpu.h"
#include "lsslot.h"

/**
 * list_cpus
 * @brief list all cpus
 *
 */
void
list_cpus(struct dr_info *dr_info)
{
	struct dr_node *cpu;
	struct thread *t;
	char *fmt_s = "%-11s%-20s%-13s%-13s\n";
	char *fmt = "%-11s%-20s%-12x";

	printf(fmt_s, "drc-name", "OFDT-node", "drc_index", "thread id(s)");

	for (cpu = dr_info->all_cpus; cpu != NULL; cpu = cpu->next) {
		if (cpu->is_owned) {
			printf(fmt, cpu->drc_name, cpu->name, cpu->drc_index);
			for (t = cpu->cpu_threads; t; t = t->sibling)
				printf(" %x", t->id);
			printf("\n");
		}
	}

	return;
}

/**
 * list_caches
 * @brief list all caches
 *
 */
void
list_caches(struct dr_info *dr_info)
{
	struct cache_info *cache = NULL;

	printf("cache-name              phandle\n");

	for (cache = dr_info->all_caches; cache != NULL; cache = cache->next)
		printf("%-21s%-8x\n", cache->name, cache->phandle);

	return;
}

/**
 * list_all_cpus_and_caches
 * @bried list all of the cpus and caches
 *
 */
void
list_cpus_and_caches(struct dr_info *dr_info)
{
	struct dr_node *cpu = NULL;
	struct thread *t;
	int thread_id_field_sz = 14;
	char *fmt_s = "%-11s%-20s%-13s%-13s%-11s%-11s\n";
	char *fmt = "%-11s%-20s%-12x%";
	char *fmt_caches = "%-11s%-11s\n";

	printf(fmt_s, "drc-name", "OFDT-node", "drc_index", "thread id(s)",
	       "l2-cache", "l3-cache");

	for (cpu = dr_info->all_cpus; cpu != NULL; cpu = cpu->next) {
		int i, count = 0;
		struct cache_info *l2_cache = NULL;
		struct cache_info *l3_cache = NULL;

		printf(fmt, cpu->drc_name, cpu->name, cpu->drc_index);

		for (t = cpu->cpu_threads; t; t = t->sibling) {
			printf(" %x", t->id);
			count += 2;
		}

		/* pad out the thread ids field */
		for (i = count; i < thread_id_field_sz; i++)
			printf(" ");

		l2_cache = cpu_get_dependent_cache(cpu, dr_info);
		if (l2_cache)
			l3_cache = cache_get_dependent_cache(l2_cache, dr_info);

		printf(fmt_caches, (l2_cache ? l2_cache->name : "N/A"),
		       (l3_cache ? l3_cache->name : "N/A"));
	}

	return;
}

/**
 * lsslot_chrp_cpu
 * @brief main entry point for lsslot_chrp_cpu command
 *
 * @param opts
 * @returns 0 on success, !0 otherwise
 */
int
lsslot_chrp_cpu(struct cmd_opts *opts)
{
	struct stat	sb;
	struct dr_info dr_info;

	/* Mask signals so the command doesn't get interrupted */
	if (sig_setup()) {
		fprintf(stderr, "\nUnknown failure. Rerun the command.\n\n");
		return 1;
	}

	/* Check if this is an LPAR System.  */
	if (stat("/proc/device-tree/ibm,lpar-capable", &sb)) {
		fprintf(stderr, "\nThe system is not LPAR.\n\n");
		return 1;
	}

	if (init_cpu_drc_info(&dr_info)) {
		fprintf(stderr, "\nThere are no dynamically reconfigurable "
			"CPUs on this system.\n\n");
		return 1;
	}

	if (opts->b_flag)
		list_cpus_and_caches(&dr_info);
	else if (opts->p_flag)
		list_caches(&dr_info);
	else
		list_cpus(&dr_info);

	return 0;
}

