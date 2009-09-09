/**
 * Copyright (C) 2007 Anton Blanchard <anton@au.ibm.com> IBM Corporation
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <librtas.h>
#include <sys/stat.h>
#define _GNU_SOURCE
#include <getopt.h>

#define SYSFS_CPUDIR	"/sys/devices/system/cpu/cpu%d"
#define SYSFS_PATH_MAX	128
#define MAX_THREADS	1024

#define DIAGNOSTICS_RUN_MODE	42

int threads_per_cpu;

int get_attribute(char *path, int *value)
{
	FILE *fp;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;

	fscanf(fp, "%i", value);
	fclose(fp);

	return 0;
}

int set_attribute(char *path, int value)
{
	FILE *fp;

	fp = fopen(path, "w");
	if (fp == NULL)
		return -1;

	fprintf(fp, "%d", value);
	fclose(fp);

	return 0;
}

int get_threads_per_cpu(void)
{
	DIR *d;
	struct dirent *de;
	int nthreads;
	int rc;

	d = opendir("/proc/device-tree/cpus");
	if (!d)
		return -1;

	while ((de = readdir(d)) != NULL) {
		if (!strncmp(de->d_name, "PowerPC", 7)) {
			struct stat sbuf;
			char path[128];

			sprintf(path, "/proc/device-tree/cpus/%s/ibm,ppc-interrupt-server#s",
				de->d_name);
			rc = stat(path, &sbuf);
			if (!rc)
				nthreads = sbuf.st_size / 4;

			break;
		}
	}

	closedir(d);
	return nthreads;
}

int is_smt_capable(void)
{
	struct stat sb;
	char path[SYSFS_PATH_MAX];
	int i;

	for (i = 0; i < MAX_THREADS; i++) {
		sprintf(path, SYSFS_CPUDIR"/smt_snooze_delay", i);
		if (stat(path, &sb))
			continue;
		return 1;
	}

	return 0;
}

int get_one_smt_state(int primary_thread)
{
	struct stat sb;
	char online_file[SYSFS_PATH_MAX];
	int thread_state;
	int smt_state = 0;
	int i, rc;

	for (i = 0; i < threads_per_cpu; i++) {
		sprintf(online_file, SYSFS_CPUDIR"/%s", primary_thread + i, "online");
		if (stat(online_file, &sb))
			return -1;

		rc = get_attribute(online_file, &thread_state);
		if (rc)
			return -1;

		smt_state += thread_state;
	}

	return smt_state ? smt_state : -1;
}

int get_smt_state(void)
{
	int system_state = -1;
	int i;

	for (i = 0; i < MAX_THREADS; i += threads_per_cpu) {
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
		snprintf(path, SYSFS_PATH_MAX, SYSFS_CPUDIR"/%s", thread + i, "online");
		rc = set_attribute(path, 1);
		if (rc)
			return rc;
	}

	for (; i < threads_per_cpu; i++) {
		snprintf(path, SYSFS_PATH_MAX, SYSFS_CPUDIR"/%s", thread + i, "online");
		rc = set_attribute(path, 0);
		if (rc)
			break;
	}

	return rc;
}

int set_smt_state(int smt_state)
{
	int i, rc;

	for (i = 0; i < MAX_THREADS; i += threads_per_cpu) {
		rc = set_one_smt_state(i, smt_state);
		if (rc)
			break;
	}

	return rc;
}

int is_dscr_capable(void)
{
	struct stat sb;
	char path[SYSFS_PATH_MAX];
	int i;

	for (i = 0; i < MAX_THREADS; i++) {
		sprintf(path, SYSFS_CPUDIR"/dscr", i);
		if (stat(path, &sb))
			continue;
		return 1;
	}

	return 0;
}

int cpu_online(int thread)
{
	char path[SYSFS_PATH_MAX];
	int rc, online;

	sprintf(path, SYSFS_CPUDIR"/online", thread);
	rc = get_attribute(path, &online);
	if (rc || !online)
		return 0;

	return 1;
}

int get_system_attribute(char *attribute)
{
	char path[SYSFS_PATH_MAX];
	int i, rc;
	int system_attribute = -1;

	for (i = 0; i < MAX_THREADS; i++) {
		int cpu_attribute;

		/* only check online cpus */
		if (!cpu_online(i))
			continue;

		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = get_attribute(path, &cpu_attribute);
		if (rc)
			continue;

		if (system_attribute == -1)
			system_attribute = cpu_attribute;
		else if (system_attribute != cpu_attribute)
			return -1;
	}

	return system_attribute;
}

int set_system_attribute(char *attribute, int state)
{
	char path[SYSFS_PATH_MAX];
	int i, rc;

	for (i = 0; i < MAX_THREADS; i++) {
		/* only set online cpus */
		if (!cpu_online(i))
			continue;

		sprintf(path, SYSFS_CPUDIR"/%s", i, attribute);
		rc = set_attribute(path, state);
		if (rc)
			return -1;
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

		if ((smt_state == 0) || (smt_state > threads_per_cpu)) {
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
		int dscr = get_system_attribute("dscr");
		if (dscr == -1)
			printf("Inconsistent DSCR\n");
		else
			printf("dscr is %d\n", dscr);
	} else
		rc = set_system_attribute("dscr", strtol(state, NULL, 0));

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
		int ssd = get_system_attribute("smt_snooze_delay");
		if (ssd == -1)
			printf("Inconsistent smt_snooze_delay\n");
		else
			printf("smt_snooze_delay is %d\n", ssd);
	} else {
		int delay;

		if (!strcmp(state, "off"))
			delay = -1;
		else
			delay = strtol(state, NULL, 0);

		rc = set_system_attribute("smt_snooze_delay", delay);
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
			if (rc == -3)
				printf("Machine does not support diagnostic run mode\n");
			else
				printf("Could not retrieve current diagnostics mode\n");
		} else
			printf("run-mode=%c\n", mode[2]);
	} else {
		signed char rmode = *run_mode;

		if (rmode < 0 || rmode > 3) {
			printf("Invalid run-mode=%c\n", rmode);
			return -1;
		}

		*(short *)mode = 1;
		mode[2] = rmode;

		rc = rtas_set_sysparm(DIAGNOSTICS_RUN_MODE, mode);
		if (rc) {
			if (rc == -3)
				printf("Machine does not support diagnostic run mode\n");
			else
				printf("Could not set diagnostics mode\n");
		}
	}

	return rc;
}

void usage(void)
{
	printf("\tppc64_cpu --smt			# Get current SMT state\n"
	       "\tppc64_cpu --smt={on|off}	# Turn SMT on/off\n\n"
	       "\tppc64_cpu --smt=X		# Set SMT state to X\n\n"
	       "\tppc64_cpu --dscr		# Get current DSCR setting\n"
	       "\tppc64_cpu --dscr=<val>		# Change DSCR setting\n\n"
	       "\tppc64_cpu --smt-snooze-delay	# Get current smt-snooze-delay setting\n"
	       "\tppc64_cpu --smt-snooze-delay=<val> # Change smt-snooze-delay setting\n\n"
	       "\tppc64_cpu --run-mode		# Get current diagnostics run mode\n"
	       "\tppc64_cpu --run-mode=<val>	# Set current diagnostics run mode\n\n");
}

struct option longopts[] = {
	{"smt",			optional_argument, NULL, 's'},
	{"dscr",		optional_argument, NULL, 'd'},
	{"smt-snooze-delay",	optional_argument, NULL, 'S'},
	{"run-mode",		optional_argument, NULL, 'r'},
	{0,0,0,0}
};

int main(int argc, char *argv[])
{
	int rc, opt;
	int option_index;

	if (argc == 1) {
		usage();
		return 0;
	}

	threads_per_cpu = get_threads_per_cpu();
	if (threads_per_cpu < 0) {
		printf("Could not determine thread count\n");
		return -1;
	}

	while (1) {
		opt = getopt_long(argc, argv, "s::d::S::r::", longopts, &option_index);
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

		    default:
			usage();
			break;
		}
	}

	return rc;
}
