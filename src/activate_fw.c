/**
 * @file activate_fw.c
 * @brief Activate Firmware command
 */
/**
 * @mainpage activate_firmware documentation
 * @section Copyright
 * Copyright (c) 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @section Overview
 * Simple command to call the "ibm,activate-firmware" rtas call via librtas.so
 * when called without any option. With -e option, the command either fetches
 * the current access key expiry date or sets the access key expiry date if
 * <keyfile> is provided.
 *
 * The return codes for this command are as follows:<br>
 *      0 - Success!!<br>
 *      1 - This platform doesn't support concurrent activation of firmware.<br>
 *      2 - There's no new firmware ready to activate (RTAS returned -9001).<br>
 *      3 - You must have root authority to run this command.<br>
 *	4 - Hardware failure (RTAS returned -1).<br>
 *	5 - Memory/resource allocation error.<br>
 *	6 - General error.<br>
 *	7 - Error in case of getting UAK expiry date or setting UAK.<br>
 *	8 - Parameter error when activating firmware.<br>
 *
 * For the specific mappings of librtas and rtas_call return codes (librtas
 * return codes are in all caps) to the return codes listed above see the
 * switch statement in the code.  There are two values that can be returned
 * by the rtas call but are not explicitly handled below and are handled by
 * the default case statement. These are -2 (busy, try again) and 990x
 * (extended delay).  The librtas module intercepts these return codes and
 * handles them itself, they should never be returned from librtas.
 *
 * In case of keyfile and general errors, the appropriate error description
 * (if available) is written to stderr.
 *
 * @author Nathan Fontenot <nfont@linux.vnet.ibm.com>
 * @author Chandni Verma <chandni@linux.vnet.ibm.com>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <endian.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <err.h>
#include <librtas.h>
#include "librtas_error.h"
#include "pseries_platform.h"

/**
 * @def say(_f, _a...)
 * @brief DEBUG definition of printf
 */
#ifdef DEBUG
#define say(_f, _a...)	printf(_f, ##_a)
#else
#define say(_f, _a...)
#endif

/* Size of array */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Parameter tokens */
#define SYS_PARAM_UAK_EXPIRY_DATE 53
#define SYS_PARAM_UAK_KEY 54

/* Update Access Key expiry date buffer length */
#define UAK_EXPIRY_DATE_DATA_LENGTH 11

/* Actual Update Access Key length */
#define UAK_KEY_LENGTH 34

/* UAK key data buffer length */
#define UAK_KEY_DATA_LENGTH (UAK_KEY_LENGTH + 3)

/* Size of buffer recording RTAS error description */
#define ERR_BUF_SIZE 40

#define UAK_ERROR 7

static int activate_firmware(void)
{
	int rc;

	rc = rtas_activate_firmware();

	/* Map 'rc' to valid return code listed above */
	switch (rc) {
	/* 0 - Success!! */
	case 0:
		say("activate_firmware: rtas call succeeded\n");
		break;

	/* 1 - activate-firmware not supported */
	case RTAS_KERNEL_INT:  /* No kernel interface to firmware */
	case RTAS_KERNEL_IMP:  /* No kernel implementation of function */
	case RTAS_UNKNOWN_OP:  /* No firmware implementation of function */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 1);
		rc = 1;
		break;

	/* 2 - no new firmware to activate */
	case -9001:	   /* No valid firmware to activate */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 2);
		rc = 2;
		break;

	/* 3 - no root authority  */
	case RTAS_PERM:	   /* No root authority */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 3);
		rc = 3;
		 break;

	/* 4 - hardware error */
	case -1:	   /* Hardware error */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 4);
		rc = 4;
		break;

	/* 5 - Memory/resource allocation error */
	case RTAS_NO_MEM:
	case RTAS_NO_LOWMEM:
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 5);
		rc = 5;
		break;

	case -3:
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 8);
		rc = 8;
		break;

	/* 6 - catch all other return codes here */
	default:
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 6);
		rc = 6;
		break;
	}

	return rc;
}

static int get_uak_expiry_date(void)
{
	int rc;
	char date[UAK_EXPIRY_DATE_DATA_LENGTH] = {0};
	char msg[120];
	char error_buf[ERR_BUF_SIZE];

	rc = rtas_get_sysparm(SYS_PARAM_UAK_EXPIRY_DATE, ARRAY_SIZE(date),
			      date);

	if (rc == 0) {
		/* +2 since first 2 bytes in date buffer filled by the RTAS call
		 * are used for storing length of the buffer excluding the first
		 * two bytes and including ending '\0'
		 */
		printf("Update Access Key expiry date (yyyymmdd) is: %s\n",
		       date + 2);
	} else {
		switch (rc) {
		case -1:
			warnx("Hardware Error");
			break;
		case -2:
			warnx("Busy, Try again later");
			break;
		case -3:
			warnx("System parameter not supported");
			break;
		case -9002:
			warnx("Not authorized");
			break;
		case -9999:
			warnx("Parameter Error");
			break;
		case 9900 ... 9905:
			strcat(msg, "Delay of %ld milliseconds is expected ");
			strcat(msg, "before calling ibm,get-system-parameter ");
			strcat(msg, "with the same parameter index");

			warnx(msg, (long) pow(10, rc-9900));
			break;
		default:
			if (is_librtas_error(rc)) {
				librtas_error(rc, error_buf, ERR_BUF_SIZE);
				warnx("%s", error_buf);
			} else {
				warnx("Unknown error");
			}
		}

		rc = UAK_ERROR;
	}

	return rc;
}

static bool is_keyfile_valid(const char *keyfile, char *keydata)
{
	int fd;
	int len;
	uint16_t size;

	fd = open(keyfile, O_RDONLY);
	if (fd == -1) {
		/* errno is set appropriately by the call to open() */
		perror("Keyfile error");
		return false;
	}

	/* First two bytes of data are used for storing the length.
	 * +1 read for validation.
	 */
	len = read(fd, keydata + 2, UAK_KEY_LENGTH + 1);
	close(fd);

	if (len != UAK_KEY_LENGTH) {
		warnx("Keyfile of incorrect length");
		return false;
	}

	/* Terminating '\0' is implied as buffer has been set to 0
	 * and exactly UAK_KEY_LENGTH bytes have been copied
	 */

	/* Fill first 2 bytes with the UAK length + 1 (+1 for ending '\0') */
	size = htobe16(UAK_KEY_LENGTH + 1);
	memcpy(keydata, &size, sizeof(size));

	return true;
}

static int apply_uak_key(const char *keyfile)
{
	int rc = 0;
	char keyvalue[UAK_KEY_DATA_LENGTH + 1] = {0}; /* +1 for validation */
	char msg[120];
	char error_buf[ERR_BUF_SIZE];

	if (!is_keyfile_valid(keyfile, keyvalue))
		return UAK_ERROR;

	rc = rtas_set_sysparm(SYS_PARAM_UAK_KEY, keyvalue);

	if (rc == 0) {
		printf("Update Access Key set successfully\n");
	} else {
		switch (rc) {
		case -1:
			warnx("Hardware Error");
			break;
		case -2:
			warnx("Busy, Try again later");
			break;
		case -3:
			warnx("System parameter not supported");
			break;
		case -9002:
			warnx("Setting not allowed/authorized");
			break;
		case -9999:
			warnx("Parameter Error");
			break;
		case 9900 ... 9905:
			strcat(msg, "Delay of %ld milliseconds is expected ");
			strcat(msg, "before calling ibm,set-system-parameter ");
			strcat(msg, "with the same parameter index");

			warnx(msg, (long) pow(10, rc-9900));
			break;
		default:
			if (is_librtas_error(rc)) {
				librtas_error(rc, error_buf, ERR_BUF_SIZE);
				warnx("%s", error_buf);
			}
		}

		rc = UAK_ERROR;
	}

	return rc;
}

static void print_usage(const char *cmd)
{
	printf("Usage: %s [-e [keyfile]]\n", cmd);
	printf("Without any option, the activate_firmware utility will cause");
	printf(" a firmware image that has already been flashed to be");
	printf(" activated concurrently.\n");
	printf("\n\tOption summary:\n");
	printf("\t-e:           prints the current Update Access Key expiry");
	printf(" date\n");
	printf("\t-e <keyfile>: applies the provided Update Access key-file");
	printf(" to extend the service expiry date\n\n");
}

int main(int argc, char *argv[])
{
	int opt;
	char *key = NULL;
	bool e_flag = false;
	int rc;

	if (get_platform() != PLATFORM_PSERIES_LPAR)
		errx(1,	"activate_firmware is not supported on the %s platform",
		     platform_name);

	while ((opt = getopt(argc, argv, "e::h")) != -1) {
		switch (opt) {
		case 'e':
			e_flag = true;
			/* optarg isn't set to the option argument if it's
			 * optional hence using optind
			 */
			if (argv[optind] && argv[optind][0] != '-') {
				key = argv[optind];
				/* So that getopt doesn't try to parse
				 * the keyfile option argument
				 */
				optind++;
			}
			break;
		case 'h':
		case '?':
			print_usage(argv[0]);
			return (opt == 'h' ? 0 : -1);
		}
	}

	if (e_flag) {
		if (key)
			rc = apply_uak_key(key);
		else
			rc = get_uak_expiry_date();
	} else {
		rc = activate_firmware();
	}
	return rc;
}
