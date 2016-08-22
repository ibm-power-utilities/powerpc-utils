/**
 * @file drslot_chrp_phb.c
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
#include <librtas.h>
#include <errno.h>
#include "dr.h"
#include "drpci.h"
#include "ofdt.h"

static char *usagestr = "-c phb [-Q | -r | -a] -s <drc_name | drc_index>";

static int release_phb(struct dr_node *);
static int acquire_phb(char *, struct dr_node **);


/**
 * phb_usage
 * @brief return the usage message
 *
 * @returns 1, always
 */
void
phb_usage(char **pusage)
{
	*pusage = usagestr;
}

static int phb_has_dlpar_children(struct dr_node *phb)
{
	struct dr_node *child;

	/* If this PHB still owns children that are not hotplug, fail. */
	for (child = phb->children; child; child = child->next) {
		if ((child->is_owned) && (child->dev_type != PCI_HP_DEV))
			return 1;
	}

	return 0;
}

static int phb_has_display_adapter(struct dr_node *phb)
{
	struct dr_node *child;

	for (child = phb->children; child; child = child->next) {
		if (is_display_adapter(child))
			return 1;
	}

	return 0;
}

/**
 * query_phb
 *
 * @param op
 * @returns 0 if a remove would succeed, or if it's alreday been removed
 * @returns 1 if a remove would not succeed
 */
static int
query_phb(struct options *opts)
{
	struct dr_node *phb;
	int rc;

	phb = get_node_by_name(opts->usr_drc_name, PHB_NODES);

	if (phb == NULL)
		rc = RC_NONEXISTENT;
	else if (phb_has_display_adapter(phb))
		rc = RC_IN_USE;
	else if (phb_has_dlpar_children(phb))
		rc = RC_IN_USE;
	else
		rc = RC_LINUX_SLOT;

	if (phb)
		free_node(phb);

	return rc;
}

/**
 * release_phb
 *
 */
static int
release_phb(struct dr_node *phb)
{
	int rc;

	rc = remove_device_tree_nodes(phb->ofdt_path);
	if (rc)
		return rc;

	if (phb->phb_ic_ofdt_path[0] != '\0') {
		rc = remove_device_tree_nodes(phb->phb_ic_ofdt_path);
		if (rc)
			return rc;
	}

	rc = release_drc(phb->drc_index, PHB_DEV);

	return rc;
}

struct hpdev {
	struct hpdev *next;
	char path[256];
	char devspec[256];
};

#define SYSFS_PCI_DEV_PATH	"/sys/bus/pci/devices"

static void free_hpdev_list(struct hpdev *hpdev_list)
{
	struct hpdev *hpdev;

	while (hpdev_list) {
		hpdev = hpdev_list;
		hpdev_list = hpdev_list->next;
		free(hpdev);
	}
}

static int get_os_hp_devices(struct hpdev **hpdev_list)
{
	struct hpdev *hp_list = NULL;
	struct hpdev *hpdev;
	DIR *d;
	struct dirent *de;
	int rc = 0;

	d = opendir(SYSFS_PCI_DEV_PATH);
	if (!d) {
		say(ERROR, "Failed to open %s\n", SYSFS_PCI_DEV_PATH);
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		if (is_dot_dir(de->d_name))
			continue;

		hpdev = zalloc(sizeof(*hpdev));
		if (!hpdev) {
			rc = -1;
			break;
		}

		hpdev->next = hp_list;
		hp_list = hpdev;

		rc = sprintf(hpdev->path, "%s/%s", SYSFS_PCI_DEV_PATH,
			     de->d_name);
		if (rc < 0)
			break;

		rc = get_str_attribute(hpdev->path, "devspec", hpdev->devspec,
				       256);
		if (rc)
			break;

		say(EXTRA_DEBUG, "HPDEV: %s\n       %s\n", hpdev->path,
		    hpdev->devspec);
	}

	closedir(d);

	if (rc) {
		free_hpdev_list(hp_list);
		hp_list = NULL;
	}

	*hpdev_list = hp_list;
	return rc;
}

static int hp_remove_os_device(struct hpdev *hpdev)
{
	FILE *file;
	char path[256];
	int rc;

	sprintf(path, "%s/%s", hpdev->path, "remove");

	file = fopen(path, "w");
	if (!file)
		return -1;

	say(DEBUG, "Removing %s\n", hpdev->path);
	rc = fwrite("1", 1, 1, file);
	if (rc == 1)
		rc = 0;

	fclose(file);
	sleep(5);
	return rc;
}

static int disable_os_hp_children_recurse(struct dr_node *phb,
					  struct hpdev *hpdev_list, char *ofpath)
{
	struct hpdev *hpdev;
	DIR *d;
	struct dirent *de;
	int rc = 0;

	d = opendir(ofpath);
	if (!d)
		return -1;

	while ((de = readdir(d)) != NULL) {
		char devspec[256];

		if (is_dot_dir(de->d_name))
			continue;

		if (de->d_type == DT_DIR) {
			char lpath[4096];
			sprintf(lpath, "%s/%s", ofpath, de->d_name);
			rc = disable_os_hp_children_recurse(phb, hpdev_list, lpath);
		}

		memset(devspec, 0, 256);
		sprintf(devspec, "%s/%s", ofpath + strlen(OFDT_BASE),
			de->d_name);

		for (hpdev = hpdev_list; hpdev; hpdev = hpdev->next) {
			if (!strcmp(hpdev->devspec, devspec)) {
				rc = hp_remove_os_device(hpdev);
				break;
			}
		}

		if (rc) {
			say(ERROR, "Failed to hotplug remove %s\n",
			    hpdev->path);
			break;
		}
	}

	closedir(d);
	return rc;
}

static int disable_os_hp_children(struct dr_node *phb)
{
	struct hpdev *hpdev_list;
	int rc = 0;

	rc = get_os_hp_devices(&hpdev_list);
	if (rc)
		return -1;

	rc = disable_os_hp_children_recurse(phb, hpdev_list, phb->ofdt_path);
	free_hpdev_list(hpdev_list);
	return rc;
}

/**
 * remove_phb
 *
 * @param op
 * @returns 0 on success, !0 otherwise
 */
static int
remove_phb(struct options *opts)
{
	struct dr_node *phb;
	struct dr_node *child;
	struct dr_node *hp_list = NULL;
	int rc = 0;

	phb = get_node_by_name(opts->usr_drc_name, PHB_NODES);
	if (phb == NULL) {
		say(ERROR, "Could not find PHB %s\n", opts->usr_drc_name);
		return RC_NONEXISTENT;
	}

	if (phb_has_display_adapter(phb)) {
		say(ERROR, "This PHB contains a display adapter, DLPAR "
		    "remove of display adapters is not supported.\n");
		goto phb_remove_error;
	}

	if (phb_has_dlpar_children(phb)) {
		rc = -1;
		goto phb_remove_error;
	}

	/* Now, disable any hotplug children */
	hp_list = get_hp_nodes();

	for (child = phb->children; child; child = child->next) {
		struct dr_node *slot;

		if (child->dev_type == PCI_HP_DEV) {
			rc = disable_hp_children(child->drc_name);
			if (rc)
				say(ERROR,
				    "failed to disable hotplug children\n");

			/* find dr_node corresponding to child slot's drc_name */
			for (slot = hp_list; slot; slot = slot->next)
				if (!strcmp(child->drc_name, slot->drc_name))
					break;

			/* release any hp children from the slot */
			rc = release_hp_children_from_node(slot);
			if (rc && rc != -EINVAL) {
				say(ERROR,
				    "failed to release hotplug children\n");
				goto phb_remove_error;
			}
		}
	}

	/* If there are any directories under the phb left at this point,
	 * they are OS hotplug devies.  Note: this is different from DR
	 * hotplug devices.  This really occurs on systems that do not
	 * support DR hotplug devices.  The device tree does not get populated
	 * with drc information for these devices and such they do not appear
	 * on the list generated by the calls to get_node_*
	 *
	 * For these devices we simply hotplug remove them from the OS.
	 */
	rc = disable_os_hp_children(phb);
	if (rc)
		goto phb_remove_error;

	rc = dlpar_remove_slot(phb->drc_name);
	if (rc) {
		say(ERROR, "kernel remove failed for %s, rc = %d\n",
			phb->drc_name, rc);
		goto phb_remove_error;
	}

	rc = release_phb(phb);

phb_remove_error:
	if (phb)
		free_node(phb);

	if (hp_list)
		free_node(hp_list);

	return rc;
}

/**
 * acquire_phb
 *
 */
static int acquire_phb(char *drc_name, struct dr_node **phb)
{
	struct dr_connector drc;
	struct of_node *of_nodes;
	char path[DR_PATH_MAX];
	int rc;

	rc = get_drc_by_name(drc_name, &drc, path, OFDT_BASE);
	if (rc) {
		say(ERROR, "Could not find drc index for %s, unable to add the"
		    "PHB.\n", drc_name);
		return rc;
	}

	rc = acquire_drc(drc.index);
	if (rc)
		return rc;

	of_nodes = configure_connector(drc.index);
	if (of_nodes == NULL) {
		release_drc(drc.index, PHB_DEV);
		return -1;
	}

	rc = add_device_tree_nodes(path, of_nodes);
	free_of_node(of_nodes);
	if (rc) {
		say(ERROR, "add_device_tree_nodes failed at %s\n", path);
		release_drc(drc.index, PHB_DEV);
		return -1;
	}

	/* Now that the node has been added to the device-tree, retrieve it.
	 * This also acts as a sanity check that everything up to this
	 * point has succeeded.
	 */
	*phb = get_node_by_name(drc_name, PHB_NODES);
	if (*phb == NULL) {
		say(ERROR, "Could not get find \"%s\"\n", drc_name);
		/* or should we call release_drc? but need device type */
		release_drc(drc.index, PHB_DEV);
		return -1;
	}

	return 0;
}

/**
 * add_phb
 *
 * @param op
 * @returns 0 on success, !0 otherwise
 */
static int
add_phb(struct options *opts)
{
	struct dr_node *phb = NULL;
	int rc, n_children = 0;

	phb = get_node_by_name(opts->usr_drc_name, PHB_NODES);
	if (phb) {
		say(ERROR, "PHB is already owned by this partition\n");
		rc = RC_ALREADY_OWN;
		goto phb_add_error;
	}

	rc = acquire_phb(opts->usr_drc_name, &phb);
	if (rc)
		return rc;

	rc = acquire_hp_children(phb->ofdt_path, &n_children);
	if (rc) {
		if (release_phb(phb)) {
			say(ERROR, "Unknown failure. Data may be out of sync "
			    "and\nthe system may require a reboot.\n");
		}
		goto phb_add_error;
	}

	rc = dlpar_add_slot(phb->drc_name);
	if (rc) {
		if (n_children) {
			if (release_hp_children(phb->drc_name)) {
				say(ERROR, "Unknown failure. Data may be out "
				    "of sync and\nthe system may require "
				    "a reboot.\n");
			}
		}

		if (release_phb(phb)) {
			say(ERROR, "Unknown failure. Data may be out of sync "
			    "and\nthe system may require a reboot.\n");
		}
		goto phb_add_error;
	}

	if (n_children) {
		rc = enable_hp_children(phb->drc_name);
		if (rc) {
			say(ERROR, "Adapter configuration failed.\n");
			if (release_hp_children(phb->drc_name)) {
				say(ERROR, "Unknown failure. Data may be out "
				    "of sync and \nthe system may require "
				    "a reboot.\n");
			}

			if (dlpar_remove_slot(phb->drc_name)) {
				say(DEBUG, "remove %s from hotplug subsystem "
				    "failed\n", phb->drc_name);
				say(ERROR, "Unknown failure. Data may be out "
				    "of sync and \nthe system may require "
				    "a reboot.\n");
			}

			if (release_phb(phb)) {
				say(ERROR, "Unknown failure. Data may be out "
				    "of sync and \nthe system may require "
				    "a reboot.\n");
			}
		}
	}

phb_add_error:
	if (phb)
		free_node(phb);

	return rc;
}

int
valid_phb_options(struct options *opts)
{
	/* The -s option can specify a drc name or drc index */
	if (opts->usr_drc_name && !strncmp(opts->usr_drc_name, "0x", 2)) {
		opts->usr_drc_index = strtoul(opts->usr_drc_name, NULL, 16);
		opts->usr_drc_name = NULL;
	}

	if (opts->usr_drc_name == NULL && !opts->usr_drc_index) {
		say(ERROR, "A drc name or index must be specified\n");
		return -1;
	}

        if ((usr_action != ADD) && (usr_action != REMOVE)
            && (usr_action != QUERY)) {
                say(ERROR, "The '-r', '-a', or '-Q' option must be specified "
                    "for PHB operations.\n");
                return -1;
        }

	return 0;
}

int
drslot_chrp_phb(struct options *opts)
{
	int rc = -1;

	if (! phb_dlpar_capable()) {
		say(ERROR, "DLPAR PHB operations are not supported on"
		    "this kernel.");
		return rc;
	}

	if (!opts->usr_drc_name) {
		struct dr_connector *drc_list = get_drc_info(OFDT_BASE);
		opts->usr_drc_name = drc_index_to_name(opts->usr_drc_index,
						       drc_list);
		if (!opts->usr_drc_name) {
			say(ERROR,
			    "Could not locate DRC name for DRC index: 0x%x",
			    opts->usr_drc_index);
			return -1;
		}
	}

	switch(usr_action) {
	case ADD:
		rc = add_phb(opts);
		break;
	case REMOVE:
		rc = remove_phb(opts);
		break;
	case QUERY:
		rc = query_phb(opts);
		break;
	default:
		rc = -1;
	}

	return rc;
}
