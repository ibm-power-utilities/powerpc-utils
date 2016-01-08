/**
 * @file        platform.c
 *
 * Copyright (C) 2014 IBM Corporation
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
 * @author      Aruna Balakrishnaiah <aruna@linux.vnet.ibm.com>
 */

#include <stdio.h>
#include <string.h>
#include "pseries_platform.h"

#define LENGTH		512

const char *power_platform_name[] = {
        "Unknown",
        "PowerNV",
        "Power KVM pSeries Guest",
        "PowerVM pSeries LPAR",
        /* Add new platforms name here */
};

const char *platform_name;

int
get_platform(void)
{
	int rc = PLATFORM_UNKNOWN;
	FILE *fp;
	char line[LENGTH];

	if((fp = fopen(PLATFORM_FILE, "r")) == NULL)
		return rc;

	while (fgets(line, LENGTH, fp)) {
		if (strstr(line, "PowerNV")) {
			rc = PLATFORM_POWERNV;
			break;
		} else if (strstr(line, "IBM pSeries (emulated by qemu)")) {
			rc = PLATFORM_POWERKVM_GUEST;
			break;
		} else if (strstr(line, "pSeries")) {
			rc = PLATFORM_PSERIES_LPAR;
			/* catch model for PowerNV guest */
			continue;
		}
	}

	platform_name = power_platform_name[rc];

	fclose(fp);
	return rc;
}
