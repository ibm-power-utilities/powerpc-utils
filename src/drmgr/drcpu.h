/**
 * @file drcpu.h
 *
 * Copyright (C) IBM Corporation 2006
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

#ifndef _H_DRCPU
#define _H_DRCPU

#include "dr.h"

#define CPU_PROBE_FILE		"/sys/devices/system/cpu/probe"
#define CPU_RELEASE_FILE	"/sys/devices/system/cpu/release"

struct cache_info {
	char		name[DR_BUF_SZ];	/* node name */
	char		path[DR_BUF_SZ];	/* node path */
	uint32_t	phandle;
	uint32_t	l2cache;
	uint32_t	removed;
	struct cache_info *next;		/* global list */
};

struct dr_info {
	struct dr_node *all_cpus;
	struct cache_info *all_caches;
	struct thread *all_threads;
};

int init_cpu_drc_info(struct dr_info *);
void free_cpu_drc_info(struct dr_info *);

int get_thread_state(struct thread *);
int set_thread_state(struct thread *, int);

int get_cpu_state(struct dr_node *);
int offline_cpu(struct dr_node *);
int online_cpu(struct dr_node *, struct dr_info *);

int cpu_enable_smt(struct dr_node *, struct dr_info *);
int cpu_disable_smt(struct dr_node *);

int smt_enabled(struct dr_info *);
int system_enable_smt(struct dr_info *);
int system_disable_smt(struct dr_info *);

struct cache_info * cpu_get_dependent_cache(struct dr_node *, struct dr_info *);
struct cache_info * cache_get_dependent_cache(struct cache_info *,
					      struct dr_info *);
int release_cpu(struct dr_node *, struct dr_info *);
int probe_cpu(struct dr_node *, struct dr_info *);
struct dr_node *get_available_cpu(struct dr_info *);

#endif /* _H_DRCPU */
