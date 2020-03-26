/**
 * @file vcpustat.c
 * @brief vcpustat command
 *
 * Copyright (c) 2020 International Business Machines
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
 * @author Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 * 	   Adapted from 'lparstat.c'
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "pseries_platform.h"

#define VCPUSTAT_FILE	"/proc/powerpc/vcpudispatch_stats"
#define NR_CPUS 4096

struct vcpudispatch_stat {
	unsigned long idx;
	int total;
	int same_cpu;
	int same_chip;
	int same_package;
	int diff_package;
	int home_numa_node;
	int next_numa_node;
	int far_numa_node;
};

unsigned long idx;
int retain_stats, numeric_stats, raw_stats, stats_off, intr;

int read_stats(struct vcpudispatch_stat stats[])
{
	struct vcpudispatch_stat stat;
	int cpu, rc = -1;
	char buf[144];
	FILE *f;

	f = fopen(VCPUSTAT_FILE, "r");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", VCPUSTAT_FILE);
		return -1;
	}

	if ((fgets(buf, 144, f)) == NULL) {
		fprintf(stderr, "Could not read %s\n", VCPUSTAT_FILE);
		goto out;
	}

	if (!strncmp(buf, "off", 3)) {
		stats_off = 1;
		rc = 0; /* not an error */
		goto out;
	} else
		stats_off = 0;

	idx++;

	do {
		rc = sscanf(buf, "cpu%d %d %d %d %d %d %d %d %d", &cpu,
				  &stat.total, &stat.same_cpu, &stat.same_chip,
				  &stat.same_package, &stat.diff_package,
				  &stat.home_numa_node, &stat.next_numa_node,
				  &stat.far_numa_node);
		if (rc != 9) {
			fprintf(stderr, "Error parsing %s\n", VCPUSTAT_FILE);
			goto out;
		}
		if (cpu < 0 || cpu >= NR_CPUS) {
			fprintf(stderr, "Cpu (%d) out of range\n", cpu);
			goto out;
		}
		stat.idx = idx;
		stats[cpu] = stat;
	} while (!intr && (fgets(buf, 144, f)) != NULL);

	rc = 0;

out:
	fclose(f);
	return rc;
}

void print_alltime_stats(struct vcpudispatch_stat stats[])
{
	char raw_header1[] = "%22s %43s | %32s\n";
	char raw_header2[] = "%-7s | %10s | %10s %10s %10s %10s | %10s %10s %10s\n";
	char raw_fmt[] = "cpu%-4d | %10d | %10d %10d %10d %10d | %10d %10d %10d\n";
	int i;

	printf(raw_header1, " ",
				"========== dispatch dispersions ==========",
				"======= numa dispersions =======");
	printf(raw_header2, "cpu", "total",
					"core", "chip", "socket", "cec",
					"home", "adj", "far");

	for (i = 0; i < NR_CPUS; i++) {
		if (!stats[i].idx)
			continue;

		printf(raw_fmt, i,
			stats[i].total, stats[i].same_cpu, stats[i].same_chip,
			stats[i].same_package, stats[i].diff_package,
			stats[i].home_numa_node, stats[i].next_numa_node,
			stats[i].far_numa_node);
	}

	printf("\n");
	fflush(stdout);
}

void print_stats(struct vcpudispatch_stat stats1[],
		 struct vcpudispatch_stat stats2[])
{
	char percent_header1[] = "%35s | %20s\n";
	char percent_header2[] = "%-7s %6s %6s %6s %6s | %6s %6s %6s\n";
	char percent_fmt[] = "cpu%-4d %6.2f %6.2f %6.2f %6.2f | %6.2f %6.2f %6.2f\n";
	char raw_header1[] = "%22s %43s | %32s\n";
	char raw_header2[] = "%-7s | %10s | %10s %10s %10s %10s | %10s %10s %10s\n";
	char raw_fmt[] = "cpu%-4d | %10d | %10d %10d %10d %10d | %10d %10d %10d\n";
	struct vcpudispatch_stat stat;
	int i;

	if (stats_off) {
		printf("off\n");
		return;
	}

	if (numeric_stats || raw_stats) {
		printf(raw_header1, " ",
				"========== dispatch dispersions ==========",
				"======= numa dispersions =======");
		printf(raw_header2, "cpu", "total",
					"core", "chip", "socket", "cec",
					"home", "adj", "far");
	} else {
		printf(percent_header1, "         == dispatch dispersions ==",
					"= numa dispersions =");
		printf(percent_header2, "cpu", "core", "chip", "socket", "cec",
					"home", "adj", "far");
	}

	for (i = 0; i < NR_CPUS; i++) {
		if (stats2[i].idx != idx || stats1[i].idx != (idx - 1))
			continue;

		if (!raw_stats) {
			stat.total = stats2[i].total - stats1[i].total;
			stat.same_cpu = stats2[i].same_cpu - stats1[i].same_cpu;
			stat.same_chip = stats2[i].same_chip - stats1[i].same_chip;
			stat.same_package = stats2[i].same_package - stats1[i].same_package;
			stat.diff_package = stats2[i].diff_package - stats1[i].diff_package;
			stat.home_numa_node = stats2[i].home_numa_node - stats1[i].home_numa_node;
			stat.next_numa_node = stats2[i].next_numa_node - stats1[i].next_numa_node;
			stat.far_numa_node = stats2[i].far_numa_node - stats1[i].far_numa_node;
		}

		if (numeric_stats)
			printf(raw_fmt, i,
				stat.total, stat.same_cpu, stat.same_chip,
				stat.same_package, stat.diff_package,
				stat.home_numa_node, stat.next_numa_node,
				stat.far_numa_node);
		else if (raw_stats)
			printf(raw_fmt, i,
				stats2[i].total, stats2[i].same_cpu, stats2[i].same_chip,
				stats2[i].same_package, stats2[i].diff_package,
				stats2[i].home_numa_node, stats2[i].next_numa_node,
				stats2[i].far_numa_node);
		else
			printf(percent_fmt, i,
				100 * (float)stat.same_cpu / stat.total,
				100 * (float)stat.same_chip / stat.total,
				100 * (float)stat.same_package / stat.total,
				100 * (float)stat.diff_package / stat.total,
				100 * (float)stat.home_numa_node / stat.total,
				100 * (float)stat.next_numa_node / stat.total,
				100 * (float)stat.far_numa_node / stat.total);
	}

	printf("\n");
	fflush(stdout);
}

void process_stats(int interval, int count)
{
	struct vcpudispatch_stat *stats1, *stats2, *stats_tmp;
	int rc, dec = count;

	stats1 = calloc(NR_CPUS, sizeof(struct vcpudispatch_stat));
	stats2 = calloc(NR_CPUS, sizeof(struct vcpudispatch_stat));

	if (!stats1 || !stats2) {
		fprintf(stderr, "Error allocating memory for stats\n");
		return;
	}

	rc = read_stats(stats1);
	if (rc)
		goto out;
	sleep(interval);

	while (!intr) {
		rc = read_stats(stats2);
		if (rc)
			goto out;

		print_stats(stats1, stats2);

		stats_tmp = stats2;
		stats2 = stats1;
		stats1 = stats_tmp;

		if (count) {
			dec--;
			if (!dec)
				break;
		}

		if (!intr)
			sleep(interval);
	}

out:
	free(stats1);
	free(stats2);
}

void display_raw_counts(void)
{
	struct vcpudispatch_stat *stats;
	int rc;

	stats = calloc(NR_CPUS, sizeof(struct vcpudispatch_stat));

	if (!stats) {
		fprintf(stderr, "Error allocating memory for stats\n");
		return;
	}

	rc = read_stats(stats);
	if (rc)
		goto out;

	if (stats_off) {
		fprintf(stderr,
			"Dispatch statistics are not enabled. Please specify an interval to monitor.\n");
		goto out;
	}

	print_alltime_stats(stats);

out:
	free(stats);
}

int init_stats(bool enable, bool user_requested)
{
	char buf[144];
	int rc = 0;
	FILE *f;

	f = fopen(VCPUSTAT_FILE, "r+");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", VCPUSTAT_FILE);
		return -1;
	}

	if ((fgets(buf, 144, f)) == NULL) {
		fprintf(stderr, "Could not read %s\n", VCPUSTAT_FILE);
		rc = -1;
		goto out;
	}

	if (strncmp(buf, "off", 3))
		retain_stats = 1;

	if (user_requested) {
		if (enable && retain_stats) {
			fprintf(stderr, "Stats are already enabled!\n");
			goto out;
		} else if (!enable && !retain_stats) {
			fprintf(stderr, "Stats are already disabled!\n");
			goto out;
		}
	}

	/* Enable stats */
	if (fprintf(f, "%d", enable) <= 0) {
		fprintf(stderr, "Couldn't setup stats: error writing to %s\n",
				VCPUSTAT_FILE);
		rc = -1;
	}

out:
	fclose(f);
	return rc;
}

void disable_stats(void)
{
	FILE *f;

	if (retain_stats)
		return;

	f = fopen(VCPUSTAT_FILE, "w");
	if (!f) {
		fprintf(stderr, "Couldn't disable stats: error opening %s\n",
			VCPUSTAT_FILE);
		return;
	}

	if (fprintf(f, "0") <= 0)
		fprintf(stderr, "Couldn't disable stats: error writing to %s\n",
			VCPUSTAT_FILE);

	fclose(f);
}

static void sighandler(int signum)
{
	intr = 1;
}

static void usage(void)
{
	printf("Usage:  vcpustat [ options ] [ <interval> [ count ] ]\n\n"
	       "options:\n"
	       "\t-e, --enable          Enable gathering statistics.\n"
	       "\t-d, --disable         Disable gathering statistics.\n"
	       "\t-n, --numeric         Display the statistics in numbers, rather than percentage.\n"
	       "\t-r, --raw             Display the raw counts, rather than the difference in an interval.\n"
	       "\t-h, --help            Show this message and exit.\n"
	       "\t-V, --version         Display vcpustat version information.\n"
	       "\tinterval              The interval parameter specifies the amount of time between each report.\n"
	       "\tcount                 The count parameter specifies how many reports will be displayed.\n");
}

static struct option long_opts[] = {
	{"version",	no_argument,		NULL,	'V'},
	{"help",	no_argument,		NULL,	'h'},
	{"enable",	no_argument,		NULL,	'e'},
	{"disable",	no_argument,		NULL,	'd'},
	{"numeric",	no_argument,		NULL,	'n'},
	{"raw",		no_argument,		NULL,	'r'},
	{0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
	bool enable_only = false, disable_only = false;
	int platform = get_platform();
	int interval = 0, count = 0;
	struct sigaction sa;
	int c, opt_idx = 0;

	if (platform != PLATFORM_PSERIES_LPAR ||
			access(VCPUSTAT_FILE, F_OK) == -1) {
		if (platform == PLATFORM_PSERIES_LPAR)
			fprintf(stderr, "%s is not supported on this LPAR\n",
							argv[0]);
		else
			fprintf(stderr, "%s is not supported on the %s platform\n",
							argv[0], platform_name);
		exit(1);
	}

	while ((c = getopt_long(argc, argv, "Vhnred",
				long_opts, &opt_idx)) != -1) {
		switch (c) {
		case 'V':
			printf("vcpustat - %s\n", VERSION);
			return 0;
		case 'h':
			usage();
			return 0;
		case '?':
			usage();
			return 1;
		case 'e':
			enable_only = true;
			break;
		case 'd':
			disable_only = true;
			break;
		case 'n':
			numeric_stats = 1;
			break;
		case 'r':
			raw_stats = 1;
			break;
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

	if (interval < 0 || count < 0) {
		fprintf(stderr, "Invalid interval/count specified\n");
		return -1;
	}

	if (enable_only && disable_only) {
		fprintf(stderr, "Please select only one of -e and -d\n");
		return -1;
	}

	if ((enable_only || disable_only) && (raw_stats || numeric_stats || interval)) {
		fprintf(stderr, "-e|-d cannot be used with other options\n");
		return -1;
	}

	if (enable_only || disable_only)
		return init_stats(enable_only, true);

	if (!interval) {
		display_raw_counts();
		return 0;
	}

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sighandler;

	if (sigaction(SIGINT, &sa, NULL) == -1) {
		fprintf(stderr, "Unable to setup signal handler\n");
		return -1;
	}

	if (init_stats(true, false))
		return -1;

	process_stats(interval, count);

	disable_stats();

	return 0;
}
