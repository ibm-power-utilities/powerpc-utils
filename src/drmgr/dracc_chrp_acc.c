/**
 * @file dracc_chrp_acc.c
 *
 * Copyright (C) IBM Corporation 2022
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <inttypes.h>
#include <dirent.h>
#include <time.h>
#include "dr.h"

#define SYSFS_VAS_QOSCREDIT_FILE "/sys/devices/virtual/misc/vas/vas0/gzip/qos_capabilities/update_total_credits"

static char *acc_usagestr = "-c acc -t <accelType> -q <QoS_credit_count>";

/**
 * acc_usage
 *
 */
void
acc_usage(char **ausage)
{
	*ausage = acc_usagestr;
}

int valid_acc_options(void)
{
	if (!usr_t_option) {
		say(ERROR, "-t gzip must be specified\n");
		return -1;
	}

	if (usr_drc_type != DRC_TYPE_ACC) {
		say(ERROR, "The value \"%d\" for the -c option is not valid\n",
				usr_drc_type);
		return -1;
	}

	/*
	 * Only gzip Accelerator type is supported right now
	 */
	if (strcmp(usr_t_option, "gzip") == 0) {
		say(ERROR, "Invalid Accelerator type: %s\n", usr_t_option);
		return -1;
	}

	return 0;
}

int dracc_chrp_acc(void)
{
	int fd, rc;
	char buf[64];

	if (strcmp(usr_t_option, "gzip") == 0) {
		say(ERROR, "Invalid Accelerator type: %s\n", usr_t_option);
		return -1;
	}

	fd = open(SYSFS_VAS_QOSCREDIT_FILE, O_WRONLY);
	if (fd < 0) {
		say(ERROR, "Could not open \"%s\" to write QoS credits\n",
			SYSFS_VAS_QOSCREDIT_FILE);
		return -1;
	}

	sprintf(buf, "%d", usr_drc_count);

	rc = write(fd, buf, strlen(buf));
	close(fd);
	if (rc < 0) {
		say(ERROR, "Could not write QoS credits\n");
		return errno;
	}

	say(DEBUG, "Successful update total QoS credits\n");

	return 0;
}
