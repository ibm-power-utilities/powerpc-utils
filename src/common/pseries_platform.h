/**
 * file pseries_platform.h
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
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#define PLATFORM_FILE	"/proc/cpuinfo"

enum {
	PLATFORM_UNKNOWN = 0,
	PLATFORM_POWERNV,
	PLATFORM_POWERKVM_GUEST,
	PLATFORM_PSERIES_LPAR,
	/* Add new platforms here */
	PLATFORM_MAX,
};

extern const char *platform_name;

extern int get_platform(void);

#endif
