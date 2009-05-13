/**
 * @file drslot_chrp_phb.c
 *
 *
 * Copyright (C) IBM Corporation 2006
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librtas.h>
#include <errno.h>
#include "dr.h"
#include "drpci.h"
#include "ofdt.h"

static char *usagestr = "-c phb [-Q | -r | -a] -s phb_name";

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
	struct dr_node *child;

	phb = get_node_by_name(opts->usr_drc_name, PHB_NODES);
	if (phb == NULL)
		return RC_NONEXISTENT;

	/* If this PHB still owns children that are not hotplug, fail. */
	for (child = phb->children; child; child = child->next) {
		if ((child->is_owned) && (child->dev_type != PCI_HP_DEV)) {
			free_node(phb);
			return RC_IN_USE;
		}
	}

	free_node(phb);
	return RC_LINUX_SLOT;
}

/**
 * release_phb
 *
 */
static int
release_phb(struct dr_node *phb)
{
	int rc;

	rc = release_drc(phb->drc_index, PHB_DEV);
	if (rc)
		return rc;

	rc = remove_device_tree_nodes(phb->ofdt_path);
	if (rc) {
		acquire_drc(phb->drc_index);
		return rc;
	}

	rc = remove_device_tree_nodes(phb->phb_ic_ofdt_path);
	if (rc)
		acquire_phb(phb->drc_name, &phb);

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
	int rc = 0;

	phb = get_node_by_name(opts->usr_drc_name, PHB_NODES);
	if (phb == NULL)
		return RC_NONEXISTENT;

	/* If this PHB still owns children that are not hotplug, fail. */
	for (child = phb->children; child; child = child->next) {
		if ((child->is_owned) && (child->dev_type != PCI_HP_DEV)) {
			rc = -1;
			goto phb_remove_error;
		}
	}

	/* Now, disable any hotplug children */
	for (child = phb->children; child; child = child->next) {
		if (child->dev_type == PCI_HP_DEV) {
			rc = disable_hp_children(child->drc_name);
			if (rc)
				err_msg("failed to disable hotplug children\n");

			rc = release_hp_children(child->drc_name);
			if (rc && rc != -EINVAL) {
				err_msg("failed to release hotplug children\n");
				goto phb_remove_error;
			}
		}
	}

	rc = dlpar_io_kernel_op(dlpar_remove_slot, phb->drc_name);
	if (rc) {
		err_msg("kernel remove failed for %s, rc = %d\n",
			phb->drc_name, rc);
		goto phb_remove_error;
	}

	rc = release_phb(phb);

phb_remove_error:
	if (phb)
		free_node(phb);

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
		err_msg("Could not find drc index for %s, unable to add the"
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
		err_msg("add_device_tree_nodes failed at %s\n", path);
		release_drc(drc.index, PHB_DEV);
		return -1;
	}

	/* Now that the node has been added to the device-tree, retrieve it.
	 * This also acts as a sanity check that everything up to this
	 * point has succeeded.
	 */
	*phb = get_node_by_name(drc_name, PHB_NODES);
	if (*phb == NULL) {
		err_msg("Could not get find \"%s\"\n", drc_name);
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

	rc = acquire_phb(opts->usr_drc_name, &phb);
	if (rc)
		return rc;

	rc = acquire_hp_children(phb->ofdt_path, &n_children);
	if (rc) {
		if (release_phb(phb)) {
			err_msg("Unknown failure. Data may be out of sync and "
					"\nthe system may require a reboot.\n");
		}
		goto phb_add_error;
	}

	rc = dlpar_io_kernel_op(dlpar_add_slot, phb->drc_name);
	if (rc) {
		if (n_children) {
			if (release_hp_children(phb->drc_name)) {
				err_msg("Unknown failure. Data may be out of "
					"sync and\nthe system may require "
					"a reboot.\n");
			}
		}

		if (release_phb(phb)) {
			err_msg("Unknown failure. Data may be out of sync and "
					"\nthe system may require a reboot.\n");
		}
		goto phb_add_error;
	}

	if (n_children) {
		rc = enable_hp_children(phb->drc_name);
		if (rc) {
			err_msg("Adapter configuration failed.\n");
			if (release_hp_children(phb->drc_name)) {
				err_msg("Unknown failure. Data may be out of "
					"sync and \nthe system may require "
					"a reboot.\n");
			}

			if (dlpar_io_kernel_op(dlpar_remove_slot, phb->drc_name)) {
				dbg("remove %s from hotplug subsystem failed\n",
				    phb->drc_name);
				err_msg("Unknown failure. Data may be out of "
					"sync and \nthe system may require "
					"a reboot.\n");
			}

			if (release_phb(phb)) {
				err_msg("Unknown failure. Data may be out of "
					"sync and \nthe system may require "
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
	if (opts->usr_drc_name == NULL) {
		err_msg("A drc name must be specified\n");
		return -1;
	}

	return 0;
}

int
drslot_chrp_phb(struct options *opts)
{
	int rc;

	if (! phb_dlpar_capable()) {
		err_msg("DLPAR PHB operations are not supported on"
			"this kernel.");
		return -1;
	}

	switch(opts->action) {
	    case ADD:
		rc = add_phb(opts);
		break;
	    case REMOVE:
		rc = remove_phb(opts);
		break;
	    case QUERY:
		rc = query_phb(opts);
		break;
	}

	return rc;
}
