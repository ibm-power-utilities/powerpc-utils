/**
 * @file common_numa.c
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

#include <stdio.h>
#include <errno.h>
#include <numa.h>

#include "dr.h"
#include "ofdt.h"
#include "drmem.h"		/* for DYNAMIC_RECONFIG_MEM */
#include "common_numa.h"

#define RTAS_DIRECTORY		"/proc/device-tree/rtas"
#define CHOSEN_DIRECTORY	"/proc/device-tree/chosen"
#define ASSOC_REF_POINTS	"ibm,associativity-reference-points"
#define ASSOC_LOOKUP_ARRAYS	"ibm,associativity-lookup-arrays"
#define ARCHITECTURE_VEC_5	"ibm,architecture-vec-5"

/*
 * Allocate and read a property, return the size.
 * The read property is not converted to the host endianess.
 */
static int load_property(const char *dir, const char *prop, uint32_t **buf)
{
	int size;

	size = get_property_size(dir, prop);
	if (!size)
		return -ENOENT;

	*buf = zalloc(size);
	if (!*buf) {
		say(ERROR, "Could not allocate buffer read %s (%d bytes)\n",
		    prop, size);
		return -ENOMEM;
	}

	if (get_property(dir, prop, *buf, size)) {
		free(*buf);
		say(ERROR, "Can't retrieve %s/%s\n", dir, prop);
		return -EINVAL;
	}

	return size;
}

/*
 * Get the minimal common depth, based on the form 1 of the ibm,associativ-
 * ity-reference-points property. We only support that form.
 *
 * We should check that the "ibm,architecture-vec-5" property byte 5 bit 0
 * has the value of one.
 */
static int get_min_common_depth(struct ppcnuma_topology *numa)
{
	int size;
	uint32_t *p;
	unsigned char val;

	size = load_property(CHOSEN_DIRECTORY, ARCHITECTURE_VEC_5, &p);
	if (size < 0)
		return size;

	/* PAPR byte start at 1 (and not 0) but there is the length field */
	if (size < 6) {
		report_unknown_error(__FILE__, __LINE__);
		free(p);
		return -EINVAL;
	}
	val = ((unsigned char *)p)[5];
	free(p);

	if (!(val & 0x80))
		return -ENOTSUP;

	size = load_property(RTAS_DIRECTORY, ASSOC_REF_POINTS, &p);
	if (size <= 0)
		return size;
	if (size < sizeof(uint32_t)) {
		report_unknown_error(__FILE__, __LINE__);
		free(p);
		return -EINVAL;
	}

	/* Get the first entry */
	numa->min_common_depth = be32toh(*p);
	free(p);
	return 0;
}

static int get_assoc_arrays(struct ppcnuma_topology *numa)
{
	int size;
	int rc;
	uint32_t *prop, i;
	struct assoc_arrays *aa = &numa->aa;

	size = load_property(DYNAMIC_RECONFIG_MEM, ASSOC_LOOKUP_ARRAYS, &prop);
	if (size < 0)
		return size;

	size /= sizeof(uint32_t);
	if (size < 2) {
		say(ERROR, "Could not find the associativity lookup arrays\n");
		free(prop);
		return -EINVAL;
	}

	aa->n_arrays = be32toh(prop[0]);
	aa->array_sz = be32toh(prop[1]);

	rc = -EINVAL;
	if (numa->min_common_depth > aa->array_sz) {
		say(ERROR, "Bad min common depth or associativity array size\n");
		goto out_free;
	}

	/* Sanity check */
	if (size != (aa->n_arrays * aa->array_sz + 2)) {
		say(ERROR, "Bad size of the associativity lookup arrays\n");
		goto out_free;
	}

	aa->min_array = zalloc(aa->n_arrays * sizeof(uint32_t));

	/* Keep only the most significant value */
	for (i = 0; i < aa->n_arrays; i++) {
		int prop_index = i * aa->array_sz + numa->min_common_depth + 1;

		aa->min_array[i] = be32toh(prop[prop_index]);
	}
	rc = 0;

out_free:
	free(prop);
	return rc;
}

struct ppcnuma_node *ppcnuma_fetch_node(struct ppcnuma_topology *numa, int nid)
{
	struct ppcnuma_node *node;

	if (nid > MAX_NUMNODES) {
		report_unknown_error(__FILE__, __LINE__);
		return NULL;
	}

	node = numa->nodes[nid];
	if (node)
		return node;

	node = zalloc(sizeof(struct ppcnuma_node));
	if (!node) {
		say(ERROR, "Can't allocate a new node\n");
		return NULL;
	}

	node->node_id = nid;

	if (!numa->node_count || nid < numa->node_min)
		numa->node_min = nid;
	if (nid > numa->node_max)
		numa->node_max = nid;

	numa->nodes[nid] = node;
	numa->node_count++;

	return node;
}

/*
 * Read the number of CPU for each node using the libnuma to get the details
 * from sysfs.
 */
static int read_numa_topology(struct ppcnuma_topology *numa)
{
	struct bitmask *cpus;
	struct ppcnuma_node *node;
	int rc, max_node, nid, i;

	if (numa_available() < 0)
		return -ENOENT;

	max_node = numa_max_node();
	if (max_node >= MAX_NUMNODES) {
		say(ERROR, "Too many nodes %d (max:%d)\n",
		    max_node, MAX_NUMNODES);
		return -EINVAL;
	}

	rc = 0;

	/* In case of allocation error, the libnuma is calling exit() */
	cpus = numa_allocate_cpumask();

	for (nid = 0; nid <= max_node; nid++) {

		if (!numa_bitmask_isbitset(numa_nodes_ptr, nid))
			continue;

		node = ppcnuma_fetch_node(numa, nid);
		if (!node) {
			rc = -ENOMEM;
			break;
		}

		rc = numa_node_to_cpus(nid, cpus);
		if (rc < 0)
			break;

		/* Count the CPUs in that node */
		for (i = 0; i < cpus->size; i++)
			if (numa_bitmask_isbitset(cpus, i))
				node->n_cpus++;

		numa->cpu_count += node->n_cpus;
	}

	numa_bitmask_free(cpus);

	if (rc) {
		ppcnuma_foreach_node(numa, nid, node)
			node->n_cpus = 0;
		numa->cpu_count = 0;
	}

	return rc;
}

int ppcnuma_get_topology(struct ppcnuma_topology *numa)
{
	int rc;

	rc = numa_available();
	if (rc < 0)
		return rc;

	rc = get_min_common_depth(numa);
	if (rc)
		return rc;

	rc = get_assoc_arrays(numa);
	if (rc)
		return rc;

	rc = read_numa_topology(numa);
	if (rc)
		return rc;

	if (!numa->node_count)
		return -1;

	return 0;
}
