/**
 * @file cpu_info_helpers.h
 * @brief Header of common routines to capture cpu information
 *
 * Copyright (c) 2007, 2020 International Business Machines
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
 * @author Anton Blanchard <anton@au.ibm.com>
 * @author Kamalesh Babulal <kamaleshb@linux.vnet.ibm.com>
 */
#ifndef _CPU_INFO_HELPERS_H
#define _CPU_INFO_HELPERS_H

#define SYSFS_CPUDIR    "/sys/devices/system/cpu/cpu%d"
#define SYSFS_SUBCORES  "/sys/devices/system/cpu/subcores_per_core"
#define INTSERV_PATH    "/proc/device-tree/cpus/%s/ibm,ppc-interrupt-server#s"

#define SYSFS_PATH_MAX	128

extern int __sysattr_is_readable(char *attribute, int threads_in_system);
extern int __sysattr_is_writeable(char *attribute, int threads_in_system);
extern int cpu_physical_id(int thread);
extern int cpu_online(int thread);
extern int is_subcore_capable(void);
extern int num_subcores(void);
extern int get_attribute(char *path, const char *fmt, int *value);
extern int get_cpu_info(int *threads_per_cpu, int *cpus_in_system,
			int *threads_in_system);
extern int __is_smt_capable(int threads_in_system);
extern int __get_one_smt_state(int core, int threads_per_cpu);
extern int __do_smt(bool numeric, int cpus_in_system, int threads_per_cpu,
		    bool print_smt_state);

#endif /* CPU_INFO_H */
