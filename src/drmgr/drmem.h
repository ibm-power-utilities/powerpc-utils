/*
 * @file drmem.h
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
#include "drpci.h"

struct lmb_list_head {
	struct dr_node	*lmbs;
	struct dr_node	*last;
	char		*drconf_buf;
	int		drconf_buf_sz;
	unsigned	lmbs_modified;
	int		sort;
	int		lmbs_found;
};

struct drconf_mem {
	uint64_t	address;
	uint32_t	drc_index;
	uint32_t	reserved;
	uint32_t	assoc_index;
	uint32_t	flags;
};

struct drconf_mem_v2 {
	uint32_t	seq_lmbs;
	uint64_t	base_addr;
	uint32_t	drc_index;
	uint32_t	aa_index;
	uint32_t	flags;
} __attribute__((packed));

#define DRMEM_ASSIGNED		0x00000008
#define DRMEM_DRC_INVALID	0x00000020

#define MEM_PROBE_FILE		"/sys/devices/system/memory/probe"
#define MEM_BLOCK_SIZE_BYTES	"/sys/devices/system/memory/block_size_bytes"
#define DYNAMIC_RECONFIG_MEM	"/proc/device-tree/ibm,dynamic-reconfiguration-memory"
#define DYNAMIC_RECONFIG_MEM_V1	DYNAMIC_RECONFIG_MEM "/ibm,dynamic-memory"
#define DYNAMIC_RECONFIG_MEM_V2	DYNAMIC_RECONFIG_MEM "/ibm,dynamic-memory-v2"

#define LMB_NORMAL_SORT		0
#define LMB_REVERSE_SORT	1
#define LMB_RANDOM_SORT		2

extern uint64_t block_sz_bytes;

struct lmb_list_head *get_lmbs(unsigned int);
void free_lmbs(struct lmb_list_head *);
