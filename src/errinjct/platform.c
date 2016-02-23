/**
 * @file platform.c
 * @brief Hardware Error Injection Tool platform specific module
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Linas Vepstas <linas@us.ibm.com>
 *
 * Inject platform-specific errors.
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
#include <fcntl.h>
#include "errinjct.h"

/**
 * @var fname
 * @brief file containg platform specific error injection data
 */
static char *fname;

/**
 * platform_specific_usage
 * @brief print the "platform specific" error injection usage message
 *
 * @param ei_func errinjct functionality
 */
static void platform_specific_usage(ei_function *ei_func)
{
	printf("Usage: %s %s [OPTIONS]\n", progname, ei_func->name);
	printf("%s\n\n", ei_func->desc);
	printf("Mandatory argument:\n");
	printf(HELP_FMT, "-f fname", "file name to read platform specific");
	printf(HELP_FMT, "", "error injection data from");

	print_optional_args();
	print_cpu_arg();
	print_token_arg();
}

/**
 * platform_specific_arg
 * @brief check for "platform specific" cmdline args
 *
 * @param arg cmdline arg to check
 * @param optarg optional cmdline argument to 'arg'
 * @return 0 - indicates this is a platform specific arg
 * @return 1 - indicates this is not a platform specific arg
 */
int platform_specific_arg(char arg, char *optarg)
{
	switch (arg) {
	case 'f':
		fname = optarg;
		break;
	default:
		return 1;
	}

	return 0;
}

/**
 * platform_specific
 * @brief "platform specific" error injection handler
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int platform_specific(ei_function *ei_func)
{
	char *buf;
	int rc;

	if (ext_help) {
		platform_specific_usage(ei_func);
		return 1;
	}

	if (fname == NULL) {
		perr(0, "Please specify a file with the -f option");
		platform_specific_usage(ei_func);
		return 1;
	}

	buf = read_file(fname, NULL);
	if (!buf)
		return 1;

	if (!be_quiet)
		printf("Injecting a %s error with data from %s\n",
			ei_func->name, fname);

	if (dryrun) {
		free(buf);
		return 0;
	}

	rc = do_rtas_errinjct(ei_func);

	free(buf);
	return rc;
}
