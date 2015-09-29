/**
 * @file open_close.c
 * @brief Hardware error injection tool - open/close module
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Linas Vepstas <linas@us.ibm.com>
 *
 * Open and close the RTAS error injection facitlity
 *
 * Copyright (c) 2004 IBM Corporation
 */

#include <stdio.h>
#include "errinjct.h"

/**
 * ei_open_usage
 * @brief print the "open" error injection usage statement
 *
 * @param ei_func errinjct functionality
 */
static void ei_open_usage(ei_function *ei_func)
{
	printf("Usage: %s %s\n", progname, ei_func->name);
	printf("%s\n\n", ei_func->desc);

	print_optional_args();
}

/**
 * ei_open_arg
 * @brief check for "open" cmdline arg
 *
 * There are no additional args to open, return failure
 *
 * @param arg cmdline arg to check
 * @param optarg optional cmdline argument to 'arg'
 * @return 1, always
 */
int ei_open_arg(char arg, char *optarg)
{
	return 1;
}

/**
 * ei_open
 * @brief "open" error injection handler
 *
 * @param ei_func pointer to errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int ei_open(ei_function *ei_func)
{
	int rc;

	if (ext_help) {
		ei_open_usage(ei_func);
		return 1;
	}

	if (verbose || dryrun)
		printf("Opening RTAS error injection facility\n");

	if (dryrun)
		return 0;

	rc = open_rtas_errinjct(ei_func);

	if (rc == 0)
		printf("RTAS error injection facility open, token = %d\n",
			ei_token);

	return rc;
}

/**
 * ei_close_usage
 * @brief Print the "close" usage statement error injection
 *
 * @param ei_func pointer to errinjct functionality
 */
static void ei_close_usage(ei_function *ei_func)
{
	printf("Usage: %s %s\n", progname, ei_func->name);
	printf("%s\n\n", ei_func->desc);

	printf("Mandatory argument:\n");
	print_token_arg();

	print_optional_args();
	print_cpu_arg();
}

/**
 * ei_close_arg
 * @brief check for "close" specific cmdline args
 *
 * The errinjct close functionality does not take any additional
 * args, always return 1 (failure).
 *
 * @param arg cmdline arg to check
 * @param optarg optional cmdline argument to 'arg'
 * @return 1, always
 */
int ei_close_arg(char arg, char *optarg)
{
	return 1;
}

/**
 * ei_close
 * @brief Closes the RTAS error injection facility
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int ei_close(ei_function *ei_func)
{
	int rc;

	if (ext_help || check_token_arg()) {
		ei_close_usage(ei_func);
		return 1;
	}

	if (verbose || dryrun)
		printf("Closing RTAS error injection facility with token %d\n",
			ei_token);

	if (dryrun)
		return 0;

	rc = close_rtas_errinjct(ei_func);

	if ((rc == 0) && verbose)
		printf("RTAS error injection facility closed.\n");

	return rc;
}
