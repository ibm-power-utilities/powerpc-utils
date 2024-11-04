/**
 * @file lparnumascore.c
 *
 * Copyright (C) IBM Corporation 2021
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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <inttypes.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include "pseries_platform.h"
#include "numa.h"
#include "dr.h"
#include "drcpu.h"
#include "drmem.h"

#include "options.c"

#define NUMA_NO_NODE	-1

unsigned output_level = 0;
int log_fd = 0;
int min_common_depth;
int read_dynamic_memory_v2 = 1;

static bool check_node(char *syspath, int node)
{
	static char nodepath[PATH_MAX];
	struct stat sbuf;

	snprintf(nodepath, PATH_MAX, "%s/node%d", syspath, node);
	say(EXTRA_DEBUG, "checking %s\n", nodepath);
	return stat(nodepath, &sbuf) == 0;
}


static int find_node(char *syspath, int node_hint)
{
	int node;

	if (check_node(syspath, node_hint))
		return node_hint;

	say(EXTRA_DEBUG, "Checking up to node %d\n", numa_max_node());
	for (node = 0; node <= numa_max_node(); node++) {
		if (node == node_hint)
			continue;
		if (check_node(syspath, node))
			return node;
	}

	return NUMA_NO_NODE;
}

static void print_mem(struct dr_node *lmb,
		      struct mem_scn *scn,
		      int nid, int dtnid,
		      bool first)
{
	if (first) {
		say(INFO, "# Badly binded LMBs\n");
		say(INFO, "# DRC index\tAddr\tLinux node\tDT node\n");
	}

	say(INFO, "0x%x\t%lx\t%d\t%d\n",
	    lmb->drc_index, scn->phys_addr, nid, dtnid);
}


static int compute_mem_score(void)
{
	struct lmb_list_head *lmb_list;
	struct dr_node *lmb;
	struct assoc_arrays aa;
	unsigned long memory_size = 0, memory_badly_binded_size = 0;
	int rc;

	if (get_assoc_arrays(DYNAMIC_RECONFIG_MEM, &aa, min_common_depth))
		return 1;

	lmb_list = get_lmbs(LMB_NORMAL_SORT);
	if (lmb_list == NULL || lmb_list->lmbs == NULL) {
		say(WARN, "Can't read the LMB list\n");
		return 1;
	}

	rc = 1;
	for (lmb = lmb_list->lmbs; lmb; lmb = lmb->next) {
		int dtnid, nid;
		struct mem_scn *scn;

		if (!lmb->is_owned)
			continue;

		memory_size += lmb->lmb_size;

		dtnid = aa_index_to_node(&aa, lmb->lmb_aa_index);
		if (dtnid == NUMA_NO_NODE) {
			say(ERROR, "Can't get DT NUMA node of LMB %lx\n",
			    lmb->lmb_address);
			goto out_free;
		}

		say(DEBUG, "Checking LMB %lx DT node:%d aa_index:%d\n",
		    lmb->lmb_address, dtnid, lmb->lmb_aa_index);

		for (scn = lmb->lmb_mem_scns; scn; scn = scn->next) {
			nid = find_node(scn->sysfs_path, dtnid);
			if (nid != dtnid) {
				print_mem(lmb, scn, nid, dtnid,
					  memory_badly_binded_size == 0);
				memory_badly_binded_size += block_sz_bytes;
			}
		}
	}
	rc = 0;
	printf("MEM score: %lu\n",
	       ((memory_size - memory_badly_binded_size) * 100) / memory_size);

out_free:
	free_lmbs(lmb_list);
	return rc;
}

/*
 * CPU Score processing, find CPUs badly binded.
 * only the first CPU of a thread group is checked.
 */
static void dump_cpu_table(struct dr_info *dr_info)
{
	struct dr_node *cpu;
	int dtnid;

	say(DEBUG, "CPU\tLinux Node\tDT node\n");
	for (cpu = dr_info->all_cpus; cpu != NULL; cpu = cpu->next) {
		if (!cpu->is_owned || !cpu->cpu_threads)
			continue;

		/*
		 * Number of threads: cpu->cpu_nthreads
		 * First CPU id: cpu->cpu_threads->id
		 */
		dtnid = of_associativity_to_node(cpu->ofdt_path,
						 min_common_depth);

		say(DEBUG, "%d-%d\t%d\t%d\n", cpu->cpu_threads->id,
		    cpu->cpu_threads->id + cpu->cpu_nthreads,
		    numa_node_of_cpu(cpu->cpu_threads->id), dtnid);
	}
}

static void print_cpu(struct dr_node *cpu, int nid, int dtnid, bool first)
{
	if (first) {
		say(INFO, "# Badly binded CPUs\n");
		say(INFO, "# DRC index\tCPU\tLinux Node\tDT Node\n");
	}
	say(INFO, "0x%x\t%d-%d\t%d\t%d\n", cpu->drc_index,
	    cpu->cpu_threads->id, cpu->cpu_threads->id + cpu->cpu_nthreads,
	    nid, dtnid);
}

static int compute_cpu_score(void)
{
	struct dr_info dr_info;
	struct dr_node *cpu;
	int dtnid, nid;
	unsigned int ncpus = 0, badly_binded_cpus = 0;

	if (init_cpu_drc_info(&dr_info)) {
		say(ERROR, "\nThere are no dynamically reconfigurable "
		    "CPUs on this system.\n\n");
		return 1;
	}

	if (output_level >= DEBUG)
		dump_cpu_table(&dr_info);

	for (cpu = dr_info.all_cpus; cpu != NULL; cpu = cpu->next) {
		if (!cpu->is_owned || !cpu->cpu_threads)
			continue;

		dtnid = of_associativity_to_node(cpu->ofdt_path,
						 min_common_depth);
		nid = numa_node_of_cpu(cpu->cpu_threads->id);

		ncpus += cpu->cpu_nthreads;
		if (dtnid != nid) {
			print_cpu(cpu, nid, dtnid, (badly_binded_cpus == 0));
			badly_binded_cpus += cpu->cpu_nthreads;
		}
	}
	if (badly_binded_cpus)
		say(INFO, "# %u/%u CPUs badly binded\n",
		    badly_binded_cpus, ncpus);

	printf("CPU score: %u\n",
	       ((ncpus - badly_binded_cpus) * 100) / ncpus);

	free_cpu_drc_info(&dr_info);
	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"Usage: lparnumascore [-d detail_level] [-c {mem | cpu}]\n");
}

static int parse_options(int argc, char *argv[])
{
	int c;

	/* disable getopt error messages */
	opterr = 0;

	while ((c = getopt(argc, argv, "d:c:h")) != -1) {
		switch (c) {
		case 'c':
			if (usr_drc_type != DRC_TYPE_NONE) {
				usage();
				return -1;
			}
			usr_drc_type = to_drc_type(optarg);
			if (usr_drc_type == DRC_TYPE_NONE) {
				usage();
				return -1;
			}
			break;
		case 'd':
			set_output_level(strtoul(optarg, NULL, 10));
			break;
		case 'h':
			usage();
			return -1;
		default:
			fprintf(stderr, "Invalid option specified '%c'\n",
				optopt);
			return -1;
		}
	}

	switch (usr_drc_type) {
	case DRC_TYPE_CPU:
	case DRC_TYPE_MEM:
	case DRC_TYPE_NONE:
		break;
	default:
		usage();
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	if (parse_options(argc, argv))
		exit(1);

	if (get_platform() != PLATFORM_PSERIES_LPAR) {
		say(ERROR, "%s: is not supported on the %s platform\n",
		    argv[0], platform_name);
		exit(1);
	}

	if (numa_available() == -1) {
		say(ERROR, "%s: NUMA is not available", argv[0]);
		exit(1);
	}

	min_common_depth = get_min_common_depth();
	if (min_common_depth < 0)
		exit(1);

	switch (usr_drc_type) {
	case DRC_TYPE_CPU:
		compute_cpu_score();
		break;
	case DRC_TYPE_MEM:
		compute_mem_score();
		break;
	default:
		compute_cpu_score();
		compute_mem_score();
		break;
	}
	return 0;
}
