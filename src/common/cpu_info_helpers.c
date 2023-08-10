/**
 * @file cpu_info_helpers.c
 * @brief Common routines to capture cpu information
 *
 * Copyright (c) 2007, 2020 International Business Machines
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
 *
 * @author Anton Blanchard <anton@au.ibm.com>
 * @author Kamalesh Babulal <kamalesh@linux.vnet.ibm.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <stdbool.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "cpu_info_helpers.h"

int get_attribute(char *path, const char *fmt, int *value)
{
	FILE *fp;
	int rc;

	rc = access(path, F_OK);
	if (rc)
		return -1;

	fp = fopen(path, "r");
	if (!fp)
		return -1;

	rc = fscanf(fp, fmt, value);
	fclose(fp);

	if (rc == EOF)
		return -1;

	return 0;
}

static int test_sysattr(char *attribute, int perms, int threads_in_system)
{
	char path[SYSFS_PATH_MAX];
	int i;

	for (i = 0; i < threads_in_system; i++) {
		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		if (access(path, F_OK))
			continue;

		if (access(path, perms))
			return 0;
	}

	return 1;
}

int __sysattr_is_readable(char *attribute, int threads_in_system)
{
	return test_sysattr(attribute, R_OK, threads_in_system);
}

int __sysattr_is_writeable(char *attribute, int threads_in_system)
{
	return test_sysattr(attribute, W_OK, threads_in_system);
}

int cpu_physical_id(int thread)
{
	char path[SYSFS_PATH_MAX];
	int rc, physical_id;

	sprintf(path, SYSFS_CPUDIR"/physical_id", thread);
	rc = get_attribute(path, "%d", &physical_id);

	/* This attribute does not exist in kernels without hotplug enabled */
	if (rc && errno == ENOENT)
		return -1;
	return physical_id;
}

int cpu_online(int thread)
{
	char path[SYSFS_PATH_MAX];
	int rc, online;

	sprintf(path, SYSFS_CPUDIR"/online", thread);
	rc = get_attribute(path, "%d", &online);

	/* This attribute does not exist in kernels without hotplug enabled */
	if (rc && errno == ENOENT)
		return 1;

	if (rc || !online)
		return 0;

	return 1;
}

int is_subcore_capable(void)
{
	return access(SYSFS_SUBCORES, F_OK) == 0;
}

int num_subcores(void)
{
	int rc, subcores;

	rc = get_attribute(SYSFS_SUBCORES, "%d", &subcores);
	if (rc)
		return -1;
	return subcores;
}

int get_cpu_info(int *_threads_per_cpu, int *_cpus_in_system,
		 int *_threads_in_system)
{
	DIR *d;
	struct dirent *de;
	int first_cpu = 1;
	int rc;
	int subcores;
	int threads_in_system;
	int threads_per_cpu = 0;
	int cpus_in_system = 0;

	d = opendir("/proc/device-tree/cpus");
	if (!d)
		return -1;

	while ((de = readdir(d)) != NULL) {
		if (!strncmp(de->d_name, "PowerPC", 7)) {
			if (first_cpu) {
				struct stat sbuf;
				char path[PATH_MAX];

				snprintf(path, sizeof(path), INTSERV_PATH, de->d_name);
				rc = stat(path, &sbuf);
				if (!rc)
					threads_per_cpu = sbuf.st_size / 4;

				first_cpu = 0;
			}

			cpus_in_system++;
		}
	}

	closedir(d);
	threads_in_system = cpus_in_system * threads_per_cpu;

	subcores = num_subcores();
	if (is_subcore_capable() && subcores > 0) {
		threads_per_cpu /= subcores;
		cpus_in_system *= subcores;
	}

	*_threads_per_cpu = threads_per_cpu;
	*_threads_in_system = threads_in_system;
	*_cpus_in_system = cpus_in_system;

	return 0;
}

int __is_smt_capable(int threads_per_cpu)
{
	return threads_per_cpu > 1;
}

int __get_one_smt_state(int core, int threads_per_cpu)
{
	int primary_thread = core * threads_per_cpu;
	int smt_state = 0;
	int i;

	if (!__sysattr_is_readable("online", threads_per_cpu)) {
		perror("Cannot retrieve smt state");
		return -2;
	}

	for (i = 0; i < threads_per_cpu; i++) {
		smt_state += cpu_online(primary_thread + i);
	}

	return smt_state;
}

static void print_cpu_list(const cpu_set_t *cpuset, int cpuset_size,
		           int cpus_in_system)
{
	int core;
	const char *comma = "";

	for (core = 0; core < cpus_in_system; core++) {
		int begin = core;
		if (CPU_ISSET_S(core, cpuset_size, cpuset)) {
			while (CPU_ISSET_S(core+1, cpuset_size, cpuset))
				core++;

			if (core > begin)
				printf("%s%d-%d", comma, begin, core);
			else
				printf("%s%d", comma, core);
			comma = ",";
		}
	}
}

int __do_smt(bool numeric, int cpus_in_system, int threads_per_cpu,
	     bool print_smt_state)
{
	int thread, c, smt_state = 0;
	cpu_set_t **cpu_states = NULL;
	int cpu_state_size = CPU_ALLOC_SIZE(cpus_in_system);
	int start_cpu = 0, stop_cpu = cpus_in_system;
	int rc = 0;

	cpu_states = (cpu_set_t **)calloc(threads_per_cpu, sizeof(cpu_set_t));
	if (!cpu_states)
		return -ENOMEM;

	for (thread = 0; thread < threads_per_cpu; thread++) {
		cpu_states[thread] = CPU_ALLOC(cpus_in_system);
		CPU_ZERO_S(cpu_state_size, cpu_states[thread]);
	}

	for (c = start_cpu; c < stop_cpu; c++) {
		int threads_online = __get_one_smt_state(c, threads_per_cpu);

		if (threads_online < 0) {
			rc = threads_online;
			goto cleanup_get_smt;
		}
		if (threads_online)
			CPU_SET_S(c, cpu_state_size,
					cpu_states[threads_online - 1]);
	}

	for (thread = 0; thread < threads_per_cpu; thread++) {
		if (CPU_COUNT_S(cpu_state_size, cpu_states[thread])) {
			if (smt_state == 0)
				smt_state = thread + 1;
			else if (smt_state > 0)
				smt_state = 0; /* mix of SMT modes */
		}
	}

	if (!print_smt_state)
		return smt_state;

	if (smt_state == 1) {
		if (numeric)
			printf("SMT=1\n");
		else
			printf("SMT is off\n");
	} else if (smt_state == 0) {
		for (thread = 0; thread < threads_per_cpu; thread++) {
			if (CPU_COUNT_S(cpu_state_size,
						cpu_states[thread])) {
				printf("SMT=%d: ", thread + 1);
				print_cpu_list(cpu_states[thread],
						cpu_state_size, cpus_in_system);
				printf("\n");
			}
		}
	} else {
		printf("SMT=%d\n", smt_state);
	}

cleanup_get_smt:
	for (thread = 0; thread < threads_per_cpu; thread++)
		CPU_FREE(cpu_states[thread]);

	return rc;
}
