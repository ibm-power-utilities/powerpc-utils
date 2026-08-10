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
	errno = 0;
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

int get_present_cpu_count(void)
{
        int start, end, total_cpus = 0;
        size_t len = 0;
        char *line = NULL;
        FILE *fp;
        char *token;

        fp = fopen(CPU_PRESENT_PATH, "r");
        if (!fp) {
                perror("Error opening CPU_PRESENT_PATH");
                return -1;
        }

        if (getline(&line, &len, fp) == -1) {
                perror("Error reading CPU_PRESENT_PATH");
                fclose(fp);
                free(line);
                return -1;
        }
        fclose(fp);

        token = strtok(line, ",");
        while (token) {
                if (sscanf(token, "%d-%d", &start, &end) == 2) {
                        total_cpus += (end - start + 1);
                } else if (sscanf(token, "%d", &start) == 1) {
                        total_cpus++;
                }
                token = strtok(NULL, ",");
        }

        free(line);
        return total_cpus;
}

int get_present_core_list(int **present_cores, int *num_present_cores, int threads_per_cpu)
{
        FILE *fp = NULL;
        char *line = NULL;
        char *token = NULL;
        size_t len = 0;
        ssize_t read;
        int core_count = 0;
        int core_list_size;
        int *cores = NULL;
        int start, end, i;

        if (threads_per_cpu <= 0) {
                fprintf(stderr, "Invalid threads_per_cpu value, got %d expected >= 1\n", threads_per_cpu);
                return -1;
        }

        core_list_size = get_present_cpu_count() / threads_per_cpu;
        if (core_list_size <= 0) {
                fprintf(stderr, "Error while calculating core list size\n");
                return -1;
        }

        cores = malloc(core_list_size * sizeof(int));
        if (!cores) {
                perror("Memory allocation failed");
                goto cleanup;
        }

        fp = fopen(CPU_PRESENT_PATH, "r");
        if (!fp) {
                perror("Error opening file");
                goto cleanup;
        }

        read = getline(&line, &len, fp);
        if (read == -1) {
                perror("Error reading file");
                goto cleanup;
        }

        token = strtok(line, ",");
        while (token) {
                if (sscanf(token, "%d-%d", &start, &end) == 2) {
                        for (i = start; i <= end; i++) {
                                if (i % threads_per_cpu == 0) {
                                        cores[core_count++] = i / threads_per_cpu;
                                }
                        }
                } else if (sscanf(token, "%d", &start) == 1) {
                        if (start % threads_per_cpu == 0) {
                                cores[core_count++] = start / threads_per_cpu;
                        }
                }
                token = strtok(NULL, ",");
        }

        *present_cores = cores;
        *num_present_cores = core_count;
        free(line);
        return 0;

cleanup:
        if (fp) {
                fclose(fp);
        }
        free(line);
        free(cores);
        return -1;
}

static void print_cpu_list(const cpu_set_t *cpuset, int cpuset_size,
		                   int threads_per_cpu)
{
	int *present_cores = NULL;
	int num_present_cores;
	int start, end, i = 0;
	const char *comma = "";

	if (get_present_core_list(&present_cores, &num_present_cores, threads_per_cpu) != 0) {
		fprintf(stderr, "Failed to get present_cores list\n");
		return;
	}

	while (i < num_present_cores) {
		start = present_cores[i];
		if (CPU_ISSET_S(start, cpuset_size, cpuset)) {
			end = start;
			while (i + 1 < num_present_cores &&
				   CPU_ISSET_S(present_cores[i + 1], cpuset_size, cpuset) &&
				   present_cores[i + 1] == end + 1) {
				end = present_cores[++i];
			}
			if (start == end) {
				printf("%s%d", comma, start);
			} else {
				printf("%s%d-%d", comma, start, end);
			}
			comma = ",";
		}
		i++;
	}
	free(present_cores);
}

int __do_smt(bool numeric, int cpus_in_system, int threads_per_cpu, bool print_smt_state)
{
	cpu_set_t **cpu_states = NULL;
	int thread, smt_state = -1;
	int cpu_state_size;
	int rc = 0;
	int i, core_id, threads_online;
	int *present_cores = NULL;
	int num_present_cores;

	if (get_present_core_list(&present_cores, &num_present_cores, threads_per_cpu) != 0) {
		fprintf(stderr, "Failed to get present core list\n");
		return -ENOMEM;
	}
	cpu_state_size = CPU_ALLOC_SIZE(num_present_cores);
	cpu_states = (cpu_set_t **)calloc(threads_per_cpu, sizeof(cpu_set_t *));
	if (!cpu_states) {
		rc = -ENOMEM;
		goto cleanup_present_cores;
	}

	for (thread = 0; thread < threads_per_cpu; thread++) {
		cpu_states[thread] = CPU_ALLOC(num_present_cores);
		if (!cpu_states[thread]) {
			rc = -ENOMEM;
			goto cleanup_cpu_states;
		}
		CPU_ZERO_S(cpu_state_size, cpu_states[thread]);
	}

	for (i = 0; i < num_present_cores; i++) {
		core_id = present_cores[i];
		threads_online = __get_one_smt_state(core_id, threads_per_cpu);
		if (threads_online < 0) {
			rc = threads_online;
			goto cleanup_cpu_states;
		}
		if (threads_online) {
			CPU_SET_S(core_id, cpu_state_size, cpu_states[threads_online - 1]);
		}
	}

	for (thread = 0; thread < threads_per_cpu; thread++) {
		if (CPU_COUNT_S(cpu_state_size, cpu_states[thread])) {
			if (smt_state == -1)
				smt_state = thread + 1;
			else if (smt_state > 0)
				smt_state = 0; /* mix of SMT modes */
		}
	}

	if (!print_smt_state) {
		rc = smt_state;
		goto cleanup_cpu_states;
	}

	if (smt_state == 1) {
		if (numeric)
			printf("SMT=1\n");
		else
			printf("SMT is off\n");
	} else if (smt_state == 0) {
		for (thread = 0; thread < threads_per_cpu; thread++) {
			if (CPU_COUNT_S(cpu_state_size, cpu_states[thread])) {
				printf("SMT=%d: ", thread + 1);
				print_cpu_list(cpu_states[thread], cpu_state_size, threads_per_cpu);
				printf("\n");
			}
		}
	} else {
		printf("SMT=%d\n", smt_state);
	}

cleanup_cpu_states:
	for (thread = 0; thread < threads_per_cpu; thread++)
		CPU_FREE(cpu_states[thread]);
    free(cpu_states);
cleanup_present_cores:
	free(present_cores);

	return rc;
}
