/**
 * @file rtas_ibm_get_vpd.c
 * @brief helper utility for retrieving dynamic VPD on IBM ppc64-based systems.
 */
/**
 * @mainpage rtas_ibm_get_vpd documentation
 * @section Copyright
 * Copyright (c) 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @section Overview
 * The rtas_ibm_get_vpd utility is a utility to assist inventory retrieval 
 * applications by gathering dynamically changing vital product data on 
 * IBM ppc64 systems.   The output of this utility is formatted to be parsed 
 * by other applications; as such, it is not intended for general 
 * command-line usage, though there is no reason that it should not be used 
 * in that manner.
 *
 * @author Michael Strosaker <strosake@us.ibm.com>
 * @author Martin Schwenke <schwenke@au1.ibm.com>
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>
#include <librtas.h>
#include "librtas_error.h"
#include "pseries_platform.h"

#define PROC_FILE_RTAS_CALL "/proc/device-tree/rtas/ibm,get-vpd"
#define BUF_SIZE	2048
#define ERR_BUF_SIZE	40

/* Return codes from the RTAS call (not already handled by librtas) */
#define SUCCESS		0
#define CONTINUE	1
#define HARDWARE_ERROR	-1
#define PARAMETER_ERROR	-3
#define VPD_CHANGED	-4

/**
 * @struct buf_element
 * @brief List element for data returned by rtas_get_vpd()
 */
struct buf_element {
	char buf[BUF_SIZE];		/**< data buffer for rtas_get_vpd() */
	struct buf_element *next;
	unsigned int size;		/**< amount of the buffer filled in 
					 *   by rtas_get_vpd() */
};

/**
 * print_usage
 * @brief print the usage statement for rtas_ibm_get_vpd
 *
 * @param cmd command name for rtas_ibm_get_vpd invocation (argv[0])
 */
void print_usage(char *cmd) {
	printf ("Usage: %s [-l location_code] [-h]\n", cmd);
}

/**
 * print_help
 * @brief print the help statement for rtas_ibm_get_vpd
 *
 * @param cmd command name for rtas_ibm_get_vpd invocation (argv[0])
 */
void print_help(char *cmd) 
{
	print_usage(cmd);
	printf ("  -l location_code  print the dynamic VPD for the specified location code\n");
	printf ("                    if the -l option is not used, all dynamic VPD will be printed\n");
	printf ("  -h                print this help message\n");
}

/**
 * check_rtas_call
 * @brief Ensure that the ibm,get-vpd property exists in the OF tree
 *
 * @return 0 on success, !0 otherwise
 */
int check_rtas_call(void) 
{
	int fd;

	if ((fd = open(PROC_FILE_RTAS_CALL, O_RDONLY, 0)) == -1) {
		return 0;
	}
	close(fd);
	return 1;
}

/**
 * delete_list
 * @brief free all of the elements on a list
 *
 * @param elem pointer to the beginning of the list to delete
 */ 
void delete_list(struct buf_element *elem) 
{
	if (!elem)
		return;
	delete_list(elem->next);
	free (elem);
	return;
}

int main(int argc, char **argv) 
{
	char *loc_code = "";
	char err_buf[ERR_BUF_SIZE];
	int lflag = 0, rc, c;
	unsigned int seq = 1, next_seq;
	struct buf_element *list, *current;

	switch (get_platform()) {
	case PLATFORM_UNKNOWN:
	case PLATFORM_POWERKVM_HOST:
		fprintf(stderr, "%s: is not supported on the %s platform\n",
							argv[0], platform_name);
		exit(1);
	}

	if (!check_rtas_call()) {
		fprintf(stderr, "The ibm,get-vpd RTAS call is not available "
			"on this system.\n");
		return 4;
	}

	/* Parse command line options */
	opterr = 0;
	while ((c = getopt (argc, argv, "l:h")) != -1) {
		switch (c) {
		case 'l':
			loc_code = optarg;
			lflag = 1;
			break;
		case 'h':
			print_help(argv[0]);
			return 0;
		case '?':
			if (isprint (optopt))
				fprintf(stderr, "Unrecognized option: -%c.\n", 
					optopt);
			else
				fprintf(stderr, "Unrecognized option character "
					"\\x%x.\n", optopt);
			print_usage(argv[0]);
			return 1;
		default:
			abort();
		}
	}

	list = (struct buf_element *)malloc(sizeof(struct buf_element));
	if (!list) {
		fprintf(stderr, "Out of memory\n");
		return 5;
	}
	list->size = 0;
	list->next = NULL;
	current = list;

	do {
		rc = rtas_get_vpd(loc_code, current->buf, BUF_SIZE,
				seq, &next_seq, &(current->size));

		switch (rc) {
		case CONTINUE:
			seq = next_seq;
			current->next = (struct buf_element *)
					malloc(sizeof(struct buf_element));
			if (!current->next) {
				fprintf(stderr, "Out of memory\n");
				delete_list(list);
				return 5;
			}
			current = current->next;
			current->size = 0;
			current->next = NULL;
			/* fall through */
		case SUCCESS:
			break;
		case VPD_CHANGED:
			seq = 1;
			delete_list(list);
			list = (struct buf_element *)
					malloc(sizeof(struct buf_element));
			if (!list) {
				fprintf(stderr, "Out of memory\n");
				return 5;
			}
			list->size = 0;
			list->next = NULL;
			current = list;
			break;
		case PARAMETER_ERROR:
			delete_list(list);
			return 1;
		case HARDWARE_ERROR:
			delete_list(list);
			return 2;
		default:
			delete_list(list);
			if (is_librtas_error(rc)) {
				librtas_error(rc, err_buf, ERR_BUF_SIZE);
				fprintf(stderr, "Could not gather vpd\n%s\n", err_buf);
			} else {
				fprintf(stderr, "Could not gather vpd\n");
			}

	                return 3;
        	}
	} while(rc != SUCCESS);

	current = list;
	do {
		size_t count;

		if (current->size <= 0) 
			continue;
		
		count = fwrite(current->buf, 1, current->size, stdout);
		if (count < current->size)
			break;

	} while ((current = (current->next)) != NULL);

	delete_list(list);

	return 0;
}

