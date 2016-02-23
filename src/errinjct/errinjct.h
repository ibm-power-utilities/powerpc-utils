/**
 * @file errinjct.h
 * @brief Hardware Error Injection Tool main
 * @author Nathan Fontenot <nfont@us.ibm.com>
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

#ifndef _ERRINJCT_H
#define _ERRINJCT_H

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>

extern int verbose;		/**< verbose flag specified? */
extern int dryrun;		/**< Are we doing a dry run */
extern int ei_token;		/**< error injection token */
extern int logical_cpu;		/**< logical cpu to bind to */
extern int ext_help;		/**< print the extended help message */
extern int be_quiet;		/**< Shhh... don't say anything */

extern char *progname;		/**< argv[0] */

#define EI_BUFSZ	1024
extern uint err_buf[];		/**< buffer for RTAS call args */
/**
 * struct ei_function
 * @brief description of an error injection capabilty
 *
 * For each erro injection capability we maintain an ei_function
 * structure to define the capability.
 */
typedef struct ei_function_s {
	/*cmdline name*/
	char *name;
	/*alternate cmdline name*/
	char *alt_name;
	/*brief description of the capability*/
	char *desc;
	/*RTAS token for this capability*/
	int rtas_token;
	/*arg function*/
	int (*arg)(char, char *);
	/*capability function handler*/
	int (*func)(struct ei_function_s *);
} ei_function;

/* Error inject open functions (errinjct.c) */
int ei_open(ei_function *);
int ei_open_arg(char, char *);

/* Error inject close functions (errinjct.c) */
int ei_close(ei_function *);
int ei_close_arg(char, char *);

/* D-cache functions (dcache.c) */
int corrupted_dcache(ei_function *);
int corrupted_dcache_arg(char, char *);

/* I-cache functions (icache.c) */
int corrupted_icache(ei_function *);
int corrupted_icache_arg(char, char *);

/* Corrupted SLB functions (slb.c) */
int corrupted_slb(ei_function *);
int corrupted_slb_arg(char, char *);

/* Corrupted TLB functions (tlb.c) */
int corrupted_tlb(ei_function *);
int corrupted_tlb_arg(char, char *);

/* IOA Bus Error (EEH) (ioa_bus_error.c) */
int ioa_bus_error32(ei_function *);
int ioa_bus_error64(ei_function *);
int ioa_bus_error_arg(char, char *);

/* Platform Specific (platform.c) */
int platform_specific(ei_function *);
int platform_specific_arg(char, char *);

#define NUM_ERRINJCT_FUNCS	17

/* errinjct.c */
int do_rtas_errinjct(ei_function *);
void print_optional_args(void);
void print_cpu_arg(void);
int check_cpu_arg(void);
void print_token_arg(void);
int check_token_arg(void);
void perr(int, const char *, ...);
int open_rtas_errinjct(ei_function *);
int close_rtas_errinjct(ei_function *);
int sysfs_check(void);
char *read_file(const char *, int *);

#define HELP_FMT    "  %-15s%s\n"  /**< common help format */

#endif /* _ERRINJCT_H */
