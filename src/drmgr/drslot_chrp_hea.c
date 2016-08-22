/**
 * @file drslot_chrp_hea.c
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

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <librtas.h>
#include <dirent.h>

#include "dr.h"
#include "drpci.h"
#include "ofdt.h"

static char *usagestr = "-c port {-a | -r | -Q} -s drc_name";

/**
 * hea_uasge
 */
void
hea_usage(char **pusage)
{
	*pusage = usagestr;
}

/**
 * sysfs_hea_op
 * @brief Write to the sysfs file to hotplug add/remove a HEA adapter or port
 *
 * @param fname sysfs file to write to
 * @param name information to write to the sysfs file
 * @returns 0 on success, !0 otherwise
 */
static int
sysfs_write(char *fname, char *name)
{
	int rc, len;
	FILE *file;

    	file = fopen(fname, "w");
    	if (file == NULL) {
		say(ERROR, "Could not open %s:\n%s\n", fname, strerror(errno));
		return -ENODEV;
    	}

	len = strlen(name);
    	rc = fwrite(name, 1, len, file);
	fclose(file);

	rc = (rc >= 0) ? 0 : rc;
	if (rc)
		say(ERROR, "Write to %s failed:\n%s\n", fname, strerror(errno));

	return rc;
}

/**
 * hotplug_hea
 * @brief Hotplug add/remove a HEA adapter from the system.
 *
 * @param action add or remove the adapter
 * @param name name of the HEA adapter to add/remove
 * @returns 0 on success, !0 otherwise
 */
static int
hotplug_hea(int action, char *name)
{
	int rc;
	char *action_str = (action == ADD) ? "add" : "remove";
	char *action_path = (action == ADD) ? HEA_ADD_SLOT : HEA_REMOVE_SLOT;
	
	say(DEBUG, "Attempting to hotplug %s %s\n", action_str, name);
	
	rc = sysfs_write(action_path, name);
	if (rc)
		say(ERROR, "Could not hotplug %s %s\n", action_str, name);

	return rc;
}

/**
 * hotplug_port
 * @brief Hotplug add/remove a HEA port to the system.
 *
 * @param action add/remove of the port
 * @parm hea HEA adpater of the port to be added
 * @param port port to be added/removed
 * @return 0 on success, !0 otherwise
 */
static int
hotplug_port(int action, struct dr_node *hea, struct dr_node *port)
{
	char fname[128];
	char port_str[4];
	char *action_str = (action == ADD) ? "add" : "remove";
	uint port_no;
	int rc;

	say(DEBUG, "Attempting to hotplug %s Port.\n", action_str);
	
	if (! hea->sysfs_dev_path) {
		say(DEBUG, "Non-existant sysfs dev path for Port, hotplug "
		    "failed.\n");
		return -EINVAL;
	}

	rc = get_property(port->ofdt_path, "ibm,hea-port-no", &port_no,
			  sizeof(port_no));
	if (rc)
		return -EINVAL;

	sprintf(port_str, "%d", port_no);
	sprintf(fname, "%s/%s", hea->sysfs_dev_path,
		(action == ADD) ? "probe_port" : "remove_port");

	rc = sysfs_write(fname, port_str);
	if (rc)
		say(ERROR, "Hotplug %s of Port %d failed\n", action_str,
		    port_no);

	return rc;
}

/**
 * remove_port
 * @brief Remove the HEA adapater or port specified by the options
 *
 * @param opts command options
 * @returns 0 on success, !0 otherwise
 */
static int
remove_port(struct options *opts)
{
	struct dr_node *hea;
	struct dr_node *port, *tmp;
	int rc;
	int no_ports = 0;
	int hea_hp_removed = 0;

	hea = get_node_by_name(usr_drc_name, HEA_NODES);
	if (hea == NULL)
		return RC_NONEXISTENT;

	for (port = hea->children; port; port = port->next) {
		if (! strcmp(port->drc_name, usr_drc_name))
			break;
	}

	if (!port) {
		say(ERROR, "Could not find HEA Port \"%s\" to remove\n",
		    usr_drc_name);
		free_node(hea);
		return -1;
	}

	rc = hotplug_port(REMOVE, hea, port);
	if (rc) {
		free_node(hea);
		return -1;
	}

	/* if this is the last port to be removed, we need to remove the
	 * hea adapter from the OS also.  do not actually remove the
	 * HEA adapter from the device tree or unallocate it, we will get
	 * a request to do that later.
	 */
	for (tmp = hea->children; tmp; tmp = tmp->next)
		no_ports++;

	if (no_ports == 1) {
		rc = hotplug_hea(REMOVE, strstr(hea->ofdt_path, "/lhea"));
		if (rc) {
			hotplug_port(ADD, hea, port);
			free_node(hea);
			return -1;
		}

		hea_hp_removed = 1;
	}

	rc = release_drc(port->drc_index, port->dev_type);
	if (rc) {
		if (hea_hp_removed)
			hotplug_hea(ADD, strstr(hea->ofdt_path, "/lhea"));
		hotplug_port(ADD, hea, port);
		free_node(hea);
		return rc;
	}

	rc = remove_device_tree_nodes(port->ofdt_path);
	if (rc) {
		if (hea_hp_removed)
			hotplug_hea(ADD, strstr(hea->ofdt_path, "/lhea"));
		hotplug_port(ADD, hea, port);
		free_node(hea);
		return rc;
	}

	say(DEBUG, "device node(s) for %s removed\n", port->drc_name);
	free_node(hea);
	return 0;
}

/**
 * remove_hea
 * @brief Remove the HEA adapater or port specified by the options
 *
 * @param opts command options
 * @returns 0 on success, !0 otherwise
 */
static int
remove_hea(struct options *opts)
{
	struct dr_node *hea;
	int rc;

	hea = get_node_by_name(usr_drc_name, HEA_NODES);
	if (hea == NULL)
		return RC_NONEXISTENT;

	rc = release_drc(hea->drc_index, hea->dev_type);
	if (rc) {
		free_node(hea);
		return rc;
	}

	rc = remove_device_tree_nodes(hea->ofdt_path);
	if (rc)
		say(ERROR, "Error removing HEA adapter from the device tree\n");

	free_node(hea);
	return rc;
}

/**
 * add_slot
 * @bried Add the HEA adapter or port specified by the options
 *
 * @param opts command options
 * @returns 0 on success, !0 otherwise
 */
static int
add_slot(struct options *opts)
{
	struct dr_connector drc;
	struct of_node *of_nodes;
	struct dr_node *hea;
	struct dr_node *port;
	char ofdt_path[DR_PATH_MAX];
	char *slot_type = (usr_drc_name[0] == 'H') ? "HEA" : "Port";
	int rc = 0;

	rc = get_drc_by_name(usr_drc_name, &drc, ofdt_path, OFDT_BASE);
	if (rc)
		return rc;

	rc = acquire_drc(drc.index);
	if (rc)
		return rc;

	of_nodes = configure_connector(drc.index);
	if (of_nodes == NULL) {
		release_drc(drc.index, HEA_DEV);
		return -1;
	}

	rc = add_device_tree_nodes(ofdt_path, of_nodes);
	free_of_node(of_nodes);
	if (rc) {
		say(ERROR, "Error adding % to the device tree\n", slot_type);
		release_drc(drc.index, HEA_DEV);
		return rc;
	}

	hea = get_node_by_name(usr_drc_name, HEA_NODES);
	if (hea == NULL) {
		say(ERROR, "Could not get find \"%s\" in the updated device "
		    "tree,\nAddition of %s failed.\n", usr_drc_name,
		    slot_type);

		remove_device_tree_nodes(ofdt_path);
		release_drc(drc.index, HEA_DEV);
		return -1;
	}

	switch (usr_drc_name[0]) {
	    case 'H':
		rc = hotplug_hea(ADD, strstr(hea->ofdt_path, "/lhea"));
		break;
	    case 'P':
		for (port = hea->children; port; port = port->next) {
			if (! strcmp(usr_drc_name, port->drc_name))
				break;
		}

		rc = hotplug_port(ADD, hea, port);
		break;
	}
	if (rc) {
		remove_device_tree_nodes(ofdt_path);
		release_drc(drc.index, HEA_DEV);
	}

	free_node(hea);
	return rc;
}

int
valid_hea_options(struct options *opts)
{
	if (!usr_drc_name) {
		say(ERROR, "A drc name  must be specified\n");
		return -1;
	}

	if ((usr_action != ADD) && (usr_action != REMOVE)
	    && (usr_action != QUERY)) {
		say(ERROR, "The '-r', '-a', or '-Q' option must be specified "
		    "for HEA operations.\n");
		return -1;
	}

	return 0;
}

int
drslot_chrp_hea(struct options *opts)
{
	int rc;

	if (! hea_dlpar_capable()) {
		say(ERROR, "DLPAR HEA operations are not supported on"
		    "this kernel");
		return -1;
	}

	switch (usr_action) {
	    case ADD:
		rc = add_slot(opts);
		break;

	    case REMOVE:
		if (! strcmp(opts->ctype, "port"))
			rc = remove_port(opts);
		else if (! strcmp(opts->ctype, "slot"))
			rc = remove_hea(opts);
		else {
			say(ERROR, "The connector type %s is not supported.\n",
			    opts->ctype);
			rc = -1;
		}
		break;

	    case QUERY:
		{
			struct dr_node *node;
			node = get_node_by_name(usr_drc_name, HEA_NODES);
			if (node == NULL) {
				say(ERROR, "%s not owned by partition\n",
				    usr_drc_name);
				rc = RC_DONT_OWN;
			} else {
				/* Special case for HMC */
				rc = RC_LINUX_SLOT;
			}

			free_node(node);
			break;
		}

	    default:
		rc = -1;
	}

	return rc;
}
