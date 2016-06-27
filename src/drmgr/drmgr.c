/**
 * @file drmgr.c
 *
 * Copyright (C) IBM Corporation 2006
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
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "dr.h"
#include "pseries_platform.h"

#define DRMGR_ARGS	"ac:d:Iimnp:P:Qq:Rrs:w:t:hCV"

int output_level = 1; /* default to lowest output level */

int log_fd = 0;
int action_cnt = 0;

static int display_capabilities = 0;
static int handle_prrn_event = 0;
static int display_usage = 0;

typedef int (cmd_func_t)(struct options *);
typedef int (cmd_args_t)(struct options *);
typedef void (cmd_usage_t)(char **);

int drmgr(struct options *);
void drmgr_usage(char **);
int valid_drmgr_options(struct options *);

struct command {
	cmd_func_t	*func;
	cmd_args_t	*validate_options;
	cmd_usage_t	*usage;
};

#define DRMGR			0
#define DRSLOT_CHRP_SLOT	1
#define DRSLOT_CHRP_PHB		2
#define DRSLOT_CHRP_PCI		3
#define DRSLOT_CHRP_MEM		4
#define DRSLOT_CHRP_HEA		5
#define DRSLOT_CHRP_CPU		6
#define DRMIG_CHRP_PMIG		7
#define DRSLOT_CHRP_PHIB	8

static struct command commands[] = {
	{ .func = drmgr,
	  .validate_options = valid_drmgr_options,
	  .usage = drmgr_usage,
	},
	{ .func = drslot_chrp_slot,
	  .validate_options = valid_slot_options,
	  .usage = slot_usage,
	},
	{ .func = drslot_chrp_phb,
	  .validate_options = valid_phb_options,
	  .usage = phb_usage,
	},
	{ .func = drslot_chrp_pci,
	  .validate_options = valid_pci_options,
	  .usage = pci_usage,
	},
	{ .func = drslot_chrp_mem,
	  .validate_options = valid_mem_options,
	  .usage = mem_usage,
	},
	{ .func = drslot_chrp_hea,
	  .validate_options = valid_hea_options,
	  .usage = hea_usage,
	},
	{ .func = drslot_chrp_cpu,
	  .validate_options = valid_cpu_options,
	  .usage = cpu_usage,
	},
	{ .func = drmig_chrp_pmig,
	  .validate_options = valid_pmig_options,
	  .usage = pmig_usage,
	},
	{ .func = drmig_chrp_pmig,
	  .validate_options = valid_pmig_options,
	  .usage = phib_usage,
	},
};

static struct option long_options[] = {
	{"capabilities",	no_argument,	NULL, 'C'},
	{"help",		no_argument,	NULL, 'h'},
	{0,0,0,0}
};
#define MAX_USAGE_LENGTH 512
void
command_usage(struct command *command) {

	/*
	 * Display the common usage options
	 */
	fprintf(stderr, "Usage: drmgr %s",
			"[-w minutes] [-d detail_level] [-C | --capabilities] [-h | --help]\n");

	/*
	 * Now retrieve the command specific usage text
	 */
	char *buffer = zalloc(MAX_USAGE_LENGTH);
	char *pusage=buffer; /* pusage may be over written */
	command->usage(&pusage);

	fprintf(stderr, "%s\n", pusage);

	free(buffer);
}
static char *usagestr = "{-c {port | slot | phb | pci | mem | cpu} | -m}\n"
	"For more information on the specific options for the various\n"
	"connector types, run drmgr -c <type> -h";

void
drmgr_usage(char **pusage)
{
	*pusage = usagestr;
}

int
valid_drmgr_options(struct options *opts)
{

	if (opts->ctype == NULL) {
		say(ERROR, "A connector type (-c) must be specified\n");
		return -1;
	}

	if (action_cnt == 0) {
		say(ERROR, "At least one action must be specified\n");
		return -1;
	}

	if (action_cnt > 1) {
		say(ERROR, "Only one action may be specified\n");
		return -1;
	}

	if ((opts->quantity > 1) && (opts->usr_drc_name)) {
		say(ERROR, "The -q and -s flags are mutually exclusive\n");
		return -1;
	}

	if (opts->timeout < 0) {
		say(ERROR, "Invalid timeout specified: %s\n", optarg);
		return -1;
	}

	return 0;
}

int
parse_options(int argc, char *argv[], struct options *opts)
{
	int c;
	int option_indx;
	int option_found = 0;

	memset(opts, 0, sizeof(*opts));

	/* disable getopt error messages */
	opterr = 0;

	while ((c = getopt_long(argc, argv, DRMGR_ARGS, long_options,
				&option_indx)) != -1) {
		option_found = 1;

		switch (c) {
		    case 'a':
			opts->action = ADD;
			action_cnt++;
			break;
		    case 'c':
			opts->ctype = optarg;
			break;
		    case 'C':
			display_capabilities = 1;
			break;
		    case 'd':
			set_output_level(atoi(optarg));
			break;
		    case 'I':
			opts->no_ident = 1;
			break;
		    case 'i':
			opts->action = IDENTIFY;
			action_cnt++;
			break;
		    case 'n':
			  /* The -n option is also used to specify a number of
			   * seconds to attempt a self-arp.  Linux ignores this
			   * for hibernation.
			   */
			opts->noprompt = 1;
			break;
		    case 'p':
			opts->p_option = optarg;
			break;
		    case 'P':
			opts->prrn_filename = optarg;
			handle_prrn_event = 1;
			break;
		    case 'q':
			opts->quantity = strtoul(optarg, NULL, 0);
			break;
		    case 'R':
			opts->action = REPLACE;
			action_cnt++;
			break;
		    case 'r':
			opts->action = REMOVE;
			action_cnt++;
			break;
		    case 's':
			opts->usr_drc_name = optarg;
			break;
		    case 'Q':
			opts->action = QUERY;
			action_cnt++;
			break;
		    case 'm':
			opts->action = MIGRATE;
			action_cnt++;
			break;
		    case 'w':
			opts->timeout = strtol(optarg, NULL, 10) * 60;
			break;
		    case 'h':
		    	display_usage = 1;
		    	return 0;
		    	break;
		    case 't': /* target lpid (pmig, not used) */
			break;
		    case 'V': /* qemu virtio pci device (workaround) */
                        opts->pci_virtio = 1;
                        break;

		    default:
			say(ERROR, "Invalid option specified '%c'\n", optopt);
			return -1;
			break;
		}
	}

	if (!option_found)
		display_usage = 1;

	return 0;
}

struct command *
get_command(struct options *opts)
{
	/* Unfortunately, the connector type specified doesn't always result
	 * in a 1-to-1 relationship with the resulting command to run so we
	 * have to do some extra checking to build the correct command.
	 */
	if (opts->action == MIGRATE)
		return &commands[DRMIG_CHRP_PMIG];

	if (!opts->ctype)
		return &commands[DRMGR];

	if ((! strncmp(opts->ctype, "port", 4)) ||
	    ((opts->usr_drc_name) && (! strncmp(opts->usr_drc_name, "HEA", 3))))
		return &commands[DRSLOT_CHRP_HEA];
	
	if (! strcmp(opts->ctype, "slot"))
		return &commands[DRSLOT_CHRP_SLOT];
			
	if (! strcmp(opts->ctype, "phb"))
		return &commands[DRSLOT_CHRP_PHB];
			
	if (! strcmp(opts->ctype, "pci"))
		return &commands[DRSLOT_CHRP_PCI];
			
	if (! strcmp(opts->ctype, "mem"))
		return &commands[DRSLOT_CHRP_MEM];
			
	if (! strcmp(opts->ctype, "cpu"))
		return &commands[DRSLOT_CHRP_CPU];

	if (! strcmp(opts->ctype, "phib")) {
		opts->action = HIBERNATE;
		return &commands[DRSLOT_CHRP_PHIB];
	}
			
	/* If we make it this far, the user specified an invalid
	 * connector type.
	 */
	say(ERROR, "Dynamic reconfiguration is not supported for connector\n"
	    "type \"%s\" on this system\n", opts->ctype);

	return &commands[DRMGR];
}

int drmgr(struct options *opts) {
	say(ERROR, "Invalid command: %d\n", opts->action);
	return -1;
}

int main(int argc, char *argv[])
{
	struct options opts;
	char log_msg[DR_PATH_MAX];
	struct command *command;
	int i, rc, offset;

	switch (get_platform()) {
	case PLATFORM_UNKNOWN:
	case PLATFORM_POWERNV:
	   fprintf(stderr, "%s: is not supported on the %s platform\n",
						argv[0], platform_name);
	   exit(1);
	}

	parse_options(argc, argv, &opts);

	rc = dr_init(&opts);
	if (rc) {
		if (handle_prrn_event) {
			say(ERROR, "Failed to handle PRRN event\n");
			unlink(opts.prrn_filename);
		}
		return rc;
	}

	if (display_capabilities) {
		print_dlpar_capabilities();
		dr_fini();
		return 0;
	}

	if (handle_prrn_event) {
		rc = handle_prrn(opts.prrn_filename);
		if (rc)
			say(ERROR, "Failed to handle PRRN event\n");
		unlink(opts.prrn_filename);
		dr_fini();
		return rc;
	}

	command = get_command(&opts);

	if (display_usage) {
		command_usage(command);
		dr_fini();
		return 0;
	}

	/* Validate the options for the action we want to perform */
	rc = command->validate_options(&opts);
	if (rc) {
		dr_fini();
		return -1;
	}

	/* Validate this platform */
	if (!valid_platform("chrp")) {
		dr_fini();
		return -1;
	}

	set_timeout(opts.timeout);

	/* Log this invocation to /var/log/messages and /var/log/drmgr */
	offset = sprintf(log_msg, "drmgr: ");
	for (i = 1; i < argc; i++)
		offset += sprintf(log_msg + offset, "%s ", argv[i]);
	log_msg[offset] = '\0';
	syslog(LOG_LOCAL0 | LOG_INFO, "%s", log_msg);
	
	say(DEBUG, "%s\n", log_msg);

	/* Now, using the actual command, call out to the proper handler */
	rc = command->func(&opts);

	dr_fini();
	return rc;
}
