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
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/resource.h>
#ifdef HAVE_LINUX_PERF_EVENT_H
#include <linux/perf_event.h>
#endif
#include "librtas_error.h"
#include <errno.h>

#define PPC64_CPU_VERSION	"1.1"

#define SYSFS_CPUDIR	"/sys/devices/system/cpu/cpu%d"
#define DSCR_DEFAULT_PATH "/sys/devices/system/cpu/dscr_default"
#define INTSERV_PATH	"/proc/device-tree/cpus/%s/ibm,ppc-interrupt-server#s"

#define SYSFS_PATH_MAX		128
#define MAX_NR_CPUS		1024
#define DIAGNOSTICS_RUN_MODE	42
#define CPU_OFFLINE		-1

#ifdef HAVE_LINUX_PERF_EVENT_H
static unsigned long long cpu_freq[MAX_NR_CPUS];
static int counters[MAX_NR_CPUS];

#ifndef __NR_perf_event_open
#define __NR_perf_event_open	319
#endif

#endif

int threads_per_cpu = 0;
int cpus_in_system = 0;
int threads_in_system = 0;

int test_attr(char *path, char *perms)
{
	FILE *fp;

	fp = fopen(path, perms);
	if (fp) {
		fclose(fp);
		return 1;
	}

	if (errno == ENOENT)
		/* cpu probably offline */
		return 1;

	return 0;
}

int attr_is_readable(char *path)
{
	return test_attr(path, "r");
}

int attr_is_writeable(char *path)
{
	return test_attr(path, "w");
}

int test_sysattr(char *attribute, char *perms)
{
	char path[SYSFS_PATH_MAX];
	int i;

	for (i = 0; i < threads_in_system; i++) {
		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		if (!test_attr(path, perms))
			return 0;
	}

	return 1;
}

int sysattr_is_readable(char *attribute)
{
	return test_sysattr(attribute, "r");
}

int sysattr_is_writeable(char *attribute)
{
	return test_sysattr(attribute, "w");
}

int get_attribute(char *path, const char *fmt, int *value)
{
	FILE *fp;
	int rc;

	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT)
			/* No attribute, cpu probably offline */
			return 0;
		else
			return -1;
	}

	rc = fscanf(fp, fmt, value);
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

		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = get_attribute(path, fmt, &cpu_attribute);
		if (rc)
			return rc;

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
		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = set_attribute(path, fmt, state);
		if (rc)
			return rc;
	}

	return 0;
}

int dscr_default_exists(void)
{
	struct stat sb;

	if (!stat(DSCR_DEFAULT_PATH, &sb))
		return 1;

	return 0;
}

/* On newer systems we just set the default_dscr value instead of the cpu
 * specific dscr value.  This is because the dscr value is now thread
 * specific.
 */
int set_dscr(int state)
{
	int rc;

	if (dscr_default_exists()) {
		if (!attr_is_writeable(DSCR_DEFAULT_PATH)) {
			perror("Cannot set default dscr value");
			return -2;
		}

		rc = set_attribute(DSCR_DEFAULT_PATH, "%x", state);
	} else {
		if (!sysattr_is_writeable("dscr")) {
			perror("Cannot set dscr");
			return -2;
		}

		rc = set_system_attribute("dscr", "%x", state);
	}

	return rc;
}

int get_dscr(int *value)
{
	int rc;

	if (dscr_default_exists()) {
		if (!attr_is_readable(DSCR_DEFAULT_PATH)) {
			perror("Cannot retrieve default dscr");
			return -2;
		}

		rc = get_attribute(DSCR_DEFAULT_PATH, "%x", value);
	} else {
		if (!sysattr_is_readable("dscr")) {
			perror("Cannot retrieve dscr");
			return -2;
		}

		rc = get_system_attribute("dscr", "%x", value);
	}

	return rc;
}

int set_smt_snooze_delay(int delay)
{
	if (!sysattr_is_writeable("smt_snooze_delay")) {
		perror("Cannot set smt snooze delay");
		return -2;
	}

	return set_system_attribute("smt_snooze_delay", "%d", delay);
}

int get_smt_snooze_delay(int *delay)
{
	if (!sysattr_is_readable("smt_snooze_delay")) {
		perror("Cannot retrieve smt snooze delay");
		return -2;
	}

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

	if (!sysattr_is_readable("online")) {
		perror("Cannot retrieve smt state");
		return -2;
	}

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

	if (!sysattr_is_writeable("online")) {
		perror("Cannot set smt state");
		return -1;
	}

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

	if (dscr_default_exists())
		return 1;

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

		if (smt_state == -2)
			return -1;

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

#define PTRACE_DSCR 44

int do_dscr_pid(int dscr_state, pid_t pid)
{
	int rc;

	rc = ptrace(PTRACE_ATTACH, pid, NULL, NULL);
	if (rc) {
		fprintf(stderr, "Could not attach to process %d to %s the "
			"DSCR value\n%s\n", pid, (dscr_state ? "set" : "get"),
			strerror(errno));
		return rc;
	}

	wait(NULL);

	if (dscr_state) {
		rc = ptrace(PTRACE_POKEUSER, pid, PTRACE_DSCR << 3, dscr_state);
		if (rc) {
			fprintf(stderr, "Could not set the DSCR value for pid "
				"%d\n%s\n", pid, strerror(errno));
			ptrace(PTRACE_DETACH, pid, NULL, NULL);
			return rc;
		}
	}

	rc = ptrace(PTRACE_PEEKUSER, pid, PTRACE_DSCR << 3, NULL);
	if (errno) {
		fprintf(stderr, "Could not get the DSCR value for pid "
			"%d\n%s\n", pid, strerror(errno));
		rc = -1;
	} else {
		printf("DSCR for pid %d is %d\n", pid, rc);
	}

	ptrace(PTRACE_DETACH, pid, NULL, NULL);
	return rc;
}

int do_dscr(char *state, pid_t pid)
{
	int rc = 0;
	int dscr_state = 0;

	if (!is_dscr_capable()) {
		fprintf(stderr, "Machine is not DSCR capable\n");
		return -1;
	}

	if (state)
		dscr_state = strtol(state, NULL, 0);

	if (pid != -1)
		return do_dscr_pid(dscr_state, pid);

	if (!state) {
		int dscr;

		rc = get_dscr(&dscr);
		switch (rc) {
		    case -1:
			printf("Could not retrieve DSCR\n");
			break;
		    case 0:
			if (dscr == -1)
				printf("Inconsistent DSCR\n");
			else
				printf("DSCR is %d\n", dscr);
			break;
		}
	} else
		rc = set_dscr(dscr_state);

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

	if (getuid() != 0) {
		fprintf(stderr, "Cannot %s run mode: Permission denied\n",
			run_mode ? "set" : "get");
		return -1;
	}

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
		if (!cpu_online(i))
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

char *power_mode(uint64_t mode)
{
	switch (mode) {
	case 0x0001:
		return "Dynamic, Favor Performance\n";
	case 0x0002:
		return "None\n";
	case 0x0003:
		return "Static\n";
	case 0x00ff:
		return "Dynamic, Favor Power\n";
	default:
		return "Unknown";
	}
}

void report_system_power_mode(void)
{
	FILE *f;
	char line[128];

	f = fopen("/proc/ppc64/lparcfg", "r");
	if (!f)
		return;

	while (fgets(line, 128, f) != NULL) {
		char *name, *value;
		uint64_t mode, system_mode, partition_mode;

		if ((line[0] == '\n') || (!strncmp(&line[0], "lparcfg", 7)))
			continue;

		name = &line[0];
		value = strchr(line, '=');
		*value = '\0';
		value++;

		if (strcmp(name, "power_mode_data"))
			continue;

		/* The power mode result is defined as
		 * XXXX XXXX XXXX XXXX
		 * XXXX			: System Power Mode
		 *                XXXX	: Partition Power Mode
		 * They mode is the first 4 bytes of the value reported in
		 * the lparcfg file.
		 */
		mode = strtoul(value, NULL, 16);
		system_mode = (mode >> 48) & 0xffff;
		partition_mode = mode & 0xffff;

		if (system_mode != partition_mode) {
			printf("System Power Savings Mode: %s",
			       power_mode(system_mode));
			printf("Partition Power Savings Mode: %s",
			       power_mode(partition_mode));
		} else {
			printf("Power Savings Mode: %s",
			       power_mode(system_mode));
		}
	}

	fclose(f);
	return;
}

/* We need an FD per CPU, with a few more for stdin/out/err etc */
void setrlimit_open_files(void)
{
	struct rlimit old_rlim, new_rlim;
	int new = threads_in_system + 8;

	getrlimit(RLIMIT_NOFILE, &old_rlim);

	if (old_rlim.rlim_cur > new)
		return;

	new_rlim.rlim_cur = new;
	new_rlim.rlim_max = old_rlim.rlim_max;

	setrlimit(RLIMIT_NOFILE, &new_rlim);
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

	setrlimit_open_files();

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

	report_system_power_mode();
	printf("min:\t%.3f GHz (cpu %ld)\n", 1.0 * min / 1000000000ULL,
	       min_cpu);
	printf("max:\t%.3f GHz (cpu %ld)\n", 1.0 * max / 1000000000ULL,
	       max_cpu);
	printf("avg:\t%.3f GHz\n\n", 1.0 * (sum / count) / 1000000000ULL);
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

	if (state) {
		if (!sysattr_is_writeable("online")) {
			perror("Cannot set cores online");
			return -1;
		}
	} else {
		if (!sysattr_is_readable("online")) {
			perror("Cannot get online cores");
			return -1;
		}
	}

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

	if (number_to_have > cpus_in_system) {
		printf("Cannot online more cores than are present.\n");
		do_cores_present(NULL);
		return -1;
	}

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
	printf(
"Usage: ppc64_cpu [command] [options]\n"
"ppc64_cpu --smt                     # Get current SMT state\n"
"ppc64_cpu --smt={on|off}            # Turn SMT on/off\n"
"ppc64_cpu --smt=X                   # Set SMT state to X\n\n"
"ppc64_cpu --cores-present           # Get the number of cores present\n"
"ppc64_cpu --cores-on                # Get the number of cores currently online\n"
"ppc64_cpu --cores-on=X              # Put exactly X cores online\n\n"
"ppc64_cpu --dscr                    # Get current DSCR system setting\n"
"ppc64_cpu --dscr=<val>              # Change DSCR system setting\n"
"ppc64_cpu --dscr [-p <pid>]         # Get DSCR setting for process <pid>\n"
"ppc64_cpu --dscr=<val> [-p <pid>]   # Change DSCR setting for process <pid>\n\n"
"ppc64_cpu --smt-snooze-delay        # Get current smt-snooze-delay setting\n"
"ppc64_cpu --smt-snooze-delay=<val>  # Change smt-snooze-delay setting\n\n"
"ppc64_cpu --run-mode                # Get current diagnostics run mode\n"
"ppc64_cpu --run-mode=<val>          # Set current diagnostics run mode\n\n"
"ppc64_cpu --frequency               # Determine cpu frequency.\n\n");
}

struct option longopts[] = {
	{"smt",			optional_argument, NULL, 's'},
	{"dscr",		optional_argument, NULL, 'd'},
	{"smt-snooze-delay",	optional_argument, NULL, 'S'},
	{"run-mode",		optional_argument, NULL, 'r'},
	{"frequency",		no_argument,	   NULL, 'f'},
	{"cores-present",	no_argument,	   NULL, 'C'},
	{"cores-on",		optional_argument, NULL, 'c'},
	{"version",		no_argument,	   NULL, 'V'},
	{0,0,0,0}
};

int main(int argc, char *argv[])
{
	int rc = 0;
	char *action;
	char *action_arg = NULL;
	char *equal_char;
	int opt;
	pid_t pid = -1;

	if (argc == 1) {
		usage();
		return 0;
	}

	rc = get_cpu_info();
	if (rc) {
		printf("Could not determine system cpu/thread information.\n");
		return rc;
	}

	/* The first arg is the action to be taken with an optional action
	 * arg in the form --action=XXX. Parse this out so we can call the
	 * appropriate action.
	 */
	action = argv[1];

	/* skipp past the '--' */
	action += 2;

	equal_char = strchr(action, '=');
	if (equal_char) {
		*equal_char = '\0';
		action_arg = equal_char + 1;
	}

	/* Now parse out any additional options. Currently there is only
	 * the -p <pid> option for the --dscr action.
	 */
	optind = 2;
	while (1) {
		opt = getopt(argc, argv, "p:");
		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
			/* only valid for do_dscr option */
			if (strcmp(action, "dscr")) {
				fprintf(stderr, "The p option is only valid "
					"with the --dscr option\n");
				usage();
				exit(-1);
			}

			pid = atoi(optarg);
			break;
		default:
			fprintf(stderr, "%c is not a valid option\n", opt);
			usage();
			exit(-1);
		}
	}

	if (!strcmp(action, "smt"))
		rc = do_smt(action_arg);
	else if (!strcmp(action, "dscr"))
		rc = do_dscr(action_arg, pid);
	else if (!strcmp(action, "smt-snooze-delay"))
		rc = do_smt_snooze_delay(action_arg);
	else if (!strcmp(action, "run-mode"))
		rc = do_run_mode(action_arg);
	else if (!strcmp(action, "frequency"))
		rc = do_cpu_frequency();
	else if (!strcmp(action, "cores-present"))
		rc = do_cores_present(action_arg);
	else if (!strcmp(action, "cores-on"))
		rc = do_cores_online(action_arg);
	else if (!strcmp(action, "version"))
		printf("ppc64_cpu: version %s\n", PPC64_CPU_VERSION);
	else
		usage();

	return rc;
}
