/**
 * @file rtas_dbg.c
 * @brief rtas_dbg command
 *
 * Copyright (c) 2015 International Business Machines
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
 *
 * The rtas_dbg tool enables rtas debug output to the system console
 * for a given rtas call. Currently, rtas_dbg is only supported on
 * PowerVM systems.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <dirent.h>
#include <string.h>
#include <endian.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include "pseries_platform.h"

/* Syscall number */
#ifndef __NR_rtas
#define __NR_rtas 255
#endif

#define MAX_ARGS		16
#define RTAS_DBG_ENABLE		0x81
#define OFDT_RTAS_PATH		"/proc/device-tree/rtas"
#define MAX_RTAS_NAME_LEN	80

#ifdef _syscall1
_syscall1(int, rtas, void *, args);
#else
#define rtas(args) syscall(__NR_rtas, (args))
#endif

typedef uint32_t rtas_arg_t;

struct rtas_args {
	uint32_t token;
	uint32_t ninputs;
	uint32_t nret;
	rtas_arg_t args[MAX_ARGS];
	rtas_arg_t *rets;     /* Pointer to return values in args[]. */
};

struct rtas_token {
	struct rtas_token	*next;
	uint32_t		token;
	char			*name;
};

void usage(void)
{
	fprintf(stderr, "Usage: rtas_dbg [-l] <rtas token | rtas name>\n");
	fprintf(stderr, "\t-l    Print the specified rtas token or all tokens if not specified\n");
}

void free_rtas_tokens(struct rtas_token *tok_list)
{
	struct rtas_token *tok;

	while (tok_list != NULL) {
		tok = tok_list;
		tok_list = tok->next;
		free(tok->name);
		free(tok);
	}
}

void insert_token(struct rtas_token *tok, struct rtas_token **tok_listp)
{
	struct rtas_token *tmp, *prev = NULL;

	if (*tok_listp == NULL) {
		tok->next = NULL;
		*tok_listp = tok;
		return;
	}

	/* This list won't get big, just brute force it :) */
	for (tmp = *tok_listp; tmp; prev = tmp, tmp = tmp->next) {
		if (strncmp(tmp->name, tok->name, MAX_RTAS_NAME_LEN) >= 0)
			break;
	}

	tok->next = tmp;
	if (prev)
		prev->next = tok;
	if (tmp == *tok_listp)
		*tok_listp = tok;
}

struct rtas_token *get_rtas_tokens(void)
{
	DIR	*dir;
	struct dirent *dp;
	struct rtas_token *tok_list = NULL;
	uint32_t betoken;

	dir = opendir(OFDT_RTAS_PATH);
	if (dir == NULL) {
		fprintf(stderr, "Could not open %s:\n%s\n", OFDT_RTAS_PATH,
			strerror(errno));
		return NULL;
	}

	while ((dp = readdir(dir)) != NULL) {
		FILE *fp;
		struct rtas_token *tok;
		char dir[512];
		int rc;

		if (dp->d_name[0] == '.')
			continue;

		snprintf(dir, sizeof(dir), "%s/%s", OFDT_RTAS_PATH, dp->d_name);

		fp = fopen(dir, "r");
		if (fp == NULL) {
			fprintf(stderr, "Could not get rtas token for %s\n",
				dp->d_name);
			continue;
		}

		rc = fread(&betoken, sizeof(betoken), 1, fp);
		fclose(fp);
		if (rc <= 0) {
			fprintf(stderr, "Could not get rtas token for %s\n",
				dp->d_name);
			continue;
		}

		tok = malloc(sizeof(*tok));
		if (tok == NULL) {
			fprintf(stderr, "Could not allocate token list\n");
			free_rtas_tokens(tok_list);
			tok_list = NULL;
			break;
		}

		tok->token = be32toh(betoken);
		tok->name = strdup(dp->d_name);
		insert_token(tok, &tok_list);
	}

	closedir(dir);
	return tok_list;
}

struct rtas_token *get_rtas_token_by_name(char *name,
					  struct rtas_token *tok_list)
{
	struct rtas_token *tok;

	for (tok = tok_list; tok; tok = tok->next) {
		if (!strncmp(name, tok->name, MAX_RTAS_NAME_LEN))
			return tok;
	}

	return NULL;
}

struct rtas_token *get_rtas_token_by_value(unsigned value,
					   struct rtas_token *tok_list)
{
	struct rtas_token *tok;

	for (tok = tok_list; tok; tok = tok->next) {
		if (value == tok->token)
			return tok;
	}

	return NULL;
}

void print_rtas_tokens(struct rtas_token *tok, struct rtas_token *tok_list)
{
	struct rtas_token *t;

	if (tok)
		printf("%-40s%u\n", tok->name, tok->token);
	else {
		for (t = tok_list; t; t = t->next)
			printf("%-40s%u\n", t->name, t->token);
	}
}

int set_rtas_dbg(struct rtas_token *tok)
{
	struct rtas_args args;
	int rc;

	args.token = htobe32(RTAS_DBG_ENABLE);
	args.ninputs = htobe32(1);
	args.nret = htobe32(1);
	args.args[0] = htobe32(tok->token);

	printf("Enabling rtas debug for %s (%u)\n", tok->name, tok->token);

	rc = rtas(&args);

	if (rc)
		fprintf(stderr, "RTAS syscall failure, errno=%d\n", errno);

	return rc;
}

int main(int argc, char *argv[])
{
	struct rtas_token *tok_list = NULL;
	struct rtas_token *tok = NULL;
	int print_tokens = 0;
	char *dbg_arg = NULL;
	int c, rc;

	if (get_platform() != PLATFORM_PSERIES_LPAR) {
		fprintf(stderr, "%s: is not supported on the %s platform\n",
							argv[0], platform_name);
		exit(1);
	}

	tok_list = get_rtas_tokens();
	if (tok_list == NULL)
		return -1;

	while ((c = getopt(argc, argv, "l")) != -1) {
		switch (c) {
		case 'l':
			print_tokens++;
			break;
		}
	}

	dbg_arg = argv[optind];

	if (dbg_arg == NULL) {
		if (print_tokens) {
			print_rtas_tokens(NULL, tok_list);
			free_rtas_tokens(tok_list);
			return 0;
		}

		fprintf(stderr, "A rtas name or token must be specified\n");
		free_rtas_tokens(tok_list);
		usage();
		return -1;
	}

	if ((dbg_arg[0] >= '0') && (dbg_arg[0] <= '9'))
		tok = get_rtas_token_by_value(strtoul(dbg_arg, NULL, 0),
					      tok_list);
	else
		tok = get_rtas_token_by_name(dbg_arg, tok_list);

	if (tok != NULL) {
		if (print_tokens) {
			print_rtas_tokens(tok, tok_list);
			rc = 0;
		} else {
			rc = set_rtas_dbg(tok);
		}
	} else {
		fprintf(stderr, "Unknown rtas token or name specified: %s\n",
			dbg_arg);
		usage();
		rc = -1;
	}

	free_rtas_tokens(tok_list);

	return rc;
}
