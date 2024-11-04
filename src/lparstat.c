/**
 * @file lparstat.c
 * @brief lparstat command
 *
 * Copyright (c) 2011 International Business Machines
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
 * @author Nathan Fontenot <nfont@linux.vnet.ibm.com>
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "lparstat.h"
#include "pseries_platform.h"
#include "cpu_info_helpers.h"
#include <time.h>
#include <sys/auxv.h>

#ifndef PPC_FEATURE2_ARCH_3_1
#define PPC_FEATURE2_ARCH_3_1 0x00040000
#endif

#define LPARCFG_FILE	"/proc/ppc64/lparcfg"
#define SE_NOT_FOUND	"???"
#define SE_NOT_VALID	"-"

static bool o_legacy = false;
static bool o_scaled = false;
static bool o_security = false;

static int threads_per_cpu;
static int cpus_in_system;
static int threads_in_system;

static cpu_sysfs_fd *cpu_sysfs_fds;
static cpu_set_t *online_cpus;

struct sysentry *get_sysentry(char *name)
{
	struct sysentry *se = &system_data[0];

	while (se->name[0] != '\0') {
		if (!strcmp(se->name, name))
			return se;
		se++;
	}

	return NULL;
}

void get_sysdata(char *name, char **descr, char *value)
{
	struct sysentry *se;

	se = get_sysentry(name);
	if (!se) {
		*descr = name;
		sprintf(value, SE_NOT_FOUND);
		return;
	}

	if (se->get) {
		se->get(se, value);
	} else if (se->value[0] == '\0') {
		sprintf(value, SE_NOT_VALID);
	} else {
		sprintf(value, "%s", se->value);
	}

	*descr = se->descr;
}

static int is_smt_capable(void)
{
	return __is_smt_capable(threads_per_cpu);
}

static int parse_smt_state(void)
{
	return __do_smt(false, cpus_in_system, threads_per_cpu, false);
}

static int get_one_smt_state(int core)
{
	return __get_one_smt_state(core, threads_per_cpu);
}

static int assign_read_fd(const char *path)
{
	int rc = 0;

	rc = access(path, R_OK);
	if (rc)
		return -1;

	rc = open(path, O_RDONLY);
	return rc;
}

static void close_cpu_sysfs_fds(int threads_in_system)
{
	int i;

	for (i = 0; i < threads_in_system && cpu_sysfs_fds[i].spurr; i++) {
		close(cpu_sysfs_fds[i].spurr);
		close(cpu_sysfs_fds[i].idle_purr);
		close(cpu_sysfs_fds[i].idle_spurr);
	}

	free(cpu_sysfs_fds);
}

static int assign_cpu_sysfs_fds(int threads_in_system)
{
	int cpu_idx, i;
	char sysfs_file_path[SYSFS_PATH_MAX];

	cpu_sysfs_fds =
		(cpu_sysfs_fd*)calloc(threads_in_system, sizeof(cpu_sysfs_fd));
	if (!cpu_sysfs_fds) {
		fprintf(stderr, "Failed to allocate memory for sysfs file descriptors\n");
		return -1;
	}

	for (cpu_idx = 0, i = 0; i < threads_in_system; i++) {
		if (!cpu_online(i))
			continue;

		cpu_sysfs_fds[cpu_idx].cpu = i;

		snprintf(sysfs_file_path, SYSFS_PATH_MAX, SYSFS_PERCPU_SPURR, i);
		cpu_sysfs_fds[cpu_idx].spurr = assign_read_fd(sysfs_file_path);
		if (cpu_sysfs_fds[cpu_idx].spurr == -1)
			goto error;

		snprintf(sysfs_file_path, SYSFS_PATH_MAX, SYSFS_PERCPU_IDLE_PURR, i);
		cpu_sysfs_fds[cpu_idx].idle_purr = assign_read_fd(sysfs_file_path);
		if (cpu_sysfs_fds[cpu_idx].idle_purr == -1)
			goto error;

		snprintf(sysfs_file_path, SYSFS_PATH_MAX, SYSFS_PERCPU_IDLE_SPURR, i);
		cpu_sysfs_fds[cpu_idx].idle_spurr = assign_read_fd(sysfs_file_path);
		if (cpu_sysfs_fds[cpu_idx].idle_spurr == -1)
			goto error;

		cpu_idx++;
	}

	/* Mark extra slots for offline threads unset, see parse_sysfs_values */
	for (; cpu_idx < threads_in_system; cpu_idx++)
		cpu_sysfs_fds[cpu_idx].spurr = -1;

	return 0;
error:
	fprintf(stderr, "Failed to open %s: %s\n",
			sysfs_file_path, strerror(errno));
	close_cpu_sysfs_fds(threads_in_system);
	return -1;
}

int parse_sysfs_values(void)
{
	unsigned long long spurr, idle_spurr, idle_purr, value;
	char line[SYSDATA_VALUE_SZ];
	struct sysentry *se;
	int i, rc;

	spurr = idle_spurr = idle_purr = 0UL;

	for (i = 0; (i < threads_in_system) && (cpu_sysfs_fds[i].spurr >= 0); i++) {
		rc = pread(cpu_sysfs_fds[i].spurr, (void *)line, sizeof(line), 0);
		if (rc == -1) {
			fprintf(stderr, "Failed to read /sys/devices/system/cpu/cpu%d/spurr\n",
					cpu_sysfs_fds[i].cpu);
			goto check_cpu_hotplug;
		}

		value = strtoll(line, NULL, 16);
		spurr += value;

		rc = pread(cpu_sysfs_fds[i].idle_purr, (void *)line, sizeof(line), 0);
		if (rc == -1) {
			fprintf(stderr, "Failed to read /sys/devices/system/cpu/cpu%d/idle_purr\n",
					cpu_sysfs_fds[i].cpu);
			goto check_cpu_hotplug;
		}

		value = strtoll(line, NULL, 16);
		idle_purr += value;

		rc = pread(cpu_sysfs_fds[i].idle_spurr, (void *)line, sizeof(line), 0);
		if (rc == -1) {
			fprintf(stderr, "Failed to read /sys/devices/system/cpu/cpu%d/idle_spurr\n",
					cpu_sysfs_fds[i].cpu);
			goto check_cpu_hotplug;
		}

		value = strtoll(line, NULL, 16);
		idle_spurr += value;
	}

	se = get_sysentry("spurr");
	sprintf(se->value, "%llu", spurr);
	se = get_sysentry("idle_purr");
	sprintf(se->value, "%llu", idle_purr);
	se = get_sysentry("idle_spurr");
	sprintf(se->value, "%llu", idle_spurr);

	return 0;

check_cpu_hotplug:
	if(!cpu_online(cpu_sysfs_fds[i].cpu))
		return 1;

	return rc;
}

static void sig_int_handler(int signal)
{
	close_cpu_sysfs_fds(threads_in_system);
	exit(1);
}

long long get_delta_value(char *se_name)
{
	long long value, old_value;
	struct sysentry *se;

	se = get_sysentry(se_name);
	if (se->value[0] == '\0')
		return 0LL;

	value = strtoll(se->value, NULL, 0);
	old_value = strtoll(se->old_value, NULL, 0);

	return (value - old_value);
}

void get_time_ns(void)
{
	struct sysentry *se;
	struct timespec ts;
	int err;

	err = clock_gettime(CLOCK_BOOTTIME, &ts);
	if (err)
		return;

	se = get_sysentry("time");
	sprintf(se->value, "%lld",
		(long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec);
}

double get_delta_time(void)
{
	return (get_delta_value("time") / 1000000000.0);
}

int get_time_base()
{
	FILE *f;
	char buf[80];
	char *tb = NULL;
	struct sysentry *se;

	f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		fprintf(stderr, "Could not open /proc/cpuinfo\n");
		return -1;
	}

	while ((fgets(buf, 80, f)) != NULL) {
		if (!strncmp(buf, "timebase", 8)) {
			tb = strchr(buf, ':') + 2;
			break;
		}
	}
	fclose(f);

	if (!tb)
		return -1;

	se = get_sysentry("timebase");
	sprintf(se->value, "%s", tb);
	return 0;
}

double get_scaled_tb(void)
{
	double elapsed, timebase;
	struct sysentry *se;
	int online_cores;

	se = get_sysentry("online_cores");
	online_cores = atoi(se->value);

	elapsed = get_delta_time();

	se = get_sysentry("timebase");
	timebase = atoi(se->value);

	return (timebase * elapsed) * online_cores;
}

int get_nominal_frequency(void)
{
	FILE *f;
	char buf[80];
	struct sysentry *se;
	char *nfreq = NULL;

	f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		fprintf(stderr, "Could not open /proc/cpuinfo\n");
		return -1;
	}

	while ((fgets(buf, 80, f)) != NULL) {
		if (!strncmp(buf, "clock", 5)) {
			nfreq = strchr(buf, ':') + 2;
			break;
		}
	}

	fclose(f);

	if (!nfreq) {
		fprintf(stderr, "Failed to read Nominal frequency\n");
		return -1;
	}

	se = get_sysentry("nominal_freq");
	snprintf(se->value, sizeof(se->value), "%s", nfreq);

	return 0;
}

void get_effective_frequency()
{
	struct sysentry *se;
	double delta_purr, delta_spurr;
	double nominal_freq, effective_freq;

	se = get_sysentry("nominal_freq");
	nominal_freq = strtol(se->value, NULL, 10);

	/*
	 * Calculate the Effective Frequency (EF)
	 * EF = (delta SPURR / delta PURR) * nominal frequency
	 */
	delta_purr = get_delta_value("purr");
	delta_spurr = get_delta_value("spurr");

	effective_freq = (delta_spurr / delta_purr) * nominal_freq;

	se = get_sysentry("effective_freq");
	sprintf(se->value, "%f", effective_freq);
}

void get_cpu_physc(struct sysentry *unused_se, char *buf)
{
	struct sysentry *se;
	float elapsed;
	float delta_purr;
	float timebase, physc;
	float delta_tb;

	delta_purr = get_delta_value("purr");

	se = get_sysentry("tbr");
	if (se->old_value[0] != '\0') {
		delta_tb = get_delta_value("tbr");

		physc = delta_purr / delta_tb;
	} else {
		elapsed = get_delta_time();

		se = get_sysentry("timebase");
		timebase = atoi(se->value);

		physc = delta_purr/timebase/elapsed;
	}

	sprintf(buf, "%.2f", physc);
}

void get_per_entc(struct sysentry *unused_se, char *buf)
{
	char *descr;
	char physc[32];
	char entc[32];

	get_sysdata("DesEntCap", &descr, entc);
	get_sysdata("physc", &descr, physc);

	sprintf(buf, "%.2f", atof(physc) / atof(entc) * 100.0);
}

void get_cpu_app(struct sysentry *unused_se, char *buf)
{
	struct sysentry *se;
	float timebase, app, elapsed_time;
	long long new_app, old_app;

	elapsed_time = get_delta_time();

	se = get_sysentry("timebase");
	timebase = atof(se->value);

	se = get_sysentry("pool_idle_time");
	new_app = strtoll(se->value, NULL, 0);
	if (se->old_value[0] == '\0') {
		se = get_sysentry("boot_pool_idle_time");
		old_app = strtoll(se->value, NULL, 0);
	} else {
		old_app = strtoll(se->old_value, NULL, 0);
	}

	app = (new_app - old_app)/timebase/elapsed_time;
	sprintf(buf, "%.2f", app);
}

static double round_off_freq(void)
{
	double effective_freq, nominal_freq, freq;
	struct sysentry *se;

	se = get_sysentry("effective_freq");
	effective_freq = strtod(se->value, NULL);

	se = get_sysentry("nominal_freq");
	nominal_freq = strtod(se->value, NULL);

	freq = ((int)((effective_freq/nominal_freq * 100)+ 0.44) -
	       (effective_freq/nominal_freq * 100)) /
	       (effective_freq/nominal_freq * 100) * 100;

	return freq;
}

void get_cpu_util_purr(struct sysentry *unused_se, char *buf)
{
	double delta_tb, delta_purr, delta_idle_purr;
	double physc;

	delta_tb = get_scaled_tb();
	delta_purr = get_delta_value("purr");
	delta_idle_purr = get_delta_value("idle_purr");

	/*
	 * Given that these values are read from different
	 * sources (purr from lparcfg and idle_purr from sysfs),
	 * a small variation in the values is possible.
	 * In such cases, round down delta_idle_purr to delta_purr.
	 */
	if (delta_idle_purr > delta_purr)
		delta_idle_purr = delta_purr;

	physc = (delta_purr - delta_idle_purr) / delta_tb;
	physc *= 100.00;

	sprintf(buf, "%.2f", physc);
}

void get_cpu_idle_purr(struct sysentry *unused_se, char *buf)
{
	double delta_tb, delta_purr, delta_idle_purr;
	double physc, idle;
	char *descr;
	char mode[32];

	delta_tb = get_scaled_tb();
	delta_purr = get_delta_value("purr");
	delta_idle_purr = get_delta_value("idle_purr");

	get_sysdata("shared_processor_mode", &descr, mode);
	if (!strcmp(mode, "Dedicated"))
		get_sysdata("DedDonMode", &descr, mode);

	/*
	 * Given that these values are read from different
	 * sources (purr from lparcfg and idle_purr from sysfs),
	 * a small variation in the values is possible.
	 * In such cases, round down delta_idle_purr to delta_purr.
	 */
	if (delta_idle_purr > delta_purr)
		delta_idle_purr = delta_purr;
        /*
	 * Round down delta_purr to delta_tb if delta_tb - delta_purr
	 * error is under -1%.
	 */
	if (((delta_tb - delta_purr + delta_idle_purr) / delta_tb * 100) > -1 && ((delta_tb - delta_purr + delta_idle_purr) / delta_tb * 100) < 0)
		delta_purr = delta_tb;

	if (!strcmp(mode, "Capped")) {
		/* For dedicated - capped mode */
		physc = (delta_purr - delta_idle_purr) / delta_tb;
		idle = (delta_purr / delta_tb) - physc;
		idle *= 100.00;
	} else {
		/* For shared and dedicated - donate mode */
		idle = (delta_tb - delta_purr + delta_idle_purr) / delta_tb;
		idle *= 100.00;
	}

	sprintf(buf, "%.2f", idle);
}

void get_cpu_util_spurr(struct sysentry *unused_se, char *buf)
{
	double delta_tb, delta_spurr, delta_idle_spurr;
	double physc, rfreq;

	delta_tb = get_scaled_tb();
	delta_spurr = get_delta_value("spurr");
	delta_idle_spurr = get_delta_value("idle_spurr");

	physc = (delta_spurr - delta_idle_spurr) / delta_tb;
	physc *= 100.00;

	rfreq = round_off_freq();
	physc += ((physc * rfreq) / 100);

	sprintf(buf, "%.2f", physc);
}

void get_cpu_idle_spurr(struct sysentry *unused_se, char *buf)
{
	double delta_tb, delta_spurr, delta_idle_spurr;
	double physc, idle;
	double rfreq;
	char *descr;
	char mode[32];

	delta_tb = get_scaled_tb();
	delta_spurr = get_delta_value("spurr");
	delta_idle_spurr = get_delta_value("idle_spurr");

	get_sysdata("shared_processor_mode", &descr, mode);
	if (!strcmp(mode, "Dedicated"))
		get_sysdata("DedDonMode", &descr, mode);

	if (delta_spurr > delta_tb)
		delta_spurr = delta_tb;

	if (!strcmp(mode, "Capped")) {
		/* For dedicated - capped mode */
		physc = (delta_spurr - delta_idle_spurr) / delta_tb;
		idle = (delta_spurr / delta_tb) - physc;
		idle *= 100.00;
	} else {
		/* For shared and dedicated - donate mode */
		idle = (delta_tb - delta_spurr + delta_idle_spurr) / delta_tb;
		idle *= 100.00;
	}

	rfreq = round_off_freq();
	idle += ((idle * rfreq) / 100);

	sprintf(buf, "%.2f", idle);
}

int parse_lparcfg()
{
	FILE *f;
	char line[128];
	char *first_line;

	f = fopen(LPARCFG_FILE, "r");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", LPARCFG_FILE);
		return -1;
	}

	/* parse the file skipping the first line */
	first_line = fgets(line, 128, f);
	if (!first_line) {
		fclose(f);
		fprintf(stderr, "Could not read first line of %s\n",
			LPARCFG_FILE);
		return -1;
	}

	while (fgets(line, 128, f) != NULL) {
		char *name, *value, *nl;
		struct sysentry *se;

		if (line[0] == '\n')
			continue;

		name = &line[0];
		value = strchr(line, '=');
		*value = '\0';
		value++;

		nl = strchr(value, '\n');
		*nl = '\0';
		
		se = get_sysentry(name);
		if (se) {
			strncpy(se->value, value, SYSDATA_VALUE_SZ - 1);
			se->value[SYSDATA_VALUE_SZ - 1] = '\0';
		}
	}

	fclose(f);
	return 0;
}

int parse_proc_ints()
{
	FILE *f;
	char *line;
	size_t n = 0;
	char *value;
	struct sysentry *se;
	long long int phint = 0;
	const char *delim = " ";

	f = fopen("/proc/interrupts", "r");
	if (!f) {
		fprintf(stderr, "Could not open /proc/interrupts\n");
		return -1;
	}

	while (getline(&line, &n, f) != -1) {
		/* we just need the SPU line */
		if (!strstr(line, "SPU:"))
			continue;

		/* target line. omit the 'SPU:' */
		value = strtok(line, delim);
		if (!value)
			break;

		while ((value = strtok(NULL, delim)) && value[0] != 'S') {
			int v;
			v = atoi(value);
			phint += v;
		}

		/* skim the rest of the lines */
		break;
	}

	free(line);
	fclose(f);

	se = get_sysentry("phint");
	sprintf(se->value, "%lld", phint);

	return 0;
}

int parse_proc_stat()
{
	FILE *f;
	char line[128];
	char *value;
	int i, entries = 6;
	long long statvals[entries];
	struct sysentry *se;
	char *first_line;
	char *names[] = {"cpu_total", "cpu_user", "cpu_nice", "cpu_sys",
			 "cpu_idle", "cpu_iowait"};

	/* we just need the first line */
	f = fopen("/proc/stat", "r");
	if (!f) {
		fprintf(stderr, "Could not open /proc/stat\n");
		return -1;
	}

	first_line = fgets(line, 128, f);
	fclose(f);

	if (!first_line) {
		fprintf(stderr, "Could not read first line of /proc/stat\n");
		return -1;
	}

	statvals[0] = 0;
	value = line;
	for (i = 1; i <= (entries - 1); i++) {
		long long v;
		value = strchr(value, ' ') + 1;
		if (i == 1)
			value++;
		v = atoll(value);
		statvals[i] = v;
		statvals[0] += v;
	}

	for (i = 0; i < entries; i++) {
		se = get_sysentry(names[i]);
		sprintf(se->value, "%lld", statvals[i]);
	}

	se = get_sysentry("cpu_lbusy");
	sprintf(se->value, "%lld", statvals[1] + statvals[3]);

	return 0;
}

void get_processor_type(struct sysentry *se, char *buf)
{
	char *value = "?";

	if (se->value[0] == '1')
		value = "Shared";
	else 
		value = "Dedicated";

	sprintf(buf, "%s", value);
}
		
void get_capped_mode(struct sysentry *se, char *buf)
{
	char *value = "?";

	if (se->value[0] == '1')
		value = "Capped";
	else 
		value = "Uncapped";

	sprintf(buf, "%s", value);
}

void get_dedicated_mode(struct sysentry *se, char *buf)
{
	const char *value = "Capped";

	if (se->value[0] == '1')
		value = "Donating";

	sprintf(buf, "%s", value);
}

void get_percent_entry(struct sysentry *se, char *buf)
{
	float value;

	value = atoi(se->value);
	sprintf(buf, "%.2f", (value /100));
}

void get_phys_cpu_percentage(struct sysentry *se, char *buf)
{
	struct sysentry *tmp_se;
	int entcap, active;

	tmp_se = get_sysentry("DesEntCap");
	entcap = atoi(tmp_se->value);

	tmp_se = get_sysentry("partition_active_processors");
	active = atoi(tmp_se->value);

	sprintf(buf, "%d", entcap/active);
}

void get_active_cpus_in_pool(struct sysentry *se, char *buf)
{
	struct sysentry *tmp;

	tmp = get_sysentry("physical_procs_allocated_to_virtualization");
	if (tmp) {
		sprintf(buf, "%d", atoi(tmp->value));
	} else {
		tmp = get_sysentry("pool_capacity");
		sprintf(buf, "%d", atoi(tmp->value)/100);
	}
}

void get_memory_mode(struct sysentry *se, char *buf)
{
	struct sysentry *tmp;

	tmp = get_sysentry("entitled_memory_pool_number");
	/*
	 * from power10 onwards Active Memory Sharing(AMS) is not
	 * supported. Hence always display it as dedicated for those
	 */
	if (atoi(tmp->value) == 65535 || (getauxval(AT_HWCAP2) & PPC_FEATURE2_ARCH_3_1))
		sprintf(buf, "Dedicated");
	else
		sprintf(buf, "Shared");
}

void get_name(const char *file, char *buf)
{
	FILE *f;
	char tmpbuf[64];
	int rc;

	f = fopen(file, "r");
	if(!f) {
		sprintf(buf, "%c", '\0');
		return;
	}
	rc = fread(tmpbuf, 64, 1, f);
	fclose(f);

	if (!rc)
		sprintf(buf, "%s", tmpbuf);
}

void get_node_name(struct sysentry *se, char *buf)
{
	char *nl;

	get_name("/proc/sys/kernel/hostname", buf);

	/* For some reason this doesn't get null-terminated and makes
	 * for ugly output.
	 */
	nl = strchr(buf, '\n');
	*nl = '\0';
}

void get_partition_name(struct sysentry *se, char *buf)
{
	if (se->value[0] != '\0') {
		strcpy(buf, se->value);
		return;
	}
	return get_name("/proc/device-tree/ibm,partition-name", buf);
}

void get_mem_total(struct sysentry *se, char *buf)
{
	FILE *f;
	char line[128];
	char *mem, *nl, *first_line, *unit;

	f = fopen("/proc/meminfo", "r");
	if (!f) {
		fprintf(stderr, "Could not open /proc/meminfo\n");
		return;
	}

	first_line = fgets(line, 128, f);
	fclose(f);

	if (!first_line) {
		fprintf(stderr, "Could not read first line of /proc/meminfo\n");
		return;
	}

	mem = strchr(line, ':');
	do {
		mem++;
	} while (*mem == ' ');

	unit = strchr(mem, ' ');
	*unit = '\0';

	do {
		unit++;
	} while (*unit == ' ');

	nl = strchr(unit, '\n');
	*nl = '\0';

	if (o_legacy) {
		sprintf(buf, "%lld %s", atoll(mem) / 1024, "MB");
	} else {
		sprintf(buf, "%s %s", mem, unit);
	}
}

void get_smt_mode(struct sysentry *se, char *buf)
{
	int smt_state = 0;

	if (!is_smt_capable()) {
		sprintf(buf, "1");
		return;
	}

	smt_state = parse_smt_state();
	if (smt_state == -1) {
		fprintf(stderr, "Failed to get smt state\n");
		return;
	}

	if (smt_state == 1)
		sprintf(buf, "Off");
	else if (smt_state == 0)
		sprintf(buf, "Mixed");
	else
		sprintf(buf, "%d", smt_state);
}

void get_online_cores(void)
{
	struct sysentry *se;
	int *core_state;
	int online_cores = 0;
	int i;

	core_state = calloc(cpus_in_system, sizeof(int));
	if (!core_state) {
		fprintf(stderr, "Failed to read online cores\n");
		return;
	}

	for (i = 0; i < cpus_in_system; i++) {
		core_state[i] = (get_one_smt_state(i) > 0);
		if (core_state[i])
			online_cores++;
	}

	se = get_sysentry("online_cores");
	sprintf(se->value, "%d", online_cores);

	free(core_state);
}

void get_cpu_stat(struct sysentry *se, char *buf)
{
	float percent;
	long long total, delta_val;

	total = get_delta_value("cpu_total");
	delta_val = get_delta_value(se->name);
	percent = (delta_val/(long double)total) * 100;
	sprintf(buf, "%.2f", percent);
}

int has_cpu_topology_changed(void)
{
	int i, changed = 1;
	cpu_set_t *tmp_cpuset;
	size_t online_cpus_size = CPU_ALLOC_SIZE(threads_in_system);

	if (!online_cpus) {
		online_cpus = CPU_ALLOC(threads_in_system);
		if (!online_cpus) {
			fprintf(stderr, "Failed to allocate memory for cpu_set\n");
			return -1;
		}

		CPU_ZERO_S(online_cpus_size, online_cpus);

		for (i = 0; i < threads_in_system; i++) {
			if (!cpu_online(i))
				continue;
			CPU_SET_S(i, online_cpus_size, online_cpus);
		}

		return changed;
	}

	tmp_cpuset = CPU_ALLOC(threads_in_system);
	if (!tmp_cpuset) {
		fprintf(stderr, "Failed to allocate memory for cpu_set\n");
		return -1;
	}

	CPU_ZERO_S(online_cpus_size, tmp_cpuset);

	for (i = 0; i < threads_in_system; i++) {
		if (!cpu_online(i))
			continue;
		CPU_SET_S(i, online_cpus_size, tmp_cpuset);
	}

	changed = CPU_EQUAL_S(online_cpus_size, online_cpus, tmp_cpuset);

	CPU_FREE(online_cpus);
	online_cpus = tmp_cpuset;

	return changed;
}

void init_sysinfo(void)
{
	int rc = 0;

	/* probe one time system cpu information */
	rc = get_cpu_info(&threads_per_cpu, &cpus_in_system,
			  &threads_in_system);
	if (rc) {
		fprintf(stderr, "Failed to capture system CPUs information\n");
		exit(rc);
	}

	/* refer to init_sysdata for explanation */
	if (!o_scaled)
		return;

	get_online_cores();

	rc = get_nominal_frequency();
	if (rc)
		exit(rc);

	if (signal(SIGINT, sig_int_handler) == SIG_ERR) {
		fprintf(stderr, "Failed to add signal handler\n");
		exit(-1);
	}

	rc = assign_cpu_sysfs_fds(threads_in_system);
	if (rc)
		exit(rc);
}

void init_sysdata(void)
{
	int rc = 0;

	get_time_ns();
	parse_lparcfg();
	parse_proc_stat();
	parse_proc_ints();
	get_time_base();

	/* Skip reading spurr, purr, idle_{purr,spurr} and calculating
	 * effective frequency for default option */
	if (!o_scaled)
		return;

	rc = has_cpu_topology_changed();
	if (!rc)
		goto cpu_hotplug_restart;

	rc = parse_sysfs_values();
	if (rc == -1)
		exit(rc);
	else if (rc)
		goto cpu_hotplug_restart;

	get_effective_frequency();

	return;

cpu_hotplug_restart:
	close_cpu_sysfs_fds(threads_in_system);
	init_sysinfo();
}

void update_sysdata(void)
{
	struct sysentry *se = &system_data[0];
	while (se->name[0] != '\0') {
		memcpy(se->old_value, se->value, SYSDATA_VALUE_SZ);
		se++;
	}
	
	init_sysdata();
}

int print_iflag_data()
{
	char *fmt = "%-45s: %s\n";
	char value[64];
	char *descr;
	int i = 0;

	while (iflag_entries[i] != NULL) {
		get_sysdata(iflag_entries[i], &descr, value);
#ifndef DEBUG
		if (strcmp(value, SE_NOT_VALID) && strcmp(value, SE_NOT_FOUND))
#endif
			fprintf(stdout, fmt, descr, value);
		i++;
	}

	return 0;
}

void print_system_configuration(void)
{
	char *descr;
	char buf[128];
	int offset, smt, active_proc;
	char type[32];
	char value[32];

	memset(buf, 0, 128);
	get_sysdata("shared_processor_mode", &descr, value);
	offset = sprintf(buf, "type=%s ", value);
	sprintf(type, "%s", value);
	if (!strcmp(value, "Dedicated"))
		get_sysdata("DedDonMode", &descr, value);
	else
		get_sysdata("capped", &descr, value);
	offset += sprintf(buf + offset, "mode=%s ", value);
	get_sysdata("smt_state", &descr, value);
	offset += sprintf(buf + offset, "smt=%s ", value);
	if (!strcmp(value, "Off"))
		smt = 1;
	else
		smt = atoi(value);
	get_sysdata("partition_active_processors", &descr, value);
	active_proc = atoi(value);
	if (o_legacy)
		offset += sprintf(buf + offset, "lcpu=%d ", active_proc*smt);
	else
		offset += sprintf(buf + offset, "lcpu=%s ", value);
	get_sysdata("MemTotal", &descr, value);
	offset += sprintf(buf + offset, "mem=%s ", value);
	get_sysdata("active_cpus_in_pool", &descr, value);
	if (o_legacy) {
		if (strcmp(type, "Dedicated"))
			offset += sprintf(buf + offset, "psize=%s ", value);
	} else {
		offset += sprintf(buf + offset, "cpus=%s ", value);
	}
	get_sysdata("DesEntCap", &descr, value);
	offset += sprintf(buf + offset, "ent=%s ", value);

	fprintf(stdout, "\nSystem Configuration\n%s\n\n", buf);
}

void print_default_output(int interval, int count)
{
	char *fmt = "%5s %5s %5s %8s %8s %5s %5s %5s %5s %5s\n";
	char *descr;
	char user[32], sys[32], wait[32], idle[32], physc[32], entc[32];
	char lbusy[32], app[32], vcsw[32], phint[32];

	print_system_configuration();

	fprintf(stdout, fmt, "\%user", "\%sys", "\%wait", "\%idle", "physc",
		"\%entc", "lbusy", "app", "vcsw", "phint");
	fprintf(stdout, fmt, "-----", "-----", "-----", "-----", "-----",
		"-----", "-----", "-----", "-----", "-----");

	do {
		if (interval) {
			sleep(interval);
			update_sysdata();
		}

		get_sysdata("cpu_user", &descr, user);
		get_sysdata("cpu_sys", &descr, sys);
		get_sysdata("cpu_iowait", &descr, wait);
		get_sysdata("cpu_idle", &descr, idle);
		get_sysdata("cpu_lbusy", &descr, lbusy);
		get_sysdata("dispatches", &descr, vcsw);
		get_sysdata("physc", &descr, physc);
		get_sysdata("per_entc", &descr, entc);
		get_sysdata("phint", &descr, phint);
		get_sysdata("app", &descr, app);

		fprintf(stdout, fmt, user, sys, wait, idle, physc, entc,
			lbusy, app, vcsw, phint);
		fflush(stdout);
	} while (--count > 0);
}

void print_scaled_output(int interval, int count)
{
	char purr[32], purr_idle[32], spurr[32], spurr_idle[32];
	char nominal_f[32], effective_f[32];
	double nominal_freq, effective_freq;
	char *descr;

	print_system_configuration();

	fprintf(stdout, "---Actual---                 -Normalized-\n");
	fprintf(stdout, "%%busy  %%idle   Frequency     %%busy  %%idle\n");
	fprintf(stdout, "------ ------  ------------- ------ ------\n");
	do {
		if (interval) {
			sleep(interval);
			update_sysdata();
		}

		get_sysdata("purr_cpu_util", &descr, purr);
		get_sysdata("purr_cpu_idle", &descr, purr_idle);
		get_sysdata("spurr_cpu_util", &descr, spurr);
		get_sysdata("spurr_cpu_idle", &descr, spurr_idle);
		get_sysdata("nominal_freq", &descr, nominal_f);
		get_sysdata("effective_freq", &descr, effective_f);
		nominal_freq = strtod(nominal_f, NULL);
		effective_freq = strtod(effective_f, NULL);

		fprintf(stdout, "%6s %6s %5.2fGHz[%3d%%] %6s %6s\n",
			purr, purr_idle,
			effective_freq/1000,
			(int)((effective_freq/nominal_freq * 100)+ 0.44 ),
			spurr, spurr_idle );
		fflush(stdout);
	} while (--count > 0);
}

static void print_security_flavor(void)
{
	char value[64];
	char *descr;

	get_sysdata("security_flavor", &descr, value);
	fprintf(stdout, "%-45s: %s\n", descr, value);
}

static void usage(void)
{
	printf("Usage:  lparstat [ options ]\n\tlparstat <interval> [ count ]\n\n"
	       "options:\n"
	       "\t-h, --help		Show this message and exit.\n"
	       "\t-V, --version	\tDisplay lparstat version information.\n"
	       "\t-i			Lists details on the LPAR configuration.\n"
	       "\t-x			Print the security mode settings for the LPAR.\n"
	       "\t-E			Print SPURR metrics.\n"
	       "\t-l, --legacy		Print the report in legacy format.\n"
	       "interval		The interval parameter specifies the amount of time between each report.\n"
	       "count			The count parameter specifies how many reports will be displayed.\n");
}

static struct option long_opts[] = {
	{"version",	no_argument,	NULL,	'V'},
	{"help",	no_argument,	NULL,	'h'},
	{"legacy",	no_argument,	NULL,	'l'},
	{0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
	int interval = 0, count = 0;
	int c, opt_index = 0;
	int i_option = 0;

	if (get_platform() != PLATFORM_PSERIES_LPAR) {
		fprintf(stderr, "%s: is not supported on the %s platform\n",
							argv[0], platform_name);
		exit(1);
	}

	while ((c = getopt_long(argc, argv, "iEVhlx",
				long_opts, &opt_index)) != -1) {
		switch(c) {
			case 'i':
				i_option = 1;
				break;
			case 'l':
				o_legacy = true;
				break;
			case 'E':
				o_scaled = true;
				break;
			case 'V':
				printf("lparstat - %s\n", VERSION);
				return 0;
			case 'x':
				o_security = true;
				break;
			case 'h':
				usage();
				return 0;
			case '?':
				usage();
				return 1;
			default:
				break;
		}
	}

	/* see if there is an interval specified */
	if (optind < argc)
		interval = atoi(argv[optind++]);

	/* check for count specified */
	if (optind < argc)
		count = atoi(argv[optind++]);

	init_sysinfo();
	init_sysdata();

	if (i_option)
		print_iflag_data();
	else if (o_security)
		print_security_flavor();
	else if (o_scaled) {
		print_scaled_output(interval, count);
		close_cpu_sysfs_fds(threads_in_system);
	} else {
		print_default_output(interval, count);
	}
	return 0;
}
