/**
 * @file common_cpu.c
 * @brief Common routines for cpu data
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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <librtas.h>
#include "dr.h"
#include "drcpu.h"
#include "ofdt.h"

/* format strings for easy access */
#define DR_THREAD_DIR_PATH "/sys/devices/system/cpu/cpu%d"
#define DR_THREAD_ONLINE_PATH "/sys/devices/system/cpu/cpu%d/online"
#define DR_THREAD_PHYSID_PATH "/sys/devices/system/cpu/cpu%d/physical_id"

#define DR_CPU_INTSERVERS_PATH \
        "/proc/device-tree/cpus/%s/ibm,ppc-interrupt-server#s"

/**
 * free_thread_info
 * @brief free the thread list and any associated allocated memory
 *
 * @param thread_list list of threads to free
 */
static inline void
free_thread_info(struct thread *thread_list)
{
	struct thread *thread;

	while (thread_list) {
		thread = thread_list;
		thread_list = thread->next;
		free(thread);
	}
}

/**
 * get_cpu_threads
 * Associate a thread to the cpu it belongs to.
 *
 * @param all_cpus list of all cpus
 * @param thread thread to associate.
 */
static void
get_cpu_threads(struct dr_node *cpu, struct thread *all_threads)
{
	struct thread *thread;
	struct thread *last = NULL;
	int i;

	for (thread = all_threads; thread; thread = thread->next) {
		for (i = 0; i < cpu->cpu_nthreads; i++) {
			if (cpu->cpu_intserv_nums[i] != thread->phys_id)
				continue;

			/* Special case for older kernels where the default
			 * physical id of a thread was 0, which is also a
			 * valid physical id.
			 */
			if (thread->phys_id == 0) {
				char *cpuid = strrchr(thread->path, 'c');
				if (cpuid[3] != '0')
					continue;
			}

			if (last)
				last->sibling = thread;
			else
				cpu->cpu_threads = thread;

			last = thread;
			thread->cpu = cpu;
		}
	}
}

/**
 * init_thread_info
 * @brief Initialize thread data
 *
 * Initialize global thread or "logical cpu" data. The physical cpu data
 * initialization must have been completed before this is called.
 *
 * @returns pointer to thread_info on success, NULL otherwise
 */
static int
init_thread_info(struct dr_info *dr_info)
{
	struct thread *thread = NULL;
	struct thread *thread_list = NULL;
	struct thread *last = NULL;
	int rc, i = 0;
	struct stat s;
	char path[DR_PATH_MAX];
	int nr_threads, thread_cnt = 0;

	if (stat("/sys/devices/system/cpu", &s)) {
		say(ERROR, "Cannot gather CPU thread information,\n"
		    "stat(\"/sys/devices/system/cpu\"): %s\n",
		    strerror(errno));
		return -1;
	}

	nr_threads = s.st_nlink - 2;

	sprintf(path, DR_THREAD_DIR_PATH, i);
	for (i = 0; 0 == stat(path, &s); i++) {
		thread = zalloc(sizeof(*thread));

		thread->id = i;
		snprintf(thread->path, DR_PATH_MAX, "%s", path);

		rc = get_int_attribute(thread->path, "physical_id",
				       &thread->phys_id,
				       sizeof(thread->phys_id));
		if (rc) {
			say(ERROR, "Could not get \"physical_id\" of thread "
			    "%s\n", thread->path);
			free(thread);
			return -1;
		}

		if (thread_list)
			last->next = thread;
		else
			thread_list = thread;

		last = thread;

		sprintf(path, DR_THREAD_DIR_PATH, i + 1);
		thread_cnt++;
	}

	say(EXTRA_DEBUG, "Expecting %d threads...found %d.\n", nr_threads,
	    thread_cnt);
	dr_info->all_threads = thread_list;
	return 0;
}

static int
cpu_index_to_path(struct dr_node *cpu)
{
	DIR *d;
	struct dirent *de;
	int found = 0;
	int rc = -1;
	char path[DR_PATH_MAX];

	d = opendir(CPU_OFDT_BASE);
	if (d == NULL) {
		say(ERROR, "Could not open %s: %s\n", CPU_OFDT_BASE,
		    strerror(errno));
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		uint32_t my_drc_index;

		if ((de->d_type != DT_DIR) || is_dot_dir(de->d_name))
			continue;

		if (strncmp(de->d_name, "PowerPC", 7))
			continue;

		sprintf(path, "%s/%s", CPU_OFDT_BASE, de->d_name);

		rc = get_my_drc_index(path, &my_drc_index);
		if (rc) {
			/* This is an error, but continue searching since
			 * this may not be the drc index we are looking for
			 */
			say(DEBUG, "Could not retrieve drc_index for %s\n",
			    path);
			continue;
		}

		if (my_drc_index == cpu->drc_index) {
			found = 1;
			break;
		}
	}

	closedir(d);

	if (found) {
		snprintf(cpu->ofdt_path, DR_PATH_MAX, "%s", path);
		rc = 0;
	}

	return rc;
}

static int
update_cpu_node(struct dr_node *cpu, const char *path, struct dr_info *dr_info)
{
	struct stat sb;
	char intserv_path[DR_PATH_MAX];
	int rc, i;

	if (path) {
		snprintf(cpu->ofdt_path, DR_PATH_MAX, "%s", path);
	} else {
		rc = cpu_index_to_path(cpu);
		if (rc) {
			say(ERROR, "Could not find ofdt path for drc index "
			    "%s\n", cpu->drc_index);
			return rc;
		}
	}

	/* Skip past CPU_OFDT_BASE plus the '/' */
	cpu->name = cpu->ofdt_path + strlen(CPU_OFDT_BASE) + 1;
	memset(&cpu->cpu_intserv_nums, -1, sizeof(cpu->cpu_intserv_nums));
	rc = get_property(cpu->ofdt_path, "ibm,ppc-interrupt-server#s",
			  &cpu->cpu_intserv_nums,
			  sizeof(cpu->cpu_intserv_nums));

	/* Making sure the intserv_nums are in correct endian format */
        for (i = 0; i < MAX_CPU_INTSERV_NUMS; i++)
                cpu->cpu_intserv_nums[i] = be32toh(cpu->cpu_intserv_nums[i]);

	if (rc) {
		say(ERROR, "Could not retrieve ibm,ppc-interrupt-server#s "
		    "property for %s\n", cpu->name);
		return -1;
	}

	/* The number of threads is the number of 32-bit ints in the
	 * cpu's ibm,ppc-interrupt-server#s property.
	 */
	sprintf(intserv_path,
		"/proc/device-tree/cpus/%s/ibm,ppc-interrupt-server#s",
		strstr(cpu->name, "PowerPC"));

	if (stat(intserv_path, &sb))
		/* Assume no SMT */
		cpu->cpu_nthreads = 1;
	else
		cpu->cpu_nthreads = sb.st_size / 4;

	rc = get_ofdt_uint_property(cpu->ofdt_path, "reg", &cpu->cpu_reg);
	if (rc) {
		say(ERROR, "Could not retrieve reg property for %s\n",
		    cpu->name);
		return -1;
	}

	/* l2-cache may not exist */
	cpu->cpu_l2cache = 0xffffffff;
	get_ofdt_uint_property(cpu->ofdt_path, "l2-cache", &cpu->cpu_l2cache);

	get_cpu_threads(cpu, dr_info->all_threads);
	cpu->is_owned = 1;
	return 0;
}

/**
 * init_cpu_info
 * @brief Initialize cpu information
 *
 * @returns pointer to cpu_info on success, NULL otherwise
 */
static int
init_cpu_info(struct dr_info *dr_info)
{
	struct dr_connector *drc_list, *drc;
	struct dr_node *cpu, *cpu_list = NULL;
	DIR *d;
	struct dirent *de;
	int rc = 0;

	drc_list = get_drc_info(CPU_OFDT_BASE);
	if (drc_list == NULL) {
		say(ERROR, "Could not get drc information for %s\n",
		    CPU_OFDT_BASE);
		return -1;
	}

	/* For cpu dlpar, we need a list of all possible cpus on the system */
	for (drc = drc_list; drc; drc = drc->next) {
		cpu = alloc_dr_node(drc, CPU_DEV, NULL);
		if (cpu == NULL) {
			say(ERROR, "Could not allocate CPU node structure: "
			    "%s\n", strerror(errno));
			free_node(cpu_list);
			return -1;
		}

		cpu->next = cpu_list;
		cpu_list = cpu;
	}

	d = opendir(CPU_OFDT_BASE);
	if (d == NULL) {
		say(ERROR, "Could not open %s: %s\n", CPU_OFDT_BASE,
		    strerror(errno));
		free_node(cpu_list);
		return -1;
	}

	while ((de = readdir(d)) != NULL) {
		char path[DR_PATH_MAX];

		if ((de->d_type != DT_DIR) || is_dot_dir(de->d_name))
			continue;

		if (! strncmp(de->d_name, "PowerPC", 7)) {
			uint32_t my_drc_index;

			memset(path, 0, 1024);
			sprintf(path, "%s/%s", CPU_OFDT_BASE, de->d_name);

			rc = get_my_drc_index(path, &my_drc_index);
			if (rc) {
				say(ERROR, "Could not retrieve drc index for "
				    "%s\n", path);
				break;
			}

			for (cpu = cpu_list; cpu; cpu = cpu->next) {
				if (cpu->drc_index == my_drc_index)
					break;
			}

			if (cpu == NULL) {
				say(ERROR, "Could not find cpu with drc index "
				    "%x\n", my_drc_index);
				rc = -1;
				break;
			}

			rc = update_cpu_node(cpu, path, dr_info);
			if (rc)
				break;

			say(EXTRA_DEBUG, "Found cpu %s\n", cpu->name);
		}
	}

	closedir(d);

	if (rc)
		free_node(cpu_list);
	else
		dr_info->all_cpus = cpu_list;

	return rc;
}

/**
 * cpu_get_dependent_cache
 * @brief Return the cache whose ibm,phandle matches the given cpu's
 *        l2-cache property.
 *
 * @param cpu
 * @returns pointer to cache_info on success, NULL otherwise
 */
struct cache_info *
cpu_get_dependent_cache(struct dr_node *cpu, struct dr_info *dr_info)
{
	struct cache_info * cache;

	for (cache = dr_info->all_caches; cache != NULL; cache = cache->next) {
		if (cache->removed)
			continue;

		if (cache->phandle == cpu->cpu_l2cache) {
			say(EXTRA_DEBUG, "found cache %s for cpu %s\n",
			    cache->name, cpu->name);
			return cache;
		}
	}

	return NULL;
}

/**
 * cache_get_dependent_cache
 * @brief Return the cache whose ibm,phandle matches the given cache's
 *       l2-cache property.
 *
 * @param cache
 * @returns pointer to cache_info on success, NULL otherwise
 */
struct cache_info *
cache_get_dependent_cache(struct cache_info *cache, struct dr_info *dr_info)
{
	struct cache_info *c;

	for (c = dr_info->all_caches; c != NULL; c = c->next) {
		if (cache->removed)
			continue;

		if (cache->phandle == cache->l2cache) {
			say(EXTRA_DEBUG, "found cache %s for cache %s\n",
			    c->name, cache->name);
			return c;
		}
	}

	return NULL;
}

/**
 * cache_remove_devnode
 * @brief Remove the given cache device node from the kernel's device tree.
 *        Also remove the cache from the global cache list.
 *
 * @param cache
 * @returns
 */
static int
cache_remove_devnode(struct cache_info *cache)
{
	int rc;

	rc = remove_device_tree_nodes(cache->path);
	if (!rc)
		cache->removed = 1;

	return rc;
}

/**
 * cache_get_use_count
 * @brief Calculate a cache's use count
 *
 * @param cache
 * @returns
 */
static int
cache_get_use_count(struct cache_info *cache, struct dr_info *dr_info)
{
	int count = 0;
	struct dr_node *cpu;
	struct cache_info *c;

	if (cache == NULL || cache->removed)
		return -1;

	/* Any cpu or cache node whose 'l2-cache' property matches
	 * the ibm,phandle is using our cache
	 */
	for (cpu = dr_info->all_cpus; cpu != NULL; cpu = cpu->next) {
		if (!cpu->is_owned)
			continue;

		if (cache == cpu_get_dependent_cache(cpu, dr_info)) {
			say(EXTRA_DEBUG, "Cache %s is a dependent of cpu %s\n",
			    cache->name, cpu->name);
			count++;
		}
	}

	for (c = dr_info->all_caches; c != NULL; c = c->next) {
		if (cache == cache_get_dependent_cache(c, dr_info)) {
			say(EXTRA_DEBUG, "Cache %s is a dependent of cache %s\n",
			    cache->name, c->name);
			count++;
		}
	}

	say(EXTRA_DEBUG, "Cache %s dependency count: %d\n",
	    cache->name, count);
	return count;
}

/**
 * free_cache_info
 * @brief free the cache list and any associated allocated memory
 *
 * @param cache_list list of cache structs to free
 */
void
free_cache_info(struct cache_info *cache_list)
{
	struct cache_info *tmp = cache_list;

	while (tmp != NULL) {
		cache_list = cache_list->next;
		free(tmp);
		tmp = cache_list;
	}
}

/**
 * init_cache_info
 *
 * @returns pointer to cache_info on success, NULL otherwise
 */
static int
init_cache_info(struct dr_info *dr_info)
{
	struct cache_info *cache_list = NULL;
	DIR *d;
	struct dirent *ent;
	int rc;

	d = opendir(CPU_OFDT_BASE);
	if (d == NULL) {
		say(ERROR, "Could not open %s: %s\n", CPU_OFDT_BASE,
		    strerror(errno));
		return -1;
	}

	while ((ent = readdir(d))) {
		char path[DR_PATH_MAX];
		struct stat sb;

		/* skip everything but directories */
		sprintf(path, "/proc/device-tree/cpus/%s", ent->d_name);
		if (lstat(path, &sb)) {
			say(ERROR, "Could not access %s,\nstat(): %s\n",
			    path, strerror(errno));
			break;
		}

		if (!S_ISDIR(sb.st_mode))
			continue;

		if (strstr(ent->d_name, "-cache@")) {
			struct cache_info *cache;

			cache = zalloc(sizeof(*cache));
			if (cache == NULL) {
				say(ERROR, "Could not allocate cache info."
				    "\n%s\n", strerror(errno));
				free_cache_info(cache_list);
				return -1;
			}

			snprintf(cache->name, DR_BUF_SZ, "%s", ent->d_name);
			snprintf(cache->path, DR_BUF_SZ, "%s", path);

			cache->removed = 0;
			cache->next = cache_list;
			cache_list = cache;

			rc = get_ofdt_uint_property(cache->path, "ibm,phandle",
						    &cache->phandle);
			if (rc) {
				say(ERROR, "Could not retreive ibm,phandle "
				    "property for %s\n", cache->path);
				free_cache_info(cache_list);
				return -1;
			}

			/* l3-caches do not have a l2-cache property */
			cache->l2cache = 0xffffffff;
			get_ofdt_uint_property(cache->path, "l2-cache",
					       &cache->l2cache);

			say(EXTRA_DEBUG, "Found cache %s\n", cache->name);
		}
	}

	closedir(d);

	dr_info->all_caches = cache_list;
	return 0;
}

/**
 * refresh_cache_info
 *
 * @returns 0 on success, !0 otherwise
 */
static int
refresh_cache_info(struct dr_info *dr_info)
{
	int rc;

	/* Some systems do not have cache device nodes.  Assume
	 * that if we did not find any cache nodes during initialization,
	 * we won't find any during a refresh.
	 */
	if (dr_info->all_caches == NULL)
		return 0;

	free_cache_info(dr_info->all_caches);
	
	rc = init_cache_info(dr_info);
	if (rc) {
		say(ERROR, "failed to refresh cache information\n");
		return rc;
	}

	return 0;
}

/**
 * acquire_cpu
 *
 * Acquire a new cpu for this partition from the hypervisor.
 * If drc_index is zero, look up an available index to use.
 *
 * @param drc_index
 * @returns pointer to cpu_info on success, NULL otherwise
 */
static int
acquire_cpu(struct dr_node *cpu, struct dr_info *dr_info)
{
	struct of_node *of_nodes;
	int rc;

	rc = acquire_drc(cpu->drc_index);
	if (rc) {
		say(DEBUG, "Could not acquire drc resources for %s\n",
		    cpu->name);
		return rc;
	}

	of_nodes = configure_connector(cpu->drc_index);
	if (of_nodes == NULL) {
		say(ERROR, "Call to configure_connector failed for %s\n",
		    cpu->name);
		release_drc(cpu->drc_index, CPU_DEV);
		return -1;
	}

	rc = add_device_tree_nodes(CPU_OFDT_BASE, of_nodes);
	free_of_node(of_nodes);
	if (rc) {
		say(ERROR, "Failure to add device tree nodes for %s\n",
		    cpu->name);
		release_drc(cpu->drc_index, CPU_DEV);
		return rc;
	}

	update_cpu_node(cpu, NULL, dr_info);
	refresh_cache_info(dr_info);

	return 0;
}

#ifdef NOT_YET
int do_cpu_kernel_dlpar(struct dr_node *cpu, int action)
{
	char cmdbuf[256];
	int offset;

	offset = sprintf(cmdbuf, "%s ", "cpu");

	switch (action) {
	case ADD:
		offset += sprintf(cmdbuf + offset, "add ");
		break;
	case REMOVE:
		offset += sprintf(cmdbuf + offset, "remove ");
		break;
	default:
		say(ERROR, "Invalid DRC type specified\n");
		return -EINVAL;
	}

	offset += sprintf(cmdbuf + offset, "index 0x%x", cpu->drc_index);

	return do_kernel_dlpar(cmdbuf, offset);
}
#endif

int
probe_cpu(struct dr_node *cpu, struct dr_info *dr_info)
{
	char drc_index[DR_STR_MAX];
	int probe_file;
	int write_len;
	int rc = 0;

#ifdef NOT_YET
	if (kernel_dlpar_exists()) {
		rc = do_cpu_kernel_dlpar(cpu, ADD);
	} else {
#endif
		probe_file = open(CPU_PROBE_FILE, O_WRONLY);
		if (probe_file <= 0) {
			/* Attempt to add cpu from user-space, this may be
			 * an older kernel without the infrastructure to
			 * handle dlpar.
			 */
			rc = acquire_cpu(cpu, dr_info);
			if (rc)
				return rc;

			rc = online_cpu(cpu, dr_info);
			if (rc) {
				/* Roll back the operation.  Is this the
				 * correct behavior?
				 */
				say(ERROR, "Unable to online %s\n",
				    cpu->drc_name);
				offline_cpu(cpu);
				release_cpu(cpu, dr_info);
				cpu->unusable = 1;
			}
		} else {
			memset(drc_index, 0, DR_STR_MAX);
			write_len = sprintf(drc_index, "0x%x", cpu->drc_index);

			say(DEBUG, "Probing cpu 0x%x\n", cpu->drc_index);
			rc = write(probe_file, drc_index, write_len);
			if (rc != write_len)
				say(ERROR, "Probe failed! rc = %x\n", rc);
			else
				/* reset rc to success */
				rc = 0;

			close(probe_file);
		}
#ifdef NOT_YET
	}
#endif

	if (!rc) {
		update_cpu_node(cpu, NULL, dr_info);
		refresh_cache_info(dr_info);
	}

	return rc;
}

/**
 * release_caches
 * Remove any unused cache info.  Failure to remove the cache, while not
 * good, should not affect the removal of the cpu.  Additionally, the only
 * way to add the cache info back to the device tree is via a
 * configure-connector call which could then put multiple copies of any
 * cache that wasn't fully removed into the device tree.  For these reasons
 * we ignore cache removal failures and do not try to recover.
 *
 * @param cpu
 * @param dr_info
 */
static void
release_caches(struct dr_node *cpu, struct dr_info *dr_info)
{
	struct cache_info *L2_cache, *L3_cache;
	int usecount, rc;

	L2_cache = cpu_get_dependent_cache(cpu, dr_info);

	usecount = cache_get_use_count(L2_cache, dr_info);
	if (usecount == 0) {
		L3_cache = cache_get_dependent_cache(L2_cache, dr_info);

		rc = cache_remove_devnode(L2_cache);
		if (rc) 
			return;
		
		usecount = cache_get_use_count(L3_cache, dr_info);
		if (usecount == 0)
			cache_remove_devnode(L3_cache);
	}

	return; 
}

/**
 * release_cpu
 *
 * Release a cpu back to the hypervisor, using the cpu's drc-index.
 * The cpu must have already been offlined.  We remove the cpu from the
 * device tree only when the isolate and set_indicator have succeeded.
 * The caller must not use the value given for the cpu parameter after
 * this function has returned.
 *
 * @param cpu
 * @returns 0 on success, !0 otherwise
 */
int
release_cpu(struct dr_node *cpu, struct dr_info *dr_info)
{
	int release_file;
	int rc;

#ifdef NOT_YET
	if (kernel_dlpar_exists())
		return do_cpu_kernel_dlpar(cpu, REMOVE);
#endif

	release_file = open(CPU_RELEASE_FILE, O_WRONLY);
	if (release_file > 0) {
		/* DLPAR can be done in kernel */
		char *path = cpu->ofdt_path + strlen(OFDT_BASE);
		int write_len = strlen(path);

		say(DEBUG, "Releasing cpu \"%s\"\n", path);
		rc = write(release_file, path, write_len);
		if (rc != write_len)
			say(ERROR, "Release failed! rc = %d\n", rc);
		else
			/* set rc to success */
			rc = 0;

		close(release_file);
	} else {
		/* Must do DLPAR from user-space */
		rc = offline_cpu(cpu);
		if (rc) {
			say(ERROR, "Could not offline cpu %s\n", cpu->drc_name);
			return rc;
		}

		rc = release_drc(cpu->drc_index, CPU_DEV);
		if (rc) {
			say(ERROR, "Could not release drc resources for %s\n",
			    cpu->name);
			return rc;
		}

		rc = remove_device_tree_nodes(cpu->ofdt_path);
		if (rc) {
			struct of_node *of_nodes;

			say(ERROR, "Could not remove device tree nodes %s\n",
			    cpu->name);
		
			of_nodes = configure_connector(cpu->drc_index);
			if (of_nodes == NULL) {
				say(ERROR, "Call to configure_connector failed "
				    "for %s. The device tree\nmay contain "
				    "invalid data for this cpu and a "
				    "re-activation of the partition is "
				    "needed to correct it.\n", cpu->name);
			} else {
				rc = add_device_tree_nodes(CPU_OFDT_BASE,
							   of_nodes);
				free_of_node(of_nodes);
			}

			acquire_drc(cpu->drc_index);
			return rc;
		}

		release_caches(cpu, dr_info);
	}

	return rc;
}


/**
 * init_cpu_drc_info
 *
 * @returns pointer to drc_info on success, NULL otherwise
 */
int
init_cpu_drc_info(struct dr_info *dr_info)
{
	struct dr_node *cpu;
	struct thread *t;
	int rc;

	memset(dr_info, 0, sizeof(*dr_info));

	rc = init_thread_info(dr_info);
	if (rc) {
		return -1;
	}

	rc = init_cpu_info(dr_info);
	if (rc) {
		free_cpu_drc_info(dr_info);
		return -1;
	}

	rc = init_cache_info(dr_info);
	if (rc) {
		free_cpu_drc_info(dr_info);
		return -1;
	}
	
	if (output_level >= EXTRA_DEBUG) {
		say(EXTRA_DEBUG, "Start CPU List.\n");
		for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
			say(EXTRA_DEBUG, "%x : %s\n", cpu->drc_index,
			    cpu->drc_name);
			for (t = cpu->cpu_threads; t; t = t->sibling)
				say(EXTRA_DEBUG, "\tthread: %d: %s\n",
				    t->phys_id, t->path);
		}
		say(EXTRA_DEBUG, "Done.\n");
	}

	return 0;
}

/**
 * free_cpu_drc_info
 * @brief free the allocated lists hanging off of the drc_info struct
 *
 * @param dr_info dr_info struct to free memory from
 */
void
free_cpu_drc_info(struct dr_info *dr_info)
{
	free_cache_info(dr_info->all_caches);
	free_thread_info(dr_info->all_threads);
	free_node(dr_info->all_cpus);

	memset(dr_info, 0, sizeof(*dr_info));
}

/**
 * set thread_state
 * @brief Set the thread state
 *
 * Set the given thread to the desired state.  This just writes "1" or "0"
 * to /sys/devices/system/cpu/cpu[id]/online.
 *
 * @param thread
 * @param state
 * @returns 0 on success, !0 otherwise
 */
int
set_thread_state(struct thread *thread, int state)
{
	char path[DR_PATH_MAX];
	FILE *file;
	int rc = 0;

	say(DEBUG, "%slining thread id %d\n",
	    ((state == ONLINE) ? "On" : "Off"), thread->id);
	sprintf(path, DR_THREAD_ONLINE_PATH, thread->id);
	file = fopen(path, "w");
	if (file == NULL) {
		say(ERROR, "Could not open %s, unable to set thread state "
		    "to %d\n", path);
		return -1;
	}

	fprintf(file, "%d", state);
	fclose(file);

	/* fprintf apparently does not return negative number
	 * if the write() gets an -EBUSY, so explicitly check the
	 * thread state.
	 */
	if (state != get_thread_state(thread)) {
		say(ERROR, "Failure setting thread state for %s\n", path);
		rc = -1;
	}

	return rc;
}

/**
 * get_thread_state
 * @brief Get the "online" status of the given thread.
 *
 * @param thread
 * @returns 0 = offline, 1 = online, -1 on error
 */
int
get_thread_state(struct thread *thread)
{
	char path[DR_PATH_MAX];
	int rc, status = -1;

	sprintf(path, DR_THREAD_ONLINE_PATH, thread->id);

	rc = get_int_attribute(path, NULL, &status, sizeof(status));

	return rc ? rc : status;
}

/**
 * cpu_enable_smt
 *
 * For the given physical cpu, online all of its threads.
 * Just call online_spu for now.
 *
 * @param cpu cpu to enable smt on
 * @returns 0 on success, !0 otherwise
 */
int
cpu_enable_smt(struct dr_node *cpu, struct dr_info *dr_info)
{
	return online_cpu(cpu, dr_info);
}

/**
 * cpu_diable_smt
 * @brief Disable all but one of a cpu's threads
 *
 * @param cpu cpu to disable smt on
 * @returns 0 on success, !0 otherwise
 */
int
cpu_disable_smt(struct dr_node *cpu)
{
	int rc = 0;
	struct thread *t;
	int survivor_found = 0;

	/* Ensure that the first thread of the processor is the thread that is left online
	 * when disabling SMT.
	 */
	t = cpu->cpu_threads;
	if (get_thread_state(t) == OFFLINE)
		rc |= set_thread_state(t, ONLINE);

	for (t = cpu->cpu_threads; t != NULL; t = t->sibling) {
		if (ONLINE == get_thread_state(t)) {
			if (survivor_found)
				rc |= set_thread_state(t, OFFLINE);
			survivor_found = 1;
		}
	}

	return rc;
}

/**
 * online_first_dead_cpu
 * @brief Find the first cpu with attributes online and physical_id both
 *        set to 0 and online it.
 *
 * @param nthreads
 * @returns 0 on success, !0 otherwise
 */
int
online_first_dead_cpu(int nthreads, struct dr_info *dr_info)
{
	struct thread *thread;
	int rc = 1;

	for (thread = dr_info->all_threads; thread; thread = thread->next) {
		if (OFFLINE == get_thread_state(thread)
		    && ((thread->phys_id == 0xffffffff)
			|| (thread->phys_id == 0))) {
			    
			/* just assume that there will be nthreads to online. */
			while (nthreads--) {
				set_thread_state(thread, ONLINE);
				thread = thread->next;
			}

			rc = 0;
			break;
		}
	}

	if (rc)
		say(ERROR, "Could not find any threads to online\n");

	return rc;
}

/**
 * offline_cpu
 * @brief Mark the specified cpu as offline
 *
 * @param cpu
 * @param dr_info
 * @returns 0 on success, !0 otherwise
 */
int
offline_cpu(struct dr_node *cpu)
{
	int rc = 0;
	struct thread *thread;

	say(DEBUG, "Offlining cpu %s (%d threads)\n", cpu->name,
	    cpu->cpu_nthreads);

	for (thread = cpu->cpu_threads; thread; thread = thread->sibling) {
		if (get_thread_state(thread) != OFFLINE)
			rc |= set_thread_state(thread, OFFLINE);
	}

	return rc;
}

/**
 * online cpu
 *
 * @param cpu
 * @param dr_info
 * @returns 0 on success, !0 otherwise
 */
int
online_cpu(struct dr_node *cpu, struct dr_info *dr_info) 
{
	int rc = 0;
	struct thread *thread = NULL;
	int found = 0;

	say(DEBUG, "Onlining cpu %s (%d threads)\n", cpu->name,
	    cpu->cpu_nthreads);

	/* Hack to work around kernel brain damage (LTC 7692) */
	for (thread = dr_info->all_threads; thread; thread = thread->next) {
		if (thread->cpu == cpu) {
			found = 1;
			break;
		}
	}
	
	if (!found) {
		/* There are no threads which match this cpu because
		 * the physical_id attribute is not updated until the
		 * cpu is onlined -- this case is for cpus which are
		 * not present at boot but are added afterwards.
		 */
		return online_first_dead_cpu(cpu->cpu_nthreads, dr_info);
	}

	for (thread = cpu->cpu_threads; thread; thread = thread->sibling) {
		if (get_thread_state(thread) != ONLINE)
			rc |= set_thread_state(thread, ONLINE);
	}

	return rc;
}

/**
 * smt_enabled
 * @brief Is smt enabled?
 *
 * @returns 1 if smt enabled, 0 if not
 */
int
smt_enabled(struct dr_info *dr_info)
{
	struct dr_node *cpu;

	/* Just return true if the number of threads in the
	 * first owned cpu is more than one.
	 */
	for (cpu = dr_info->all_cpus; cpu; cpu = cpu->next) {
		if (cpu->is_owned)
			break;
	}

	if (cpu && (cpu->cpu_nthreads > 1))
		return 1;

	return 0;
}

/**
 * system_enable_smt
 * @brief Activate all threads of each cpu
 *
 * @returns 0 on success, !0 otherwise
 */
int system_enable_smt(struct dr_info *dr_info)
{
	struct dr_node *cpu;
	int rc = 0;

	for (cpu = dr_info->all_cpus; cpu != NULL; cpu = cpu->next) {
		if (cpu->is_owned)
			rc |= cpu_enable_smt(cpu, dr_info);
	}

	return rc;
}

/**
 * system_disable_smt
 * @brief Offline all but one thread of each cpu
 *
 * @returns 0 on success, !0 otherwise
 */
int
system_disable_smt(struct dr_info *dr_info)
{
	struct dr_node *cpu;
	int rc = 0;

	for (cpu = dr_info->all_cpus; cpu != NULL; cpu = cpu->next) {
		if (cpu->is_owned)
			rc |= cpu_disable_smt(cpu);
	}

	return rc;
}
