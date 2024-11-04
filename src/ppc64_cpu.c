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
#include <sys/param.h>

#ifdef WITH_LIBRTAS
#include <librtas.h>
#include "librtas_error.h"
#endif

#ifdef HAVE_LINUX_PERF_EVENT_H
#include <linux/perf_event.h>
#endif

#include <errno.h>
#include "cpu_info_helpers.h"

#define PPC64_CPU_VERSION	"1.2"

#define DSCR_DEFAULT_PATH "/sys/devices/system/cpu/dscr_default"

#define DIAGNOSTICS_RUN_MODE	42
#define CPU_OFFLINE		-1

#define SYS_SMT_CONTROL "/sys/devices/system/cpu/smt/control"

#ifdef HAVE_LINUX_PERF_EVENT_H
struct cpu_freq {
	int offline;
	int counter;
	pthread_t tid;
	double freq;
};

struct energy_freq_info {
	char power_perf_mode[64];
	char ips[64];
	float min_freq_mhz;
	float stat_freq_mhz;
	float max_freq_mhz;
	int processor_folding_status;
};

enum energy_freq_attrs {
	POWER_PERFORMANCE_MODE = 1,
	IDLE_POWER_SAVER_STATUS = 2,
	MIN_FREQ = 3,
	STAT_FREQ = 4,
	MAX_FREQ = 6,
	PROC_FOLDING_STATUS = 8
};

#ifndef __NR_perf_event_open
#define __NR_perf_event_open	319
#endif

#endif

static int threads_per_cpu = 0;
static int cpus_in_system = 0;
static int threads_in_system = 0;

static int do_info(void);

static int sysattr_is_readable(char *attribute)
{
	return __sysattr_is_readable(attribute, threads_in_system);
}

static int sysattr_is_writeable(char *attribute)
{
	return __sysattr_is_writeable(attribute, threads_in_system);
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
		 * directory, for example dscr. The absence of the file is not
		 * an error, so detect and clear the error when set_attribute
		 * indicates ENOENT. */
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

static int online_thread(const char *path)
{
	return set_attribute(path, "%d", 1);
}

static int offline_thread(const char *path)
{
	return set_attribute(path, "%d", 0);
}

static int is_smt_capable(void)
{
	return __is_smt_capable(threads_per_cpu);
}

static int get_one_smt_state(int core)
{
	return __get_one_smt_state(core, threads_per_cpu);
}

static int get_smt_state(void)
{
	int smt_state = -1;
	int i, rc;
	int *present_cores;
	int num_present_cores;

	rc = get_present_core_list(&present_cores, &num_present_cores, threads_per_cpu);
	if (rc != 0) {
		return -1;
	}

	for (i = 0; i < num_present_cores; i++) {
		int cpu_state = get_one_smt_state(present_cores[i]);

		if (cpu_state == 0)
			continue;

		if (smt_state == -1)
			smt_state = cpu_state;

		if (smt_state != cpu_state) {
			smt_state = -1;
			break;
		}
	}

	free(present_cores);
	return smt_state;
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
	int i, j, rc = 0;
	int error = 0;
	int cpu_base, cpu_id, core_id;
	int *present_cores = NULL;
	int num_present_cores;

	if (!sysattr_is_writeable("online")) {
		perror("Cannot set smt state");
		return -1;
	}

	rc = get_present_core_list(&present_cores, &num_present_cores, threads_per_cpu);

	if (rc != 0) {
		fprintf(stderr, "Failed to retrieve present core list\n");
		return rc;
	}

	for (i = 0; i < num_present_cores; i++) {

		core_id = present_cores[i];
		cpu_base = core_id * threads_per_cpu;

		/* Online means any thread on this core running, so check all
		 * threads in the core, not just the first. */
		for (j = 0; j < threads_per_cpu; j++) {
			cpu_id = cpu_base + j;

			if (!cpu_online(cpu_id))
				continue;

			rc = set_one_smt_state(cpu_base, smt_state);
			/* Record an error, but do not check result: if we
			 * have failed to set this core, keep trying
			 * subsequent ones. */
			if (rc)
				error = 1;
			break;
		}
	}

	free(present_cores);

	if (error) {
		fprintf(stderr, "One or more CPUs could not be on/offlined\n");
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

/*
 * Depends on kernel's CONFIG_HOTPLUG_CPU
 * Return -1 for fatal error, -2 to retry.
 */
static int set_smt_control(int smt_state)
{
	if (set_attribute(SYS_SMT_CONTROL, "%d", smt_state)) {
		switch (errno) {
			case ENOENT:
			/*
			 * The kernel does not have the interface.
			 * Try the old method.
			 */
				return -2;
			case ENODEV:
			/*
			 * Setting SMT state not supported by this interface.
			 * On older kernels (before Linux 6.6) the generic interface
			 * may exist but is not hooked on powerpc resulting in ENODEV
			 * on kernels that can set SMT using the old interface.
			 */
				return -2;
			default:
				perror(SYS_SMT_CONTROL);
				return -1;
		}
	}
	return 0;
}

static int do_smt(char *state, bool numeric)
{
	int rc = 0;
	int smt_state = 0;

	if (!is_smt_capable()) {
		if (numeric)
			printf("SMT=1\n");
		else
			fprintf(stderr, "Machine is not SMT capable\n");
		return -1;
	}

	if (!state) {
		rc = __do_smt(numeric, cpus_in_system, threads_per_cpu, true);
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

		/* Try using smt/control if failing, fall back to the legacy way */
		if ((rc = set_smt_control(smt_state)) == -2)
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

static int setup_counters(struct cpu_freq *cpu_freqs, int max_thread)
{
	int i;
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_HARDWARE;
	attr.config = PERF_COUNT_HW_CPU_CYCLES;
	attr.disabled = 1;
	attr.size = sizeof(attr);

	/* Record how long the event ran for */
	attr.read_format |= PERF_FORMAT_TOTAL_TIME_RUNNING;

	for (i = 0; i < max_thread; i++) {
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

static void start_counters(struct cpu_freq *cpu_freqs, int max_thread)
{
	int i;

	for (i = 0; i < max_thread; i++) {
		if (cpu_freqs[i].offline)
			continue;

		ioctl(cpu_freqs[i].counter, PERF_EVENT_IOC_ENABLE);
	}
}

static void stop_counters(struct cpu_freq *cpu_freqs, int max_thread)
{
	int i;

	for (i = 0; i < max_thread; i++) {
		if (cpu_freqs[i].offline)
			continue;

		ioctl(cpu_freqs[i].counter, PERF_EVENT_IOC_DISABLE);
	}
}

struct read_format {
	uint64_t value;
	uint64_t time_running;
};

static void read_counters(struct cpu_freq *cpu_freqs, int max_thread)
{
	int i;
	struct read_format vals;

	for (i = 0; i < max_thread; i++) {
		size_t res;

		if (cpu_freqs[i].offline)
			continue;

		res = read(cpu_freqs[i].counter, &vals, sizeof(vals));
		assert(res == sizeof(vals));

		/* Warn if we don't get at least 0.1s of time on the CPU */
		if (vals.time_running < 100000000) {
			fprintf(stderr, "Measurement interval was too small, is someone running perf?\n");
			exit(1);
		}

		cpu_freqs[i].freq = 1.0 * vals.value / vals.time_running;

		close(cpu_freqs[i].counter);
	}
}

static void check_threads(struct cpu_freq *cpu_freqs, int max_thread)
{
	int i;

	for (i = 0; i < max_thread; i++) {
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
	unsigned new = threads_in_system + 8;

	getrlimit(RLIMIT_NOFILE, &old_rlim);

	if (old_rlim.rlim_cur > new)
		return;

	new_rlim.rlim_cur = new;
	new_rlim.rlim_max = old_rlim.rlim_max;

	setrlimit(RLIMIT_NOFILE, &new_rlim);
}

static bool has_str_val(int id)
{
	switch(id){
		case POWER_PERFORMANCE_MODE:
		case IDLE_POWER_SAVER_STATUS:
			return true;
	}
	return false;
}

static int report_platform_energy_freq_mode(struct energy_freq_info *eq)
{
	const char *path = "/sys/firmware/papr/energy_scale_info";
	struct dirent *entry;
	struct stat s;
	DIR *dirp;

	if (stat(path, &s) || !S_ISDIR(s.st_mode))
		return -1;
	dirp = opendir(path);

	while ((entry = readdir(dirp)) != NULL) {
		char val_buf[64], file_name[64];
		int id, num_val;
		FILE *f;

		if (strcmp(entry->d_name,".") == 0 ||
		    strcmp(entry->d_name,"..") == 0)
			continue;

		id = atoi(entry->d_name);

		sprintf(file_name, "%s/%d/value", path, id);
		f = fopen(file_name, "r");
		if (!f)
			return -1;
		if (fgets(val_buf, 64, f) == NULL)
			return -1;
		fclose(f);
		num_val = atoi(val_buf);

		if (has_str_val(id)) {
			sprintf(file_name, "%s/%d/value_desc", path, id);
			f = fopen(file_name, "r");
			if (!f || fgets(val_buf, 64, f) == NULL)
				return -1;
			fclose(f);
		}

		switch(id){
		case POWER_PERFORMANCE_MODE:
			strcpy(eq->power_perf_mode, val_buf);
			break;
		case IDLE_POWER_SAVER_STATUS:
			strcpy(eq->ips, val_buf);
			break;
		case MIN_FREQ:
			eq->min_freq_mhz = num_val;
			break;
		case STAT_FREQ:
			eq->stat_freq_mhz = num_val;
			break;
		case MAX_FREQ:
			eq->max_freq_mhz = num_val;
			break;
		case PROC_FOLDING_STATUS:
			eq->processor_folding_status = num_val;
			break;
		}
	}

	closedir(dirp);

	return 0;
}

static int do_cpu_frequency(int sleep_time)
{
	int i, rc;
	double min = -1ULL;
	unsigned long min_cpu = -1UL;
	double max = 0;
	unsigned long max_cpu = -1UL;
	double sum = 0;
	unsigned long count = 0;
	struct cpu_freq *cpu_freqs;
	struct energy_freq_info eq;
	int max_thread;

	setrlimit_open_files();

	max_thread = MIN(threads_in_system, CPU_SETSIZE);
	if (max_thread < threads_in_system)
		printf("ppc64_cpu currently supports up to %d CPUs\n",
			CPU_SETSIZE);

	cpu_freqs = calloc(max_thread, sizeof(*cpu_freqs));
	if (!cpu_freqs)
		return -ENOMEM;

	rc = setup_counters(cpu_freqs, max_thread);
	if (rc) {
		free(cpu_freqs);
		return rc;
	}

	/* Start a soak thread on each CPU */
	for (i = 0; i < max_thread; i++) {
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

	start_counters(cpu_freqs, max_thread);
	/* Count for specified timeout in seconds */
	usleep(sleep_time * 1000000);

	stop_counters(cpu_freqs, max_thread);
	check_threads(cpu_freqs, max_thread);
	read_counters(cpu_freqs, max_thread);

	for (i = 0; i < max_thread; i++) {
		double frequency;

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

	if (report_platform_energy_freq_mode(&eq)) {
		report_system_power_mode();
	} else {
		printf("Power and Performance Mode: %s", eq.power_perf_mode);
		printf("Idle Power Saver Status: %s", eq.ips);
		if (strcmp(eq.ips, "Not Supported\n")) {
			printf("Processor Folding Status: %d\n",
			       eq.processor_folding_status);
		}
		printf("Platform reported frequencies\n");
		printf("min\t:\t%.3f GHz\n", (eq.min_freq_mhz/1000));
		printf("max\t:\t%.3f GHz\n", (eq.max_freq_mhz/1000));
		printf("static\t:\t%.3f GHz\n\n", (eq.stat_freq_mhz/1000));
	}
	printf("Tool Computed frequencies\n");
	printf("min\t:\t%.3f GHz (cpu %ld)\n", min, min_cpu);
	printf("max\t:\t%.3f GHz (cpu %ld)\n", max, max_cpu);
	printf("avg\t:\t%.3f GHz\n", sum / count);

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
			printf("Unable to take CPU %d offline\n", i);
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
	int *core_state = NULL, *desired_core_state = NULL;
	int i, rc = 0;
	int core, valid = 0, core_idx = 0;
	char *str, *token, *end_token;
	bool first_core = true;
	int *present_cores = NULL;
	int num_present_cores;

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

	rc = get_present_core_list(&present_cores, &num_present_cores, threads_per_cpu);
	if (rc != 0) {
		fprintf(stderr, "Failed to retrieve present core list\n");
		return rc;
	}

	smt_state = get_smt_state();

	core_state = calloc(num_present_cores, sizeof(int));
	if (!core_state) {
		free(present_cores);
		return -ENOMEM;
	}

	for (i = 0; i < num_present_cores; i++) {
		core_state[i] = (get_one_smt_state(present_cores[i]) > 0);
	}

	if (!cores) {
		printf("Cores %s = ", state == 0 ? "offline" : "online");
		for (i = 0; i < num_present_cores; i++) {
			if (core_state[i] == state) {
				if (first_core)
					first_core = false;
				else
					printf(",");
				printf("%d", present_cores[i]);
			}
		}
		printf("\n");
		free(core_state);
		free(present_cores);
		return 0;
	}

	if (smt_state == -1) {
		printf("Bad or inconsistent SMT state: use ppc64_cpu --smt=on|off to set all\n"
				"cores to have the same number of online threads to continue.\n");
		do_info();
		free(present_cores);
		return -1;
	}

	desired_core_state = calloc(num_present_cores, sizeof(int));
	if (!desired_core_state) {
		free(core_state);
		free(present_cores);
		return -ENOMEM;
	}

	for (i = 0; i < num_present_cores; i++) {
		/*
		 * Not specified on command-line
		 */
		desired_core_state[i] = -1;
	}

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

		for (i = 0; i < num_present_cores; i++) {
			if (core == present_cores[i]) {
				valid = 1;
				core_idx = i;
				break;
			}
		}

		if (!valid) {
			printf("Invalid core to %s: %d\n", state == 0 ? "offline" : "online", core);
			rc = -1;
			continue;
		}

		desired_core_state[core_idx] = state;
	}

	if (rc) {
		goto cleanup;
	}

	for (i = 0; i < num_present_cores; i++) {
		if (desired_core_state[i] != -1) {
			rc = set_one_core(smt_state, present_cores[i], state);
			if (rc) {
				fprintf(stderr, "Failed to set core %d to %s\n", present_cores[i], state == 0 ? "offline" : "online");
				break;
			}
		}
	}

cleanup:
	free(core_state);
	free(desired_core_state);
	free(present_cores);

	return rc;
}

static int do_cores_on(char *state)
{
	int smt_state;
	int cores_now_online = 0, core_id = 0;
	int i, rc = 0;
	int number_to_have, number_to_change = 0, number_changed = 0;
	int *core_state = NULL;
	int new_state;
	char *end_state;
	int *present_cores = NULL;
	int num_present_cores;

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

	core_state = calloc(cpus_in_system, sizeof(int));
	if (!core_state)
		return -ENOMEM;

	rc = get_present_core_list(&present_cores, &num_present_cores, threads_per_cpu);
	if (rc != 0) {
		fprintf(stderr, "Failed to retrieve present core list\n");
		free(core_state);
		return rc;
	}
	for (i = 0; i < num_present_cores; i++) {
		int core = present_cores[i];
		core_state[i] = (get_one_smt_state(core) > 0);
		if (core_state[i]) {
			cores_now_online++;
		}
	}

	if (!state) {
		printf("Number of cores online = %d\n", cores_now_online);
		rc = 0;
		goto cleanup;
	}

	smt_state = get_smt_state();
	if (smt_state == -1) {
		printf("Bad or inconsistent SMT state: use ppc64_cpu --smt=on|off to set all\n"
			   "cores to have the same number of online threads to continue.\n");
		do_info();
		rc = -1;
		goto cleanup;
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
		rc = 0;
		goto cleanup;
	}

	if (number_to_have <= 0 || number_to_have > cpus_in_system) {
	        printf("Error: Invalid number of cores requested: %d, possible values \
        	        should be in range: (1-%d)\n", number_to_have, cpus_in_system);
		do_cores_present();
		rc = -1;
		goto cleanup;
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
			core_id = present_cores[i];
			if (!core_state[i]) {
				rc = set_one_core(smt_state, core_id, new_state);
				if (!rc) {
					number_changed++;
				}
				if (number_changed >= number_to_change) {
					break;
				}
			}
		}
	} else {
		for (i = cpus_in_system - 1; i >= 0; i--) {
			core_id = present_cores[i];
			if (core_state[i]) {
				rc = set_one_core(smt_state, core_id, new_state);
				if (!rc) {
					number_changed++;
				}
				if (number_changed >= number_to_change) {
					break;
				}
			}
		}
	}

	if (number_changed != number_to_change) {
		cores_now_online = 0;
		for (i = 0; i < cpus_in_system; i++) {
			core_id = present_cores[i];
			if (cpu_online(core_id * threads_per_cpu)) {
				cores_now_online++;
			}
		}
		printf("Failed to set requested number of cores online.\n"
			   "Requested: %d cores, Onlined: %d cores\n",
			   number_to_have, cores_now_online);
		rc = -1;
	}

cleanup:
	free(core_state);
	free(present_cores);
	return rc;
}

static bool core_is_online(int core)
{
	return  cpu_physical_id(core * threads_per_cpu) != -1;
}

static int do_info(void)
{
	int i, j, thread_num;
	char online;
	int subcores = 0, core_id = 0;
	int *present_cores = NULL;
	int num_present_cores;

	if (is_subcore_capable()) {
		subcores = num_subcores();
	}

	int rc = get_present_core_list(&present_cores, &num_present_cores, threads_per_cpu);
	if (rc != 0) {
		fprintf(stderr, "Failed to retrieve present core list\n");
		return rc;
	}

	for (i = 0; i < num_present_cores; i++) {
		core_id = present_cores[i];
		if (!core_is_online(core_id)) {
			continue;
		}

		if (subcores > 1) {
			if (core_id % subcores == 0) {
				printf("Core %3d:\n", core_id / subcores);
			}
			printf("  Subcore %3d: ", core_id);
		} else {
			printf("Core %3d: ", core_id);
		}

		for (j = 0; j < threads_per_cpu; j++) {
			thread_num = core_id * threads_per_cpu + j;
			online = cpu_online(thread_num) ? '*' : ' ';
			printf("%4d%c ", thread_num, online);
		}
		printf("\n");
	}
	free(present_cores);
	return 0;
}
static void usage(void)
{
	printf(
"Usage: ppc64_cpu [command] [options]\n"
"ppc64_cpu --smt [-n]                # Get current SMT state. [-n] shows numeric output\n"
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
"ppc64_cpu --run-mode                # Get current diagnostics run mode\n"
"ppc64_cpu --run-mode=<val>          # Set current diagnostics run mode\n\n"
"ppc64_cpu --frequency [-t <time>]   # Determine cpu frequency for <time>\n"
"                                    # seconds, default is 1 second.\n\n"
"ppc64_cpu --subcores-per-core       # Get number of subcores per core\n"
"ppc64_cpu --subcores-per-core=X     # Set subcores per core to X (1 or 4)\n"
"ppc64_cpu --threads-per-core        # Get threads per core\n"
"ppc64_cpu --info                    # Display system state information\n"
"ppc64_cpu --version                 # Display version of ppc64-cpu\n");
}

struct option longopts[] = {
	{"smt",			optional_argument, NULL, 's'},
	{"dscr",		optional_argument, NULL, 'd'},
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
	bool numeric = false;
	pid_t pid = -1;

	if (argc == 1) {
		usage();
		return 0;
	}

	rc = get_cpu_info(&threads_per_cpu, &cpus_in_system, &threads_in_system);
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
		opt = getopt(argc, argv, "p:t:n");
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
		case 'n':
			if (strcmp(action, "smt")) {
				fprintf(stderr, "The n option is only valid "
					"with the --smt option\n");
				usage();
				exit(-1);
			}
			numeric = true;
			break;
		default:
			fprintf(stderr, "%c is not a valid option\n", opt);
			usage();
			exit(-1);
		}
	}

	if (!strcmp(action, "smt"))
		rc = do_smt(action_arg, numeric);
	else if (!strcmp(action, "dscr"))
		rc = do_dscr(action_arg, pid);
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
