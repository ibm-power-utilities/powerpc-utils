/**
 * @file ofdt.h
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
#ifndef _OFDT_H_
#define _OFDT_H_

#define DRC_STR_MAX 48
#define OFDT_BASE	"/proc/device-tree"
#define CPU_OFDT_BASE	"/proc/device-tree/cpus"

#define DR_PATH_MAX	1024
#define DR_STR_MAX	128
#define MAX_CPU_INTSERV_NUMS 8

struct dr_connector {
	char		name[DRC_STR_MAX];
	char		type[DRC_STR_MAX];
	char		ofdt_path[DR_PATH_MAX];
	unsigned int	index;
	unsigned int	powerdomain;
	struct dr_connector *next;
	struct dr_connector *all_next;
};

struct assoc_arrays {
	uint32_t        n_arrays;
	uint32_t        array_sz;
	uint32_t        *min_array;
};

struct mem_scn {
	struct mem_scn	*next;
	int		removable;
	uint64_t	phys_addr;
	char		sysfs_path[DR_PATH_MAX];
};

struct thread {
	int		id;			/* linux "logical" cpu id */
	uint32_t	phys_id;
	char		path[DR_PATH_MAX];	/* node path */
	struct thread	*next;
	struct thread	*sibling;
	struct dr_node	*cpu;
};

/* This structure represents a DR-capable node. Data from
 * the Open Firmware tree is gathered here. There is a pointer
 * to a linked list of the OF nodes representing the device(s)
 * connected to this slot (if they exist)
 */
struct dr_node {
	struct dr_node	*next;
	struct dr_node	*children;

	uint32_t	drc_index;
	char		drc_type[DR_STR_MAX];
	char		drc_name[DR_STR_MAX];
	uint32_t	drc_power;

	char		loc_code[DR_STR_MAX];
	char		ofdt_path[DR_PATH_MAX];
	char		*name;	/* This will just point to the name part of
				 * the ofdt_path buffer, no need to free
				 */

	char		ofdt_dname[DR_STR_MAX];
	char		sysfs_dev_path[DR_PATH_MAX];
	uint32_t	dev_type;

	uint32_t	is_owned:1;
	uint32_t	skip:1;
	uint32_t	unusable:1;
	uint32_t	is_removable:1;
	uint32_t	post_replace_processing:1;
	uint32_t	reserved:27;

	union {
		struct mem_info {
			uint64_t	_address;
			uint64_t	_lmb_size;
			uint32_t	_lmb_aa_index;
			struct mem_scn	*_mem_scns;
			struct of_node	*_of_node;
			struct dr_node	*_numa_next;
		} _smem;

#define lmb_address	_node_u._smem._address
#define lmb_size	_node_u._smem._lmb_size
#define lmb_aa_index	_node_u._smem._lmb_aa_index
#define lmb_mem_scns	_node_u._smem._mem_scns
#define lmb_of_node	_node_u._smem._of_node
#define lmb_numa_next	_node_u._smem._numa_next

		struct hea_info {
			uint		_port_no;
			uint		_port_tenure;
		}_shea;

#define hea_port_no	_node_u._shea._port_no
#define hea_port_tenure	_node_u._shea._port_tenure

		struct pci_info {
			uint            _vendor_id;	/* vendor ID */
			uint            _device_id;	/* device ID */
			uint            _class_code;	/* class code */
		}_spci;

#define pci_vendor_id	_node_u._spci._vendor_id
#define pci_device_id	_node_u._spci._device_id
#define pci_class_code	_node_u._spci._class_code

		struct phb_info {
			char	_ic_ofdt_path[DR_PATH_MAX];
		}_sphb;

#define phb_ic_ofdt_path	_node_u._sphb._ic_ofdt_path

		struct cpu_info {
			uint32_t	_intserv_nums[MAX_CPU_INTSERV_NUMS];
			int		_nthreads;
			uint32_t	_reg;
			uint32_t	_l2cache;
			struct thread	*_threads;
		}_scpu;

#define cpu_intserv_nums	_node_u._scpu._intserv_nums
#define cpu_nthreads		_node_u._scpu._nthreads
#define cpu_reg			_node_u._scpu._reg
#define cpu_l2cache		_node_u._scpu._l2cache
#define cpu_threads		_node_u._scpu._threads

	} _node_u;
};

static inline void
set_drc_info(struct dr_node *node, struct dr_connector *drc)
{
	node->drc_index = drc->index;
	node->drc_power = drc->powerdomain;

	snprintf(node->drc_name, DR_STR_MAX, "%s", drc->name);
	snprintf(node->drc_type, DR_STR_MAX, "%s", drc->type);
}

struct dr_connector *get_drc_info(const char *);
void free_drc_info(void);

char *of_to_full_path(const char *);

/* Search types for search_drc_list() */
#define DRC_NAME 	0
#define DRC_TYPE 	1
#define DRC_INDEX 	2
#define DRC_POWERDOMAIN 3

struct dr_connector *search_drc_list(struct dr_connector *,
				     struct dr_connector *, int, void *);

int get_my_drc_index(char *, uint32_t *);
int get_my_partner_drc_index(struct dr_node *, uint32_t *);
int drc_name_to_index(const char *, struct dr_connector *);
char * drc_index_to_name(uint32_t, struct dr_connector *);
int get_drc_by_name(char *, struct dr_connector *, char *, char *);
int get_drc_by_index(uint32_t, struct dr_connector *, char *, char *);

int get_min_common_depth(void);
int get_assoc_arrays(const char *dir, struct assoc_arrays *aa,
		     unsigned min_common_depth);
int of_associativity_to_node(const char *dir, unsigned min_common_depth);
int init_node(struct dr_node *);

static inline int aa_index_to_node(struct assoc_arrays *aa, uint32_t aa_index)
{
	if (aa_index < aa->n_arrays)
		return aa->min_array[aa_index];
	return -1;
}

#endif /* _OFDT_H_ */
