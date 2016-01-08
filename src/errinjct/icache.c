/**
 * @file icache.c
 * @brief Hardware Error Injection Tool I-cache module
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Linas Vepstas <linas@us.ibm.com>
 *
 * Inject corrupted-icache-start and  corrupted-icache-end errors.
 *
 * Copyright (c) 2004 IBM Corporation
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

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include "errinjct.h"

/**
 * @var action
 * @brief action code for I-cache error injections
 */
static int action = -1;
/**
 * @var nature
 * @brief nature of I-cache error injection
 */
static int nature = -1;

/**
 * @var action_codes
 * @brief descriptions of the I-cache actions codes
 */
static char *action_codes[] = {
	"Parity error",
	"I-ERAT partiy error",
	"Cache directory 0 parity error",
	"Cache directory 1 parity error"
};
#define MAX_ICACHE_ACTION_CODE		3

/**
 * @var nature_codes
 * @brief descriptions of the I-cache nature codes
 */
static char *nature_codes[] = { "Single", "Solid", "Hang" };
#define MAX_ICACHE_NATURE_CODE		2

/**
 * corrupted_icache_usage
 * @brief Print the "corrupted I-cache" error injection usage statement
 *
 * @param ei_func errinjct functionality
 */
static void corrupted_icache_usage(ei_function *ei_func)
{
	int i;

	printf("Usage: %s %s [OPTIONS]\n", progname, ei_func->name);
	printf("       %s %s [OPTIONS]\n", progname, ei_func->alt_name);
	printf("%s\n\n", ei_func->desc);

	printf("Mandatory Arguments:\n");
	printf(HELP_FMT, "-a action", "type of I-cache error to inject");
	for (i = 0; i <= MAX_ICACHE_ACTION_CODE; i++)
		printf("%22d: %s\n", i, action_codes[i]);
	printf(HELP_FMT, "-n nature", "nature of I-cache error to inject");
	for (i = 0; i <= MAX_ICACHE_NATURE_CODE; i++)
		printf("%22d: %s\n", i, nature_codes[i]);

	print_cpu_arg();
	print_token_arg();

	print_optional_args();
}

/**
 * corrupted_icache_arg
 * @brief check for "corruptred I-cache" cmdline args
 *
 * @param arg cmdline arg to check
 * @param optarg optional cmdline argument to 'arg'
 * @return 1 - indicates this is a corrupted I-cache arg
 * @return 0 - indicates this is not a corrupted I-cache arg
 */
int corrupted_icache_arg(char arg, char *optarg)
{
	switch (arg) {
	case 'a':
		action = atoi(optarg);
		break;
	case 'n':
		nature = atoi(optarg);
		break;
	default:
		return 1;
	}

	return 0;
}

/**
 * corrupted_icache
 * @brief "corrupted I-cache" error injection handler
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int corrupted_icache(ei_function *ei_func)
{
	int rc;

	if (ext_help || check_cpu_arg() || check_token_arg()) {
		corrupted_icache_usage(ei_func);
		return 1;
	}

	if ((action < 0) || (action > 3)) {
		perr(0, "Invalid action code (%d)", action);
		corrupted_icache_usage(ei_func);
		return 1;
	}

	if ((nature < 0) || (nature > 2)) {
		perr(0, "Invalid nature code (%d)", nature);
		corrupted_icache_usage(ei_func);
		return 1;
	}

	err_buf[0] = action;
	err_buf[1] = nature;

	if (!be_quiet) {
		printf("Injecting a %s error\n", ei_func->name);
		printf("Action: %d - %s\n", action, action_codes[action]);
		printf("Nature: %d - %s\n", nature, nature_codes[nature]);
	}

	if (dryrun)
		return 0;

	rc = do_rtas_errinjct(ei_func);

	return rc;
}
