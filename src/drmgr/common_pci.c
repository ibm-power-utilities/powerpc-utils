/**
 * @file common_pci.c
 * @brief Common routines for pci data
 *
 * Copyright (C) IBM Corporation 2006
 *
 */
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <assert.h>
#include <librtas.h>
#include "dr.h"
#include "drpci.h"
#include "ofdt.h"

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
 * find_ofdt_dname
 *
 * Find "name" of the device_node that is a child
 * of given node and its "ibm,loc-code" is the same
 * as node->name.
 *
 * @param node
 * @param path
 * @param ofdt_dname
 */
static int
find_ofdt_dname(struct dr_node *node, char *path)
{
	DIR *d = NULL;
	struct dirent *de;
	struct stat sb;
	char new_path[DR_PATH_MAX];
	char loc_code[DR_BUF_SZ];
	char *q;
	int found = 0;
	int rc;

	rc = get_property(path, "ibm,loc-code", &loc_code, DR_BUF_SZ);
	if ((rc == 0) && (strstr(loc_code, node->drc_name))) {
		rc = get_property(path, "name", node->ofdt_dname,
				  sizeof(node->ofdt_dname));
		if (rc == 0)
			return 1;
	}

	/* First look for a ofdt node that has ibm,loc-code property
	 * with a value that matches node->name.
	 *
	 * Set node->node->ofdt_dname to "name" of the ofdt node.
	 */
	d = opendir(path);
	if (d == NULL) {
		say(ERROR, "Could not open dir %s\n%s\n", path,
		    strerror(errno));
		return 0;
	}

	strcpy(new_path, path);
	q = new_path + strlen(new_path);
	*q++ = '/';

	while(((de = readdir(d)) != NULL) && (! found)) {
		/* skip all dot files */
		if (de->d_name[0] == '.')
			continue;

		strcpy(q, de->d_name);
		if (lstat(new_path, &sb) < 0)
			continue;

		if (S_ISLNK(sb.st_mode))
			continue;

		if (S_ISDIR(sb.st_mode)) {
			rc = get_property(path, "ibm,loc-code", &loc_code,
					  DR_BUF_SZ);
			if ((rc != 0) || (! strstr(loc_code, node->drc_name))) {
				found = find_ofdt_dname(node, new_path);
				continue;
			}

			rc = get_property(path, "name", node->ofdt_dname,
					  sizeof(node->ofdt_dname));
			if (rc == 0) {
				found = 1;
				break;
			}
		}
	}

	if (d != NULL)
		closedir(d);

	return found;
}

/**
 * add_child_node
 *
 * Create information about the Open Firmware node and
 * add that information to the appropriate per-node list of
 * Open Firmware nodes. Also create the corresponding information
 * for any PCI device.
 *
 * NOTES:
 *	1) does not need to be concerned about one or more Open
 *	   Firmware nodes having the used-by-rtas property present.
 *	   One of the RTAS services used during removing a PCI adapter
 *	   must take the appropriate action (most likely eliminate RTAS
 *	   usage) in this case.
 *	2) Open Firmware node RPA physical location code:
 *
 *	  [Un.m-]Pn[-Pm]-In[[/Zn]-An]
 *
 *	  where
 *		Un.m is for the enclosure for multi-enclosue systems
 *		Pn is for the planar
 *		In is for the slot
 *		/Zn is for the connector on the adapter card
 *		An is for the device connected to the adapter
 *
 *	  note
 *		There may be multiple levels of planars [Pm].
 *
 * RECOVERY OPERATION:
 *	1) This function does not add the Open Firmware node if the user
 *	   mode process has exceeded all available malloc space. This
 *	   should not happen based on the rather small total amount of
 *	   memory allocation required. The node is marked as skip.
 *	2) This function does not add the device information if there
 *	   is a problem initializing device. The node is marked as skip.
 *
 * @param parent
 * @param child_path
 */
static void
add_child_node(struct dr_node *parent, char *child_path)
{
	struct dr_connector *drc_list, *drc;
	char loc_code[DR_BUF_SZ];
	char *slash;
	struct dr_node *child;
	uint my_drc_index;
	int rc;

	assert(parent != NULL);
	assert(strlen(child_path) != 0);

	/* Make sure that the Open Firmware node is not added twice
	 * in case the ibm,my-drc-index property is put in all nodes
	 * for the adapter instead of just the ones at the connector.
	 */
	if (parent->children != NULL) {
		struct dr_node *tmp;
		for (tmp = parent->children; tmp; tmp = tmp->next) {
			if (! strcmp(tmp->ofdt_path, child_path))
				return;
		}
	}

	/* Create the Open Firmware node's information and insert that
	 * information into the node's list based on the node's RPA
	 * physical location code.  Ignore the OF node if the node
	 * does not have an RPA physical location code because that is
	 * a firmware error.
	 */
	rc = get_property(child_path, "ibm,loc-code", &loc_code, DR_BUF_SZ);
	if (rc)
		return;

	/* Skip the Open Firmware node if it is a device node. Determine that
	 * the node is for a device by looking for a hyphen after the last
	 * slash (...-In/Z1-An).
	 */
	slash = strrchr(loc_code, '/');
	if (slash != NULL) {
		char *hyphen;
		hyphen = strchr(slash, '-');
		if (hyphen != NULL)
			return;

		*slash = '\0';
	}

	if (parent->dev_type == PCI_HP_DEV) { 	/* hotplug */
		/* Squadrons don't have "/" in devices' (scsi,
		 * ethernet, tokenring ...) loc-code strings.
		 *
		 * Skip the Open Firmware node if the node's RPA
		 * physical location code does not match the node's
		 * location code.  Ignore the connector information,
		 * i.e. information after last slash if no hyphen
		 * follows.
		 */
		if ((strcmp(parent->drc_name, loc_code) != 0)
		    && (slash != NULL)) {
			parent->skip = 1;
			return;
		}
	}

	/* Restore the slash because the full RPA location code
	 * is saved for each OF node so that the connector
	 * information can be used to sort the OF node list for
	 * each node.
	 */
	if (slash != NULL)
		*slash = '/';

	if (get_my_drc_index(child_path, &my_drc_index))
		return;

	/* need the drc-info in the dir above */
	slash = strrchr(child_path, '/');
	*slash = '\0';
	drc_list = get_drc_info(child_path);
	*slash = '/';

	for (drc = drc_list; drc != NULL; drc = drc->next) {
		if (drc->index == my_drc_index)
			break;
	}

	/* Allocate space for the Open Firmware node information.  */
	child = alloc_dr_node(drc, parent->dev_type, child_path);
	if (child == NULL) {
		parent->skip = 1;
		return;
	}

	if ((! strcmp(parent->drc_type, "SLOT"))
	    && (parent->dev_type == PCI_DLPAR_DEV))
		snprintf(child->ofdt_dname, DR_STR_MAX, "%s",
			 parent->ofdt_dname);
	else
		get_property(child_path, "name", child->ofdt_dname,
			     sizeof(child->ofdt_dname));

	switch (parent->dev_type) {
	    case PCI_HP_DEV:
	    case PCI_DLPAR_DEV:
	    {
		get_property(child_path, "vendor-id", &child->pci_vendor_id,
			     sizeof(child->pci_vendor_id));
		get_property(child_path, "device-id", &child->pci_device_id,
			     sizeof(child->pci_device_id));
		get_property(child_path, "class_code", &child->pci_class_code,
			     sizeof(child->pci_class_code));
		break;
	    }

	    case HEA_DEV:
	    {
		child->dev_type = HEA_PORT_DEV;

		get_property(child_path, "ibm,hea-port-no",
			     &child->hea_port_no,
			     sizeof(child->hea_port_no));
		get_property(child_path, "ibm,hea-port-tenure",
			     &child->hea_port_tenure,
			     sizeof(child->hea_port_tenure));
		break;
	    }
	}

	child->next = parent->children;
	parent->children = child;
}

/* This forward declaration is needed because init_node and examine_child
 * call each other.
 */
static int examine_child(struct dr_node *, char *);

/**
 * init_node
 *
 * @param node
 * @returns 0 on success, !0 otherwise
 */
static int
init_node(struct dr_node *node)
{
	struct dirent **de_list, *de;
	char *newpath;
	int count;
	int rc, i;

	if (node->is_owned)
		find_ofdt_dname(node, node->ofdt_path);

	count = scandir(node->ofdt_path, &de_list, 0, alphasort);
	for (i = 0; i < count; i++) {
		de = de_list[i];
		if ((de->d_type != DT_DIR) || is_dot_dir(de->d_name))
			continue;

		newpath = zalloc(strlen(node->ofdt_path) +
				 strlen(de->d_name) + 2);
		if (newpath == NULL) {
			say(ERROR, "Could not allocate path for node at "
			    "%s/%s\n", node->ofdt_path, de->d_name);
			return 1;
		}

		sprintf(newpath, "%s/%s", node->ofdt_path, de->d_name);
		rc = examine_child(node, newpath);
		if (rc)
			return rc;
	}

	return 0;
}

/**
 * examine_children
 *
 * @param node
 * @param child_path
 * @returns 0 on success, !0 otherwise
 */
static int
examine_child(struct dr_node *node, char *child_path)
{
	uint32_t my_drc_index;
	int used = 0;
	int rc = 0;

	if (get_my_drc_index(child_path, &my_drc_index))
		goto done;

	if (node->dev_type == PCI_HP_DEV) {
		if (node->drc_index == my_drc_index) {
			/* Add hotplug children */
			add_child_node(node, child_path);
			used = 1;
		}
	} else {
		if (! node->is_owned) {
			if (node->drc_index == my_drc_index) {
				/* Update node path */
				snprintf(node->ofdt_path, DR_PATH_MAX, "%s",
					 child_path);
				node->is_owned = 1;
				used = 1;

				/* Populate w/ children */
				rc = init_node(node);
			}
		} else {
			/* Add all DR-capable children */
			add_child_node(node, child_path);
			used = 1;
		}
	}
done:
	if (! used)
		free(child_path);

	return rc;
}

static inline int is_hp_type(char *type)
{
	return (strtoul(type, NULL, 10) > 0);
}

static inline int is_logical_type(char *type)
{
	return (!strcmp(type, "SLOT"));
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
 * devspec_check_node
 *
 * @param node
 * @param sysfs_path
 * @param full_of_path
 * @param found
 * @returns 0 on success, !0 otherwise
 */
static int
devspec_check_node(struct dr_node *node, char *sysfs_path,
		   char *full_of_path, int *found)
{
	struct dr_node *child;

	*found = 0;

	if (node->ofdt_path == NULL)
		return 0;

	if (! strcmp(full_of_path, node->ofdt_path)) {
		snprintf(node->sysfs_dev_path, DR_PATH_MAX, "%s", sysfs_path);
		*found = 1;
		return 0;
	}

	for (child = node->children; child; child = child->next) {
		if (! strcmp(full_of_path, child->ofdt_path)) {
			snprintf(child->sysfs_dev_path, DR_PATH_MAX, "%s",
				 sysfs_path);
			*found = 1;
			return 0;
		}
	}

	return 0;
}

/**
 * correlate_devspec
 *
 * @param sysfs_path
 * @param ofdt_path
 */
static int
correlate_devspec(char *sysfs_path, char *ofdt_path, struct dr_node *node_list)
{
	struct dr_node *node;
	char *full_of_path;
	int found;
	int rc;

	full_of_path = of_to_full_path(ofdt_path);
	for (node = node_list; node != NULL; node = node->next) {
		rc = devspec_check_node(node, sysfs_path, full_of_path, &found);
		if (rc)
			return rc;
		if (found)
			break;
	}

	free(full_of_path);
	return 0;
}

/**
 * add_linux_devices
 *
 */
static void
add_linux_devices(char *start_dir, struct dr_node *node_list)
{
	struct dirent *de;
	DIR *d;
	char *dir;
	int rc;

	if (start_dir == NULL)
		dir = "/sys/devices";
	else
		dir = start_dir;

	d = opendir(dir);
	if (d == NULL) {
		say(ERROR, "failed to open %s\n%s\n", dir, strerror(errno));
		return;
	}

	while ((de = readdir(d)) != NULL) {
		char buf[1024];

		if (is_dot_dir(de->d_name))
			continue;

		if (de->d_type == DT_DIR) {
			sprintf(buf, "%s/%s", dir, de->d_name);
			add_linux_devices(buf, node_list);
		} else if (! strcmp(de->d_name, "devspec")) {
			char devspec[DR_PATH_MAX];

			sprintf(buf, "%s/%s", dir, de->d_name);
			rc = get_str_attribute(buf, NULL, devspec, DR_PATH_MAX);
			if (rc == 0)
				rc = correlate_devspec(dir, devspec, node_list);
		}
	}
	closedir(d);
}

/**
 * add_hea_node
 * @brief Add a node for an HEA adapter
 *
 * @param path ofdt_path to this node
 * @param drc_list list of drc's at OFDT_BASE
 * @param pointer to the node list to add new nodes to
 * @return 0 on success, !0 otherwise
 */
static int
add_hea_node(char *path, struct dr_connector *drc_list,
	     struct dr_node **node_list)
{
	struct dr_connector *drc;
	struct dr_node *hea_node;
	uint my_drc_index;
	int rc;

	if (drc_list == NULL)
		return -1;

	if (get_my_drc_index(path, &my_drc_index))
		return -1;

	for (drc = drc_list; drc != NULL; drc = drc->next) {
		if (drc->index == my_drc_index)
			break;
	}

	if (drc == NULL) {
		say(ERROR, "Could not find drc index 0x%x to add to hea list\n",
		    my_drc_index);
		return -1;
	}

	hea_node = alloc_dr_node(drc, HEA_DEV, path);
	if (hea_node == NULL) {
		say(ERROR, "Could not allocate hea node for drc index 0x%x\n",
		    my_drc_index);
		return -1;
	}

	hea_node->is_owned = 1;
	rc = init_node(hea_node);
	if (rc) {
		free(hea_node);
		return -1;
	}

	hea_node->next = *node_list;
	*node_list = hea_node;
	return 0;
}

/**
 * add_pci_vio_node
 * @bried Add a PCI or virtual device node
 *
 * @param path ofdt path to this node
 * @param dev_type type of device
 * @param node_list pointer to list to add node to
 * @returns 0 on success, !0 otherwise
 */
static int
add_pci_vio_node(const char *path, int dev_type, struct dr_node **node_list)
{
	struct dr_connector *drc_list;
	struct dr_connector *drc;
	struct dr_node *node;
	int child_dev_type = 0;
	int rc = -1;

	drc_list = get_drc_info(path);
	if (drc_list == NULL)
		return -1;

	for (drc = drc_list; drc != NULL; drc = drc->next) {
		switch (dev_type) {
			case PCI_HP_DEV:
				if (! is_hp_type(drc->type))
					continue;
				child_dev_type = dev_type;
				break;

			case PCI_DLPAR_DEV:
			case VIO_DEV:
				if (! is_logical_type(drc->type))
					continue;
				child_dev_type = dev_type;
				break;
			case PHB_DEV:
				if (is_logical_type(drc->type))
					child_dev_type = PCI_DLPAR_DEV;
				else
					child_dev_type = PCI_HP_DEV;
				break;
		}

		node = alloc_dr_node(drc, child_dev_type, path);
		if (node == NULL) {
			say(ERROR, "Could not allocate pci/vio node\n");
			return -1;
		}

		if (child_dev_type == PCI_HP_DEV)
			node->is_owned = 1;

		rc = init_node(node);
		if (rc) {
			free(node);
			return rc;
		}

		node->next = *node_list;
		*node_list = node;
	}

	return rc;
}

/**
 * add_phb_node
 * @brief Add a PHB node to the node list
 *
 * @param ofdt_poath, ofdt path to this node
 * @param drc_list list of drc's at OFDT_BASE
 * @param node_list list of nodes to add node to
 * @returns 0 on success, !0 otherwise
 */
static int
add_phb_node(char *ofdt_path, struct dr_connector *drc_list,
	     struct dr_node **node_list)
{
	struct dr_node *phb_node;
	struct dr_connector *drc;
	uint my_drc_index;

	if (get_my_drc_index(ofdt_path, &my_drc_index))
		return -1;

	for (drc = drc_list; drc; drc = drc->next) {
		if (drc->index == my_drc_index)
			break;
	}

	if (drc == NULL) {
		say(ERROR, "Could not find drc index 0x%x to add to phb list\n",
		    my_drc_index);
		return -1;
	}

	phb_node = alloc_dr_node(drc, PHB_DEV, ofdt_path);
	if (phb_node == NULL) {
		say(ERROR, "Could not allocate PHB node for drc index 0x%x\n",
		    my_drc_index);
		return -1;
	}

	phb_node->is_owned = 1;

	add_pci_vio_node(ofdt_path, PHB_DEV, &phb_node->children);

	phb_node->next = *node_list;
	*node_list = phb_node;
	return 0;
}

/**
 * update_phb_ic_info
 * @brief Add interrupt controller information to PHB nodes
 *
 * We need to have the interrupt-controller ofdt paths for all PHB
 * nodes to do DLPAR.  This routine adds that information for PHB nodes
 * that we found.
 *
 * @param node_list list of PHB nodes
 * @returns 0 on success, !0 otherwise
 */
static int
update_phb_ic_info(struct dr_node *node_list)
{
	char *ic_dir = "interrupt-controller";
	struct dr_node *node;
	struct dirent *de;
	DIR *d;
	int rc = 0;

	d = opendir(OFDT_BASE);
	if (d == NULL) {
		say(ERROR, "failed to open %s\n%s\n", OFDT_BASE,
		    strerror(errno));
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		uint my_drc_index;
		char ofdt_path[DR_PATH_MAX];

		if ((de->d_type != DT_DIR) || is_dot_dir(de->d_name))
			continue;

		if (strncmp(de->d_name, ic_dir, strlen(ic_dir)))
			continue;

		sprintf(ofdt_path, "%s/%s", OFDT_BASE, de->d_name);
		rc = get_my_drc_index(ofdt_path, &my_drc_index);
		if (rc)
			/* This is expected to fail sometimes, as there can be
			 * more ICs than PHBs on a system.  In this case, some
			 * ICs won't have my-drc-index. */
			continue;

		for (node = node_list; node; node = node->next) {
			if ((node->dev_type == PHB_DEV)
			    && (node->drc_index == my_drc_index)) {
				snprintf(node->phb_ic_ofdt_path, DR_PATH_MAX,
					 "%s", ofdt_path);
				break;
			}
		}
	}

	closedir(d);
	return 0;
}

/**
 * get_dlpar_nodes
 *
 * @param list_type
 * @returns pointer to node list on success, NULL otherwise
 */
struct dr_node *
get_dlpar_nodes(uint32_t node_types)
{
	struct dr_connector *drc_list = NULL;
	struct dr_node *node_list = NULL;
	struct dirent *de;
	DIR *d;
	char path[1024];

	say(DEBUG, "Getting node types 0x%08x\n", node_types);

	d = opendir(OFDT_BASE);
	if (d == NULL) {
		say(ERROR, "failed to open %s\n%s\n", OFDT_BASE,
		    strerror(errno));
		return NULL;
	}

	while ((de = readdir(d)) != NULL) {
		if ((de->d_type != DT_DIR) || is_dot_dir(de->d_name))
			continue;

		memset(path, 0, 1024);
		sprintf(path, "%s/%s", OFDT_BASE, de->d_name);

		if ((! strcmp(de->d_name, "vdevice"))
		    && (node_types & VIO_NODES))
			add_pci_vio_node(path, VIO_DEV, &node_list);
		else if (! strncmp(de->d_name, "pci@", 4)) {
			if (node_types & PCI_NODES)
				add_pci_vio_node(path, PCI_DLPAR_DEV,
						 &node_list);
			else if (node_types & PHB_NODES) {
				if (drc_list == NULL)
					drc_list = get_drc_info(OFDT_BASE);
				add_phb_node(path, drc_list, &node_list);
			}
		} else if ((! strncmp(de->d_name, "lhea@", 5))
			 && (node_types & HEA_NODES)) {
			if (drc_list == NULL)
				drc_list = get_drc_info(OFDT_BASE);

			add_hea_node(path, drc_list, &node_list);
		}
	}

	closedir(d);

	if (node_list != NULL) {
		add_linux_devices(NULL, node_list);

		if (node_types & PHB_NODES)
			update_phb_ic_info(node_list);
	}

	return node_list;
}

/**
 * _get_hp_nodes
 * @brief The workhorse routine for finding hotplug nodes
 *
 * @param dir start directory for searching
 * @param pointer to list of nodes to return
 * @return 0 on success, !0 otherwise
 */
static int
_get_hp_nodes(char *dir, struct dr_node **list)
{
	struct dirent *de;
	DIR *d;
	char path[1024];

	d = opendir(dir);
	if (d == NULL) {
		say(ERROR, "failed to open %s\n%s\n", dir, strerror(errno));
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		if ((de->d_type != DT_DIR) || is_dot_dir(de->d_name))
			continue;

		if (strncmp(de->d_name, "pci@", 4))
			continue;

		memset(path, 0, 1024);
		sprintf(path, "%s/%s", dir, de->d_name);

		add_pci_vio_node(path, PCI_HP_DEV, list);
		_get_hp_nodes(path, list);
	}
	closedir(d);

	return 0;
}


/**
 * get_hp_nodes
 * @brief retrieve a list of hotplug nodes on the partition
 *
 * @return pointer to node list on success, NULL on failure
 */
struct dr_node *
get_hp_nodes()
{
	struct dr_node *node_list = NULL;

	say(DEBUG, "Retrieving hotplug nodes\n");

	_get_hp_nodes(OFDT_BASE, &node_list);
	if (node_list != NULL)
		add_linux_devices(NULL, node_list);

	return node_list;
}

struct dr_node *
get_node_by_name(const char *drc_name, uint32_t node_type)
{
	struct dr_node *node, *all_nodes;
	struct dr_node *prev_node = NULL;
	int child_found = 0;

	all_nodes = get_dlpar_nodes(node_type);
	if (all_nodes == NULL) {
		say(ERROR, "There are no DR capable slots on this system\n");
		return NULL;
	}

	print_node_list(all_nodes);

	for (node = all_nodes; node; node = node->next) {
		struct dr_node *child;
		uint32_t drc_index;

		if (strcmp(node->drc_name, drc_name) == 0)
			break;

		/* See if the drc index was specified */
		drc_index = strtoul(drc_name, NULL, 0);
		if (node->drc_index == drc_index)
			continue;

		for (child = node->children; child; child = child->next) {
			if (strcmp(drc_name, child->drc_name) == 0)
				child_found = 1;

			if (child->drc_index == drc_index)
				child_found = 1;
		}

		if (child_found)
			break;

		prev_node = node;
	}

	if (node) {
		if (prev_node)
			prev_node->next = node->next;
		else
			/* First in list */
			all_nodes = all_nodes->next;

		node->next = NULL;
	}

	free_node(all_nodes);
	return node;
}

/**
 * cmp_drcname
 *
 * Compare the drcname's opf two nodes
 *
 * @param name1
 * @param name2
 * @returns 1 if the drcnames match, 0 otherwise
 */
int
cmp_drcname(char *name1, char *name2)
{
	char	*ptr;

	if (name2 == NULL)
		return 0;

	/* The string pointed to by name2 may in fact be a location code
	 * for a device in a PCI node.  Hence, its hardware location
	 * code will not match exactly.  However, if this is the case,
	 * the substring in the device location code up to the first '/'
	 * character should match the node's location exactly.
	 *
	 * If there's a '/' in name2, then shorten string to the
	 * LAST '/' character.
	 * Note: this affects name2 globally!
	 */
	ptr = strrchr(name2, '/');
	if (ptr != NULL)
		/* End the string at the '/'
		 * compiler doesn't like NULL --Linda
		 */

		*ptr = '\0';

	/* Now compare */
	return (! strcmp(name1, name2));
}

/**
 * get_bus_id
 *
 * @param loc_code
 * @returns
 */
static char *
get_bus_id(char *loc_code)
{
	DIR *d;
	struct dirent *ent;
	char *dir = "/sys/bus/pci/slots";
	int inlen;
	char *ptr;

	/* Strip any newline from the input location */
	if ((ptr = strchr(loc_code, '\n')) != NULL)
		inlen = ptr - loc_code;
	else
		inlen = strlen(loc_code);

	d = opendir(dir);
	if (d == NULL) {
		say(ERROR, "failed to open %s: %s\n", dir, strerror(errno));
		return NULL;
	}

	while ((ent = readdir(d))) {
		char path[DR_PATH_MAX], location[DR_BUF_SZ];
		FILE *f;

		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;

		sprintf(path, "/sys/bus/pci/slots/%s/phy_location",
			ent->d_name);
		f = fopen(path, "r");
		if (f == NULL)
			continue;

		fread(location, sizeof(location), 1, f);
		fclose(f);

		/* Strip any newline from the location to compare */
		if  ((ptr = strchr(location, '\n')) != NULL)
			*ptr = '\0';

		if ((strlen(location) == inlen &&
				!strncmp(loc_code, location, inlen))) {
			char *bus_id;

			bus_id = strdup(ent->d_name);
			if (bus_id)
				return bus_id;
			else {
				say(ERROR, "Failed to allocate bus id\n");
				break;
			}
		}
	}

	closedir(d);
	return NULL;
}

/**
 * get_hp_adapter_status
 * @brief check adapter status
 *
 * @param drc_name
 * @returns 0 if slot is empty
 * @returns 1 if adapter is configured
 * @returns 2 if adapter is not configured
 * @returns <0 on error
 */
int
get_hp_adapter_status(char *drc_name)
{
	int value, rc = 0;
	char path[DR_PATH_MAX], *bus_id;

	bus_id = get_bus_id(drc_name);
	if (bus_id)
		sprintf(path, PHP_SYSFS_ADAPTER_PATH, bus_id);
	else
		sprintf(path, PHP_SYSFS_ADAPTER_PATH, drc_name);

	rc = get_int_attribute(path, NULL, &value, sizeof(value));
	if (rc)
		return -1;

	say(DEBUG, "hp adapter status for %s is %d\n", drc_name, value);

	rc = value;
	if (rc != CONFIG && rc != NOT_CONFIG && rc != EMPTY)
	    rc = -1;

	return rc;
}

/**
 * set_hp_adapter_status
 *
 * @param operation 1=> config 0=> unconfig
 * @param slot_name
 * @returns 0 on success, !0 otherwise
 */
int
set_hp_adapter_status(uint operation, char *slot_name)
{
	int rc = 0;
	FILE *file;
	char *bus_id;
	char path[DR_PATH_MAX];

	bus_id = get_bus_id(slot_name);
	if (bus_id)
		sprintf(path, PHP_SYSFS_POWER_PATH, bus_id);
	else
		sprintf(path, PHP_SYSFS_POWER_PATH, slot_name);

	say(DEBUG, "setting hp adapter status to %s for %s\n",
	    ((operation+1 - 1) ? "CONFIG adapter" : "UNCONFIG adapter"),
	    slot_name);

	file = fopen(path, "w");
	if (file == NULL) {
		say(ERROR, "failed to open %s: %s\n", path, strerror(errno));
		return -ENODEV;
	}

	rc = fwrite((operation+1 - 1) ? "1" : "0", 1, 1, file);
	if (rc != 1)
		rc = -EACCES;
	else
		rc = 0;

	fclose(file);
	return rc;
}

/**
 * pci_rescan_bus
*
 * @returns 0 on success, !0 otherwise
 */
int
pci_rescan_bus()
{
	int rc = 0;
	FILE *file;

	file = fopen(PCI_RESCAN_PATH, "w");
	if (file == NULL) {
		say(ERROR, "failed ot open %s: %s\n", PCI_RESCAN_PATH, strerror(errno));
		return -ENODEV;
	}

	rc = fwrite("1", 1, 1, file);
	if (rc != 1)
		rc = -EACCES;

	fclose(file);
	return rc;
}

/**
 * pci_remove_device
 *
 * @returns 0 on success, !0 otherwise
 */

int
pci_remove_device(struct dr_node *node)
{
	int rc = 0;
	FILE *file;
	char path[DR_PATH_MAX];

	sprintf(path, "%s/%s", node->sysfs_dev_path, "remove");
	file = fopen(path, "w");
	if (file == NULL) {
		say(ERROR, "failed to open %s: %s\n", path, strerror(errno));
		return -ENODEV;
	}

	rc = fwrite("1", 1, 1, file);
	if (rc != 1)
		rc = -EACCES;

	fclose(file);
	return rc;
}

/**
 * dlpar_io_kernel_op
 * @brief access kernel interface files
 *
 * @param interface_file
 * @param drc_name
 * @returns 0 on success, !0 otherwise
 */
static int dlpar_io_kernel_op(const char *interface_file, const char *drc_name)
{
	int rc = 0, len;
	FILE *file;

	len = strlen(drc_name);
	say(DEBUG, "performing kernel op for %s, file is %s\n", drc_name,
	    interface_file);

	do {
		errno = 0;

    		file = fopen(interface_file, "r+");
    		if (file == NULL) {
			say(ERROR, "failed to open %s: %s\n", interface_file,
			    strerror(errno));
			return -ENODEV;
    		}

    		rc = fwrite(drc_name, 1, len, file);
		fclose(file);

		sleep(1);
		if (drmgr_timed_out())
			return -1;
	} while (errno == EBUSY);

 	if (errno || (rc != len)) {
		say(ERROR, "kernel I/O op failed, rc = %d len = %d.\n%s\n",
		    rc, len, strerror(errno));

		return errno ? errno : rc;
	}

	return 0;
}

int dlpar_remove_slot(const char *drc_name)
{
	return dlpar_io_kernel_op(remove_slot_fname, drc_name);
}

int dlpar_add_slot(const char *drc_name)
{
	return dlpar_io_kernel_op(add_slot_fname, drc_name);
}

void
print_node_list(struct dr_node *first_node)
{
	struct dr_node *parent;
	struct dr_node *child;

	parent = first_node;
	say(DEBUG, "\nDR nodes list\n==============\n");
	while (parent) {
		say(DEBUG, "%s: %s\n"
		    "\tdrc index: 0x%x        description: %s\n"
		    "\tdrc name: %s\n\tloc code: %s\n", 
		    parent->ofdt_path, (parent->skip ? "(SKIP)" : ""),
		    parent->drc_index, node_type(parent),
		    parent->drc_name, parent->loc_code);

		child = parent->children;
		while (child) {
			say(DEBUG, "%s: %s\n"
			    "\tdrc index: 0x%x        description: %s\n"
			    "\tdrc name: %s\n\tloc code: %s\n",
			    child->ofdt_path, (child->skip ? "(SKIP)" : ""),
			    child->drc_index, node_type(child),
			    child->drc_name, child->loc_code);

			child = child->next;
		}

		parent = parent->next;
	}
	say(DEBUG, "\n");
}

#define ACQUIRE_HP_START	2
#define ACQUIRE_HP_SPL		3
#define ACQUIRE_HP_UNISO	4
#define ACQUIRE_HP_CFGCONN  	5
#define ACQUIRE_HP_ADDNODES 	6

/**
 * acquire_hp_resource
 *
 * @param drc
 * @param of_path
 * @returns 0 on success, !0 otherwise
 */
static int
acquire_hp_resource(struct dr_connector *drc, char *of_path)
{
	struct of_node *new_nodes;
	int progress = ACQUIRE_HP_START;
	int state;
	int rc;

	state = dr_entity_sense(drc->index);
	if (state == PRESENT || state == NEED_POWER || state == PWR_ONLY) {
		rc = set_power(drc->powerdomain, POWER_ON);
		if (rc) {
			say(ERROR, "set power failed for 0x%x\n",
			    drc->powerdomain);
			return progress;
		}

		progress = ACQUIRE_HP_SPL;
		if (state == PWR_ONLY)
			state = dr_entity_sense(drc->index);
	}

	if (state == PRESENT || state == NEED_POWER) {
		rc = rtas_set_indicator(ISOLATION_STATE, drc->index,
				UNISOLATE);
		if (rc) {
			say(ERROR, "set ind failed for 0x%x\n", drc->index);
			return progress;
		}

		progress = ACQUIRE_HP_UNISO;
		if (state == NEED_POWER)
			state = dr_entity_sense(drc->index);
	}

	if (state < 0) {
		say(ERROR, "invalid state %d\n", state);
		return progress;
	}

	if (state == PRESENT) {
		new_nodes = configure_connector(drc->index);
		if (new_nodes == NULL)
			return progress;

		progress = ACQUIRE_HP_CFGCONN;

		rc = add_device_tree_nodes(of_path, new_nodes);
		if (rc) {
			say(ERROR, "add nodes failed for 0x%x\n", drc->index);
			return progress;
		}
	}

	return 0;
}

/**
 * acquire_hp_children
 *
 * @param slot_of_path
 * @param n_acquired
 * @returns 0 on success, !0 otherwise
 */
int acquire_hp_children(char *slot_of_path, int *n_acquired)
{
	struct dr_connector *drc_list, *drc;
	int rc;
	int failure = 0, count = 0;

	drc_list = get_drc_info(slot_of_path);
	if (drc_list == NULL) {
		/* No hotplug-capable children */
		return 0;
	}

	for (drc = drc_list; drc != NULL; drc = drc->next) {
		if (is_hp_type(drc->type)) {
			rc = acquire_hp_resource(drc, slot_of_path);
			if (rc) {
				say(ERROR, "failed to acquire %s\n", drc->name);
				failure = 1;
			}
			count++;
		}
	}

	*n_acquired = count;
	return failure;
}

/**
 * release_hp_resource
 *
 * @param drc_index
 * @param power_domain
 * @returns 0 on success, !0 otherwise
 */
static int
release_hp_resource(struct dr_node *node)
{
	int rc;

	rc = remove_device_tree_nodes(node->ofdt_path);
	if (rc) {
		say(ERROR, "failed to remove kernel nodes for index 0x%x\n",
		    node->drc_index);
		return -EIO;
	}

	rc = rtas_set_indicator(DR_INDICATOR, node->drc_index, LED_OFF);
	if (rc) {
		say(ERROR, "failed to set led off for index 0x%x\n",
		    node->drc_index);
		return -EIO;
	}

	rc = rtas_set_indicator(ISOLATION_STATE, node->drc_index, ISOLATE);
	if (rc) {
		say(ERROR, "failed to isolate for index 0x%x\n",
		    node->drc_index);
		return -EIO;
	}

	rc = set_power(node->drc_power, POWER_OFF);
	if (rc) {
		struct stat sb;

		say(ERROR, "failed to power off for index 0x%x\n",
		    node->drc_index);

		if (!stat(IGNORE_HP_PO_PROP, &sb))
			say(ERROR, "Ignoring hot-plug power off failure.\n");
		else
			return -EIO;
	}

	return 0;
}

/**
 * release_hp_children
 *
 * @param parent_drc_name
 * @returns 0 on success, !0 otherwise
 */
int
release_hp_children(char *parent_drc_name)
{
	struct dr_node *hp_list, *slot, *child;
	int rc;

	hp_list = get_hp_nodes();
	for (slot = hp_list; slot; slot = slot->next)
		if (!strcmp(parent_drc_name, slot->drc_name))
			break;

	if (slot == NULL) {
		free_node(hp_list);
		return -EINVAL;
	}

	for (child = slot->children; child; child = child->next) {
		rc = release_hp_resource(child);
		if (rc)
			return rc;
	}

	free_node(hp_list);
	return (rc < 0) ? rc : 0;
}

/**
 * enable_hp_children
 *
 * @param drc_name
 * @returns 0 on success, !0 otherwise
 */
int
enable_hp_children(char *drc_name)
{
	if (get_hp_adapter_status(drc_name) == NOT_CONFIG) {
		set_hp_adapter_status(PHP_CONFIG_ADAPTER, drc_name);

		if (get_hp_adapter_status(drc_name) != CONFIG)
			return 1;
	}

	return 0;
}

/**
 * disable_hp_children
 *
 * @param drc_name
 * @returns 0 on success, !0 otherwise
 */
int disable_hp_children(char *drc_name)
{
	if (get_hp_adapter_status(drc_name) != NOT_CONFIG) {
		set_hp_adapter_status(PHP_UNCONFIG_ADAPTER, drc_name);

		if (get_hp_adapter_status(drc_name) != NOT_CONFIG)
			return 1;
	}
	return 0;
}
