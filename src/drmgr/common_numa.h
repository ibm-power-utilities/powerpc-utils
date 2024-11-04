/**
 * @file numa.h
 *
 * Copyright (C) IBM Corporation 2020
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
#ifndef _NUMA_H_
#define _NUMA_H_

#define MAX_NUMNODES	256
#define NUMA_NO_NODE	-1

struct ppcnuma_node {
	int		node_id;
	unsigned int	n_cpus;
	unsigned int	n_lmbs;
	unsigned int	ratio;
	struct dr_node	*lmbs;			/* linked by lmb_numa_next */
	struct ppcnuma_node *ratio_next;
};

struct ppcnuma_topology {
	unsigned int		cpu_count;
	unsigned int		lmb_count;
	unsigned int		cpuless_node_count;
	unsigned int		cpuless_lmb_count;
	unsigned int		node_count, node_min, node_max;
	struct ppcnuma_node	*nodes[MAX_NUMNODES];
	struct ppcnuma_node	*ratio;
	int			min_common_depth;
	struct assoc_arrays	aa;
};

int ppcnuma_get_topology(struct ppcnuma_topology *numa);
struct ppcnuma_node *ppcnuma_fetch_node(struct ppcnuma_topology *numa,
					unsigned node_id);

static inline unsigned ppcnuma_next_node(struct ppcnuma_topology *numa, unsigned nid,
				         struct ppcnuma_node **node)
{
	for (nid++; nid <= numa->node_max; nid++)
		if (numa->nodes[nid]) {
			*node = numa->nodes[nid];
			break;
		}
	return nid;
}

#define ppcnuma_foreach_node(numa, nid, node)				\
	for (nid = (numa)->node_min, node = (numa)->nodes[nid];	\
	     nid <= (numa)->node_max;					\
	     nid = ppcnuma_next_node(numa, nid, &(node)))

#define ppcnuma_foreach_node_by_ratio(numa, node)				\
	for (node = (numa)->ratio; node; node = node->ratio_next)

#endif /* _NUMA_H_ */
