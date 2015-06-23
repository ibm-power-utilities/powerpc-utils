/**
 * @file drslot_chrp_mem
 *
 *
 * Copyright (C) IBM Corporation 2006
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <inttypes.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "dr.h"
#include "ofdt.h"
#include "drmem.h"

static int block_sz_bytes = 0;
static char *state_strs[] = {"offline", "online"};

static char *usagestr = "-c mem {-a | -r} {-q <quantity> -p {variable_weight | ent_capacity} | {-q <quantity> | -s [<drc_name> | <drc_index>]}}";

/**
 * mem_usage
 * @brief return usage string
 *
 * @param char ** pointer to usage string
 */
void
mem_usage(char **pusage)
{
	*pusage = usagestr;
}

/**
 * get_phandle
 *
 * @param char * device tree node path
 * @param int *  pointer to phandle
 */
int
get_phandle(char *path, uint *phandle)
{
        int rc1,rc2;

        /* get "linux,phandle" property */
        rc1 = get_ofdt_uint_property(path, "linux,phandle", phandle);

        /* overwrite with "ibm,handle" if it exists */
        rc2 = get_ofdt_uint_property(path, "ibm,phandle", phandle);
        /* return bad if both gets failed */
        if (rc1 && rc2)
                return rc1;
        else
                return 0;
}

/**
 * free_lmbs
 * @brief Free any allocated memory referenced from the list head
 *
 * @param lmb_list list head to free
 */
void
free_lmbs(struct lmb_list_head *lmb_list)
{
	free_node(lmb_list->lmbs);

	if (lmb_list->drconf_buf)
		free(lmb_list->drconf_buf);
}

/**
 * get_mem_scns
 * @brief Find the memory sections associated with the specified lmb
 *
 * @param lmb lmb to find memory sections of
 * @return 0 on success, !0 otherwise
 */
static int
get_mem_scns(struct dr_node *lmb)
{
	uint32_t lmb_sz = lmb->lmb_size;
	uint64_t phys_addr = lmb->lmb_address;
	uint32_t mem_scn;
	int rc = 0;

	mem_scn = phys_addr / block_sz_bytes;

	/* Assume the lmb is removable.  If we find a non-removable memory
	 * section then we flip the lmb back to not removable.
	 */
	lmb->is_removable = 1;

	lmb_sz = lmb->lmb_size;
	while (lmb_sz > 0) {
		char *sysfs_path = "/sys/devices/system/memory/memory%d";
		struct mem_scn *scn;
		struct stat sbuf;

		scn = zalloc(sizeof(*scn));
		if (scn == NULL)
			return -1;

		sprintf(scn->sysfs_path, sysfs_path, mem_scn);
		scn->phys_addr = phys_addr;

		if (!stat(scn->sysfs_path, &sbuf)) {
			get_int_attribute(scn->sysfs_path, "removable",
					  &scn->removable,
					  sizeof(scn->removable));
			if (!scn->removable)
				lmb->is_removable = 0;
		}

		scn->next = lmb->lmb_mem_scns;
		lmb->lmb_mem_scns = scn;

		lmb_sz -= block_sz_bytes;
		phys_addr += block_sz_bytes;
		mem_scn = phys_addr / block_sz_bytes;
	}

	/* If we do not find any associated memory sections, mark this
	 * as not removable.
	 */
	if ((lmb->lmb_mem_scns == NULL) || lmb->unusable)
		lmb->is_removable = 0;

	return rc;
}

/**
 * get_lmb_size
 * @brief Retrieve the size of the lmb
 *
 * @param lmb lmb to get size for, or NULL for drconf lmb size
 * @param lmb_list list of lmbs, used to determine drconf usage
 * @returns size of lmb on succes, 0 on failure
 */
static int
get_lmb_size(struct dr_node *lmb)
{
	uint32_t regs[4];
	int rc;

	rc = get_property(lmb->ofdt_path, "reg", &regs, sizeof(regs));
	if (rc) {
		say(DEBUG, "Could not determine LMB size for %s\n",
		    lmb->ofdt_path);
		return rc;
	}

	lmb->lmb_size = be32toh(regs[3]);
	return 0;
}

/**
 * get_mem_node_lmbs
 * @brief Retrieve lmbs from the OF device tree represented as memory@XXX nodes
 *
 * @param lmb_list pointer to lmb list head to populate
 * @returns 0 on success, !0 on failure
 */
int
get_mem_node_lmbs(struct lmb_list_head *lmb_list)
{
	struct dr_node *lmb;
	struct dirent *de;
	DIR *d;
	int rc = 0;

	d = opendir(OFDT_BASE);
	if (d == NULL) {
		report_unknown_error(__FILE__, __LINE__);
		say(DEBUG, "Could not open %s\n", OFDT_BASE);
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		char path[1024];
		uint32_t my_drc_index;
		char *tmp;

		if (de->d_type != DT_DIR)
			continue;

		if (strncmp(de->d_name, "memory@", 7))
			continue;

		memset(path, 0, 1024);
		sprintf(path, "%s/%s", OFDT_BASE, de->d_name);

		if (get_my_drc_index(path, &my_drc_index))
			continue;

		for (lmb = lmb_list->lmbs; lmb; lmb = lmb->next) {
			if (lmb->drc_index == my_drc_index)
				break;
		}

		if (lmb == NULL) {
			say(DEBUG, "Could not find LMB with drc-index of %x\n",
			    my_drc_index);
			rc = -1;
			break;
		}

		snprintf(lmb->ofdt_path, DR_PATH_MAX, "%s", path);
		lmb->is_owned = 1;

		/* Find the lmb size for this lmb */
		/* XXX Do nothing with size and break if it can't be found
		 * but don't change rc to indicate failure?
		 * Why not continue? If we break, set rc=-1? */
		if (get_lmb_size(lmb))
			break;

		/* Find the physical address for this lmb */
		tmp = strrchr(lmb->ofdt_path, '@');
		if (tmp == NULL) {
			say(DEBUG, "Could not determine physical address for "
			    "%s\n", lmb->ofdt_path);
			/* XXX No rc change? */
			break;
		}

		lmb->lmb_address = strtoull(tmp + 1, NULL, 16);

		/* find the associated sysfs memory blocks */
		rc = get_mem_scns(lmb);
		if (rc)
			break;
	}

	closedir(d);
	return rc;
}

/**
 * get_dynamic_reconfig_lmbs
 * @brief Retrieve lmbs from OF device tree located in the ibm,dynamic-memory
 * property.
 *
 * @param lmb_list pointer to lmb list head to populate
 * @returns 0 on success, !0 on failure
 */
int
get_dynamic_reconfig_lmbs(struct lmb_list_head *lmb_list)
{
	struct drconf_mem *drmem;
	uint64_t lmb_sz;
	int i, num_entries;
	int rc = 0;
	int found = 0;

	rc = get_property(DYNAMIC_RECONFIG_MEM, "ibm,lmb-size",
			  &lmb_sz, sizeof(lmb_sz));

	/* convert for LE systems */
	lmb_sz = be64toh(lmb_sz);

	if (rc) {
		say(DEBUG, "Could not retrieve drconf LMB size\n");
		return rc;
	}

	lmb_list->drconf_buf_sz = get_property_size(DYNAMIC_RECONFIG_MEM,
						   "ibm,dynamic-memory");
	lmb_list->drconf_buf = zalloc(lmb_list->drconf_buf_sz);
	if (lmb_list->drconf_buf == NULL) {
		say(DEBUG, "Could not allocate buffer to get dynamic "
		    "reconfigurable memory\n");
		return -1;
	}

	rc = get_property(DYNAMIC_RECONFIG_MEM, "ibm,dynamic-memory",
			  lmb_list->drconf_buf, lmb_list->drconf_buf_sz);
	if (rc) {
		say(DEBUG, "Could not retrieve dynamic reconfigurable memory "
		    "property\n");
		return -1;
	}

	/* The first integer of the buffer is the number of entries */
	num_entries = *(int *)lmb_list->drconf_buf;

	/* convert for LE systems */
	num_entries = be32toh(num_entries);

	/* Followed by the actual entries */
	drmem = (struct drconf_mem *)
				(lmb_list->drconf_buf + sizeof(num_entries));
	for (i = 0; i < num_entries; i++) {
		struct dr_node *lmb;

		for (lmb = lmb_list->lmbs; lmb; lmb = lmb->next) {
			if (lmb->drc_index == be32toh(drmem->drc_index))
				break;
		}

		if (lmb == NULL) {
			say(DEBUG, "Could not find LMB with drc-index of %x\n",
			    drmem->drc_index);
			rc = -1;
			break;
		}

		sprintf(lmb->ofdt_path, DYNAMIC_RECONFIG_MEM);
		lmb->lmb_size = lmb_sz;
		lmb->lmb_address = be64toh(drmem->address);
		lmb->lmb_aa_index = be32toh(drmem->assoc_index);

		if (be32toh(drmem->flags) & DRMEM_ASSIGNED) {
			found++;
			lmb->is_owned = 1;

			/* find the associated sysfs memory blocks */
			rc = get_mem_scns(lmb);
			if (rc)
				break;
		}

		drmem++; /* trust your compiler */
	}

	say(INFO, "Found %d LMBs currently allocated\n", found);
	return rc;
}

/**
 * shuffle_lmbs
 * @brief Randomly shuffle list of lmbs
 *
 * @param list pointer to lmb list to be randomly shuffled
 * @param length number of lmbs in the list
 *
 * @return list of shuffled lmbs
 */
struct dr_node *
shuffle_lmbs(struct dr_node *lmb_list, int length)
{
	struct dr_node **shuffled_lmbs, *lmb;
	int i, j;
	
	srand(time(NULL));

	shuffled_lmbs = zalloc(sizeof(*shuffled_lmbs) * length);

	for (i = 0, lmb = lmb_list; lmb; i++, lmb = lmb->next) {
		j = rand() % (i + 1);

		if (j == i) {
			shuffled_lmbs[i] = lmb;
		} else {
			shuffled_lmbs[i] = shuffled_lmbs[j];
			shuffled_lmbs[j] = lmb;
		}
	}

	for (i = 0; i < (length - 1); i++)
		shuffled_lmbs[i]->next = shuffled_lmbs[i + 1];

	shuffled_lmbs[length - 1]->next = NULL;

	lmb = shuffled_lmbs[0];
	free(shuffled_lmbs);

	return lmb;
}

/**
 * get_lmbs
 * @brief Build a list of all possible lmbs for the system
 *
 * @param sort LMB_NORMAL_SORT or LMB_REVERSE_SORT to control sort order
 *
 * @return list of lmbs, NULL on failure
 */
struct lmb_list_head *
get_lmbs(unsigned int sort)
{
	struct dr_connector *drc_list, *drc;
	struct lmb_list_head *lmb_list = NULL;
	struct dr_node *lmb, *last = NULL;
	struct stat sbuf;
	char buf[DR_STR_MAX];
	int rc = 0;
	int found = 0;

	drc_list = get_drc_info(OFDT_BASE);
	if (drc_list == NULL) {
		report_unknown_error(__FILE__, __LINE__);
		return NULL;
	}

	lmb_list = zalloc(sizeof(*lmb_list));
	if (lmb_list == NULL) {
		say(DEBUG, "Could not allocate LMB list head\n");
		return NULL;
	}

	/* For memory dlpar, we need a list of all posiible memory nodes
	 * for the system, initalize those here.
	 */
	for (drc = drc_list; drc; drc = drc->next) {
		if (strncmp(drc->name, "LMB", 3))
			continue;

		lmb = alloc_dr_node(drc, MEM_DEV, NULL);
		if (lmb == NULL) {
			free_lmbs(lmb_list);
			return NULL;
		}

		if (sort == LMB_REVERSE_SORT) {
			if (last)
				lmb->next = last;
			lmb_list->lmbs = lmb;
			last = lmb;
		} else {
			if (last)
				last->next = lmb;
			else
				lmb_list->lmbs = lmb;
			last = lmb;
		}
		found++;
	}

	if (sort == LMB_RANDOM_SORT)
		lmb_list->lmbs = shuffle_lmbs(lmb_list->lmbs, found);

	say(INFO, "Maximum of %d LMBs\n", found);

	rc = get_str_attribute("/sys/devices/system/memory",
			       "/block_size_bytes", &buf, DR_STR_MAX);
	if (rc) {
		say(DEBUG,
		    "Could not determine block size bytes for memory.\n");
		free_lmbs(lmb_list);
		return NULL;
	}

	block_sz_bytes = strtoul(buf, NULL, 16);

	/* We also need to know which lmbs are already allocated to
	 * the system and their corresponding memory sections as defined
	 * by sysfs.  Walk the device tree and update the appropriate
	 * lmb entries (and their memory sections) as we find their device
	 * tree entries.
	 */
	if (stat(DYNAMIC_RECONFIG_MEM, &sbuf))
		rc = get_mem_node_lmbs(lmb_list);
	else {
		/* A small hack to here to allow memory add to work in
		 * certain kernels.  Due to a bug in the kernel (see comment
		 * in acquire_lmb()) we need to get lmb info from both places.
		 * For a good kernel, the get_mem_node_lmbs routine will not
		 * update the lmb_list.
		 */
		rc = get_dynamic_reconfig_lmbs(lmb_list);
		if (! rc)
			rc = get_mem_node_lmbs(lmb_list);
	}

	if (rc) {
		free_lmbs(lmb_list);
		lmb_list = NULL;
	}

	return lmb_list;
}

/**
 * get_available_lmb
 *
 * Find the first lmb which does not correspond to a lmb
 * already owned by the partition and is available, or the lmb
 * matching the one specified by the user.
 *
 * @param start_lmb first lmbs to be searched for an available lmb
 * @returns pointer to avaiable lmb on success, NULL otherwise
 */
static struct dr_node *
get_available_lmb(struct options *opts, struct dr_node *start_lmb)
{
	uint32_t drc_index;
	struct dr_node *lmb;
	struct dr_node *usable_lmb = NULL;
	int balloon_active = ams_balloon_active();

	for (lmb = start_lmb; lmb; lmb = lmb->next) {
		int rc;

		if (opts->usr_drc_name) {
			drc_index = strtoul(opts->usr_drc_name, NULL, 0);

			if ((strcmp(lmb->drc_name, opts->usr_drc_name))
			    && (lmb->drc_index != drc_index))
				continue;
		} else if (opts->usr_drc_index) {
			if (lmb->drc_index != opts->usr_drc_index)
				continue;
		}

		if (lmb->unusable)
			continue;

		switch (opts->action) {
		    case ADD:
			if (lmb->is_owned)
				continue;

			rc = dr_entity_sense(lmb->drc_index);
			if (rc != STATE_UNUSABLE)
				continue;

			usable_lmb = lmb;
			break;

		    case REMOVE:
			/* removable is ignored if AMS ballooning is active. */
			if ((!balloon_active && !lmb->is_removable) ||
			    (!lmb->is_owned))
				continue;

			usable_lmb = lmb;
			break;
		}

		/* Found an available lmb */
		break;
	}

	if (usable_lmb)
		say(DEBUG, "Found available LMB, %s, drc index 0x%x\n",
		    usable_lmb->drc_name, usable_lmb->drc_index);

	return usable_lmb;
}

static void update_drconf_affinity(struct dr_node *lmb,
				   struct drconf_mem *drmem)
{
	struct of_node *node;
	struct of_property *prop;
	uint32_t assoc_prop_sz;
	uint32_t *assoc_prop;
	uint32_t assoc_entries;
	uint32_t assoc_entry_sz;
	uint32_t *prop_val;
	int i;

	/* find the ibm,associativity property */
	node = lmb->lmb_of_node;
	for (prop = node->properties; prop; prop = prop->next) {
		if (!strcmp(prop->name, "ibm,associativity"))
			break;
	}

	if (!prop)
		return;

	/* find the associtivity index atrrays */
	assoc_prop_sz = get_property_size(DYNAMIC_RECONFIG_MEM,
					  "ibm,associativity-lookup-arrays");
	assoc_prop = zalloc(assoc_prop_sz);
	if (!assoc_prop)
		return;

	get_property(DYNAMIC_RECONFIG_MEM, "ibm,associativity-lookup-arrays",
		     assoc_prop, assoc_prop_sz);

	assoc_entries = be32toh(assoc_prop[0]);
	assoc_entry_sz = be32toh(assoc_prop[1]);

	prop_val = (uint32_t *)prop->value;
	for (i = 0; i < assoc_entries; i++) {
		if (memcmp(&assoc_prop[(i * assoc_entry_sz) + 2], &prop_val[1],
			   assoc_entry_sz * sizeof(uint32_t)))
			continue;

		/* found it */
		drmem->assoc_index = htobe32(i);
		break;
	}

	free(assoc_prop);
	return;
}
	
/**
 * update_drconf_node
 * @brief update the ibm,dynamic-memory property for added/removed memory
 *
 * @param lmb pointer to updated lmb
 * @param lmb_list pointer to all lmbs
 * @param action ADD or REMOVE
 * @returns 0 on success, !0 on failure
 */
static int
update_drconf_node(struct dr_node *lmb, struct lmb_list_head *lmb_list,
		   int action)
{
	char *prop_buf;
	size_t prop_buf_sz;
	char *tmp;
	struct drconf_mem *drmem;
	uint phandle;
	int i, entries;
	int rc;

	/* The first int of the buffer is the number of entries */
	entries = *(int *)lmb_list->drconf_buf;

	/* convert for LE systems */
	entries = be32toh(entries);

	drmem = (struct drconf_mem *)(lmb_list->drconf_buf + sizeof(entries));

	for (i = 0; i < entries; i++) {

		if (be32toh(drmem->drc_index) != lmb->drc_index) {
			drmem++;
			continue;
		}

		if (action == ADD) {
			drmem->flags |= be32toh(DRMEM_ASSIGNED);
			update_drconf_affinity(lmb, drmem);
		} else {
			drmem->flags &= be32toh(~DRMEM_ASSIGNED);
		}

		break;
	}

	/* Now create the buffer we pass to the kernel to have this
	 * property updated.  This buffer has the format
	 * "update_property <phandle> ibm,dynamic-memory <prop_len> <prop> \
	 * [strlen("remove") | strlen("add")] <drc_index> "
	 *
	 * The simple collapsing of all strings, spaces, and ints makes this
	 * a length of 61 + the size of the drconf property, round the
	 * calculation to 128 + <property_size> to ensure the buffer is
	 * always big enough.
	 */
	prop_buf_sz = 128 + lmb_list->drconf_buf_sz;
	prop_buf = zalloc(prop_buf_sz);
	if (prop_buf == NULL)
		return -1;

	rc = get_phandle(lmb->ofdt_path, &phandle);

	if (rc) {
		say(DEBUG, "Failed to get phandle for %s under %s. (rc=%d)\n", 
				lmb->drc_name, lmb->ofdt_path, rc);
		return rc;
	}

	memset(prop_buf, 0, prop_buf_sz);
	tmp = prop_buf;
	tmp += sprintf(tmp, "update_property 0x%x ibm,dynamic-memory %d ",
		       phandle, lmb_list->drconf_buf_sz);

	memcpy(tmp, lmb_list->drconf_buf, lmb_list->drconf_buf_sz);
	tmp += lmb_list->drconf_buf_sz;

	tmp += sprintf(tmp, " %s %d ", (action == ADD ? "add" : "remove"),
		       sizeof(lmb->lmb_address));
	memcpy(tmp, &lmb->lmb_address, sizeof(lmb->lmb_address));
	tmp += sizeof(lmb->lmb_address);

	rc = update_property(prop_buf, (tmp - prop_buf));

	free(prop_buf);
	return rc;
}

/**
 * remove_device_tree_lmb
 * @brief Update the device tree for the lmb being removed.
 *
 * @param lmb memory block to be removed from the device tree
 * @param lmb_list list of all lmbs
 * @returns 0 on success, !o otherwise
 */
static int
remove_device_tree_lmb(struct dr_node *lmb, struct lmb_list_head *lmb_list)
{
	if (lmb_list->drconf_buf)
		return update_drconf_node(lmb, lmb_list, REMOVE);

	return remove_device_tree_nodes(lmb->ofdt_path);
}

/**
 * add_device_tree_lmb
 * @brief Update the device tree for the lmb being added..
 *
 * @param lmb lmb to acquire
 * @param lmb_list list of all lmbs
 * @returns 0 on success, !0 otherwise
 */
static int
add_device_tree_lmb(struct dr_node *lmb, struct lmb_list_head *lmb_list)
{
        int rc;

	lmb->lmb_of_node = configure_connector(lmb->drc_index);
	if (lmb->lmb_of_node == NULL) {
		release_drc(lmb->drc_index, MEM_DEV);
		return -1;
	}
	
	if (lmb_list->drconf_buf) {
		errno = 0;
		rc = update_drconf_node(lmb, lmb_list, ADD);
		if (errno == ENODEV) {
			/* Due to bug in pre 2.6.27 kernels, updating the
			 * property in the device tree fails when the phandle
			 * is processed as a signed int instead of unsigned
			 * In this case we provide this little hack to allow
			 * memory add to work on these kernels.
			 */
			say(DEBUG, "Assuming older kernel, trying to add "
			    "node\n");

			sprintf(lmb->ofdt_path, "%s/%s", OFDT_BASE,
				lmb->lmb_of_node->name);
			rc = add_device_tree_nodes(OFDT_BASE, lmb->lmb_of_node);
		} else {
			sprintf(lmb->ofdt_path, "%s/%s", OFDT_BASE,
				"/ibm,dynamic-reconfiguration-memory");
		}
	} else {
		/* Add the new nodes to the device tree */
		sprintf(lmb->ofdt_path, "%s/%s", OFDT_BASE,
			lmb->lmb_of_node->name);
		rc = add_device_tree_nodes(OFDT_BASE, lmb->lmb_of_node);
	}

	if (rc)
		return rc;

	if (! lmb_list->drconf_buf) {
		/* Find the physical address for this lmb.  This is only
		 * needed for non-drconf memory.  The address for drconf
		 * lmbs is correctly initialized when building the lmb list
		 */
		char *tmp = strrchr(lmb->ofdt_path, '@');
		if (tmp == NULL) {
			say(DEBUG, "Could not determine physical address for "
			    "%s\n", lmb->ofdt_path);
			remove_device_tree_nodes(lmb->ofdt_path);
			return -1;
		}

		lmb->lmb_address = strtoull(tmp + 1, NULL, 16);

		rc = get_lmb_size(lmb);
		if (rc) {
			remove_device_tree_nodes(lmb->ofdt_path);
			return rc;
		}
	}

	rc = get_mem_scns(lmb);
	if (rc) 
		remove_device_tree_lmb(lmb, lmb_list);

        return rc;
}

/**
 * get_mem_scn_state
 * @brief Find the state of the specified memory section
 *
 * @param mem_scn memory section to validate
 * @return OFFLINE, ONLINE, or -1 for failures
 */
static int
get_mem_scn_state(struct mem_scn *mem_scn)
{
	char path[DR_PATH_MAX];
	char state[8];
	int file;
	int rc;

	memset(path, 0, DR_PATH_MAX);
	sprintf(path, "%s/state", mem_scn->sysfs_path);

	file = open(path, O_RDONLY);
	if (file <= 0) {
		say(DEBUG, "Could not open %s to validate its state.\n%s\n",
		    path, strerror(errno));
		return -1;
	}

	memset(state, 0, 8);
	rc = read(file, state, 8);
	close(file);

	if (!strncmp(state, "online", 6))
		return ONLINE;

	if (! strncmp(state, "offline", 7))
		return OFFLINE;

	return rc;
}

/**
 * set_mem_scn_state
 * @brief Marks a memory section as online or offline
 *
 * @param mem_scn memory section to update
 * @param state state to mark memory section
 * @return 0 on success, !0 otherwise
 */
static int
set_mem_scn_state(struct mem_scn *mem_scn, int state)
{
	int file;
	char path[DR_PATH_MAX];
	int rc = 0;
	int unused;
	time_t t;
	char tbuf[128];

	time(&t);
	strftime(tbuf, 128, "%T", localtime(&t));
	memset(path, 0, DR_PATH_MAX);
	sprintf(path, "%s/state", mem_scn->sysfs_path);
	say(DEBUG, "%s Marking %s %s\n", tbuf, mem_scn->sysfs_path,
			state_strs[state]);

	file = open(path, O_WRONLY);
	if (file <= 0) {
		say(DEBUG, "Could not open %s to %s memory.\n\t%s\n",
		    path, state_strs[state], strerror(errno));
		close(file);
		return -1;
	}

	unused = write(file, state_strs[state], strlen(state_strs[state]));
	close(file);

	if (get_mem_scn_state(mem_scn) != state) {
		time(&t);
		strftime(tbuf, 128, "%T", localtime(&t));
		say(DEBUG, "%s Could not %s %s.\n", tbuf, state_strs[state],
		    mem_scn->sysfs_path);
		rc = EAGAIN;
	} else {
		time(&t);
		strftime(tbuf, 128, "%T", localtime(&t));
		say(DEBUG, "%s Completed marking %s %s.\n", tbuf,
				mem_scn->sysfs_path, state_strs[state]);
	}

	return rc;
}

/**
 * probe_lmb
 * @brief Probe all of the memory sections of the lmb
 *
 * @param lmb pointer to lmb to probe
 * @returns 0 on success,!0 otherwise
 */
static int
probe_lmb(struct dr_node *lmb)
{
	struct mem_scn *scn;
	int probe_file;
	int rc = 0;

	probe_file = open(MEM_PROBE_FILE, O_WRONLY);
	if (probe_file <= 0) {
		say(DEBUG, "Could not open %s to probe memory\n",
		    MEM_PROBE_FILE);
		return errno;
	}

	for (scn = lmb->lmb_mem_scns; scn; scn = scn->next) {
		char addr[DR_STR_MAX];

		memset(addr, 0, DR_STR_MAX);
		sprintf(addr, "0x%"PRIx64, scn->phys_addr);

		say(DEBUG, "Probing memory address 0x%llx\n", scn->phys_addr);
		rc = write(probe_file, addr, strlen(addr));
		if (rc == -1) {
			say(DEBUG, "Probe failed:\n%s\n", strerror(errno));
			return rc;
		}
	}

	close(probe_file);
	return 0;
}

/**
 * set_lmb_state
 *
 * @param lmb lmb to set the state for
 * @param state 1 = online, 0 = offline
 * @returns 0 on success, !0 otherwise
 */
static int
set_lmb_state(struct dr_node *lmb, int state)
{
	struct mem_scn *scn;
	int rc = 0;
	struct stat sbuf;

	say(INFO, "Attempting to %s %s.\n", state_strs[state], lmb->drc_name);

	if (state == ONLINE) {
		rc = probe_lmb(lmb);
		if (rc)
			return rc;
	}

	for (scn = lmb->lmb_mem_scns; scn; scn = scn->next) {
		if (stat(scn->sysfs_path, &sbuf))
			continue;

		rc = set_mem_scn_state(scn, state);
		if (rc)
			break;
	}

	if (rc) {
		/* Revert state of any memory sections of this lmb to their
		 * original state
		 */
		int new_state = (state == OFFLINE) ? ONLINE : OFFLINE;

		for (scn = lmb->lmb_mem_scns; scn; scn = scn->next) {
			if (stat(scn->sysfs_path, &sbuf))
				continue;

			if (get_mem_scn_state(scn) == state)
				set_mem_scn_state(scn, new_state);
		}
	}

	if (rc) {
		if (rc == EAGAIN)
			say(INFO, "Could not %s %s at this time.\n",
				      state_strs[state], lmb->drc_name);
		else
			report_unknown_error(__FILE__, __LINE__);

	} else
		say(INFO, "%s is %s.\n", lmb->drc_name, state_strs[state]);

	return rc;
}

/**
 * add_lmbs
 *
 * Attempt to acquire and online the given number of LMBs.
 * This function calls itself recursively to simplify recovery
 * actions in case of an error.  This is intended only for the case
 * where the user does not specify a drc-name.
 *
 * @param nr_lmbs number of lmbs to add
 * @param lmb_list list of lmbs on the partition
 * @returns 0 on success, !0 otherwise
 */
static int
add_lmbs(struct options *opts, struct lmb_list_head *lmb_list)
{
	int rc = 0;
	struct dr_node *lmb_head = lmb_list->lmbs;
	struct dr_node *lmb;

	lmb_list->lmbs_modified = 0;
	while (lmb_list->lmbs_modified < (int)opts->quantity) {
		if (drmgr_timed_out())
			break;

		lmb = get_available_lmb(opts, lmb_head);
		if (lmb == NULL)
			return -1;

		/* Iterate only over the remaining LMBs */
		lmb_head = lmb->next;

		rc = acquire_drc(lmb->drc_index);
		if (rc) {
			report_unknown_error(__FILE__, __LINE__);
			lmb->unusable = 1;
			continue;
		}

		rc = add_device_tree_lmb(lmb, lmb_list);
		if (rc) {
			report_unknown_error(__FILE__, __LINE__);
			release_drc(lmb->drc_index, MEM_DEV);
			lmb->unusable = 1;
			continue;
		}

		rc = set_lmb_state(lmb, ONLINE);
		if (rc) {
			report_unknown_error(__FILE__, __LINE__);
			remove_device_tree_lmb(lmb, lmb_list);
			release_drc(lmb->drc_index, MEM_DEV);
			lmb->unusable = 1;
			continue;
		}

		lmb_list->lmbs_modified++;
	}

	return rc;
}

/**
 * mem_add
 * @brief Add memory to the partition
 *
 * @param opts user options
 * @returns 0 on success, !0 otherwise
 */
static int
mem_add(struct options *opts)
{
	struct lmb_list_head *lmb_list;
	int rc;

	lmb_list = get_lmbs(LMB_NORMAL_SORT);
	if (lmb_list == NULL) {
		say(ERROR, "Could not gather LMB (logical memory block "
				"information.\n");
		return -1;
	}

	say(DEBUG, "Attempting to add %d LMBs\n", opts->quantity);
	rc = add_lmbs(opts, lmb_list);

	say(DEBUG, "Added %d of %d requested LMB(s)\n", lmb_list->lmbs_modified,
	    opts->quantity);
	printf("DR_TOTAL_RESOURCES=%d\n", lmb_list->lmbs_modified);

	free_lmbs(lmb_list);
	return rc;
}

/**
 * remove_lmbs
 *
 * @param nr_lmbs
 * @param opts
 * @param lmb_list
 * @return 0 on success, !0 otherwise
 */
static int
remove_lmbs(struct options *opts, struct lmb_list_head *lmb_list)
{
	struct dr_node *lmb_head = lmb_list->lmbs;
	struct dr_node *lmb;
	int rc;

	while (lmb_list->lmbs_modified < (int)opts->quantity) {
		if (drmgr_timed_out())
			break;

		lmb = get_available_lmb(opts, lmb_head);
		if (!lmb)
			return -1;

		/* Iterate only over the remaining LMBs */
		lmb_head = lmb->next;

		rc = set_lmb_state(lmb, OFFLINE);
		if (rc) {
			lmb->unusable = 1;
			continue;
		}

		rc = remove_device_tree_lmb(lmb, lmb_list);
		if (rc) {
			report_unknown_error(__FILE__, __LINE__);
			set_lmb_state(lmb, ONLINE);
			lmb->unusable = 1;
			continue;
		}

		while (lmb->lmb_mem_scns) {
			struct mem_scn *scn = lmb->lmb_mem_scns;
			lmb->lmb_mem_scns = scn->next;
			free(scn);
		}

		rc = release_drc(lmb->drc_index, 0);
		if (rc) {
			report_unknown_error(__FILE__, __LINE__);
			add_device_tree_lmb(lmb, lmb_list);
			set_lmb_state(lmb, ONLINE);
			lmb->unusable = 1;
			continue;
		}

		lmb->is_removable = 0;
		lmb_list->lmbs_modified++;
	}

	return 0;
}

/**
 * mem_remove
 *
 * @param opts
 * @return 0 on success, !0 otherwise
 */
static int
mem_remove(struct options *opts)
{
	struct lmb_list_head *lmb_list;
	struct dr_node *lmb;
	unsigned int removable = 0;
	unsigned int requested = opts->quantity;
	int rc = 0;

	lmb_list = get_lmbs(LMB_RANDOM_SORT);
	if (lmb_list == NULL) {
		say(ERROR, "Could not gather LMB (logical memory block "
				"information.\n");
		return -1;
	}

	/* Can not know which lmbs are removable by the is_removable field
	 * if AMS ballooning is active.
	 */
	if (!ams_balloon_active()) {
		/* Make sure we have enough removable memory to fulfill
		 * this request
		 */
		for (lmb = lmb_list->lmbs; lmb; lmb = lmb->next) {
			if (lmb->is_removable)
				removable++;
		}

		if (removable == 0) {
			say(ERROR, "There is not enough removable memory "
			    "available to fulfill the request.\n");
			rc = -1;
		}

		if (removable < opts->quantity) {
			say(INFO, "Only %u LMBs are currently candidates "
					"for removal.\n", removable);
			opts->quantity = removable;
		}
	}

	if (!rc) {
		say(DEBUG, "Attempting removal of %d LMBs\n", opts->quantity);
		rc = remove_lmbs(opts, lmb_list);
	}

	say(ERROR, "Removed %d of %d requested LMB(s)\n",
	    lmb_list->lmbs_modified, requested);
	if (lmb_list->lmbs_modified < requested)
		say(ERROR, "Unable to hotplug remove the remaining %d LMB(s)\n",
		    requested - lmb_list->lmbs_modified);
	printf("DR_TOTAL_RESOURCES=%d\n", lmb_list->lmbs_modified);

	free_lmbs(lmb_list);
	return rc;
}

/* These two defines are taken from drivers/net/ehea/ehea.h in the kernel.
 * Unfortunately they do not appear in any header we can include so we
 * have to define the values here so we can check ehea capabilities.
 */
#define MEM_ADD_ATTR	0x0000000000000002
#define MEM_RM_ATTR	0x0000000000000004

/**
 * ehea_compatable
 * @brief Determine if ehea is loaded and if it can handle memory dlpar
 *
 * In order to properly support memory DLPAR on systems with HEAdevices,
 * we have to ensure that the ehea module is either not loaded or we are
 * using a version that can handle memory dlpar operations.  Otherwise bad
 * stuff happens.
 *
 * This uses system() to run lsmod and grep to check for the presence
 * of the ehea module.  If it is present we check its capabilities file
 * to determine if it can handle memory dlpar.
 *
 * @param action memory add/remove action that is to be checked for
 * @return 1 if we are ehea compatable, 0 otherwise
 */
static int
ehea_compatable(int action)
{
	FILE *fp;
	uint64_t flags = 0;
	int rc;

	rc = system("/sbin/lsmod | grep ehea >/dev/null 2>&1");
	if (WEXITSTATUS(rc) != 0) {
		/* The ehea module is not loaded, everything is good */
		return 1;
	}

	/* The module is loaded, now we need to see if it
	 * can handle memory dlpar operations.
	 */
	fp = fopen("/sys/bus/ibmebus/drivers/ehea/capabilities", "r");
	if (fp == NULL) {
		/* File doesn't exist, memory dlpar operations are not
		 * supported by this version of the ehea driver.
		 */
		say(INFO, "The eHEA module for this system does not support "
		    "memory DLPAR operations.\n");
		return 0;
	}

	rc = fscanf(fp, "%"PRIu64, &flags);
	fclose(fp);

	/* Assume memory dlpar is not supported */
	rc = 0;

	if ((action == ADD) && (flags & MEM_ADD_ATTR))
		rc = 1;

	if ((action == REMOVE) && (flags & MEM_RM_ATTR))
		rc = 1;

	if (!rc)
		say(INFO, "The eHEA modules loaded on this system does not "
		    "support memory DLPAR %s operations.\n",
		    (action == ADD) ? "add" : "remove");
	return rc;
}

int
valid_mem_options(struct options *opts)
{
	/* default to a quantity of 1 */
	if (opts->quantity == 0)
		opts->quantity = 1;

	if ((opts->action != ADD) && (opts->action != REMOVE)) {
		say(ERROR, "The '-r' or '-a' option must be specified for "
		    "memory operations\n");
		return -1;
	}

	/* The -s option can specify a drc name or drc index */
	if (opts->usr_drc_name && !strncmp(opts->usr_drc_name, "0x", 2)) {
		opts->usr_drc_index = strtoul(opts->usr_drc_name, NULL, 16);
		opts->usr_drc_name = NULL;
	}

	return 0;
}

int
drslot_chrp_mem(struct options *opts)
{
	int rc = -1;

	if (opts->p_option) {
		/* This is a entitlement or weight change */
		return update_sysparm(opts);
	}

	if (! mem_dlpar_capable() || ! ehea_compatable(opts->action)) {
		say(ERROR, "DLPAR memory operations are not supported on"
		    "this kernel.");
		return -1;
	}

	/* The recursive nature of the routines that add/remove lmbs
	 * require that the quantity be non-zero.
	 */
	if (opts->usr_drc_name)
		opts->quantity = 1;

	switch (opts->action) {
	    case ADD:
		rc = mem_add(opts);
		break;

	    case REMOVE:
		rc = mem_remove(opts);
		break;
	}

	return rc;
}
