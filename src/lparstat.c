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
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "lparstat.h"
#include "pseries_platform.h"

#define LPARCFG_FILE	"/proc/ppc64/lparcfg"
#define SE_NOT_FOUND	"???"
#define SE_NOT_VALID	"-"

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

void get_time()
{
	struct timeval t;
	struct sysentry *se;

	gettimeofday(&t, 0);

	se = get_sysentry("time");
	sprintf(se->value, "%lld",
		(long long)t.tv_sec * 1000000LL + (long long)t.tv_usec);
}

long long elapsed_time()
{
	long long newtime, oldtime = 0;
	struct sysentry *se;

	se = get_sysentry("time");
	newtime = strtoll(se->value, NULL, 0);
	oldtime = strtoll(se->old_value, NULL, 0);

	return newtime - oldtime;
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

void get_cpu_physc(struct sysentry *unused_se, char *buf)
{
	struct sysentry *se;
	float elapsed;
	float new_purr, old_purr;
	float timebase, physc;

	elapsed = elapsed_time() / 1000000.0;

	se = get_sysentry("timebase");
	timebase = atoi(se->value);

	se = get_sysentry("purr");
	new_purr = strtoll(se->value, NULL, 0);
	old_purr = strtoll(se->old_value, NULL, 0);

	physc = (new_purr - old_purr)/timebase/elapsed;
	sprintf(buf, "%.6f", physc);
}

void get_per_entc(struct sysentry *unused_se, char *buf)
{
	char *descr;
	char physc[32];
	char entc[32];

	get_sysdata("DesEntCap", &descr, entc);
	get_sysdata("physc", &descr, physc);

	sprintf(buf, "%.6f", atof(physc) / atof(entc) * 100.0);
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
		if (se)
			strncpy(se->value, value, SYSDATA_VALUE_SZ);
	}

	fclose(f);
	return 0;
}

int parse_proc_ints()
{
	FILE *f;
	char *line, *p;
	size_t n = 0;
	char *value;
	struct sysentry *se;
	long long int phint = 0;

	f = fopen("/proc/interrupts", "r");
	if (!f) {
		fprintf(stderr, "Could not open /proc/interrupts\n");
		return -1;
	}

	while (getline(&line, &n, f) != -1) {
		p = line;
		while (*p == ' ')
			p++;

		/* we just need the SPU line */
		if (p[0] != 'S' || p[1] != 'P' || p[2] != 'U')
			continue;

		for (value = &p[5]; value[2] != 'S'; value += 11) {
			int v;
			v = atoi(value);
			phint += v;
		}

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

void get_smt_state(struct sysentry *se, char *buf)
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
	if (atoi(tmp->value) == 65535)
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
	return get_name("/proc/device-tree/ibm,partition-name", buf);
}

void get_mem_total(struct sysentry *se, char *buf)
{
	FILE *f;
	char line[128];
	char *mem, *nl, *first_line;

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

	nl = strchr(mem, '\n');
	*nl = '\0';

	sprintf(buf, "%s", mem);
}

void get_smt_mode(struct sysentry *se, char *buf)
{
	FILE *f;
	char line[128];
	char *cmd = "/usr/sbin/ppc64_cpu --smt";
	char *first_line;

	f = popen(cmd, "r");
	if (!f) {
		fprintf(stderr, "Failed to execute %s\n", cmd);
		return;
	}

	first_line = fgets(line, 128, f);
	pclose(f);

	if (!first_line) {
		fprintf(stderr, "Could not read output of %s\n", cmd);
		return;
	}

	/* The output is either "SMT=x" or "SMT is off", we can cheat
	 * by looking at line[8] for an 'f'.
	 */
	if (line[8] == 'f')
		sprintf(buf, "Off");
	else
		sprintf(buf, "%c", line[4]);
}

long long get_cpu_time_diff()
{
	long long old_total = 0, new_total = 0;
	struct sysentry *se;

	se = get_sysentry("cpu_total");
	new_total = strtoll(se->value, NULL, 0);
	old_total = strtoll(se->old_value, NULL, 0);

	return new_total - old_total;
}

void get_cpu_stat(struct sysentry *se, char *buf)
{
	float percent;
	long long total, old_val, new_val;

	total = get_cpu_time_diff();
	new_val = atoll(se->value);
	old_val = atoll(se->old_value);
	percent = ((new_val - old_val)/(long double)total) * 100;
	sprintf(buf, "%.2f", percent);
}

void init_sysdata(void)
{
	get_time();
	parse_lparcfg();
	parse_proc_stat();
	parse_proc_ints();
	get_time_base();
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

void print_default_output(int interval, int count)
{
	char *fmt = "%5s %5s %5s %8s %8s %5s %5s %5s %5s\n";
	char *descr;
	char buf[128];
	int offset;
	char value[32];
	char user[32], sys[32], wait[32], idle[32], physc[32], entc[32];
	char lbusy[32], vcsw[32], phint[32];

	memset(buf, 0, 128);
	get_sysdata("shared_processor_mode", &descr, value);
	offset = sprintf(buf, "type=%s ", value);
	get_sysdata("capped", &descr, value);
	offset += sprintf(buf + offset, "mode=%s ", value);
	get_sysdata("smt_state", &descr, value);
	offset += sprintf(buf + offset, "smt=%s ", value);
	get_sysdata("partition_active_processors", &descr, value);
	offset += sprintf(buf + offset, "lcpu=%s ", value);
	get_sysdata("MemTotal", &descr, value);
	offset += sprintf(buf + offset, "mem=%s ", value);
	get_sysdata("active_cpus_in_pool", &descr, value);
	offset += sprintf(buf + offset, "cpus=%s ", value);
	get_sysdata("DesEntCap", &descr, value);
	offset += sprintf(buf + offset, "ent=%s ", value);

	fprintf(stdout, "\nSystem Configuration\n%s\n\n", buf);

	fprintf(stdout, fmt, "\%user", "\%sys", "\%wait", "\%idle", "physc",
		"\%entc", "lbusy", "vcsw", "phint");
	fprintf(stdout, fmt, "-----", "-----", "-----", "-----", "-----",
		"-----", "-----", "-----", "-----");

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

		fprintf(stdout, fmt, user, sys, wait, idle, physc, entc,
			lbusy, vcsw, phint);
		fflush(stdout);
	} while (--count > 0);
}

int main(int argc, char *argv[])
{
	int interval = 0, count = 0;
	int c;
	int i_option = 0;

	if (get_platform() != PLATFORM_PSERIES_LPAR) {
		fprintf(stderr, "%s: is not supported on the %s platform\n",
							argv[0], platform_name);
		exit(1);
	}

	while ((c = getopt(argc, argv, "i")) != -1) {
		switch(c) {
			case 'i':
				i_option = 1;
				break;
			case '?':
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

	init_sysdata();

	if (i_option)
		print_iflag_data();
	else
		print_default_output(interval, count);

	return 0;
}
