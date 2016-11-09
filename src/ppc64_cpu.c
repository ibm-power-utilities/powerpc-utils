/**
 * Copyright (C) 2007 Anton Blanchard <anton@au.ibm.com> IBM Corporation
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <sched.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/resource.h>

#ifdef WITH_LIBRTAS
#include <librtas.h>
#include "librtas_error.h"
#endif

#ifdef HAVE_LINUX_PERF_EVENT_H
#include <linux/perf_event.h>
#endif

#include <errno.h>

#define PPC64_CPU_VERSION	"1.2"

#define SYSFS_CPUDIR	"/sys/devices/system/cpu/cpu%d"
#define SYSFS_SUBCORES	"/sys/devices/system/cpu/subcores_per_core"
#define DSCR_DEFAULT_PATH "/sys/devices/system/cpu/dscr_default"
#define INTSERV_PATH	"/proc/device-tree/cpus/%s/ibm,ppc-interrupt-server#s"

#define SYSFS_PATH_MAX		128
#define MAX_NR_CPUS		1024
#define DIAGNOSTICS_RUN_MODE	42
#define CPU_OFFLINE		-1

#ifdef HAVE_LINUX_PERF_EVENT_H
struct cpu_freq {
	int offline;
	int counter;
	pthread_t tid;
	unsigned long long freq;
};

#ifndef __NR_perf_event_open
#define __NR_perf_event_open	319
#endif

#endif

static int threads_per_cpu = 0;
static int cpus_in_system = 0;
static int threads_in_system = 0;

static int do_info(void);

static int test_sysattr(char *attribute, int perms)
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

static int sysattr_is_readable(char *attribute)
{
	return test_sysattr(attribute, R_OK);
}

static int sysattr_is_writeable(char *attribute)
{
	return test_sysattr(attribute, W_OK);
}

static int get_attribute(char *path, const char *fmt, int *value)
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

static int set_attribute(const char *path, const char *fmt, int value)
{
	int fd, rc, len;
	char *str;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;

	len = asprintf(&str, fmt, value);
	if (len < 0) {
		rc = -1;
		goto close;
	}

	rc = write(fd, str, len);
	free(str);

	if (rc == len)
		rc = 0;

close:
	close(fd);
	return rc;
}

static int cpu_online(int thread)
{
	char path[SYSFS_PATH_MAX];
	int rc, online;

	sprintf(path, SYSFS_CPUDIR"/online", thread);
	rc = get_attribute(path, "%d", &online);
	if (rc || !online)
		return 0;

	return 1;
}

static int get_system_attribute(char *attribute, const char *fmt, int *value,
			 int *inconsistent)
{
	char path[SYSFS_PATH_MAX];
	int i, rc;
	int system_attribute = -1;

	for (i = 0; i < threads_in_system; i++) {
		int cpu_attribute;

		if (!cpu_online(i))
			continue;

		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = get_attribute(path, fmt, &cpu_attribute);
		if (rc)
			return rc;

		if (system_attribute == -1)
			system_attribute = cpu_attribute;
		else if (system_attribute != cpu_attribute) {
			*inconsistent = 1;
			return -1;
		}
	}

	*value = system_attribute;
	return 0;
}

static int set_system_attribute(char *attribute, const char *fmt, int state)
{
	char path[SYSFS_PATH_MAX];
	int i, rc;

	for (i = 0; i < threads_in_system; i++) {
		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = set_attribute(path, fmt, state);
		/* When a CPU is offline some sysfs files are removed from the CPU
		 * directory, for example smt_snooze_delay and dscr. The absence of the
		 * file is not an error, so detect and clear the error when
		 * set_attribute indicates ENOENT. */
		if (rc == -1 && errno == ENOENT)
			rc = errno = 0;
		if (rc)
			return rc;
	}

	return 0;
}

static int dscr_default_exists(void)
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
static int set_dscr(int state)
{
	int rc;

	if (dscr_default_exists()) {
		if (access(DSCR_DEFAULT_PATH, W_OK)) {
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

static int get_dscr(int *value, int *inconsistent)
{
	int rc;

	if (dscr_default_exists()) {
		if (access(DSCR_DEFAULT_PATH, R_OK)) {
			perror("Cannot retrieve default dscr");
			return -2;
		}

		rc = get_attribute(DSCR_DEFAULT_PATH, "%x", value);
	} else {
		if (!sysattr_is_readable("dscr")) {
			perror("Cannot retrieve dscr");
			return -2;
		}

		rc = get_system_attribute("dscr", "%x", value, inconsistent);
	}

	return rc;
}

static int set_smt_snooze_delay(int delay)
{
	if (!sysattr_is_writeable("smt_snooze_delay")) {
		perror("Cannot set smt snooze delay");
		return -2;
	}

	return set_system_attribute("smt_snooze_delay", "%d", delay);
}

static int get_smt_snooze_delay(int *delay, int *inconsistent)
{
	if (!sysattr_is_readable("smt_snooze_delay")) {
		perror("Cannot retrieve smt snooze delay");
		return -2;
	}

	return get_system_attribute("smt_snooze_delay", "%d", delay,
				    inconsistent);
}

static int online_thread(const char *path)
{
	return set_attribute(path, "%d", 1);
}

static int offline_thread(const char *path)
{
	return set_attribute(path, "%d", 0);
}

static int is_subcore_capable(void)
{
	return access(SYSFS_SUBCORES, F_OK) == 0;
}

static int num_subcores(void)
{
	int rc, subcores;
	rc = get_attribute(SYSFS_SUBCORES, "%d", &subcores);
	if (rc)
		return -1;
	return subcores;
}

static int get_cpu_info(void)
{
	DIR *d;
	struct dirent *de;
	int first_cpu = 1;
	int rc;
	int subcores;

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

	subcores = num_subcores();
	if (is_subcore_capable() && subcores > 0) {
		threads_per_cpu /= subcores;
		cpus_in_system *= subcores;
	}
	return 0;
}

static int is_smt_capable(void)
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

static int get_one_smt_state(int primary_thread)
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

static int get_smt_state(void)
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

static int set_one_smt_state(int thread, int online_threads)
{
	char path[SYSFS_PATH_MAX];
	int i, rc = 0;

	for (i = 0; i < threads_per_cpu; i++) {
		snprintf(path, SYSFS_PATH_MAX, SYSFS_CPUDIR"/%s", thread + i,
			 "online");
		if (i < online_threads)
			rc = online_thread(path);
		else
			rc = offline_thread(path);

		/* The 'online' sysfs file returns EINVAL if set to the current
		 * setting. As this is not an error, reset rc and errno to avoid
		 * returning failure. */
		if (rc == -1 && errno == EINVAL)
			rc = errno = 0;
		if (rc)
			break;
	}

	return rc;
}

static int set_smt_state(int smt_state)
{
	int i, j, rc;
	int ssd, update_ssd = 1;
	int inconsistent = 0;
	int error = 0;

	if (!sysattr_is_writeable("online")) {
		perror("Cannot set smt state");
		return -1;
	}

	rc = get_smt_snooze_delay(&ssd, &inconsistent);
	if (rc)
		update_ssd = 0;

	for (i = 0; i < threads_in_system; i += threads_per_cpu) {
		/* Online means any thread on this core running, so check all
		 * threads in the core, not just the first. */
		for (j = 0; j < threads_per_cpu; j++) {
			if (!cpu_online(i + j))
				continue;

			rc = set_one_smt_state(i, smt_state);
			/* Record an error, but do not check result: if we
			 * have failed to set this core, keep trying
			 * subsequent ones. */
			if (rc)
				error = 1;
			break;
		}
	}

	if (update_ssd)
		set_smt_snooze_delay(ssd);

	if (error) {
		fprintf(stderr, "One or more cpus could not be on/offlined\n");
		return -1;
	}
	return rc;
}

static int is_dscr_capable(void)
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

static int do_smt(char *state)
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

static inline void do_threads_per_core()
{
	printf("Threads per core: %d\n", threads_per_cpu);
}

static int do_subcores_per_core(char *state)
{
	int rc = 0;
	int subcore_state = 0;

	/* Check SMT machine. */
	if (!is_smt_capable()) {
		fprintf(stderr, "Machine is not SMT capable\n");
		return -1;
	}

	/* Check subcore capable machine/kernel. */
	if (!is_subcore_capable()) {
		fprintf(stderr, "Machine is not subcore capable\n");
		return -1;
	}

	if (!state) {
		/* Display current status. */
		subcore_state = num_subcores();
		if (subcore_state < 0) {
			fprintf(stderr, "Could not read subcore state.\n");
			return -1;
		}
		printf("Subcores per core: %d\n", subcore_state);
	} else {
                /* Kernel decides what values are valid, so no need to
                 * check here. */
		subcore_state = strtol(state, NULL, 0);
		rc = set_attribute(SYSFS_SUBCORES, "%d", subcore_state);
		if (rc) {
			fprintf(stderr, "Failed to set subcore option.\n");
			return rc;
		}

		printf("Subcores per core set to %d\n", subcore_state);
	}

	return rc;
}

#define PTRACE_DSCR 44

static int do_dscr_pid(int dscr_state, pid_t pid)
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

static int do_dscr(char *state, pid_t pid)
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
		int dscr, inconsistent = 0;

		rc = get_dscr(&dscr, &inconsistent);
		if (rc) {
			if (inconsistent)
				printf("Inconsistent DSCR\n");
			else
				printf("Could not retrieve DSCR\n");
		} else {
			printf("DSCR is %d\n", dscr);
		}
	} else
		rc = set_dscr(dscr_state);

	return rc;
}

static int do_smt_snooze_delay(char *state)
{
	int rc = 0;

	if (!is_smt_capable()) {
		fprintf(stderr, "Machine is not SMT capable\n");
		return -1;
	}

	if (!state) {
		int ssd, inconsistent = 0;
		rc = get_smt_snooze_delay(&ssd, &inconsistent);
		if (rc) {
			if (inconsistent)
				printf("Inconsistent smt_snooze_delay\n");
			else
				printf("Could not retrieve smt_snooze_delay\n");
		} else {
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

#ifdef WITH_LIBRTAS

static int do_run_mode(char *run_mode)
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
		uint16_t *first_16_bits = (uint16_t *)mode;
		short rmode = atoi(run_mode);

		if (rmode < 0 || rmode > 3) {
			printf("Invalid run-mode=%d\n", rmode);
			return -1;
		}

		*first_16_bits = htobe16(1);
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

#else

static int do_run_mode(char *run_mode)
{
	printf("Run mode determination is not supported on this platfom.\n");
	return -1;
}

#endif

#ifdef HAVE_LINUX_PERF_EVENT_H

static int setup_counters(struct cpu_freq *cpu_freqs)
{
	int i;
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_HARDWARE;
	attr.config = PERF_COUNT_HW_CPU_CYCLES;
	attr.disabled = 1;
	attr.size = sizeof(attr);

	for (i = 0; i < threads_in_system; i++) {
		if (!cpu_online(i)) {
			cpu_freqs[i].offline = 1;
			continue;
		}

		cpu_freqs[i].counter = syscall(__NR_perf_event_open, &attr,
					       -1, i, -1, 0);

		if (cpu_freqs[i].counter < 0) {
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

static void start_counters(struct cpu_freq *cpu_freqs)
{
	int i;

	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freqs[i].offline)
			continue;

		ioctl(cpu_freqs[i].counter, PERF_EVENT_IOC_ENABLE);
	}
}

static void stop_counters(struct cpu_freq *cpu_freqs)
{
	int i;

	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freqs[i].offline)
			continue;

		ioctl(cpu_freqs[i].counter, PERF_EVENT_IOC_DISABLE);
	}
}

static void read_counters(struct cpu_freq *cpu_freqs)
{
	int i;

	for (i = 0; i < threads_in_system; i++) {
		size_t res;

		if (cpu_freqs[i].offline)
			continue;

		res = read(cpu_freqs[i].counter, &cpu_freqs[i].freq,
			   sizeof(unsigned long long));
		assert(res == sizeof(unsigned long long));

		close(cpu_freqs[i].counter);
	}
}

static void check_threads(struct cpu_freq *cpu_freqs)
{
	int i;

	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freqs[i].offline)
			continue;

		/* Sending signal 0 with pthread_kill will just check for
		 * the existance of the thread without actually sending a
		 * signal, we use this to see if the thread exited.
		 */
		if (pthread_kill(cpu_freqs[i].tid, 0)) {
			/* pthread exited, mark it offline iso we don't use
			 * it in our calculations and close its perf
			 * counter.
			 */
			cpu_freqs[i].offline = 1;
			close(cpu_freqs[i].counter);
		}
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
		pthread_exit(NULL);
	}

	while (1)
		; /* Do Nothing */
}

static char *power_mode(uint64_t mode)
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

static void report_system_power_mode(void)
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
static void setrlimit_open_files(void)
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

#define freq_calc(cycles, time)	(1.0 * (cycles) / (time) / 1000000000ULL)

static int do_cpu_frequency(int sleep_time)
{
	int i, rc;
	unsigned long long min = -1ULL;
	unsigned long min_cpu = -1UL;
	unsigned long long max = 0;
	unsigned long max_cpu = -1UL;
	unsigned long long sum = 0;
	unsigned long count = 0;
	struct cpu_freq *cpu_freqs;

	setrlimit_open_files();

	cpu_freqs = calloc(threads_in_system, sizeof(*cpu_freqs));
	if (!cpu_freqs)
		return -ENOMEM;

	rc = setup_counters(cpu_freqs);
	if (rc) {
		free(cpu_freqs);
		return rc;
	}

	/* Start a soak thread on each CPU */
	for (i = 0; i < threads_in_system; i++) {
		if (cpu_freqs[i].offline)
			continue;

		if (pthread_create(&cpu_freqs[i].tid, NULL, soak,
				   (void *)(long)i)) {
			perror("pthread_create");
			free(cpu_freqs);
			return -1;
		}
	}

	/* Wait for soak threads to start */
	usleep(1000000);

	start_counters(cpu_freqs);
	/* Count for specified timeout in seconds */
	usleep(sleep_time * 1000000);

	stop_counters(cpu_freqs);
	check_threads(cpu_freqs);
	read_counters(cpu_freqs);

	for (i = 0; i < threads_in_system; i++) {
		unsigned long long frequency;

		if (cpu_freqs[i].offline)
			continue;

		frequency = cpu_freqs[i].freq;

		if (frequency < min) {
			min = frequency;
			min_cpu = i;
		}
		if (frequency > max) {
			max = frequency;
			max_cpu = i;
		}
		sum += frequency;
		count++;
	}

	report_system_power_mode();
	printf("min:\t%.3f GHz (cpu %ld)\n", freq_calc(min, sleep_time),
	       min_cpu);
	printf("max:\t%.3f GHz (cpu %ld)\n", freq_calc(max, sleep_time),
	       max_cpu);
	printf("avg:\t%.3f GHz\n\n", freq_calc((sum / count), sleep_time));

	free(cpu_freqs);
	return 0;
}

#else

static int do_cpu_frequency(int sleep_time)
{
	printf("CPU Frequency determination is not supported on this "
	       "platfom.\n");
	return EINVAL;
}

#endif

static inline void do_cores_present()
{
	printf("Number of cores present = %d\n", cpus_in_system);
}

static int set_all_threads_off(int cpu, int smt_state)
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

static int set_one_core(int smt_state, int core, int state)
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

static int do_online_cores(char *cores, int state)
{
	int smt_state;
	int *core_state, *desired_core_state;
	int i, rc = 0;
	int core;
	char *str, *token, *end_token;
	bool first_core = true;

	if (cores) {
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
		printf("Bad or inconsistent SMT state: use ppc64_cpu --smt=on|off to set all\n"
                       "cores to have the same number of online threads to continue.\n");
		do_info();
		return -1;
	}

	core_state = calloc(cpus_in_system, sizeof(int));
	if (!core_state)
		return -ENOMEM;

	for (i = 0; i < cpus_in_system ; i++)
		core_state[i] = cpu_online(i * threads_per_cpu);

	if (!cores) {
		printf("Cores %s = ", state == 0 ? "offline" : "online");
		for (i = 0; i < cpus_in_system; i++) {
			if (core_state[i] == state) {
				if (first_core)
					first_core = false;
				else
					printf(",");
				printf("%d", i);
			}
		}
		printf("\n");
		free(core_state);
		return 0;
	}

	desired_core_state = calloc(cpus_in_system, sizeof(int));
	if (!desired_core_state) {
		free(core_state);
		return -ENOMEM;
	}

	for (i = 0; i < cpus_in_system; i++)
		/*
		 * Not specified on command-line
		 */
		desired_core_state[i] = -1;

	str = cores;
	while (1) {
		token = strtok(str, ",");
		if (!token)
			break;
		/* reuse the same string */
		str = NULL;

		core = strtol(token, &end_token, 0);
		if (token == end_token || '\0' != *end_token) {
			printf("Invalid core to %s: %s\n", state == 0 ? "offline" : "online", token);
			rc = -1;
			continue;
		}
		if (core > cpus_in_system || core < 0) {
			printf("Invalid core to %s: %d\n", state == 0 ? "offline" : "online", core);
			rc = -1;
			continue;
		}
		desired_core_state[core] = state;
	}

	if (rc) {
		free(core_state);
		free(desired_core_state);
		return rc;
	}

	for (i = 0; i < cpus_in_system; i++) {
		if (desired_core_state[i] != -1) {
			rc = set_one_core(smt_state, i, state);
			if (rc)
				break;
		}
	}

	free(core_state);
	free(desired_core_state);
	return rc;
}

static int do_cores_on(char *state)
{
	int smt_state;
	int *core_state;
	int cores_now_online = 0;
	int i, rc;
	int number_to_have, number_to_change = 0, number_changed = 0;
	int new_state;
	char *end_state;

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
		printf("Bad or inconsistent SMT state: use ppc64_cpu --smt=on|off to set all\n"
                       "cores to have the same number of online threads to continue.\n");
		do_info();
		return -1;
	}

	core_state = calloc(cpus_in_system, sizeof(int));
	if (!core_state)
		return -ENOMEM;

	for (i = 0; i < cpus_in_system ; i++) {
		core_state[i] = cpu_online(i * threads_per_cpu);
		if (core_state[i])
			cores_now_online++;
	}

	if (!state) {
		printf("Number of cores online = %d\n", cores_now_online);
		free(core_state);
		return 0;
	}

	if (!strcmp(state, "all")) {
		number_to_have = cpus_in_system;
	} else {
		number_to_have = strtol(state, &end_state, 0);
		/* No digits found or trailing characters */
		if (state == end_state || '\0' != *end_state) {
			printf("Invalid number of cores to online: %s\n", state);
			free(core_state);
			return -1;
		}
	}

	if (number_to_have == cores_now_online) {
		free(core_state);
		return 0;
	}

	if (number_to_have > cpus_in_system) {
		printf("Cannot online more cores than are present.\n");
		do_cores_present();
		free(core_state);
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
				rc = set_one_core(smt_state, i, new_state);
				if (!rc)
					number_changed++;
				if (number_changed >= number_to_change)
					break;
			}
		}
	} else {
		for (i = cpus_in_system - 1; i > 0; i--) {
			if (core_state[i]) {
				rc = set_one_core(smt_state, i, new_state);
				if (!rc)
					number_changed++;
				if (number_changed >= number_to_change)
					break;
			}
		}
	}

	if (number_changed != number_to_change) {
		cores_now_online = 0;
		for (i = 0; i < cpus_in_system ; i++) {
			if (cpu_online(i * threads_per_cpu))
				cores_now_online++;
		}
		printf("Failed to set requested number of cores online.\n"
                       "Requested: %d cores, Onlined: %d cores\n",
                       number_to_have, cores_now_online);
		free(core_state);
		return -1;
	}

	free(core_state);
	return 0;
}

static int do_info(void)
{
	int i, j, thread_num;
	char online;
	int subcores = 0;

	if (is_subcore_capable())
		subcores = num_subcores();

	for (i = 0; i < cpus_in_system; i++) {

		if (subcores > 1) {
			if (i % subcores == 0)
				printf("Core %3d:\n", i/subcores);
			printf("  Subcore %3d: ", i);
		} else {
			printf("Core %3d: ", i);
		}

		for (j = 0; j < threads_per_cpu; j++) {
			thread_num = i*threads_per_cpu + j;
			online = cpu_online(thread_num) ? '*' : ' ';
			printf("%4d%c ", thread_num, online);
		}
		printf("\n");
	}
	return 0;
}

static void usage(void)
{
	printf(
"Usage: ppc64_cpu [command] [options]\n"
"ppc64_cpu --smt                     # Get current SMT state\n"
"ppc64_cpu --smt={on|off}            # Turn SMT on/off\n"
"ppc64_cpu --smt=X                   # Set SMT state to X\n\n"
"ppc64_cpu --cores-present           # Get the number of cores present\n"
"ppc64_cpu --cores-on                # Get the number of cores currently online\n"
"ppc64_cpu --cores-on=X              # Put exactly X cores online\n"
"ppc64_cpu --cores-on=all            # Put all cores online\n\n"
"ppc64_cpu --online-cores=X[,Y...]   # Put specified cores online\n\n"
"ppc64_cpu --offline-cores=X[,Y,...] # Put specified cores offline\n\n"
"ppc64_cpu --dscr                    # Get current DSCR system setting\n"
"ppc64_cpu --dscr=<val>              # Change DSCR system setting\n"
"ppc64_cpu --dscr [-p <pid>]         # Get DSCR setting for process <pid>\n"
"ppc64_cpu --dscr=<val> [-p <pid>]   # Change DSCR setting for process <pid>\n\n"
"ppc64_cpu --smt-snooze-delay        # Get current smt-snooze-delay setting\n"
"ppc64_cpu --smt-snooze-delay=<val>  # Change smt-snooze-delay setting\n\n"
"ppc64_cpu --run-mode                # Get current diagnostics run mode\n"
"ppc64_cpu --run-mode=<val>          # Set current diagnostics run mode\n\n"
"ppc64_cpu --frequency [-t <time>]   # Determine cpu frequency for <time>\n"
"                                    # seconds, default is 1 second.\n\n"
"ppc64_cpu --subcores-per-core       # Get number of subcores per core\n"
"ppc64_cpu --subcores-per-core=X     # Set subcores per core to X (1 or 4)\n"
"ppc64_cpu --threads-per-core        # Get threads per core\n"
"ppc64_cpu --info                    # Display system state information)\n");
}

struct option longopts[] = {
	{"smt",			optional_argument, NULL, 's'},
	{"dscr",		optional_argument, NULL, 'd'},
	{"smt-snooze-delay",	optional_argument, NULL, 'S'},
	{"run-mode",		optional_argument, NULL, 'r'},
	{"frequency",		no_argument,	   NULL, 'f'},
	{"cores-present",	no_argument,	   NULL, 'C'},
	{"cores-on",		optional_argument, NULL, 'c'},
	{"online-cores",	optional_argument, NULL, 'O'},
	{"offline-cores",	optional_argument, NULL, 'F'},
	{"subcores-per-core",	optional_argument, NULL, 'n'},
	{"info",		no_argument,	   NULL, 'i'},
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
	int sleep_time = 1; /* default to one second */
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

	/* Now parse out any additional options. */
	optind = 2;
	while (1) {
		opt = getopt(argc, argv, "p:t:");
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
		case 't':
			/* only valid for --frequency */
			if (strcmp(action, "frequency")) {
				fprintf(stderr, "The t option is only valid "
					"with the --frequency option\n");
				usage();
				exit(-1);
			}

			sleep_time = atoi(optarg);
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
		rc = do_cpu_frequency(sleep_time);
	else if (!strcmp(action, "cores-present"))
		do_cores_present();
	else if (!strcmp(action, "cores-on"))
		rc = do_cores_on(action_arg);
	else if (!strcmp(action, "online-cores"))
		rc = do_online_cores(action_arg, 1);
	else if (!strcmp(action, "offline-cores"))
		rc = do_online_cores(action_arg, 0);
	else if (!strcmp(action, "subcores-per-core"))
		rc = do_subcores_per_core(action_arg);
	else if (!strcmp(action, "threads-per-core"))
		do_threads_per_core();
	else if (!strcmp(action, "info"))
		rc = do_info();
	else if (!strcmp(action, "version"))
		printf("ppc64_cpu: version %s\n", PPC64_CPU_VERSION);
	else
		usage();

	return rc;
}
