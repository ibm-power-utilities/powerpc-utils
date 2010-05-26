/**
 * @file drmgr.c
 *
 *
 * Copyright (C) IBM Corporation 2006
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "dr.h"

#define DRMGR_ARGS	"ac:d:Iimnp:Qq:Rrs:w:t:hC"

int debug = 4; /* Always have debug output enabled. */

int log_fd = 0;
char *cmd;
int action_cnt = 0;

static int display_capabilities = 0;
static int display_usage = 0;

typedef int (cmd_func_t)(struct options *);
typedef int (cmd_args_t)(struct options *);
typedef void (cmd_usage_t)(char **);

int drmgr(struct options *);
void drmgr_usage(char **);
int valid_drmgr_options(struct options *);

struct command {
	int		type;
	char		*name;
	cmd_func_t	*func;
	cmd_args_t	*validate_options;
	cmd_usage_t	*usage;
};

#define DRMGR				0
#define DRSLOT_CHRP_SLOT	1
#define DRSLOT_CHRP_PHB		2
#define DRSLOT_CHRP_PCI		3
#define DRSLOT_CHRP_MEM		4
#define DRSLOT_CHRP_HEA		5
#define DRSLOT_CHRP_CPU		6
#define DRMIG_CHRP_PMIG		7
#define DRSLOT_CHRP_PHIB	8

static struct command commands[] = {
	{ .type = DRMGR,
	  .name = "drmgr",
	  .func = drmgr,
	  .validate_options = valid_drmgr_options,
	  .usage = drmgr_usage,
	},
	{ .type = DRSLOT_CHRP_SLOT,
	  .name = "drslot_chrp_slot",
	  .func = drslot_chrp_slot,
	  .validate_options = valid_slot_options,
	  .usage = slot_usage,
	},
	{ .type = DRSLOT_CHRP_PHB,
	  .name = "drslot_chrp_phb",
	  .func = drslot_chrp_phb,
	  .validate_options = valid_phb_options,
	  .usage = phb_usage,
	},
	{ .type = DRSLOT_CHRP_PCI,
	  .name = "drslot_chrp_pci",
	  .func = drslot_chrp_pci,
	  .validate_options = valid_pci_options,
	  .usage = pci_usage,
	},
	{ .type = DRSLOT_CHRP_MEM,
	  .name = "drslot_chrp_mem",
	  .func = drslot_chrp_mem,
	  .validate_options = valid_mem_options,
	  .usage = mem_usage,
	},
	{ .type = DRSLOT_CHRP_HEA,
	  .name = "drslot_chrp_hea",
	  .func = drslot_chrp_hea,
	  .validate_options = valid_hea_options,
	  .usage = hea_usage,
	},
	{ .type = DRSLOT_CHRP_CPU,
	  .name = "drslot_chrp_cpu",
	  .func = drslot_chrp_cpu,
	  .validate_options = valid_cpu_options,
	  .usage = cpu_usage,
	},
	{ .type = DRMIG_CHRP_PMIG,
	  .name = "drmig_chrp_pmig",
	  .func = drmig_chrp_pmig,
	  .validate_options = valid_pmig_options,
	  .usage = pmig_usage,
	},
	{ .type = DRSLOT_CHRP_PHIB,
	  .name = "drslot_chrp_phib",
	  .func = drmig_chrp_pmig,
	  .validate_options = valid_pmig_options,
	  .usage = phib_usage,
	},
};

static struct option long_options[] = {
	{"capabilities",	no_argument,	NULL, 'C'},
	{"help",	no_argument,	NULL, 'h'},
	{0,0,0,0}
};
#define MAX_USAGE_LENGTH 512
void
command_usage(struct command *command) {

	/*
	 * Display the common usage options
	 */
	fprintf(stderr, "Usage: %s %s", cmd,
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
		err_msg("A connector type (-c) must be specified\n");
		return -1;
	}

	if (action_cnt == 0) {
		err_msg("At least one action must be specified\n");
		return -1;
	}

	if (action_cnt > 1) {
		err_msg("Only one action may be specified\n");
		return -1;
	}

	if ((opts->quantity > 1) && (opts->usr_drc_name)) {
		err_msg("The -q and -s flags are mutually exclusive\n");
		return -1;
	}

	if (opts->timeout < 0) {
		err_msg("Invalid timeout specified: %s\n",
			optarg);
		return -1;
	}

	return 0;
}

int
parse_options(int argc, char *argv[], struct options *opts)
{
	int c;
	int option_indx;
	char *last_underscore;

	memset(opts, 0, sizeof(*opts));

	/* default timeout */
	opts->timeout = DR_WAIT;

	/* Set the default connector type based on the command name
	 * When the command ends with 'port','slot','phb','pci','mem', or 'cpu'.
	 * This will eliminate the need to specify the -c ctype option
	 * when invoking the command via symbolic link, since there is a 1-to-1
	 * mapping in these cases.
	 *
	 * i.e. if drmgr is invoke from a symbolic link
	 *      drslot_chrp_pci->drmgr, the default connector type will be pci
	 */
	last_underscore = strrchr(cmd, '_');

	if (last_underscore) {
		if (!strcmp(last_underscore,"_slot") ||
			!strcmp(last_underscore,"_phb") ||
			!strcmp(last_underscore,"_pci") ||
			!strcmp(last_underscore,"_mem") ||
			!strcmp(last_underscore,"_cpu"))
			opts->ctype = last_underscore+1;
		else
			if (!strcmp(last_underscore,"_pmig"))
			opts->action = MIGRATE;
		else
			if(!strcmp(last_underscore,"_hea"))
				opts->ctype="port";
	}

	/* disable getopt error messages */
	opterr = 0;

	while ((c = getopt_long(argc, argv, DRMGR_ARGS, long_options,
				&option_indx)) != -1) {
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
			set_debug(atoi(optarg));
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

		    default:
			err_msg("Invalid option specified '%c'\n", optopt);
			return -1;
			break;
		}
	}

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
	err_msg("Dynamic reconfiguration is not supported for connector\n"
		"type \"%s\" on this system\n", opts->ctype);

	return &commands[DRMGR];
}

int drmgr(struct options *opts) {
	err_msg("Invalid command %s %d\n",cmd, opts->action);
	return -1;
}

int
main(int argc, char *argv[])
{
	struct options opts;
	char log_msg[DR_PATH_MAX];
	struct command *command;
	char *slash;
	int i, offset;
	int rc;

	dr_init();

	/* We need to know the name of the command that was actually invoked
	 * so grab that out of argv[0]
	 */
	slash = strrchr(argv[0], '/');
	if (slash)
		cmd = slash + 1;
	else
		cmd = argv[0];

	parse_options(argc, argv, &opts);

	if (display_capabilities) {
		print_dlpar_capabilities();
		rc = 0;
		goto exit;
	}

	command = get_command(&opts);

	if (display_usage) {
		command_usage(command);
		rc = 0;
		goto exit;
	}

	/* Validate the options for the action we want to perform */
	rc = command->validate_options(&opts);
	if (rc)
		goto exit;

	/* Validate this platform */
	if (! valid_platform("chrp")) {
		rc = 1;
		goto exit;
	}

	/* Mask signals so we do not get interrupted */
	if (sig_setup()) {
		err_msg("Could not mask signals to avoid interrupts\n"); 
		rc = -1;
		goto exit;
	}

	rc = dr_lock(opts.timeout);
	if (rc) {
		err_msg("Unable to obtain Dynamic Reconfiguration lock. "
			"Please try command again later.\n");
		rc = -1;
		goto exit;
	}

	/* Log this invocation to /var/log/messages and /var/log/drmgr */
	offset = sprintf(log_msg, "drmgr: %s ", command->name);
	for (i = 1; i < argc; i++)
		offset += sprintf(log_msg + offset, "%s ", argv[i]);
	log_msg[offset] = '\0';
	syslog(LOG_LOCAL0 | LOG_INFO, log_msg);
	
	dbg("%s\n", log_msg);

	/* Now, using the actual command, call out to the proper handler */
	rc = command->func(&opts);

	dr_unlock();
exit:
	dr_fini();
	return rc;
}
