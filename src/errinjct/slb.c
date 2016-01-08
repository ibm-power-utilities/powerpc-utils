/**
 * @file slb.c
 * @brief Hardware Error Injection Tool slb module
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Linas Vepstas <linas@us.ibm.com>
 *
 * Inject SLB errors.
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

static unsigned long addr;  /**< address at which to inject the error */
static int addr_flag;       /**< indicates the address flag has been
			     *   specified */

/**
 * corrupted_slb_usage
 * @brief print the "corrupted slb" error inject usage message
 *
 * @param ei_func errinjct functionality
 */
static void corrupted_slb_usage(ei_function *ei_func)
{
	printf("Usage: %s %s [OPTIONS]\n", progname, ei_func->name);
	printf("       %s %s [OPTIONS]\n", progname, ei_func->alt_name);
	printf("%s\n\n", ei_func->desc);
	printf("Mandatory Argument:\n");
	printf(HELP_FMT, "-a addr", "effective address associated with the");
	printf(HELP_FMT, "", "SLB entry to corrupt\n");

	print_optional_args();
	print_cpu_arg();
	print_token_arg();
}

/**
 * corrupted_alb_arg
 * @brief check for "corrupted alb" specific cmdline args
 *
 * @param arg cmdline arg to check
 * @param optarg optional argument to 'arg'
 * @return 0 - indicates this is a "corrupted slb" cmdline arg
 * @return 1 - indicates this is not a "corrupted slb" cmdline arg
 */
int corrupted_slb_arg(char arg, char *optarg)
{
	switch (arg) {
	case 'a':
		addr = strtoul(optarg, NULL, 16);
		addr_flag = 1;
		break;
	default:
		return 1;
	}

	return 0;
}

/**
 * corrupted_slb
 * @brief "corrupted slb" error injection handler
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int corrupted_slb(ei_function *ei_func)
{
	int rc;

	if (ext_help) {
		corrupted_slb_usage(ei_func);
		return 1;
	}

	if (addr_flag == 0) {
		perr(0, "Please specify an address with the -a option");
		corrupted_slb_usage(ei_func);
		return 1;
	}

	if (!be_quiet) {
		printf("Injecting a %s error\n", ei_func->name);
		printf("Effective address = 0x%lx\n", addr);
	}

	if (dryrun)
		return 0;

	err_buf[0] = addr;

	rc = do_rtas_errinjct(ei_func);

	return rc;
}
