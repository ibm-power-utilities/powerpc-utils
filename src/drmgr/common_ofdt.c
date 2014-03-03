/**
 * @file common_ofdt.c
 * @brief Common routines for Open Firmware Device Tree access
 *
 * Copyright (C) IBM Corporation 2006
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <endian.h>
#include "dr.h"
#include "ofdt.h"

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

struct dr_connector *all_drc_lists = NULL;

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
	if (group->drc_names.val)
		free(group->drc_names._data);
	if (group->drc_types.val)
		free(group->drc_types._data);
	if (group->drc_indexes.val)
		free(group->drc_indexes._data);
	if (group->drc_domains.val)
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
	struct dr_connector *list = NULL;
	struct of_list_prop *drc_names;
	struct drc_prop_grp prop_grp;
	char *full_path = NULL;
	int rc, n_drcs;

	full_path = of_to_full_path(of_path);
	if (full_path == NULL)
		goto done;

	for (list = all_drc_lists; list; list = list->all_next) {
		if (! strcmp(list->ofdt_path, of_path))
			return list;
	}
	
	rc = get_drc_prop_grp(full_path, &prop_grp);
	if (rc) {
		say(DEBUG, "Could not find DRC property group in path: %s.\n",
			full_path);
		goto done;
	}

	drc_names = &prop_grp.drc_names;
	n_drcs = drc_names->n_entries;

	list = zalloc(n_drcs * sizeof(struct dr_connector));
	if (list == NULL)
		goto done;

	/* XXX Unchecked rc */
	rc = build_connectors_list(&prop_grp, n_drcs, list);

	snprintf(list->ofdt_path, DR_PATH_MAX, "%s", of_path);
	
	list->all_next = all_drc_lists;
	all_drc_lists = list;

done:
	free_drc_props(&prop_grp);
	if (full_path)
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

	drc_entry = search_drc_list(drc_list, NULL, DRC_NAME, drc_name);
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
		rc = get_drc_by_name(drc_name, drc, root_dir, dir_path);
		if (rc == 0)
			break;
	}
	closedir(d);

	return rc;
}

struct dr_connector *
get_drc_by_index(uint32_t drc_index, struct dr_connector *drc_list)
{
	struct dr_connector *drc;

	for (drc = drc_list; drc; drc = drc->next) {
		if (drc->index == drc_index)
			return drc;
	}

	return NULL;
}
