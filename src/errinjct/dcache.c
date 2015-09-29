/**
 * @file dcache.c
 * @author Nathan Fontenot <nfont@us.ibm.com>
 * @author Linas Vepstas <linas@us.ibm.com>
 *
 * @brief Inject corrupted-dcache-start and corrupted-dcache-end errors
 *
 * Copyright (c) 2004 IBM Corporation
 */

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include "errinjct.h"

/**
 * @var action
 * @brief action code for the corrupted D-cache error injection
 */
static int action = -1;

/**
 * @var action_codes
 * @brief descriptions of the corrupted D-cache action codes
 */
static char *action_codes[] = {
	"parity error",
	"D-ERAT parity error",
	"tag parity error"
};
#define MAX_DCACHE_ACTION_CODE	2

/**
 * corrupted_dcache_usage
 * @brief print the "corrupted D-cache" error injection usage statement
 *
 * @param ei_func errinjct functionality
 */
static void corrupted_dcache_usage(ei_function *ei_func)
{
	int i;

	printf("Usage: %s %s [OPTIONS]\n", progname, ei_func->name);
	printf("       %s %s [OPTIONS]\n", progname, ei_func->alt_name);
	printf("%s\n\n", ei_func->desc);

	printf("Mandatory arguments:\n");
	printf(HELP_FMT, "-a action", "type of D-cache error to inject");
	for (i = 0; i <= MAX_DCACHE_ACTION_CODE; i++)
		printf("%22d: %s\n", i, action_codes[i]);

	print_cpu_arg();
	print_token_arg();

	print_optional_args();
}

/**
 * corrupted_dcache_arg
 * @brief check for "corrupted D-cache arg" cmdline args
 *
 * @param arg cmdline arg
 * @param optarg optional cmdline argument to 'arg'
 * @return 0 - indicates this is a corrupted dcache arg
 * @return 1 - indicates this is _not_ a corrupted dcache arg
 */
int corrupted_dcache_arg(char arg, char *optarg)
{
	if (arg == 'a') {
		action = atoi(optarg);
		return 0;
	}

	return 1;
}

/**
 * corrupted_dcache
 * @brief "corrupted D-cache" error injection handler
 *
 * This will inject a corrupted D-cache error onto the system
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int corrupted_dcache(ei_function *ei_func)
{
	int rc;

	if (ext_help || check_cpu_arg() || check_token_arg()) {
		corrupted_dcache_usage(ei_func);
		return 1;
	}

	if ((action < 0) || (action > MAX_DCACHE_ACTION_CODE)) {
		perr(0, "Invalid action code (%d)", action);
		corrupted_dcache_usage(ei_func);
		return 1;
	}

	if (!be_quiet) {
		printf("Injecting a %s error\n", ei_func->name);
		printf("Action: %d - %s\n", action, action_codes[action]);
	}

	if (dryrun)
		return 0;

	err_buf[0] = action;

	rc = do_rtas_errinjct(ei_func);

	return rc;
}
