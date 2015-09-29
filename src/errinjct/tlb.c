/**
 * @file tlb.c
 * @brief Hardware Error Injection Tool TLB module
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Linas Vepstas <linas@us.ibm.com>
 *
 * Inject corrupted-tlb-start and corrupted-tlb-end errors.
 *
 * Copyright (c) 2004 IBM Corporation
 */

#include <stdio.h>
#include "errinjct.h"

/**
 * corrupted_tlb_usage
 * @brief print the "corrupted tlb" error injection usage message
 *
 * @param ei_func errinjct functionality
 */
static void corrupted_tlb_usage(ei_function *ei_func)
{
	printf("Usage: %s %s\n", progname, ei_func->name);
	printf("       %s %s\n", progname, ei_func->alt_name);
	printf("%s\n\n", ei_func->desc);

	printf("Mandatory arguments:\n");
	print_cpu_arg();
	print_token_arg();

	print_optional_args();
}

/**
 * corrupted_tlb_arg
 * @brief check for "corrupted tlb" specific cmdline args
 *
 * There are no additional args to "corrupted tlb" error injections,
 * thus we always return 1 (failure)
 *
 * @param arg cmdline arg to check
 * @param optarg optional argument to 'arg'
 * @return 1, always
 */
int corrupted_tlb_arg(char arg, char *optarg)
{
	return 1;
}

/**
 * corrupted_tlb
 * @brief "corrupted tlb" error injection handler
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int corrupted_tlb(ei_function *ei_func)
{
	int rc;

	if (ext_help || check_cpu_arg() || check_token_arg()) {
		corrupted_tlb_usage(ei_func);
		return 1;
	}

	if (!be_quiet) {
		printf("Injecting a %s error on cpu %d\n", ei_func->name,
			logical_cpu);
	}

	if (dryrun)
		return 0;

	rc = do_rtas_errinjct(ei_func);

	return rc;
}
