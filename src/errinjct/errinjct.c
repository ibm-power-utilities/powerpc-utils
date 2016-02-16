/**
 * @file errinjct.c
 * @brief Hardware Error Injection Tool main
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Linas Vepstas <linas@us.ibm.com>
 *
 * This program can be used to simulate hardware error events.
 * It uses librtas to inject artificial errors into the system.
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

#define _GNU_SOURCE
#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <librtas.h>
#include <sys/stat.h>
#include "errinjct.h"
#include "pseries_platform.h"

#define EI_TOKEN_PROCFILE	"/proc/device-tree/rtas/ibm,errinjct-tokens"
#define EI_IBM_ERRINJCT		"/proc/device-tree/rtas/ibm,errinjct"

int verbose;
int dryrun;
int ei_token = -1;
int logical_cpu = -1;
int ext_help;
int be_quiet;
int debug;

/**
 * @var progname
 * @brief name this app was invoked as (argv[0])
 */
char *progname;

uint err_buf[EI_BUFSZ / sizeof(uint)];

/**
 * @var ei_funcs
 * @brief array of known RTAS error injection functionality
 */
static ei_function ei_funcs[] = {
	{
		.name = "open",
		.alt_name = NULL,
		.desc = "open the RTAS error injection facility",
		.rtas_token = -1,
		.arg = ei_open_arg,
		.func = ei_open
	},

	{
		.name = "close",
		.alt_name = NULL,
		.desc = "close the RTAS error injection facility",
		.rtas_token = -1,
		.arg = ei_close_arg,
		.func = ei_close
	},

	{
		.name = "corrupted-dcache-start",
		.alt_name = "dcache-start",
		.desc = "Start causing a LI data cache error",
		.rtas_token = -1,
		.arg = corrupted_dcache_arg,
		.func = corrupted_dcache
	},

	{
		.name = "corrupted-dcache-end",
		.alt_name = "dcache-end",
		.desc = "Stop causing a LI data cache error",
		.rtas_token = -1,
		.arg = corrupted_dcache_arg,
		.func = corrupted_dcache
	},

	{
		.name = "corrupted-icache-start",
		.alt_name = "icache-start",
		.desc = "Start causing an instruction cache error",
		.rtas_token = -1,
		.arg = corrupted_icache_arg,
		.func = corrupted_icache
	},

	{
		.name = "corrupted-icache-end",
		.alt_name = "icache-end",
		.desc = "Stop causing an instruction cache error",
		.rtas_token = -1,
		.arg = corrupted_icache_arg,
		.func = corrupted_icache
	},

	{
		.name = "corrupted-page",
		.alt_name = NULL,
		.desc = "Corrupt the specified location (and potentially surrounding locations up to the containing page)",
		.rtas_token = -1,
		.arg = NULL,
		.func = NULL
	},

	{
		.name = "corrupted-slb",
		.alt_name = "slb",
		.desc = "Corrupt the SLB entry associated with a specific effective address",
		.rtas_token = -1,
		.arg = corrupted_slb_arg,
		.func = corrupted_slb
	},

	{
		.name = "corrupted-tlb-start",
		.alt_name = "tlb-start",
		.desc = "Start corrupting TLB",
		.rtas_token = -1,
		.arg = corrupted_tlb_arg,
		.func = corrupted_tlb
	},

	{
		.name = "corrupted-tlb-end",
		.alt_name = "tlb-end",
		.desc = "Stop corrupting TLB",
		.rtas_token = -1,
		.arg = corrupted_tlb_arg,
		.func = corrupted_tlb
	},

	{
		.name = "fatal",
		.alt_name = NULL,
		.desc = "Simulate a platform fatal error",
		.rtas_token = -1,
		.arg = NULL,
		.func = NULL
	},

	{
		.name = "ioa-bus-error",
		.alt_name = "eeh",
		.desc = "Simulate an error on an IOA bus",
		.rtas_token = -1,
		.arg = ioa_bus_error_arg,
		.func = ioa_bus_error32
	},

	{
		.name = "ioa-bus-error-64",
		.alt_name = "eeh-64",
		.desc = "Simulate an error on a 64-bit IOA bus",
		.rtas_token = -1,
		.arg = ioa_bus_error_arg,
		.func = ioa_bus_error64
	},

	{
		.name = "platform-specific",
		.alt_name = "platform",
		.desc = "Request the firmware perform a platform specific error injection",
		.rtas_token = -1,
		.arg = platform_specific_arg,
		.func = platform_specific
	},

	{
		.name = "recovered-random-event",
		.alt_name = "random-event",
		.desc = "Simulate a recovered random event",
		.rtas_token = -1,
		.arg = NULL,
		.func = NULL
	},

	{
		.name = "recovered-special-event",
		.alt_name = "special-event",
		.desc = "Simulate a recoverd special (statistically significant) event",
		.rtas_token = -1,
		.arg = NULL,
		.func = NULL
	},

	{
		.name = "translator-failure",
		.alt_name = NULL,
		.desc = "Simulate a translator failure",
		.rtas_token = -1,
		.arg = NULL,
		.func = NULL
	},

	{
		.name = "upstream-IO-error",
		.alt_name = NULL,
		.desc = "Inject I/O error above the IOA",
		.rtas_token = -1,
		.arg = NULL,
		.func = NULL
	}
};

/**
 * perr
 * @brief Creates a formatted error message and prints it to stderr
 * @param fmt format of the message to be printed
 * @param ... variable args
 */
void perr(int error, const char *fmt, ...)
{
	char		buf[EI_BUFSZ];
	va_list		ap;
	int		len;

	memset(buf, 0, EI_BUFSZ);

	len = sprintf(buf, "%s: ", progname);
	va_start(ap, fmt);
	len += vsprintf(buf + len, fmt, ap);
	va_end(ap);

	if (error)
		len += sprintf(buf + len, ", %s\n", strerror(error));
	else
		len += sprintf(buf + len, "\n");

	buf[len] = '\0';

	fprintf(stderr, "%s\n", buf);
	fflush(stderr);
}

/**
 * ei_ext_usage
 * @brief Print the extended usage message for errinjct
 *
 * This will print the name and description for all currently supported
 * error injection functions.
 */
static void ei_ext_usage(void)
{
	int i;

	printf("Currently supported functions:\n");
	for (i = 0; i < NUM_ERRINJCT_FUNCS; i++) {
		if (ei_funcs[i].func != NULL)
			printf("    %-25s%s\n", ei_funcs[i].name,
			       ei_funcs[i].desc);
	}

	printf("\n");
	printf("Try \"%s function -H\" for more information\n", progname);
}

/**
 * print_optional_args
 * @brief Print the optional arguments to errijnjct
 *
 * There are several options to the errinjct tool that are not specific
 * to any of the error injection functions (i.e. global).  This routine
 * is provided for all of the error injection functions to use to print
 * these optional args.
 */
void print_optional_args(void)
{
	printf("Optional arguments:\n");
	printf(HELP_FMT, "--dry-run", "don't perform the action,");
	printf(HELP_FMT, "", "just print what would have been done");
	printf(HELP_FMT, "-H --help",
	       "print usage information for a particular function");
	printf(HELP_FMT, "-v --verbose", "be more verbose with messages");
	printf(HELP_FMT, "-vv", "turn on librtas tracing");
	printf(HELP_FMT, "-vvv", "turn on RTAS call argument tracing");
	printf(HELP_FMT, "-q --quiet", "shhhh.... only report errors");
}

/**
 * print_cpu_arg
 * @brief Print the usage for the "-C cpu" option
 *
 * Print the "-C cpu" part of a usage statement.  This argument may or
 * may not be mandatory (depending on the function) so we provide a
 * common print routine to centrlize messages.
 */
void print_cpu_arg(void)
{
	printf(HELP_FMT, "-C cpu", "cpu to inject errors on");
}

/**
 * check_cpu_arg
 * @brief Common routine to check if the "-C cpu" flag was used.
 *
 * This routine is provided to check if the "-C cpu" option was
 * specified and generate a common erro message.
 */
int check_cpu_arg(void)
{
	if (logical_cpu == -1) {
		perr(0, "Please specify a logical cpu with the -C option");
		return 1;
	}

	return 0;
}

/**
 * print_token_arg
 * @brief Print the "-k token" part of the usage statement
 *
 * Print the "-k token" part of the usage statement.  This argument may or
 * may not be mandatory (depending on the function) so we provide a
 * common print routine to centralize messages.
 */
void print_token_arg(void)
{
	printf(HELP_FMT, "-k token", "token returned from error inject open");
}

/**
 * check_token_arg
 * @brief Common routine to check if the "-k token" flag was used.
 *
 * This provides a common point for checking to see if the "-k token"
 * option was specified and generates a common error statement.
 */
int check_token_arg(void)
{
	if (ei_token == -1) {
		perr(0, "Please specify the error inject token with the -k option");
		return 1;
	}

	return 0;
}

/**
 * ei_usage
 * @brief Print the usage statement for errinjct
 */
static void ei_usage(void)
{
	printf("Usage: %s FUNCTION [OPTIONS]\n", progname);
	printf("This will inject an error into the system via rtas\n");

	ei_ext_usage();
}

/**
 * check_librtas_returns
 * @brief check the return code from librtas
 *
 * Check the return code from a call to librtas.  Librtas can return
 * values tat librtas specific values to indicate errors in librtas.
 * There is no method provided by librtas to do this so we have to do
 * it here.
 *
 * @param rc return code from a librtas call
 * @param ei_func functionality pointer
 */
void check_librtas_returns(int rc, ei_function *ei_func)
{
	switch (rc) {
	case -1: /* Hardware Error */
		perr(0, "RTAS: %s: Hardware error (-1)", ei_func->name);
		break;
	case -2: /* Thank you, come again */
		perr(0, "RTAS: %s: Busy, try again later (-2)", ei_func->name);
		break;
	case -3: /* Arg error */
		perr(0, "RTAS: %s: Argument error (-3)", ei_func->name);
		break;
	case -4: /* Call error */
		perr(0, "RTAS: %s: The error injection facility is not open\n"
			"or you are not the one that opened it", ei_func->name);
		break;
	case -1001: /* RTAS_KERNEL_INT */
		perr(0, "librtas: No Kernel Interface to Firmware");
		break;
	case -1002: /* RTAS_KERNEL_IMP */
		perr(0, "librtas: No Kernel Implementation of function %s",
		     ei_func->name);
		break;
	case -1003: /* RTAS_PERM */
		perr(0, "librtas: You must be root to access rtas calls");
		break;
	case -1004: /* RTAS_NO_MEM */
		perr(0, "librtas: Out of memory");
		break;
	case -1005: /* RTAS_NO_LOMEM */
		perr(0, "librtas: Kernel out of low memory");
		break;
	case -1006: /* RTAS_FREE_ERR */
		perr(0, "librtas: Attempt to free nonexistant rmo buffer");
		break;
	case -1007: /* RTAS_TIMEOUT */
		perr(0, "librtas: RTAS delay exceeded specified timeout");
		break;
	case -1098: /* RTAS_IO_ASSERT */
		perr(0, "librtas: %s: Unexpected I/O error", ei_func->name);
		break;
	case -1099: /* RTAS_UNKNOWN_OP */
		perr(0, "librtas: No firmware implementation of function %s",
		     ei_func->name);
		break;
	default:
		perr(0, "librtas returned an unknown error code (%d) for function %s",
		     rc, ei_func->name);
		break;
	}
}

/**
 * open_rtas_errinjct
 * @brief Open the RTAS error injection facility
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int open_rtas_errinjct(ei_function *ei_func)
{
	int rc;

	rc = rtas_errinjct_open(&ei_token);
	if (rc != 0) {
		perr(0, "Could not open RTAS error injection facility");
		if (rc == -4)
			perr(0, "the facility is already open, please\n"
			     "specify the open token with the -k option");
		else if (rc == -5)
			perr(0, "PCI Error Injection is not enabled.");
		else
			check_librtas_returns(rc, ei_func);

	}

	return rc;
}

/**
 * close_rtas_errinjct
 * @brief Close the RTAS error injection facility
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int close_rtas_errinjct(ei_function *ei_func)
{
	int rc;

	rc = rtas_errinjct_close(ei_token);
	if (rc != 0) {
		perr(0, "Could not close RTAS error injection facility");
		check_librtas_returns(rc, ei_func);
	}

	return rc;
}

/**
 * bind_cpu
 * @brief bind errinjct to a cpu
 *
 * Bind ourselves to a particular cpu if the cpu binding capability
 * is present on this machine.
 *
 * The inability to bind to a cpu is not considered a failure condition,
 * we simply print a message stating that cpu binding is not available
 * and return success.
 *
 * @return 0 on success, !0 otherwise
 */
static int bind_cpu(void)
{
	cpu_set_t mask;
	int rc;

	if (logical_cpu == -1)
		return 0;

	if (verbose)
		printf("Binding to logical cpu %d\n", logical_cpu);

	CPU_ZERO(&mask);
	CPU_SET(logical_cpu, &mask);

	rc = sched_setaffinity(getpid(), sizeof(mask), &mask);

	if (rc)
		perr(0, "Could not bind to logical cpu %d", logical_cpu);

	return rc;
}

/**
 * do_rtas_errinjct
 * @brief Perform the actual call to inject errors
 *
 * Perform the actual call to RTAS.  This routine will also bind us to
 * a specific processor (if requested) and open RTAS error injection if an
 * open token is not specified.
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int do_rtas_errinjct(ei_function *ei_func)
{
	int rc = 0;
	int close_errinjct = 0;

	/* First see if we should bind ourselves to a processor */
	rc = bind_cpu();
	if (rc)
		return rc;

	/* Now, see if we need to open the RTAS errinjct facility */
	if (ei_token == -1) {
		rc = open_rtas_errinjct(ei_func);
		if (rc != 0)
			return rc;

		close_errinjct = 1;
	}

	/* Make the RTAS call */
	rc = rtas_errinjct(ei_func->rtas_token, ei_token, (char *)err_buf);
	if (rc != 0) {
		perr(0, "RTAS error injection failed!");
		check_librtas_returns(rc, ei_func);
		printf("This error may have occurred because error injection\n"
		       "is disabled for this partition. Please check the\n"
		       "FSP and ensure you have error injection enabled.\n");
	} else if (!be_quiet) {
		printf("Call to RTAS errinjct succeeded!\n\n");
	}

	if (close_errinjct)
		rc = close_rtas_errinjct(ei_func);

	return rc;
}

/**
 * read_ei_tokens
 * @brief Gather the RTAS tokens stored in /proc for this machine.
 *
 * This will read the RTAS token values stored in /proc to determine
 * which RTAS error injection capabilities are avaialable on this machine
 *
 * @return 0 on success, !0 otherwise
 */
int read_ei_tokens(void)
{
	char	buf[EI_BUFSZ];
	int	len;
	int	i, found;
	char	*tmp_ptr;
	int	fd;

	fd = open(EI_TOKEN_PROCFILE, O_RDONLY);
	if (fd == -1) {
		perr(errno, "Could not open %s", EI_TOKEN_PROCFILE);
		return 1;
	}

	len = read(fd, buf, EI_BUFSZ);
	if (len == -1) {
		perr(errno, "Could not read from %s", EI_TOKEN_PROCFILE);
		close(fd);
		return 1;
	}

	tmp_ptr = buf;
	while (tmp_ptr < buf + len) {
		found = 0;
		for (i = 0; i < NUM_ERRINJCT_FUNCS; i++) {
			if (strcmp(tmp_ptr, ei_funcs[i].name) == 0) {
				found = 1;
				tmp_ptr += strlen(tmp_ptr) + 1;
				ei_funcs[i].rtas_token = be32toh(*(int *)tmp_ptr);
				tmp_ptr += sizeof(int);
				break;
			}
		}

		if (found == 0) {
			if (verbose)
				perr(0, "Could not find errinjct function for rtas token \"%s\"",
				     tmp_ptr);
			tmp_ptr += strlen(tmp_ptr) + 1 + sizeof(int);
		}
	}

	return 0;
}

/**
 * sysfs_check
 * @brief Make sure sysfs is mounted at the expected /sys location
 *
 * Several places in errinjct need to look up data in sysfs.  In this
 * routine we make sure sysfs is mounted in the expected location /sys
 *
 * @return 0 if sysfs is mounted at /sys, !0 otherwise
 */
int sysfs_check(void)
{
	struct stat sbuf;
	int rc;

	rc = stat("/sys/class", &sbuf);

	/* posix semantics returns EOVERFLOW on sysfs in newer kernels */
	if (rc && errno == EOVERFLOW)
		rc = 0;

	if (rc)
		perr(errno, "It appears that sysfs is not mounted at /sys.\n"
		     "The error injection you requested requires sysfs,\n"
		     "please check your system configuration and try again.\n");

	return rc;
}


static struct option longopts[] = {
{
	name: "dry-run",
	has_arg: 0,
	flag: NULL,
	val: 254
},
{
	name: "help",
	has_arg: 0,
	flag: NULL,
	val: 'H'
},
{
	name: "verbose",
	has_arg: 0,
	flag: NULL,
	val: 'v'
},
{
	name: "quiet",
	has_arg: 0,
	flag: NULL,
	val: 'q'
},
{
	name: NULL
}
};

int main(int argc, char *argv[])
{
	ei_function *ei_func = NULL;
	const char *funcname;
	int	c, rc;
	int	i, fd;

	progname = argv[0];

	if (argc == 1) {
		ei_usage();
		exit(1);
	}

	/* Make sure the error injection facility is available */
	fd = open(EI_IBM_ERRINJCT, O_RDONLY);
	if (fd == -1) {
		perr(0, "Could not open error injection facility,\n"
		     "file \"%s\" does not exist", EI_IBM_ERRINJCT);
		close(fd);
		exit(1);
	}

	close(fd);

	/* The function name is always first */
	funcname = argv[1];
	for (i = 0; i < NUM_ERRINJCT_FUNCS; i++) {
		if (ei_funcs[i].func == NULL)
			continue;

		if ((strcmp(funcname, ei_funcs[i].name) == 0)
		    || ((ei_funcs[i].alt_name != NULL)
		    && (strcmp(funcname, ei_funcs[i].alt_name)) == 0)) {
			ei_func = &ei_funcs[i];
			break;
		}
	}

	if (ei_func == NULL) {
		perr(0, "Could not find function \'%s\'", funcname);
		ei_ext_usage();
		exit(1);
	}

	/* shift past function name */
	argc--;
	argv++;

	while ((c = getopt_long(argc, argv, "+a:C:c:f:Hh:k:l:m:n:p:qs:v",
				longopts, NULL)) != -1) {
		switch (c) {
		case 254:
			dryrun = 1;
			break;
		case 'C':
			logical_cpu = atoi(optarg);
			break;
		case 'H':
			ext_help = 1;
			break;
		case 'k':
			ei_token = atoi(optarg);
			break;
		case 'q':
			be_quiet = 1;
			break;
		case 'v':
			/* If specified multiple times,
			 * turn on librtas debug
			 */
			if (verbose) {
				debug++;
				rtas_set_debug(debug);
			}
			verbose = 1;
			break;
		case ':':
			break;
		default:
			rc = ei_func->arg(c, optarg);
			if (rc) {
				perr(0, "\"-%c\" is not a valid option for %s",
				     c, ei_func->name);
				ext_help = 1;
			}
			break;
		}
	}

	/* Open the /proc/ppc64/rtas entry to get valid tokens */
	rc = read_ei_tokens();
	if (rc)
		exit(rc);

	memset(err_buf, 0, EI_BUFSZ);

	rc = ei_func->func(ei_func);

	exit(rc);
}
