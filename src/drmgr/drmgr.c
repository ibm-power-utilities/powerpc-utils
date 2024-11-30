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
#include <getopt.h>

#include "dr.h"
#include "pseries_platform.h"

#include "options.c"

#define DRMGR_ARGS	"ac:d:Iimnp:P:Qq:Rrs:w:t:hCVH"

unsigned output_level = 1; /* default to lowest output level */

int log_fd = 0;
int action_cnt = 0;

int read_dynamic_memory_v2 = 0;

static int handle_prrn_event = 0;
static int display_usage = 0;

typedef int (cmd_func_t)(void);
typedef int (cmd_args_t)(void);
typedef void (cmd_usage_t)(char **);

int drmgr(void);
void drmgr_usage(char **);
int valid_drmgr_options(void);

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
#define DRACC_CHRP_ACC		9

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
	{ .func = dracc_chrp_acc,
	  .validate_options = valid_acc_options,
	  .usage = acc_usage,
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
static char *usagestr = "{-c {port | slot | phb | pci | mem | cpu | acc} | -m}\n"
	"For more information on the specific options for the various\n"
	"connector types, run drmgr -c <type> -h";

void
drmgr_usage(char **pusage)
{
	*pusage = usagestr;
}

int valid_drmgr_options(void)
{

	if (usr_drc_type == DRC_TYPE_NONE) {
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

	if ((usr_drc_count > 1) && usr_drc_name) {
		say(ERROR, "The -q and -s flags are mutually exclusive\n");
		return -1;
	}

	if (usr_timeout < 0) {
		say(ERROR, "Invalid timeout specified: %s\n", usr_timeout);
		return -1;
	}

	return 0;
}

int parse_options(int argc, char *argv[])
{
	int c;
	int option_indx;
	int option_found = 0;

	/* disable getopt error messages */
	opterr = 0;

	while ((c = getopt_long(argc, argv, DRMGR_ARGS, long_options,
				&option_indx)) != -1) {
		option_found = 1;

		switch (c) {
		    case 'a':
			usr_action = ADD;
			action_cnt++;
			break;
		    case 'c':
			usr_drc_type = to_drc_type(optarg);
			break;
		    case 'C':
			display_capabilities = 1;
			break;
		    case 'd':
			set_output_level(strtoul(optarg, NULL, 10));
			break;
		    case 'I':
			usr_slot_identification = 0;
			break;
		    case 'i':
			usr_action = IDENTIFY;
			action_cnt++;
			break;
		    case 'n':
			  /* The -n option is also used to specify a number of
			   * seconds to attempt a self-arp.  Linux ignores this
			   * for hibernation.
			   */
			usr_prompt = 0;
			break;
		    case 'p':
			usr_p_option = optarg;
			break;
		    case 'P':
			prrn_filename = optarg;
			handle_prrn_event = 1;
			break;
		    case 'q':
			usr_drc_count = strtoul(optarg, NULL, 0);
			break;
		    case 'R':
			usr_action = REPLACE;
			action_cnt++;
			break;
		    case 'r':
			usr_action = REMOVE;
			action_cnt++;
			break;
		    case 's':
			usr_drc_name = optarg;
			break;
		    case 'Q':
			usr_action = QUERY;
			action_cnt++;
			break;
		    case 'm':
			usr_action = MIGRATE;
			action_cnt++;
			break;
		    case 'w':
			usr_timeout = strtol(optarg, NULL, 10) * 60;
			break;
		    case 'h':
		    	display_usage = 1;
		    	return 0;
		    	break;
		    case 'H':
			pci_hotplug_only = 1;
			break;
		    case 't': /* target lpid (pmig, not used) */
			usr_t_option = optarg;	/* Used for Accelerator type (ex:gzip) */
			break;
		    case 'V': /* qemu virtio pci device (workaround) */
                        pci_virtio = 1;
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

struct command *get_command(void)
{
	/* Unfortunately, the connector type specified doesn't always result
	 * in a 1-to-1 relationship with the resulting command to run so we
	 * have to do some extra checking to build the correct command.
	 */
	if (usr_action == MIGRATE)
		return &commands[DRMIG_CHRP_PMIG];

	if (usr_drc_name && !strncmp(usr_drc_name, "HEA", 3))
		return &commands[DRSLOT_CHRP_HEA];
	
	switch (usr_drc_type) {
	case DRC_TYPE_NONE:
		return &commands[DRMGR];
		break;
	case DRC_TYPE_PORT:
 		return &commands[DRSLOT_CHRP_HEA];
		break;
	case DRC_TYPE_SLOT:
 		return &commands[DRSLOT_CHRP_SLOT];
		break;
	case DRC_TYPE_PHB:
		return &commands[DRSLOT_CHRP_PHB];
		break;
	case DRC_TYPE_PCI:
 		return &commands[DRSLOT_CHRP_PCI];
		break;
	case DRC_TYPE_MEM:
 		return &commands[DRSLOT_CHRP_MEM];
		break;
	case DRC_TYPE_CPU:
 		return &commands[DRSLOT_CHRP_CPU];
		break;
	case DRC_TYPE_HIBERNATE:
 		usr_action = HIBERNATE;
 		return &commands[DRSLOT_CHRP_PHIB];
		break;
	case DRC_TYPE_MIGRATION:
		return &commands[DRMIG_CHRP_PMIG];
	case DRC_TYPE_ACC:
		return &commands[DRACC_CHRP_ACC];
	default:
		/* If we make it this far, the user specified an invalid
		 * connector type.
		 */
		say(ERROR, "Dynamic reconfiguration is not supported for "
		    "connector type \"%d\" on this system\n", usr_drc_type);
		break;
	}

	return &commands[DRMGR];
}

int drmgr(void) {
	say(ERROR, "Invalid command: %d\n", usr_action);
	return -1;
}

int main(int argc, char *argv[])
{
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

	parse_options(argc, argv);

	rc = dr_init();
	if (rc) {
		if (handle_prrn_event) {
			say(ERROR, "Failed to handle PRRN event\n");
			unlink(prrn_filename);
		}
		return rc;
	}

	if (display_capabilities) {
		print_dlpar_capabilities();
		dr_fini();
		return 0;
	}

	if (handle_prrn_event) {
		rc = handle_prrn();
		if (rc)
			say(ERROR, "Failed to handle PRRN event\n");
		unlink(prrn_filename);
		dr_fini();
		return rc;
	}

	command = get_command();

	if (display_usage) {
		command_usage(command);
		dr_fini();
		return 0;
	}

	/* Validate the options for the action we want to perform */
	rc = command->validate_options();
	if (rc) {
		dr_fini();
		return -1;
	}

	/* Validate this platform */
	if (!valid_platform("chrp")) {
		dr_fini();
		return -1;
	}

	set_timeout(usr_timeout);

	/* Log this invocation to /var/log/messages and /var/log/drmgr */
	offset = sprintf(log_msg, "drmgr: ");
	for (i = 1; i < argc; i++)
		offset += sprintf(log_msg + offset, "%s ", argv[i]);
	log_msg[offset] = '\0';
	syslog(LOG_LOCAL0 | LOG_INFO, "%s", log_msg);
	
	say(DEBUG, "%s\n", log_msg);

	/* Now, using the actual command, call out to the proper handler */
	rc = command->func();

	dr_fini();
	return rc;
}
