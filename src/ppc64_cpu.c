/**
 * Copyright (C) 2007 Anton Blanchard <anton@au.ibm.com> IBM Corporation
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <librtas.h>
#include <sys/stat.h>
#include <getopt.h>
#include <sched.h>
#include <assert.h>
#include <pthread.h>
#include <sys/ioctl.h>
#ifdef HAVE_LINUX_PERF_EVENT_H
#include <linux/perf_event.h>
#endif
#include "librtas_error.h"
#include <errno.h>

#define SYSFS_CPUDIR	"/sys/devices/system/cpu/cpu%d"
#define INTSERV_PATH	"/proc/device-tree/cpus/%s/ibm,ppc-interrupt-server#s"

#define SYSFS_PATH_MAX		128
#define MAX_NR_CPUS		1024
#define DIAGNOSTICS_RUN_MODE	42
#define CPU_OFFLINE		-1

static unsigned long long cpu_freq[MAX_NR_CPUS];
static int counters[MAX_NR_CPUS];

#ifndef __NR_perf_event_open
#define __NR_perf_event_open	319
#endif

int threads_per_cpu = 0;
int cpus_in_system = 0;
int threads_in_system = 0;

int get_attribute(char *path, const char *fmt, int *value)
{
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;

	fscanf(fp, fmt, value);
	fclose(fp);

	return 0;
}

int set_attribute(const char *path, const char *fmt, int value)
{
	FILE *fp;

	fp = fopen(path, "w");
	if (fp == NULL)
		return -1;

	fprintf(fp, fmt, value);
	fclose(fp);

	return 0;
}

int cpu_online(int thread)
{
	char path[SYSFS_PATH_MAX];
	int rc, online;

	sprintf(path, SYSFS_CPUDIR"/online", thread);
	rc = get_attribute(path, "%d", &online);
	if (rc || !online)
		return 0;

	return 1;
}

int get_system_attribute(char *attribute, const char *fmt, int *value)
{
	char path[SYSFS_PATH_MAX];
	int i, rc;
	int system_attribute = -1;

	for (i = 0; i < threads_in_system; i++) {
		int cpu_attribute;

		/* only check online cpus */
		if (!cpu_online(i))
			continue;

		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = get_attribute(path, fmt, &cpu_attribute);
		if (rc)
			continue;

		if (system_attribute == -1)
			system_attribute = cpu_attribute;
		else if (system_attribute != cpu_attribute)
			return -1;
	}

	*value = system_attribute;
	return 0;
}

int set_system_attribute(char *attribute, const char *fmt, int state)
{
	char path[SYSFS_PATH_MAX];
	int i, rc;

	for (i = 0; i < threads_in_system; i++) {
		/* only set online cpus */
		if (!cpu_online(i))
			continue;

		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = set_attribute(path, fmt, state);
		if (rc)
			return -1;
	}

	return 0;
}

int set_dscr(int state)
{
	return set_system_attribute("dscr", "%x", state);
}

int get_dscr(int *value)
{
	return get_system_attribute("dscr", "%x", value);
}

int set_smt_snooze_delay(int delay)
{
	return set_system_attribute("smt_snooze_delay", "%d", delay);
}

int get_smt_snooze_delay(int *delay)
{
	return get_system_attribute("smt_snooze_delay", "%d", delay);
}

int online_thread(const char *path)
{
	return set_attribute(path, "%d", 1);
}

int offline_thread(const char *path)
{
	return set_attribute(path, "%d", 0);
}


int get_cpu_info(void)
{
	DIR *d;
	struct dirent *de;
	int first_cpu = 1;
	int rc;

	d = opendir("/proc/device-tree/cpus");
	if (!d)
		return -1;

	while ((de = readdir(d)) != NULL) {
		if (!strncmp(de->d_name, "PowerPC", 7)) {
			if (first_cpu) {
				struct stat sbuf;
				char path[128];

				sprintf(path, INTSERV_PATH, de->d_name);
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
	return 0;
}

int is_smt_capable(void)
{
	struct stat sb;
	char path[SYSFS_PATH_MAX];
	int i;

	for (i = 0; i < threads_in_system; i++) {
		sprintf(path, SYSFS_CPUDIR"/smt_snooze_delay", i);
		if (stat(path, &sb))
			continue;
		return 1;
	}

	return 0;
}

int get_one_smt_state(int primary_thread)
{
	int thread_state;
	int smt_state = 0;
	int i;

	for (i = 0; i < threads_per_cpu; i++) {
		thread_state = cpu_online(primary_thread + i);
		smt_state += thread_state;
	}

	return smt_state ? smt_state : -1;
}

int get_smt_state(void)
{
	int system_state = -1;
	int i;

	for (i = 0; i < threads_in_system; i += threads_per_cpu) {
		int cpu_state;

		cpu_state = get_one_smt_state(i);
		if (cpu_state == -1)
			continue;

		if (system_state == -1)
			system_state = cpu_state;
		else if (system_state != cpu_state)
			return -1;
	}

	return system_state;
}

int set_one_smt_state(int thread, int online_threads)
{
	char path[SYSFS_PATH_MAX];
	int i, rc;

	for (i = 0; i < online_threads; i++) {
		snprintf(path, SYSFS_PATH_MAX, SYSFS_CPUDIR"/%s", thread + i,
			 "online");
		rc = online_thread(path);
		if (rc)
			return rc;
	}

	for (; i < threads_per_cpu; i++) {
		snprintf(path, SYSFS_PATH_MAX, SYSFS_CPUDIR"/%s", thread + i,
			 "online");
		rc = offline_thread(path);
		if (rc)
			break;
	}

	return rc;
}

int set_smt_state(int smt_state)
{
	int i, rc;
	int ssd, update_ssd = 1;

	rc = get_smt_snooze_delay(&ssd);
	if (rc)
		update_ssd = 0;

	for (i = 0; i < threads_in_system; i += threads_per_cpu) {
		if (cpu_online(i)) {
			rc = set_one_smt_state(i, smt_state);
			if (rc)
				break;
		}
	}

	if (update_ssd)
		set_smt_snooze_delay(ssd);

	return rc;
}

int is_dscr_capable(void)
{
	struct stat sb;
	char path[SYSFS_PATH_MAX];
	int i;

	for (i = 0; i < threads_in_system; i++) {
		sprintf(path, SYSFS_CPUDIR"/dscr", i);
		if (stat(path, &sb))
			continue;
		return 1;
	}

	return 0;
}

int do_smt(char *state)
{
	int rc = 0;
	int smt_state;

	if (!is_smt_capable()) {
		fprintf(stderr, "Machine is not SMT capable\n");
		return -1;
	}

	if (!state) {
		smt_state = get_smt_state();

		if (smt_state == 1)
			printf("SMT is off\n");
		else if (smt_state == threads_per_cpu)
			printf("SMT is on\n");
		else if (smt_state == -1)
			printf("Inconsistent state: mix of ST and SMT cores\n");
		else
			printf("SMT=%d\n", smt_state);
	} else {
		if (!strcmp(state, "on"))
			smt_state = threads_per_cpu;
		else if (!strcmp(state, "off"))
			smt_state = 1;
		else
			smt_state = strtol(state, NULL, 0);

		if ((smt_state <= 0) || (smt_state > threads_per_cpu)) {
			printf("SMT=%s is not valid\n", state);
			return -1;
		}

		rc = set_smt_state(smt_state);
	}

	return rc;
}

int do_dscr(char *state)
{
	int rc = 0;

	if (!is_dscr_capable()) {
		fprintf(stderr, "Machine is not DSCR capable\n");
		return -1;
	}

	if (!state) {
		int dscr;
		rc = get_dscr(&dscr);
		if (rc) {
			printf("Could not retrieve DSCR\n");
		} else {
			if (dscr == -1)
				printf("Inconsistent DSCR\n");
			else
				printf("dscr is %d\n", dscr);
		}
	} else
		rc = set_dscr(strtol(state, NULL, 0));

	return rc;
}

int do_smt_snooze_delay(char *state)
{
	int rc = 0;

	if (!is_smt_capable()) {
		fprintf(stderr, "Machine is not SMT capable\n");
		return -1;
	}

	if (!state) {
		int ssd;
		rc = get_smt_snooze_delay(&ssd);
		if (rc) {
			printf("Could not retrieve smt_snooze_delay\n");
		} else {
			if (ssd == -1)
				printf("Inconsistent smt_snooze_delay\n");
			else
				printf("smt_snooze_delay is %d\n", ssd);
		}
	} else {
		int delay;

		if (!strcmp(state, "off"))
			delay = -1;
		else
			delay = strtol(state, NULL, 0);

		rc = set_smt_snooze_delay(delay);
	}

	return rc;
}

int do_run_mode(char *run_mode)
{
	char mode[3];
	int rc;

	if (!run_mode) {
		rc = rtas_get_sysparm(DIAGNOSTICS_RUN_MODE, 3, mode);
		if (rc) {
			if (rc == -3) {
				printf("Machine does not support diagnostic "
				       "run mode\n");
			} else if (is_librtas_error(rc)) {
				char buf[1024];

				librtas_error(rc, &buf[0], 1024);
				printf("Could not retrieve current diagnostics "
				       "mode,\n%s\n", buf);
			} else {
				printf("Could not retrieve current diagnostics "
				       "mode\n");
			}
		} else
			printf("run-mode=%d\n", mode[2]);
	} else {
		short rmode = atoi(run_mode);

		if (rmode < 0 || rmode > 3) {
			printf("Invalid run-mode=%d\n", rmode);
			return -1;
		}

		*(short *)mode = 1;
		mode[2] = rmode;

		rc = rtas_set_sysparm(DIAGNOSTICS_RUN_MODE, mode);
		if (rc) {
			if (rc == -3) {
				printf("Machine does not support diagnostic "
				       "run mode\n");
			} else if (rc == -9002) {
				printf("Machine is not authorized to set "
				       "diagnostic run mode\n");
			} else if (is_librtas_error(rc)) {
				char buf[1024];

				librtas_error(rc, &buf[0], 1024);
				printf("Could not set diagnostics mode,\n%s\n", buf);
			} else {
				printf("Could not set diagnostics mode\n");
			}
		}
	}

	return rc;
}

#ifdef HAVE_LINUX_PERF_EVENT_H

static int setup_counters(void)
{
	int i;
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_HARDWARE;
	attr.config = PERF_COUNT_HW_CPU_CYCLES;
	attr.disabled = 1;
	attr.size = sizeof(attr);

	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freq[i] == CPU_OFFLINE)
			continue;

		counters[i] = syscall(__NR_perf_event_open, &attr, -1,
				      i, -1, 0);

		if (counters[i] < 0) {
			if (errno == ENOSYS)
				fprintf(stderr, "frequency determination "
					"not supported with this kernel.\n");
			else
				perror("Could not initialize performance "
				       "counters");
			return -1;
		}
	}

	return 0;
}

static void start_counters(void)
{
	int i;

	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freq[i] == CPU_OFFLINE)
			continue;

		ioctl(counters[i], PERF_EVENT_IOC_ENABLE);
	}
}

static void stop_counters(void)
{
	int i;

	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freq[i] == CPU_OFFLINE)
			continue;

		ioctl(counters[i], PERF_EVENT_IOC_DISABLE);
	}
}

static void read_counters(void)
{
	int i;

	for (i = 0; i < threads_in_system; i++) {
		size_t res;

		if (cpu_freq[i] == CPU_OFFLINE)
			continue;

		res = read(counters[i], &cpu_freq[i],
			   sizeof(unsigned long long));
		assert(res == sizeof(unsigned long long));

		close(counters[i]);
	}
}

static void *soak(void *arg)
{
	unsigned int cpu = (long)arg;
	cpu_set_t cpumask;

	CPU_ZERO(&cpumask);
	CPU_SET(cpu, &cpumask);

	if (sched_setaffinity(0, sizeof(cpumask), &cpumask)) {
		perror("sched_setaffinity");
		exit(1);
	}

	while (1)
		; /* Do Nothing */
}

int do_cpu_frequency(void)
{
	int i, rc;
	unsigned long long min = -1ULL;
	unsigned long min_cpu = -1UL;
	unsigned long long max = 0;
	unsigned long max_cpu = -1UL;
	unsigned long long sum = 0;
	unsigned long count = 0;

	memset(cpu_freq, 0, sizeof(cpu_freq));
	memset(counters, 0, sizeof(counters));

	rc = setup_counters();
	if (rc)
		return rc;

	/* Start a soak thread on each CPU */
	for (i = 0; i < threads_in_system; i++) {
		pthread_t tid;

		if (!cpu_online(i)) {
			cpu_freq[i] = CPU_OFFLINE;
			continue;
		}

		if (pthread_create(&tid, NULL, soak, (void *)(long)i)) {
			perror("pthread_create");
			return -1;
		}
	}

	/* Wait for soak threads to start */
	usleep(1000000);

	start_counters();
	/* Count for 1 second */
	usleep(1000000);
	stop_counters();

	read_counters();

	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freq[i] == CPU_OFFLINE)
			continue;

		/* No result - Couldn't schedule on that cpu */
		if (cpu_freq[i] == 0) {
			printf("WARNING: couldn't run on cpu %d\n", i);
			continue;
		}

		if (cpu_freq[i] < min) {
			min = cpu_freq[i];
			min_cpu = i;
		}
		if (cpu_freq[i] > max) {
			max = cpu_freq[i];
			max_cpu = i;
		}
		sum += cpu_freq[i];
		count++;
	}

	printf("min:\t%.2f GHz (cpu %ld)\n", 1.0 * min / 1000000000ULL,
	       min_cpu);
	printf("max:\t%.2f GHz (cpu %ld)\n", 1.0 * max / 1000000000ULL,
	       max_cpu);
	printf("avg:\t%.2f GHz\n\n", 1.0 * (sum / count) / 1000000000ULL);
	return 0;
}

#else

int do_cpu_frequency(void)
{
	printf("CPU Frequency determination is not supported on this "
	       "platfom.\n");
	return EINVAL;
}

#endif

int do_cores_present(char * state)
{
	printf("Number of cores present = %d\n", cpus_in_system);
	return 0;
}

int set_all_threads_off(int cpu, int smt_state)
{
	int i;
	char path[SYSFS_PATH_MAX];
	int rc = 0;

	for (i = cpu + smt_state - 1; i >= cpu; i--) {
		snprintf(path, SYSFS_PATH_MAX, SYSFS_CPUDIR"/%s", i, "online");
		rc = offline_thread(path);
		if (rc == -1)
			printf("Unable to take cpu%d offline", i);
	}

	return rc;
}

int set_one_core(int smt_state, int core, int state)
{
	int rc = 0;
	int cpu = core * threads_per_cpu;

	if (state) {
		rc = set_one_smt_state(cpu, smt_state);
		if (rc == -1)
			printf("Unable to bring core %d online\n", core);
	} else {
		rc = set_all_threads_off(cpu, smt_state);
		if (rc == -1)
			printf("Unable to take core %d offline\n", core);
	}

	return rc;
}

int do_cores_online(char *state)
{
	int smt_state;
	int *core_state;
	int cores_now_online = 0;
	int i;
	int number_to_have, number_to_change = 0, number_changed = 0;
	int new_state;

	smt_state = get_smt_state();
	if (smt_state == -1) {
		printf("Bad or inconsistent SMT state\n");
		return -1;
	}

	core_state = malloc(sizeof(int) * cpus_in_system);
	memset(core_state, 0, sizeof(int) * cpus_in_system);
	for (i = 0; i < cpus_in_system ; i++) {
		core_state[i] = cpu_online(i * threads_per_cpu);
		if (core_state[i])
			cores_now_online++;
	}

	if (!state) {
		printf("Number of cores online = %d\n", cores_now_online);
		return 0;
	}

	number_to_have = strtol(state, NULL, 0);
	if (number_to_have == cores_now_online)
		return 0;

	if (number_to_have > cpus_in_system)
		number_to_have = cpus_in_system;

	if (number_to_have > cores_now_online) {
		number_to_change = number_to_have - cores_now_online;
		new_state = 1;
	} else {
		number_to_change = cores_now_online - number_to_have;
		new_state = 0;
	}

	if (new_state) {
		for (i = 0; i < cpus_in_system; i++) {
			if (!core_state[i]) {
				set_one_core(smt_state, i, new_state);
				number_changed++;
				if (number_changed >= number_to_change)
					break;
			}
		}
	} else {
		for (i = cpus_in_system - 1; i > 0; i--) {
			if (core_state[i]) {
				set_one_core(smt_state, i, new_state);
				number_changed++;
				if (number_changed >= number_to_change)
					break;
			}
		}
	}

	return 0;
}

void usage(void)
{
	printf("\tppc64_cpu --smt               # Get current SMT state\n"
	       "\tppc64_cpu --smt={on|off}      # Turn SMT on/off\n"
	       "\tppc64_cpu --smt=X             # Set SMT state to X\n\n"
	       "\tppc64_cpu --cores-present     # Get the number of cores installed\n"
	       "\tppc64_cpu --cores-on          # Get the number of cores currently online\n"
	       "\tppc64_cpu --cores-on=X        # Put exactly X cores online\n\n"

	       "\tppc64_cpu --dscr              # Get current DSCR setting\n"
	       "\tppc64_cpu --dscr=<val>        # Change DSCR setting\n\n"
	       "\tppc64_cpu --smt-snooze-delay  # Get current smt-snooze-delay setting\n"
	       "\tppc64_cpu --smt-snooze-delay=<val> # Change smt-snooze-delay setting\n\n"
	       "\tppc64_cpu --run-mode          # Get current diagnostics run mode\n"
	       "\tppc64_cpu --run-mode=<val>    # Set current diagnostics run mode\n\n"
	       "\tppc64_cpu --frequency         # Determine cpu frequency.\n\n");
}

struct option longopts[] = {
	{"smt",			optional_argument, NULL, 's'},
	{"dscr",		optional_argument, NULL, 'd'},
	{"smt-snooze-delay",	optional_argument, NULL, 'S'},
	{"run-mode",		optional_argument, NULL, 'r'},
	{"frequency",		no_argument,	   NULL, 'f'},
	{"cores-present",	no_argument,	   NULL, 'C'},
	{"cores-on",		optional_argument, NULL, 'c'},
	{0,0,0,0}
};

int main(int argc, char *argv[])
{
	int rc = 0;
	int opt;
	int option_index;

	if (argc == 1) {
		usage();
		return 0;
	}

	rc = get_cpu_info();
	if (rc) {
		printf("Could not determine system cpu/thread information.\n");
		return rc;
	}

	while (1) {
		opt = getopt_long(argc, argv, "s::d::S::r::fCc::", longopts,
				  &option_index);
		if (opt == -1)
			break;

		switch (opt) {
		    case 's':
			rc = do_smt(optarg);
			break;

		    case 'd':
			rc = do_dscr(optarg);
			break;

		    case 'S':
			rc = do_smt_snooze_delay(optarg);
			break;

		    case 'r':
			rc = do_run_mode(optarg);
			break;

		    case 'f':
			rc = do_cpu_frequency();
			break;

		    case 'C':
			rc = do_cores_present(optarg);
			break;
		    case 'c':
			rc = do_cores_online(optarg);
			break;
		    default:
			usage();
			break;
		}
	}

	return rc;
}
