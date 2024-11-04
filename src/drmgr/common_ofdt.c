/**
 * @file common_ofdt.c
 * @brief Common routines for Open Firmware Device Tree access
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <endian.h>
#include <errno.h>
#include "dr.h"
#include "ofdt.h"

#define RTAS_DIRECTORY		"/proc/device-tree/rtas"
#define CHOSEN_DIRECTORY	"/proc/device-tree/chosen"
#define ASSOC_REF_POINTS	"ibm,associativity-reference-points"
#define ASSOC_LOOKUP_ARRAYS	"ibm,associativity-lookup-arrays"
#define ARCHITECTURE_VEC_5	"ibm,architecture-vec-5"
#define ASSOCIATIVITY		"ibm,associativity"

struct of_list_prop {
	char	*_data;
	char	*val;
	int	n_entries;
};

struct drc_prop_grp {
	struct of_list_prop drc_names;
	struct of_list_prop drc_types;
	struct of_list_prop drc_indexes;
	struct of_list_prop drc_domains;
};

struct drc_info {
	char	*drc_type;
	char	*drc_name_prefix;
	int	drc_index_start;
	int	drc_name_suffix_start;
	int	n_seq_elems;
	int	seq_inc;
	int	drc_power_domain;
};

struct dr_connector *all_drc_lists = NULL;

/**
 * alloc_node
 *
 * XXX: This doesn't do any cleanup on error conditions.  could be bad.
 *
 * @param drc
 * @param dev_type
 * @param of_path
 * @returns pointer to node on success, NULL otherwise
 */
struct dr_node *
alloc_dr_node(struct dr_connector *drc, int dev_type, const char *of_path)
{
	struct dr_node *node;

	node = zalloc(sizeof(*node));
	if (node == NULL)
		return NULL;

	node->dev_type = dev_type;
	set_drc_info(node, drc);

	if (of_path) {
		get_property(of_path, "ibm,loc-code", node->loc_code,
			     sizeof(node->loc_code));

		snprintf(node->ofdt_path, DR_PATH_MAX, "%s", of_path);
	}

	return node;
}

/**
 * free_node
 * @brief free a list of node struct and any allocated memory they reference
 *
 * @param node_list list of nodes to free
 */
void
free_node(struct dr_node *node_list)
{
	struct dr_node *node;

	if (node_list == NULL)
		return;

	while (node_list) {
		node = node_list;
		node_list = node->next;

		if (node->children)
			free_node(node->children);

		if (node->dev_type == MEM_DEV) {
			struct mem_scn *mem_scn;

			while (node->lmb_mem_scns != NULL) {
				mem_scn = node->lmb_mem_scns;
				node->lmb_mem_scns = mem_scn->next;
				free(mem_scn);
			}

			if (node->lmb_of_node)
				free(node->lmb_of_node);
		}

		free(node);
	}
}

/**
 * get_of_list_prop
 * @breif retrieve the specified open firmware property list
 *
 * @param full_path
 * @param prop_name
 * @param prop
 * @returns 0 on success, !0 otherwise
 */
static int
get_of_list_prop(char *full_path, char *prop_name, struct of_list_prop *prop)
{
	int size, rc;

	size = get_property_size(full_path, prop_name);

	prop->_data = zalloc(size);
	if (prop->_data == NULL)
		return -1;

	rc = get_property(full_path, prop_name, prop->_data, size);
	if (rc) {
		free(prop->_data);
		return -1;
	}

	prop->n_entries = be32toh(*(uint *)prop->_data);
	if (prop->n_entries == 0) {
		say(ERROR, "No entries found in %s/%s\n",
		    full_path, prop_name);
		return -1;
	}

	prop->val = prop->_data + sizeof(uint);
	return 0;
}

/**
 * get_drc_prop_grp
 *
 * @param full_path
 * @param group
 * @returns 0 on success, !0 otherwise
 */
static int
get_drc_prop_grp(char *full_path, struct drc_prop_grp *group)
{
	struct stat sbuf;
	char fname[DR_PATH_MAX];
	int rc;

	memset(group, 0, sizeof(*group));

	sprintf(fname, "%s/%s", full_path, "ibm,drc-names");
	rc = stat(fname, &sbuf);
	if (rc)
		return rc;

	rc = get_of_list_prop(full_path, "ibm,drc-names", &group->drc_names);
	if (rc)
		return rc;

	rc = get_of_list_prop(full_path, "ibm,drc-types", &group->drc_types);
	if (rc)
		return rc;

	rc = get_of_list_prop(full_path, "ibm,drc-indexes",
			&group->drc_indexes);
	if (rc)
		return rc;

	rc = get_of_list_prop(full_path, "ibm,drc-power-domains",
			&group->drc_domains);
	if (rc)
		return rc;

	return 0;
}

/**
 * free_drc_props
 * @brief free the properties associated with a drc group
 *
 * @param group
 */
static void
free_drc_props(struct drc_prop_grp *group)
{
	if (group->drc_names._data)
		free(group->drc_names._data);
	if (group->drc_types._data)
		free(group->drc_types._data);
	if (group->drc_indexes._data)
		free(group->drc_indexes._data);
	if (group->drc_domains._data)
		free(group->drc_domains._data);
}

/**
 * build_connectors_group
 *
 * @param group
 * @param n_entries
 * @param list
 * @returns 0 on success, !0 otherwise
 */
static int
build_connectors_list(struct drc_prop_grp *group, int n_entries,
	              struct dr_connector *list)
{
	struct dr_connector *entry;
	unsigned int *index_ptr;
	unsigned int *domain_ptr;
	char *name_ptr;
	char *type_ptr;
	int i;

	index_ptr = (unsigned int *) group->drc_indexes.val;
	domain_ptr = (unsigned int *) group->drc_domains.val;
	name_ptr = group->drc_names.val;
	type_ptr = group->drc_types.val;

	for (i = 0; i < n_entries; i++) {
		entry = &list[i];

		entry->index = be32toh(*(index_ptr++));
		entry->powerdomain = be32toh(*(domain_ptr++));

		strncpy(entry->name, name_ptr, DRC_STR_MAX);
		name_ptr += strlen(name_ptr) + 1;

		strncpy(entry->type, type_ptr, DRC_STR_MAX);
		type_ptr += strlen(type_ptr) + 1;

		if (i == (n_entries - 1))
			entry->next = NULL;
		else
			entry->next = &list[i+1];
	}

	return 0;
}

/**
 * drc_info_connectors_v1
 *
 * @param full_path
 * @param ofdt_path
 * @param list
 * @returns 0 on success, !0 otherwise
 */
static int drc_info_connectors_v1(char *full_path, char *ofdt_path,
				struct dr_connector **list)
{
	struct dr_connector *out_list = NULL;
	struct drc_prop_grp prop_grp;
	struct of_list_prop *drc_names;
	int n_drcs;
	int rc = 0;

	rc = get_drc_prop_grp(full_path, &prop_grp);
	if (rc) {
		say(DEBUG,
		    "Could not find DRC property group in path: %s.\n",
		    full_path);
		goto done;
	}

	drc_names = &prop_grp.drc_names;
	n_drcs = drc_names->n_entries;

	out_list = zalloc(n_drcs * sizeof(struct dr_connector));
	if (out_list == NULL)
		goto done;

	build_connectors_list(&prop_grp, n_drcs, out_list);

done:
	if (rc) {
		free_drc_props(&prop_grp);
		free(out_list);
	} else {
		snprintf(out_list->ofdt_path, DR_PATH_MAX, "%s", ofdt_path);
		*list = out_list;
	}

	return rc;
}

/**
 * drc_info_connectors_v2
 *
 * @param full_path
 * @param ofdt_path
 * @param list
 * @returns 0 on success, !0 otherwise
 */
static int drc_info_connectors_v2(char *full_path, char *ofdt_path,
				struct dr_connector **list)
{
	struct dr_connector *out_list = NULL;
	struct drc_info info;
	char *prop_name = "ibm,drc-info";
	char *prop_data, *data_ptr;
	int i, j, n_entries, size, connector_size, ics, rc;

	size = get_property_size(full_path, prop_name);
	prop_data = zalloc(size);
	if (prop_data == NULL)
		return -1;
	rc = get_property(full_path, prop_name, prop_data, size);
	if (rc) {
		free(prop_data);
		return -1;
	}

	/* Num of DRC-info sets */
	data_ptr = prop_data;
	n_entries  = be32toh(*(uint *)data_ptr);
	data_ptr += 4;

	/* Extract drc-info data */
	for (j = 0, connector_size = 0; j < n_entries; j++) {
		info.drc_type = data_ptr;
		data_ptr += strlen(info.drc_type)+1;
		info.drc_name_prefix = data_ptr;
		data_ptr += strlen(info.drc_name_prefix)+1;
		data_ptr += 4;	/* Skip drc-index-start */
		data_ptr += 4;	/* Skip drc-name-suffix-start */
		info.n_seq_elems = be32toh(*(uint *)data_ptr);
		data_ptr += 4;	/* Advance over n-seq-elems */
		data_ptr += 4;	/* Skip sequential-increment */
		data_ptr += 4;	/* Skip drc-power-domain */
		if (info.n_seq_elems <= 0)
			continue;
		connector_size += info.n_seq_elems;
	}

	/* Allocate list entry */
	out_list = zalloc(connector_size * sizeof(struct dr_connector));
	if (out_list == NULL) {
		rc = -1;
		goto done;
	}

	/* Extract drc-info data */
	data_ptr = prop_data;
	data_ptr += 4;
	for (j = 0, ics = 0; j < n_entries; j++) {
		info.drc_type = data_ptr;
		data_ptr += strlen(info.drc_type)+1;
		info.drc_name_prefix = data_ptr;
		data_ptr += strlen(info.drc_name_prefix)+1;

		info.drc_index_start  = be32toh(*(uint *)data_ptr);
		data_ptr += 4;

		info.drc_name_suffix_start = be32toh(*(uint *)data_ptr);
		data_ptr += 4;

		info.n_seq_elems = be32toh(*(uint *)data_ptr);
		data_ptr += 4;

		info.seq_inc = be32toh(*(uint *)data_ptr);
		data_ptr += 4;

		info.drc_power_domain = be32toh(*(uint *)data_ptr);
		data_ptr += 4;

		/* Build connector list */
		if (info.n_seq_elems <= 0)
			continue;

		for (i = 0; i < info.n_seq_elems; i++, ics++) {
			out_list[ics].index = info.drc_index_start+
					 (i*info.seq_inc);
			out_list[ics].powerdomain = info.drc_power_domain;

			sprintf(out_list[ics].name, "%s%d",
				info.drc_name_prefix,
				info.drc_name_suffix_start+(i*info.seq_inc));

			strncpy(out_list[ics].type, info.drc_type, DRC_STR_MAX - 1);
			out_list[ics].type[DRC_STR_MAX - 1] = '\0';

			out_list[ics].next = &out_list[ics+1];
		}
	}
        if (ics > 0)
                out_list[ics-1].next = NULL;

done:
	if (prop_data)
		free(prop_data);

	if (rc) {
		free(out_list);
		*list = NULL;
	} else {
		snprintf(out_list->ofdt_path, DR_PATH_MAX, "%s", ofdt_path);
		*list = out_list;
	}

	return rc;
}


/**
 * of_to_full_path
 *
 * NOTE: Callers of this function are expected to free full_path themselves
 *
 * @param of_path
 * @returns full path on success, NULL otherwise
 */
char *
of_to_full_path(const char *of_path)
{
	char *full_path = NULL;
	int full_path_len;

	if (!strncmp(of_path, OFDT_BASE, strlen(OFDT_BASE))) {
		full_path = strdup(of_path);
		if (full_path == NULL)
			return NULL;
	} else {
		full_path_len = strlen(OFDT_BASE) + strlen(of_path) + 2;
		full_path = zalloc(full_path_len);
		if (full_path == NULL)
			return NULL;

		if (*of_path == '/')
			sprintf(full_path, "%s%s", OFDT_BASE, of_path);
		else
			sprintf(full_path, "%s/%s", OFDT_BASE, of_path);
	}

	return full_path;
}

/**
 * get_dr_connectors
 *
 * NOTE:Callers of this function are expected to free drc_list themselves
 *
 * @param of_path
 * @param drc_list
 * @param n_drcs
 * @returns 0 on success, !0 otherwise
 */
struct dr_connector *
get_drc_info(const char *of_path)
{
	struct stat sbuf;
	char fname[DR_PATH_MAX];
	char ofdt_path[DR_PATH_MAX];
	char *full_path = NULL;
	struct dr_connector *list = NULL;
	int rc;

	for (list = all_drc_lists; list; list = list->all_next) {
		if (! strcmp(list->ofdt_path, of_path))
			return list;
	}

	full_path = of_to_full_path(of_path);
	if (full_path == NULL)
		return NULL;

	/* ibm,drc-info vs the old implementation */
	sprintf(fname, "%s/%s", full_path, "ibm,drc-info");
	snprintf(ofdt_path, DR_PATH_MAX, "%s", of_path);
	rc = stat(fname, &sbuf);
	if (rc)
		rc = drc_info_connectors_v1(full_path, ofdt_path, &list);
	else
		rc = drc_info_connectors_v2(full_path, ofdt_path, &list);

	if (rc == 0) {
		list->all_next = all_drc_lists;
		all_drc_lists = list;
	} else
		list = NULL;

	free(full_path);
	return list;
}

/**
 * free_drc_info
 *
 * @param drc_list
 */
void
free_drc_info(void)
{
	struct dr_connector *list;

	while (all_drc_lists) {
		list = all_drc_lists;
		all_drc_lists = list->all_next;

		free(list);
	}
}

/**
 * search_drc_list
 *
 * @param drc_list
 * @param n_entries
 * @param start
 * @param search_type
 * @param key
 * @param found_idx
 * @returns pointer to dr_connector on success, NULL otherwise
 */
struct dr_connector *
search_drc_list(struct dr_connector *drc_list, struct dr_connector *start,
		int search_type, void *key)
{
	struct dr_connector *drc;

	if (start)
		drc = start;
	else
		drc = drc_list;

	for ( ; drc != NULL; drc = drc->next) {
		switch (search_type) {
		    case DRC_NAME:
			if (! strcmp(drc->name, (char *)key))
				return drc;
			break;

		    case DRC_TYPE:
			if (! strcmp(drc->type, (char *)key))
				return drc;
			break;

		    case DRC_INDEX:
			if (drc->index == *(uint32_t *)key)
				return drc;
			break;

		    case DRC_POWERDOMAIN:
			if (drc->powerdomain == *(uint32_t *)key)
				return drc;
		};
	}

	return NULL;
}

/**
 * get_my_drc_index
 *
 * @param of_path
 * @param index
 * @returns 0 on success, !0 otherwise
 */
int
get_my_drc_index(char *of_path, uint32_t *index)
{
	char *full_path = NULL;
	int rc;

	full_path = of_to_full_path(of_path);
	if (full_path == NULL)
		return -1;

	rc = get_ofdt_uint_property(full_path, "ibm,my-drc-index", index);

	free(full_path);

	return rc;
}

/**
 * get_my_partner_drc_index
 *
 * @param of_full_path
 * @param index
 * @returns 0 on success, !0 otherwise
 */
int get_my_partner_drc_index(struct dr_node *node, uint32_t *index)
{
	int rc;

	if (node == NULL)
		return -1;

	rc = get_ofdt_uint_property(node->ofdt_path,
				"ibm,multipath-partner-drc", index);

	return rc;
}

/**
 * drc_name_to_index
 * @brief Find the drc index for the given name
 *
 * @param name name to find
 * @returns drc index corresponding to name on success, 0 otherwise
 */
int
drc_name_to_index(const char *name, struct dr_connector *drc_list)
{
	struct dr_connector *drc;

	for (drc = drc_list; drc != NULL; drc = drc->next) {
		if (!strcmp(name, drc->name))
			return drc->index;
	}

	return 0; /* hopefully 0 isn't a valid index... */
}

/**
 * drc_index_to_name
 * @brief find the drc name for the specified drc index
 *
 * @param index
 * @returns drc name on success, NULL otherwise
 */
char *
drc_index_to_name(uint32_t index, struct dr_connector *drc_list)
{
	struct dr_connector *drc;

	for (drc = drc_list; drc != NULL; drc = drc->next) {
		if (index == drc->index)
			return drc->name;
	}

	return NULL;
}

/**
 * search_drc_by_key
 * @brief Retrieve a dr_connector based on DRC name or DRC index
 *
 * This routine searches the drc lists for a dr_connector with the
 * specified name or index starting at the specified directory. If
 * a dr_connector is found the root_dir that the dr_connector was
 * found in is also filled out.
 *
 * @param key to serach for the dr_connector
 * @param drc pointer to a drc to point to the found dr_connector
 * @param root_dir pointer to buf to fill in with root directory
 * @param start_dir, directory to start searching
 * @param key_type whether the key is DRC name or DRC index
 * @returns 0 on success (drc and root_dir filled in), !0 on failure
 */
int
search_drc_by_key(void *key, struct dr_connector *drc, char *root_dir,
		char *start_dir, int key_type)
{
        struct dr_connector *drc_list = NULL;
        struct dr_connector *drc_entry;
	struct dirent *de;
	DIR *d;
        int rc = -1;

	memset(drc, 0, sizeof(*drc));

	/* Try to get the drc in this directory */
        drc_list = get_drc_info(start_dir);
        if (drc_list == NULL)
		return -1;

	drc_entry = search_drc_list(drc_list, NULL, key_type, key);
	if (drc_entry != NULL) {
		memcpy(drc, drc_entry, sizeof(*drc));
		sprintf(root_dir, "%s", start_dir);
                return 0;
        }

	/* If we didn't find it here, check the subdirs */
	d = opendir(start_dir);
	if (d == NULL)
		return -1;

	while ((de = readdir(d)) != NULL) {
		char dir_path[DR_PATH_MAX];

		if ((de->d_type != DT_DIR) || is_dot_dir(de->d_name))
			continue;

		sprintf(dir_path, "%s/%s", start_dir, de->d_name);
		rc = search_drc_by_key(key, drc, root_dir, dir_path,
					key_type);
		if (rc == 0)
			break;
	}
	closedir(d);

	return rc;
}

/**
 * get_drc_by_name
 * @brief Retrieve a dr_connector with the specified drc_name
 *
 * This routine searches the drc lists for a dr_connector with the
 * specified name starting at the specified directory.  If a dr_connector
 * is found the root_dir that the dr_connector was found in is also
 * filled out.
 *
 * @param drc_name name of the dr_connector to search for
 * @param drc pointer to a drc to point to the found dr_connector
 * @param root_dir pointer to buf to fill in with root directory
 * @param start_dir, directory to start searching
 * @returns 0 on success (drc and root_dir filled in), !0 on failure
 */
int
get_drc_by_name(char *drc_name, struct dr_connector *drc, char *root_dir,
		char *start_dir)
{
	int rc;

	rc = search_drc_by_key(drc_name, drc, root_dir, start_dir, DRC_NAME);

	return rc;
}

/**
 * get_drc_by_index
 * @brief Retrieve a dr_connector with the specified index
 *
 * This routine searches the drc lists for a dr_connector with the
 * specified index starting at the specified directory. If a dr_connector
 * is found the root_dir that the dr_connector was found in is also
 * filled out.
 *
 * @param index of the dr_connector to search for
 * @param drc pointer to a drc to point to the found dr_connector
 * @param root_dir pointer to buf to fill in with root directory
 * @param start_dir, directory to start searching
 * @returns 0 on success (drc and root_dir filled in), !0 on failure
 */
int
get_drc_by_index(uint32_t index, struct dr_connector *drc, char *root_dir,
	char *start_dir)
{
	int rc;

	rc = search_drc_by_key((void *)&index, drc, root_dir, start_dir,
				DRC_INDEX);

	return rc;
}

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
int get_min_common_depth(void)
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
	size = be32toh(*p);
	free(p);
	return size;
}

int get_assoc_arrays(const char *dir, struct assoc_arrays *aa,
		     unsigned min_common_depth)
{
	int size;
	int rc;
	uint32_t *prop, i;

	size = load_property(dir, ASSOC_LOOKUP_ARRAYS, &prop);
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
	if (min_common_depth > aa->array_sz) {
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
		int prop_index = i * aa->array_sz + min_common_depth + 1;

		aa->min_array[i] = be32toh(prop[prop_index]);
	}
	rc = 0;

out_free:
	free(prop);
	return rc;
}

/*
 * Read the associativity property and return the node id matching the
 * min_common_depth entry.
 */
int of_associativity_to_node(const char *dir, unsigned min_common_depth)
{
	int size;
	uint32_t *prop;

	size = load_property(dir, ASSOCIATIVITY, &prop);
	if (size < 0)
		return size;

	if (size < 1) {
		say(ERROR, "Could not read associativity for node %s", dir);
		return -EINVAL;
	}

	if (be32toh(prop[0]) < min_common_depth) {
		say(ERROR,
		    "Too short associativity property for node %s (%d/%d)",
		    dir, be32toh(prop[0]), min_common_depth);
		return -EINVAL;
	}

	return be32toh(prop[min_common_depth]);
}

