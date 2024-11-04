/**
 * @file common.c
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

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/uio.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#ifdef __GLIBC__
#include <execinfo.h>
#endif
#include <ctype.h>
#include <sys/wait.h>
#include <endian.h>
#include "dr.h"
#include "ofdt.h"

char *add_slot_fname = ADD_SLOT_FNAME;
char *remove_slot_fname = REMOVE_SLOT_FNAME;

#define DR_MAX_LOG_SZ (1 << 20)

#define DR_LOG_PATH	"/var/log/drmgr"
#define DR_LOG_PATH0	"/var/log/drmgr.0"

#define LPARCFG_PATH	"/proc/ppc64/lparcfg"

#define SYSFS_DLPAR_FILE	"/sys/kernel/dlpar"

#define DR_SCRIPT_DIR	"/etc/drmgr.d"

static int dr_lock_fd = 0;
static long dr_timeout;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const char * const drc_type_str[] = {
	[DRC_TYPE_NONE]		= "unknwon",
	[DRC_TYPE_PCI]		= "pci",
	[DRC_TYPE_SLOT]		= "slot",
	[DRC_TYPE_PHB]		= "phb",
	[DRC_TYPE_CPU]		= "cpu",
	[DRC_TYPE_MEM]		= "mem",
	[DRC_TYPE_PORT]		= "port",
	[DRC_TYPE_HIBERNATE]	= "phib",
	[DRC_TYPE_MIGRATION]	= "pmig",
	[DRC_TYPE_ACC]		= "acc",
};

static const char * const hook_phase_name[] = {
	[HOOK_CHECK]		= "check",
	[HOOK_UNDOCHECK]	= "undocheck",
	[HOOK_PRE]		= "pre",
	[HOOK_POST]		= "post",
};

static const char * const hook_action_name[] = {
	[NONE]		= "none",
	[ADD]		= "add",
	[REMOVE]	= "remove",
	[QUERY]		= "query",
	[REPLACE]	= "replace",
	[IDENTIFY]	= "identify",
	[MIGRATE]	= "migrate",
	[HIBERNATE]	= "hibernate",
};

/**
 * set_output level
 * @brief Common routine to set the output level
 *
 * @param level level to set the output level to
 */
inline void
set_output_level(int level)
{
	output_level = level;

	if (output_level >= 14) {
		say(DEBUG, "Enabling RTAS debug\n");
		rtas_set_debug(output_level);
	}
}

int say(enum say_level lvl, char *fmt, ...)
{
	va_list ap;
	char buf[256];
	int len;

	va_start(ap, fmt);
	memset(buf, 0, 256);
	len = vsnprintf(buf, 256, fmt, ap);
	va_end(ap);

	if (len >= 256) {
		strcpy(&buf[243], "<truncated>\n");
		len = 255;
	}

	if (log_fd)
		len = write(log_fd, buf, len);

	if (lvl <= output_level)
		fprintf(stderr, "%s", buf);

	return len;
}

void report_unknown_error(char *file, int line) {
	say(ERROR, "Unexpected error (%s:%d).  Contact support and provide "
			"debug log from %s.\n", file, line, DR_LOG_PATH);
}

void * __zalloc(size_t size, const char *func, int line)
{
	void *data;
	data = malloc(size);
	if (data)
		memset(data, 0, size);
	else
		say(ERROR, "Allocation failure (%lx) at %s:%d\n", size, func, line);

	return data;
}

static int check_kmods(void)
{
	struct stat sbuf;
	int rc;

	/* We only need to do this for PHB/SLOT/PCI operations */
	if (usr_drc_type != DRC_TYPE_PCI && usr_drc_type != DRC_TYPE_PHB &&
	    usr_drc_type != DRC_TYPE_SLOT && !display_capabilities)
		return 0;

	/* We don't use rpadlar_io/rpaphp for PCI operations run with the
	 * -v / virtio flag, which relies on generic PCI rescan instead
	 */
	if (usr_drc_type == DRC_TYPE_PCI && pci_virtio && !display_capabilities)
		return 0;

	/* Before checking for dlpar capability, we need to ensure that
	 * rpadlpar_io module is loaded or built into the kernel. This
	 * does make the checking a bit redundant though.
	 */
	if ((stat(add_slot_fname, &sbuf)) && (stat(ADD_SLOT_FNAME2, &sbuf))) {
		rc = system("/sbin/modprobe rpadlpar_io");
		if (WIFEXITED(rc) && WEXITSTATUS(rc)) {
			say(ERROR, "rpadlpar_io module was not loaded\n");
			return WEXITSTATUS(rc);
		}
	}

	/* For unknown reasons the add_slot and remove_slot sysfs files
	 * used for dlpar operations started appearing with quotes around
	 * the filename.  So, this little hack exists to ensure nothing
	 * breaks on the kernels where this exists.
	 *
	 * The default value is without the quotes.  This is what was and
	 * what shall be again.
	 */
	rc = stat(add_slot_fname, &sbuf);
	if (rc) {
		add_slot_fname = ADD_SLOT_FNAME2;
		remove_slot_fname = REMOVE_SLOT_FNAME2;
		rc = stat(add_slot_fname, &sbuf);
	}
	
	return rc;
}

/**
 * dr_init
 * @brief Initialization routine for drmgr and lsslot
 *
 */
inline int dr_init(void)
{
	int rc;

	rc = dr_lock();
	if (rc) {
		say(ERROR, "Unable to obtain Dynamic Reconfiguration lock. "
		    "Please try command again later.\n");
		return -1;
	}


	log_fd = open(DR_LOG_PATH, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC,
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (log_fd == -1) {
		log_fd = 0;
		say(ERROR, "Could not open log file %s\n\t%s\n", DR_LOG_PATH,
		    strerror(errno));
	} else {
		time_t t;
		char tbuf[128];

		/* Insert seperator at beginning of drmgr invocation */
		time(&t);
		strftime(tbuf, 128, "%b %d %T %G", localtime(&t));
		say(DEBUG, "\n########## %s ##########\n", tbuf);
	}

	/* Mask signals so we do not get interrupted */
	if (sig_setup()) {
		say(ERROR, "Could not mask signals to avoid interrupts\n");
		if (log_fd)
			close(log_fd);
		dr_unlock();
		return -1;
	}

	rc = check_kmods();
	if (rc) {
		if (log_fd)
			close(log_fd);
		dr_unlock();
	}

	return rc;
}

/**
 * dr_fini
 * @brief Cleanup routine for drmgr and lsslot
 *
 */
inline void
dr_fini(void)
{
	struct stat sbuf;
	int max_dr_log_sz = DR_MAX_LOG_SZ;
	int rc;
	time_t t;
	char tbuf[128];

	free_drc_info();

	if (! log_fd)
		return;

	/* Insert seperator at end of drmgr invocation */
	time(&t);
	strftime(tbuf, 128, "%b %d %T %G", localtime(&t));
	say(DEBUG, "########## %s ##########\n", tbuf);

	close(log_fd);

	/* Check for log rotation */
	rc = stat(DR_LOG_PATH, &sbuf);
	if (rc) {
		fprintf(stderr, "Cannot determine log size to check for "
			"rotation:\n\t%s\n", strerror(errno));
		return;
	}

	if (sbuf.st_size >= max_dr_log_sz) {
		fprintf(stderr, "Rotating logs...\n");

		rc = unlink(DR_LOG_PATH0);
		if (rc && (errno != ENOENT)) {
			fprintf(stderr, "Could not remove %s\n\t%s\n",
				DR_LOG_PATH0, strerror(errno));
			return;
		}

		rc = rename(DR_LOG_PATH, DR_LOG_PATH0);
		if (rc) {
			fprintf(stderr, "Could not rename %s to %s\n\t%s\n",
				DR_LOG_PATH, DR_LOG_PATH0, strerror(errno));
			return;
		}
	}

	dr_unlock();
}

/**
 * set_timeout
 *
 */
void set_timeout(int timeout)
{
	if (!timeout) {
		dr_timeout = -1;
		return;
	}

	dr_timeout = time((time_t *)0);
	if (dr_timeout == -1)
		return;

	dr_timeout += timeout;
}

/**
 *
 */
int drmgr_timed_out(void)
{
	if (dr_timeout == -1)
		return 0;	/* No timeout specified */

	if (dr_timeout > time((time_t *)0))
		return 0;

	say(WARN, "Drmgr has exceeded its specified wait time and will not "
	    "continue\n");
	return 1;
}

/**
 * dr_lock
 * @brief Attempt to lock a token
 *
 * This will attempt to lock a token (either file or directory) and wait
 * a specified amount of time if the lock cannot be granted.
 *
 * @returns lock id if successful, -1 otherwise
 */
int dr_lock(void)
{
	struct flock    dr_lock_info;
	int             rc;
	mode_t          old_mode;

	old_mode = umask(0);
	dr_lock_fd = open(DR_LOCK_FILE, O_RDWR | O_CREAT | O_CLOEXEC,
			  S_IRUSR | S_IRGRP | S_IROTH);
	if (dr_lock_fd < 0)
		return -1;

	umask(old_mode);
	dr_lock_info.l_type = F_WRLCK;
	dr_lock_info.l_whence = SEEK_SET;
	dr_lock_info.l_start = 0;
	dr_lock_info.l_len = 0;

	do {
		rc = fcntl(dr_lock_fd, F_SETLK, &dr_lock_info);
		if (rc == 0)
			return 0;

		/* lock may be held by another process */
		if (errno != EACCES && errno != EAGAIN)
			break;

		if (drmgr_timed_out())
			break;

		sleep(1);
	} while (1);

	close(dr_lock_fd);
	dr_lock_fd = 0;
	perror(DR_LOCK_FILE);
	return -1;
}

/**
 * dr_unlock
 * @brief unlock a lock granted via dr_lock()
 *
 * @param lock_id
 * @returns 0 on success, -1 otherwise
 */
int
dr_unlock(void)
{
	struct flock	dr_lock_info;

	dr_lock_info.l_whence = SEEK_SET;
	dr_lock_info.l_start = 0;
	dr_lock_info.l_len = 0;
	dr_lock_info.l_type = F_UNLCK;
	if (fcntl(dr_lock_fd, F_SETLK, &dr_lock_info) < 0)
		return -1;

	close(dr_lock_fd);
	dr_lock_fd = 0;
	return 0;

}

/**
 * add_node
 * @brief Add the specified node(s) to the device tree
 *
 * This will add the specified node(s) to the /proc Open Firmware
 * tree and the kernel Open Firmware tree.
 *
 * @param add_path
 * @param new_nodes
 * @returns 0 on success
 */
static int
add_node(char *path, struct of_node *new_nodes)
{
	int rc = 0;
	int nprops = 0;
	int fd;
	struct of_property *prop;
	char *buf, *pos;
	char *add_path;
	size_t bufsize = 0;
	struct stat sbuf;

	/* If the device node already exists, no work to be done.  This is
	 * usually the case for adding a dedicated cpu that shares a
	 * l2-cache with another apu and that cache already exists in the
	 * device tree.
	 */
	if (!stat(path, &sbuf)) {
		say(DEBUG, "Device-tree node %s already exists, skipping\n",
		    path);
		return 0;
	}

	say(DEBUG, "Adding device-tree node %s\n", path);

	/* The path passed in is a full ofdt path, remove the preceeding
	 * /proc/device-tree for the write to the kernel.
	 */
	add_path = path + strlen(OFDT_BASE);

	/* determine the total size of the buffer we need to allocate */
	bufsize += strlen("add_node ");
	bufsize += strlen(add_path) + 1;
	for (prop = new_nodes->properties; prop; prop = prop->next) {
		char tmp[16] = { '\0' }; /* for length */

		nprops++;
		bufsize += strlen(prop->name) + 1; /* name + space */
		sprintf(tmp, "%d", prop->length);

		bufsize += strlen(tmp) + 1;	/* length + space*/
		bufsize += prop->length + 1;	/* value + space */
	}

	if (! nprops) {
		say(ERROR, "new_nodes have no properties\n");
		return -1;
	}

	buf = zalloc(bufsize);
	if (buf == NULL) {
		say(ERROR, "Failed to allocate buffer to write to kernel\n");
		return errno;
	}

	strcpy(buf, "add_node ");
	strcat(buf, add_path);
	pos = buf + strlen(buf);

	/* this is less than optimal, iterating over the entire buffer
	 * for every strcat...
	 */
	for (prop = new_nodes->properties; prop; prop = prop->next) {
		char tmp[16] = { '\0' }; /* for length */

		*pos++ = ' ';

		memcpy(pos, prop->name, strlen(prop->name));
		pos += strlen(prop->name);
		*pos++ = ' ';

		sprintf(tmp, "%d", prop->length);
		memcpy(pos, tmp, strlen(tmp));
		pos += strlen(tmp);
		*pos++ = ' ';

		memcpy(pos, prop->value, prop->length);
		pos += prop->length;
	}
	*pos = '\0';

	/* dump the buffer for debugging */
	say(DEBUG, "ofdt update: %s\n", buf);

	fd = open(OFDTPATH, O_WRONLY);
	if (fd == -1) {
		say(ERROR, "Failed to open %s: %s\n", OFDTPATH,
		    strerror(errno));
		free(buf);
		return -1;
	}

	rc = write(fd, buf, bufsize);
	if (rc <= 0)
		say(ERROR, "Write to %s failed: %s\n", OFDTPATH,
		    strerror(errno));
	else
		rc = 0;

	free(buf);
	close(fd);
	return rc;
}

/**
 * remove_node
 * @brief Remove the specified node to the device tree
 *
 * This will remove the specified node to the /proc Open Firmware
 * tree and the kernel Open Firmware tree.
 *
 * @param cmd
 * @returns 0 on success
 */
static int
remove_node(const char *path)
{
	int rc = 0;
	int fd;
	int cmdlen;
	char buf[DR_PATH_MAX];

	say(DEBUG, "Removing device-tree node %s\n", path);

	memset(buf, 0, DR_PATH_MAX);

	/* The path passed in is a full device path, remove the preceeding
	 * /proc/device-tree part for the write to the kernel.
	 */
	sprintf(buf, "remove_node %s", path + strlen(OFDT_BASE));

	cmdlen = strlen(buf);

	fd = open(OFDTPATH, O_WRONLY);
	if (fd == -1) {
		say(ERROR, "Failed to open %s: %s\n", OFDTPATH,
		    strerror(errno));
		return -1;
	}

	rc = write(fd, buf, cmdlen);
	if (rc != cmdlen)
		say(ERROR, "Write to %s failed: %s\n", OFDTPATH,
		    strerror(errno));
	else
		rc = 0;

	close(fd);
	return rc;
}

/**
 * add_device_tree_nodes
 *
 * Process new_nodes from configure_connector and call add_node to
 * add new nodes to /proc device-tree and the kernel's device tree.
 *
 * @param root_path
 * @param new_nodes
 * @returns 0 on success, !0 otherwise
 */
static int
_add_device_tree_nodes(char *path, struct of_node *new_nodes)
{
	struct of_node *node;
	char add_path[DR_PATH_MAX];
	int rc = 0;

	for (node = new_nodes; node; node = node->sibling) {
		sprintf(add_path, "%s/%s", path, node->name);
		
		rc = add_node(add_path, node);
		if (rc)
			break;

		node->added = 1;
		
		if (node->child) {
			rc = _add_device_tree_nodes(add_path, node->child);
			if (rc)
				break;
		}
	}

	return rc;
}

int
add_device_tree_nodes(char *path, struct of_node *new_nodes)
{
	struct of_node *node;
	char rm_path[DR_PATH_MAX];
	int rc;

	rc = _add_device_tree_nodes(path, new_nodes);
	if (rc) {
		for (node = new_nodes; node; node = node->sibling) {
			if (!node->added)
				continue;
			
			sprintf(rm_path, "%s/%s", path, node->name);
			remove_node(rm_path);
		}
	}

	return rc;
}

/**
 * remove_device_tree_nodes
 *
 * Remove all device nodes and children device nodes from Open Firmware
 * device tree
 *
 * @param root_path
 * @returns 0 on success, !0 otherwise
 */
int
remove_device_tree_nodes(const char *path)
{
        DIR *d;
        struct dirent *de;
        struct stat sb;
	int found = 1;
	int rc;

	rc = lstat(path, &sb);
	if (rc || (!S_ISDIR(sb.st_mode)) || (S_ISLNK(sb.st_mode)))
		return rc;

	d = opendir(path);
	if (d == NULL) {
		say(ERROR, "Could not open %s: %s\n", path, strerror(errno));
		return -1;
	}

	while (found) {
		char subdir_name[DR_PATH_MAX];

		found = 0;

		/* Remove any subdirectories */
		while ((de = readdir(d)) != NULL) {
			if (is_dot_dir(de->d_name))
				continue;

			sprintf(subdir_name, "%s/%s", path, de->d_name);
			rc = lstat(subdir_name, &sb);
			if (!rc && (S_ISDIR(sb.st_mode))
			    && (!S_ISLNK(sb.st_mode))) {
				found = 1;
				break;
			}
		}

		if (found) {
			rc = remove_device_tree_nodes(subdir_name);
			rewinddir(d);
		}

		if (rc)
			break;
	}

	closedir(d);

	if (!rc)
		rc = remove_node(path);

        return rc;
}

/**
 * update_property
 *
 *
 */
int
update_property(const char *buf, size_t len)
{
	int fd, rc;

	say(DEBUG, "Updating OF property\n");

	fd = open(OFDTPATH, O_WRONLY);
	if (fd == -1) {
		say(ERROR, "Failed to open %s: %s\n", OFDTPATH,
		    strerror(errno));
		return -1;
	}

	rc = write(fd, buf, len);
	if ((size_t)rc != len)
		say(ERROR, "Write to %s failed: %s\n", OFDTPATH,
		    strerror(errno));
	else
		rc = 0;

	close(fd);
	return rc;
}

/**
 * get_att_prop
 * @brief find the value for a given property/attribute.
 *
 * @param path path to the property/attribute to retrieve
 * @param name name of the property/attribute to retrieve
 * @param buf buffer to write property/attribute to
 * @param buf_sz size of the buffer
 * @returns 0 on success, -1 otherwise
 */
static int
get_att_prop(const char *path, const char *name, char *buf, size_t buf_sz,
	     const char *attr_type)
{
	FILE *fp;
	int rc = 0;
	char dir[DR_PATH_MAX];
	struct stat sbuf;

	if (buf == NULL)
		return -1;

	if (name != NULL)
		sprintf(dir, "%s/%s", path, name);
	else
		sprintf(dir, "%s", path);

	fp = fopen(dir, "r");
	if (fp == NULL)
		return -1;

	memset(buf, 0, buf_sz);

	/* Yes, this is sort of a hack but we only read properties from
	 * either /proc or sysfs so it works and is cheaper than a strcmp()
	 */
	switch (dir[1]) {
	    case 'p':	/* /proc */
		rc = stat(dir, &sbuf);
		if (rc)
			break;

		if (sbuf.st_size > buf_sz) {
			rc = -1;
			break;
		}

		rc = fread(buf, sbuf.st_size, 1, fp);
		break;

	    case 's':	/* sysfs */
		rc = fscanf(fp, attr_type, (int *)buf);
		break;
	}

	fclose(fp);

	/*
	 * we're lucky, because if successed, both fread and fscanf will return
	 * 1, so we can check whether rc is 1 for failures of reading files
	 */
	if (rc != 1)
		return -1;

	return 0;
}

/**
 * get_property
 * @brief retrieve a device-tree property from /proc
 *
 * @param path path to the property to retrieve
 * @param name name of the property to retrieve
 * @param buf buffer to write property to
 * @param buf_sz size of the buffer
 * @returns 0 on success, !0 otherwise
 */
int
get_property(const char *path, const char *property, void *buf, size_t buf_sz)
{
	return get_att_prop(path, property, buf, buf_sz, NULL);
}

/**
 * get_int_attribute
 * @brief retrieve an integer device attribute from sysfs
 *
 * @param path path to the attribute to retrieve
 * @param name name of the attribute to retrieve
 * @param buf buffer to write attribute to
 * @param buf_sz size of the buffer
 * @returns 0 on success, -1 otherwise
 */
int
get_int_attribute(const char *path, const char *attribute, void *buf,
		  size_t buf_sz)
{
	return get_att_prop(path, attribute, buf, buf_sz, "%i");
}

/**
 * get_str_attribute
 * @brief retrieve a string device attribute from sysfs
 *
 * @param path path to the attribute to retrieve
 * @param name name of the attribute to retrieve
 * @param buf buffer to write attribute to
 * @param buf_sz size of the buffer
 * @returns 0 on success, -1 otherwise
 */
int
get_str_attribute(const char *path, const char *attribute, void *buf,
		  size_t buf_sz)
{
	return get_att_prop(path, attribute, buf, buf_sz, "%s");
}

/**
 * get_ofdt_uint_property
 * @brief retrieve an unsigned integer property from the device tree and
 * byteswap if needed (device tree is big endian).
 *
 * @param path path to the property to retrieve
 * @param name name of the property to retrieve
 * @param data unsigned integer pointer to write property to
 * @returns 0 on success, -1 otherwise
 */
int
get_ofdt_uint_property(const char *path, const char *attribute, uint *data)
{
	uint tmp;
	int rc;
	rc = get_property(path, attribute, &tmp, sizeof(tmp));
	if (!rc)
		*data = be32toh(tmp);
	return rc;
}

/**
 * get_property_size
 * @brief retrieve the size of a property
 *
 * @param path path to the property to retrieve
 * @param name name of the property to retrieve
 * @returns size of the property
 */
int
get_property_size(const char *path, const char *property)
{
	char dir[DR_PATH_MAX];
	struct stat sb;

	if (property != NULL)
		sprintf(dir, "%s/%s", path, property);
	else
		sprintf(dir, "%s", path);

	stat(dir, &sb);
	return sb.st_size;
}

/**
 * sighandler
 * @brief Simple signal handler to print signal/stack info, cleanup and exit.
 *
 * @param signo signal number we caught.
 */
void
sighandler(int signo)
{
	say(ERROR, "Received signal %d, attempting to cleanup and exit\n",
	    signo);

#ifdef __GLIBC__
	if (log_fd) {
		void *callstack[128];
		int sz;

		sz = backtrace(callstack, 128);
		backtrace_symbols_fd(callstack, sz, log_fd);
	}
#endif

	dr_fini();
	exit(-1);
}

/**
 * sig_setup
 * @brief Mask signals so that dynamic reconfig operations won't be
 * interrupted and catch others.
 *
 * @returns 0 on success, !0 otherwise
 */
int
sig_setup(void)
{
	sigset_t sigset;
	struct sigaction sigact;
	void *callstack[128];
	int rc;

	/* Now set up a mask with all signals masked */
	sigfillset(&sigset);

	/* Clear mask bits for signals we don't want to mask */
	sigdelset(&sigset, SIGBUS);
	sigdelset(&sigset, SIGXFSZ);
	sigdelset(&sigset, SIGSEGV);
	sigdelset(&sigset, SIGTRAP);
	sigdelset(&sigset, SIGILL);
	sigdelset(&sigset, SIGFPE);
	sigdelset(&sigset, SIGSYS);
	sigdelset(&sigset, SIGPIPE);
	sigdelset(&sigset, SIGVTALRM);
	sigdelset(&sigset, SIGALRM);
	sigdelset(&sigset, SIGQUIT);
	sigdelset(&sigset, SIGABRT);

	/* Now block all remaining signals */
	rc = sigprocmask(SIG_BLOCK, &sigset, NULL);
	if (rc)
		return -1;

	/* Now set up a signal handler for the signals we want to catch */
	memset(&sigact, 0, sizeof(sigact));
	sigemptyset(&sigact.sa_mask);
	sigact.sa_handler = sighandler;
	
	if (sigaction(SIGQUIT, &sigact, NULL))
		return -1;

	if (sigaction(SIGILL, &sigact, NULL))
		return -1;

	if (sigaction(SIGABRT, &sigact, NULL))
		return -1;

	if (sigaction(SIGFPE, &sigact, NULL))
		return -1;

	if (sigaction(SIGSEGV, &sigact, NULL))
		return -1;

	if (sigaction(SIGBUS, &sigact, NULL))
		return -1;

#ifdef __GLIBC__
	/* dummy call to backtrace to get symbol loaded */
	backtrace(callstack, 128);
#endif
	return 0;
}

char *php_slot_type_msg[]={
	"",
	"PCI 32 bit, 33MHz, 5 volt slot",
	"PCI 32 bit, 50MHz, 5 volt slot",
	"PCI 32 bit, 33MHz, 3.3 volt slot",
	"PCI 64 bit, 33MHz, 5 volt slot",
	"PCI 64 bit, 50MHz, 5 volt slot",	/* 5 */
	"PCI 64 bit, 33MHz, 3.3 volt slot",
	"PCI 32 bit, 66MHz, 3.3 volt slot",
	"PCI 64 bit, 66MHz, 3.3 volt slot",
	"",				/* we don't have connector types for */
	"",				/* 9 or 10, so skip these indices.   */
	"PCI-X capable, 32 bit, 66MHz slot",
	"PCI-X capable, 32 bit, 100MHz slot",
	"PCI-X capable, 32 bit, 133MHz slot",
	"PCI-X capable, 64 bit, 66MHz slot",
	"PCI-X capable, 64 bit, 100MHz slot",	/* 15 */
	"PCI-X capable, 64 bit, 133MHz slot",
	"PCI-X capable, 64 bit, 266MHz slot",
	"PCI-X capable, 64 bit, 533MHz slot",
	"PCI-E capable, Rev 1, 1x lanes",
	"PCI-E capable, Rev 1, 2x lanes", 	/* 20 */
	"PCI-E capable, Rev 1, 4x lanes",
	"PCI-E capable, Rev 1, 8x lanes",
	"PCI-E capable, Rev 1, 16x lanes",
	"PCI-E capable, Rev 1, 32x lanes",
	"PCI-E capable, Rev 2, 1x lanes",	/* 25 */
	"PCI-E capable, Rev 2, 2x lanes",
	"PCI-E capable, Rev 2, 4x lanes",
	"PCI-E capable, Rev 2, 8x lanes",
	"PCI-E capable, Rev 2, 16x lanes",
	"PCI-E capable, Rev 2, 32x lanes",	/* 30 */
	"PCI-E capable, Rev 3, 8x lanes with 1 lane connected",
	"PCI-E capable, Rev 3, 8x lanes with 4x lanes connected",
	"PCI-E capable, Rev 3, 8x lanes with 8x lanes connected",
	"PCI-E capable, Rev 3, 16x lanes with 1 lane connected",
	"PCI-E capable, Rev 3, 16x lanes with 8x lanes connected",  /* 35 */
	"PCI-E capable, Rev 3, 16x lanes with 16x lanes connected",
	"PCI-E capable, Rev 4, 8x lanes with 1 lane connected",
	"PCI-E capable, Rev 4, 8x lanes with 4x lanes connected",
	"PCI-E capable, Rev 4, 8x lanes with 8x lanes connected",
	"PCI-E capable, Rev 4, 16x lanes with 1 lane connected",    /* 40 */
	"PCI-E capable, Rev 4, 16x lanes with 8x lanes connected",
	"PCI-E capable, Rev 4, 16x lanes with 16x lanes connected",
	"U.2 PCI-E capable, Rev 3, 4x lanes with 4x lanes connected",
	"U.2 PCI-E capable, Rev 4, 4x lanes with 4x lanes connected",
	"U.2 PCI-E capable, Rev 4, 4x lanes with 2x lanes connected", 	/* 45 */
	"PCI-E capable, Rev 5, 8x lanes with 1 lane connected",
	"PCI-E capable, Rev 5, 8x lanes with 4x lanes connected",
	"PCI-E capable, Rev 5, 8x lanes with 8x lanes connected",
	"PCI-E capable, Rev 5, 16x lanes with 1 lane connected",
	"PCI-E capable, Rev 5, 16x lanes with 4x lanes connected",	/* 50 */
	"PCI-E capable, Rev 5, 16x lanes with 8x lanes connected",
	"U.2 PCI-E capable, Rev 5, 4x lanes with 2x lanes connected",
	"U.2 PCI-E capable, Rev 5, 4x lanes with 4x lanes connected",
};

char *
node_type(struct dr_node *node)
{
	int desc_msg_num;
	char *desc = "Unknown";

	desc_msg_num = atoi(node->drc_type);
	if ((desc_msg_num >= 1 &&  desc_msg_num <= 8) ||
	    (desc_msg_num >= 11 && desc_msg_num <= 53))
		desc = php_slot_type_msg[desc_msg_num];
	else {
		switch (node->dev_type) {
			case PCI_DLPAR_DEV:
				desc = "Logical I/O Slot";
				break;
			case VIO_DEV:
				desc = "Virtual I/O Slot";
				break;
			case HEA_DEV:
				desc = "HEA I/O Slot";
				break;
			case HEA_PORT_DEV:
				desc = "HEA Port I/O Slot";
				break;
			default:
				desc = "Unknown slot type";
				break;
		}
	}

	return desc;
}

/**
 * valid_platform
 * @brief Validate that the platfomr we are on matches the one specified
 *
 * @param platform Name of platform to validate
 * @return 1 if valid, 0 otherwise
 */
int
valid_platform(const char *platform)
{
	char buf[128];
	int rc;

	rc = get_property(OFDT_BASE, "device_type", buf, 128);
	if (rc) {
		say(ERROR, "Cannot validate platform %s\n", platform);
		return 0;
	}

	if (strcmp(buf, platform)) {
		say(ERROR, "This command is not supported for %s platforms.\n",
		    platform);
		return 0;
	}

	return 1;
}


/**
 * get_sysparm
 *
 * @param parm
 * @returns 0 on success, !0 otherwise
 */
static int
get_sysparm(const char *parm, unsigned long *value)
{
	int rc = -1;
	char s[DR_BUF_SZ];
	FILE *f;

	f = fopen(LPARCFG_PATH, "r");
	if (f == NULL) {
		say(ERROR, "Could not open \"%s\"\n%s\n", LPARCFG_PATH,
		    strerror(errno));
		return -1;
	}

	while (fgets(s, sizeof(s), f)) {
		if (! strncmp(s, parm, strlen(parm))) {
			char *tmp = strchr(s, '=');
			if (tmp == NULL)
				break;

			tmp++;
			rc = 0;
			*value = strtoul(tmp, NULL, 0);
			break;
		}
	}

	fclose(f);
	if (rc)
		say(ERROR, "Error finding %s in %s\n", parm, LPARCFG_PATH);

	return rc;
}

/**
 * set_sysparm
 *
 * @param parm
 * @param val
 * @returns 0 on success, !0 otherwise
 */
static int
set_sysparm(const char *parm, int val)
{
	int rc = -1;
	FILE *f;

	f = fopen(LPARCFG_PATH, "w");
	if (f == NULL) {
		say(ERROR, "Could not open \"%s\"\n%s\n", LPARCFG_PATH,
		    strerror(errno));
		return -1;
	}

	say(DEBUG, "Updating sysparm %s to %d...", parm, val);
	rc = fprintf(f, "%s=%d\n", parm, val);

	fclose(f);

	say(DEBUG, "%s.\n", (rc == -1) ? "fail" : "success");
	return (rc == -1) ? -1 : 0;
}

struct sysparm_mapping {
	char	*drmgr_name;
	char	*linux_name;
};

static struct sysparm_mapping cpu_sysparm_table[] = {
	{
		.drmgr_name = "variable_weight",
		.linux_name = "capacity_weight"
	},
	{
		.drmgr_name = "ent_capacity",
		.linux_name = "partition_entitled_capacity"
	},
	{
		.drmgr_name = NULL,
		.linux_name = NULL
	}
};

static struct sysparm_mapping mem_sysparm_table[] = {
	{
		.drmgr_name = "variable_weight",
		.linux_name = "entitled_memory_weight"
	},
	{
		.drmgr_name = "ent_capacity",
		.linux_name = "entitled_memory"
	},
	{
		.drmgr_name = NULL,
		.linux_name = NULL
	}
};

/**
 * update_sysparm
 *
 * Update the indicated system parameter by the amount specified.
 * This requires us to establish the current value of the parameter,
 * since the kernel interface accepts only absolute values.
 *
 * @param parm
 * @returns 0 on success, !0 otherwise
 */
int update_sysparm(void)
{
	struct sysparm_mapping *sysparm_table;
	unsigned long curval;
	int i;
	char *linux_parm = NULL;
	
	/* Validate capability */
	if (usr_drc_type == DRC_TYPE_CPU) {
		if (! cpu_entitlement_capable()) {
			say(ERROR, "CPU entitlement capability is not enabled "
			    "on this platform.\n");
			return -1;
		}

		sysparm_table = cpu_sysparm_table;
	} else if (usr_drc_type == DRC_TYPE_MEM) {
		if (! mem_entitlement_capable()) {
			say(ERROR, "Memory entitlement capability is not "
			    "enabled on this platform.\n");
			return -1;
		}

		sysparm_table = mem_sysparm_table;
	} else {
		say(ERROR, "Invalid entitlement update type \"%d\" "
		    "specified.\n", usr_drc_type);
		return -1;
	}
	
	/* Convert the system parameter presented to drmgr into what is
	 * expected by the kernel.
	 */
	i = 0;
	while (sysparm_table[i].drmgr_name) {
		if (!strcmp(sysparm_table[i].drmgr_name, usr_p_option)) {
			linux_parm = sysparm_table[i].linux_name;
			break;
		}

		i++;
	}

	if (linux_parm == NULL) {
		say(ERROR, "The entitlement parameter \"%s\" is not "
		    "recognized\n", usr_p_option);
		return -1;
	}

	if ((get_sysparm(linux_parm, &curval)) < 0) {
		say(ERROR, "Could not get current system parameter value of "
		    "%s (%s)\n", linux_parm, usr_p_option);
		return -1;
	}

	if (usr_action == REMOVE) {
		if (usr_drc_count > curval) {
			say(ERROR, "Cannot reduce system parameter value %s by "
			    "more than is currently available.\nCurrent "
			    "value: %lx, asking to remove: %x\n",
			    usr_p_option, curval, usr_drc_count);
			return 1;
		}

		return set_sysparm(linux_parm, curval - usr_drc_count);
	} else {
		return set_sysparm(linux_parm, curval + usr_drc_count);
	}
}

int
cpu_dlpar_capable(void)
{
        DIR *d;
        struct dirent *de;
        struct stat sbuf;
	char fname[DR_PATH_MAX];
	char *cpu_dir = "/sys/devices/system/cpu";
	int capable = 1;

	say(ERROR, "Validating CPU DLPAR capability...");
	
	d = opendir(cpu_dir);
	if (d == NULL) {
		say(ERROR, "no.\n    opendir(\"%s\"): %s\n", cpu_dir,
		    strerror(errno));
		return 0;
	}

	while ((de = readdir(d)) != NULL) {
		if (strncmp(de->d_name, "cpu", 3))
			continue;
		
		/* Ensure this is a cpu directory, i.e. cpu0, and not a
		 * non-cpu directory, i.e. cpufreq
		 */
		if (!isdigit(de->d_name[3]))
			continue;

		sprintf(fname, "%s/%s/online", cpu_dir, de->d_name);
		
		if (stat(fname, &sbuf)) {
			say(ERROR, "no.\n    stat(\"%s\"): %s\n", fname,
			    strerror(errno));
			capable = 0;
		}

		say(ERROR, "yes.\n");
		break;
	}

	closedir(d);
	return capable;
}

static inline int
dlpar_capable(const char *type, const char *fname)
{
	struct stat sbuf;
	int capable = 1;

	say(ERROR, "Validating %s capability...", type);
	
	if (stat(fname, &sbuf)) {
		say(ERROR, "no.\n    stat(\"%s\"): %s\n", fname,
		    strerror(errno));
		capable = 0;
	} else {
		say(ERROR, "yes.\n");
	}

	return capable;
}

int
mem_dlpar_capable(void)
{
	return dlpar_capable("Memory DLPAR",
			     "/sys/devices/system/memory/block_size_bytes");
}

int
slot_dlpar_capable(void)
{
	return dlpar_capable("I/O DLPAR", add_slot_fname);
}

int
phb_dlpar_capable(void)
{
	return dlpar_capable("PHB DLPAR", add_slot_fname);
}

int
pmig_capable(void)
{
	return dlpar_capable("partition migration",
			     "/proc/device-tree/ibm,migratable-partition");
}

int
phib_capable(void)
{
	return dlpar_capable("partition hibernation", "/sys/devices/system/power/hibernate");
}

int
slb_resize_capable(void)
{
	unsigned long value;
	int rc;

	rc = get_sysparm("slb_size", &value);
	if (rc == -1)
		return 0;

	return 1;
}

int
hea_dlpar_capable(void)
{
	return dlpar_capable("HEA DLPAR", HEA_ADD_SLOT);
}

int
cpu_entitlement_capable(void)
{
	unsigned long value;
	int rc;

	rc = get_sysparm("partition_entitled_capacity", &value);
	if (rc == -1)
		return 0;
	
	return 1;
}

int
mem_entitlement_capable(void)
{
	unsigned long value;
	int rc;

	rc = get_sysparm("entitled_memory", &value);
	if (rc == -1)
		return 0;

	return 1;
}

void
print_dlpar_capabilities(void)
{
	int cpu_dlpar, mem_dlpar, slot_dlpar, phb_dlpar, hea_dlpar;
	int pmig, phib, slb_resize;
	int cpu_entitled, mem_entitled;

	cpu_dlpar = cpu_dlpar_capable();
	mem_dlpar = mem_dlpar_capable();
	slot_dlpar = slot_dlpar_capable();
	phb_dlpar = phb_dlpar_capable();
	hea_dlpar = hea_dlpar_capable();

	pmig = pmig_capable();
	phib = phib_capable();
	slb_resize = slb_resize_capable();

	cpu_entitled = cpu_entitlement_capable();
	mem_entitled = mem_entitlement_capable();

	printf("cpu_dlpar=%s,mem_dlpar=%s,slot_dlpar=%s,phb_dlpar=%s,"
	       "hea_dlpar=%s,pmig=%s,cpu_entitlement=%s,mem_entitlement=%s,"
	       "slb_resize=%s,phib=%s\n",
	       (cpu_dlpar ? "yes" : "no"), (mem_dlpar ? "yes" : "no"),
	       (slot_dlpar ? "yes" : "no"), (phb_dlpar ? "yes" : "no"),
	       (hea_dlpar ? "yes" : "no"), (pmig ? "yes" : "no"),
	       (cpu_entitled ? "yes" : "no"), (mem_entitled ? "yes" : "no"),
	       (slb_resize ? "yes" : "no"), (phib ? "yes" : "no"));
}

/**
 * ams_balloon_active
 * @brief Determines if AMS and memory ballooning is enabled
 *
 * @returns 1 if ballooning is active, 0 if AMS or ballooning is inactive
 */
int ams_balloon_active(void)
{
	/* CMM's loaned_kb file only appears when AMS is enabled */
	char *ams_enabled = "/sys/devices/system/cmm/cmm0/loaned_kb";
	char *cmm_param_path = "/sys/module/cmm/parameters";
	struct stat sbuf;
	static int is_inactive = 1;
	static int ams_checked = 0;

	if (!ams_checked) {
		if (!stat(ams_enabled, &sbuf) && !stat(cmm_param_path, &sbuf))
			get_int_attribute(cmm_param_path, "disable",
					  &is_inactive, sizeof(is_inactive));

		say(DEBUG, "AMS ballooning %s active\n",
		    is_inactive?"is not":"is");
		ams_checked = 1;
	}

	return !is_inactive;
}

int is_display_adapter(struct dr_node *node)
{
	return !strncmp(node->drc_type, "display", 7);
}

/**
 * kernel_dlpar_exists
 * @brief determine if the sysfs file to do in-kernel dlpar exists
 *
 * @returns 1 if in-kernel dlpar exists, 0 otherwise.
 */
int kernel_dlpar_exists(void)
{
	struct stat sbuf;
	char buf[64];

	if (stat(SYSFS_DLPAR_FILE, &sbuf))
		return 0;

	if (!get_str_attribute(SYSFS_DLPAR_FILE, NULL, buf, sizeof(buf))) {
		switch (usr_drc_type) {
		case DRC_TYPE_MEM:
			if (strstr(buf, "memory"))
				return 1;
			break;
		case DRC_TYPE_CPU:
			if (strstr(buf, "cpu"))
				return 1;
			break;
		case DRC_TYPE_PCI:
		case DRC_TYPE_PHB:
		case DRC_TYPE_SLOT:
			if (strstr(buf, "dt"))
				return 1;
			break;
		default:
			return 0;
		}
	} else if (usr_drc_type == DRC_TYPE_MEM)
		return 1;

	return 0;
}

/**
 * do_kernel_dlpar_common
 * @brief Use the in-kernel dlpar capabilities to perform the requested
 *        dlpar operation.
 *
 * @param cmd command string to write to sysfs
 * @silent_error if not 0, error is not reported, it's up to the caller
 * @returns 0 on success, !0 otherwise
 */
int do_kernel_dlpar_common(const char *cmd, int cmdlen, int silent_error)
{
	static int fd = -1;
	int rc;

	say(DEBUG, "Initiating kernel DLPAR \"%s\"\n", cmd);

	/* write to file */
	if (fd == -1) {
		fd = open(SYSFS_DLPAR_FILE, O_WRONLY | O_CLOEXEC);
		if (fd < 0) {
			say(ERROR,
			    "Could not open %s to initiate DLPAR request\n",
			    SYSFS_DLPAR_FILE);
			return -1;
		}
	}

	rc = write(fd, cmd, cmdlen);
	if (rc <= 0) {
		if (silent_error)
			return (errno == 0) ? -1 : -errno;
		/* write does not set errno for rc == 0 */
		say(ERROR, "Failed to write to %s: %s\n", SYSFS_DLPAR_FILE,
		    (rc == 0) ? "wrote 0 bytes" : strerror(errno));
		return -1;
	}

	say(DEBUG, "Success\n");
	return 0;
}

enum drc_type to_drc_type(const char *arg)
{
	enum drc_type i;

	for (i = DRC_TYPE_NONE + 1; i < ARRAY_SIZE(drc_type_str); i++) {
		if (!strcmp(arg, drc_type_str[i]))
			return i;
	}

	return DRC_TYPE_NONE;
}

static int run_one_hook(enum drc_type drc_type, enum drmgr_action action,
			enum hook_phase phase, const char *drc_count_str,
			const char *name)
{
	int rc;
	pid_t child;

	fflush(NULL);
	child = fork();
	if (child == -1) {
		say(ERROR, "Can't fork to run a hook: %s\n", strerror(errno));
		return -1;
	}

	if (child) {
		/* Father side */
		while (waitpid(child, &rc, 0) == -1) {
			if (errno == EINTR)
				continue;
			say(ERROR, "waitpid error: %s\n", strerror(errno));
			return -1;
		}

		if (WIFSIGNALED(rc)) {
			say(INFO, "hook '%s' terminated by signal %d\n",
			    name, WTERMSIG(rc));
			rc = 1;
		} else {
			rc = WEXITSTATUS(rc);
			say(INFO, "hook '%s' exited with status %d\n",
			    name, rc);
		}
		return rc;
	}


	/* Child side */
	say(DEBUG, "Running hook '%s' for phase %s (PID=%d)\n",
	    name, hook_phase_name[phase], getpid());

	if (chdir("/")) {
		say(ERROR, "Can't change working directory to / : %s\n",
		    strerror(errno));
		exit(255);
	}

	if (clearenv() ||
	    setenv("DRC_TYPE", drc_type_str[drc_type], 1) ||
	    setenv("DRC_COUNT", drc_count_str, 1) ||
	    setenv("ACTION", hook_action_name[action], 1) ||
	    setenv("PHASE", hook_phase_name[phase], 1)) {
		say(ERROR, "Can't set environment variables: %s\n",
		    strerror(errno));
		exit(255);
	}

	execl(name, name, (char *)NULL);
	say(ERROR, "Can't exec hook %s : %s\n", strerror(errno));
	exit(255);
}

static int is_file_or_link(const struct dirent *entry)
{
	if ((entry->d_type == DT_REG) || (entry->d_type == DT_LNK))
		return 1;
	return 0;
}

/*
 * Run all executable hooks found in a given directory.
 * Return 0 if all run script have returned 0 status.
 */
int run_hooks(enum drc_type drc_type, enum drmgr_action action,
	      enum hook_phase phase, int drc_count)
{
	int rc = 0, fdd, num, i;
	DIR *dir;
	struct dirent **entries = NULL;
	char *drc_count_str;

	/* Sanity check */
	if (drc_type <= DRC_TYPE_NONE || drc_type >= ARRAY_SIZE(drc_type_str)) {
		say(ERROR, "Invalid DRC TYPE detected (%d)\n", drc_type);
		return -1;
	}

	if (phase < HOOK_CHECK || phase > HOOK_POST) {
		say(ERROR, "Invalid hook phase %d\n", phase);
		return -1;
	}

	dir = opendir(DR_SCRIPT_DIR);
	if (dir == NULL) {
		if (errno == ENOENT)
			return 0;
		say(ERROR, "Can't open %s: %s\n", DR_SCRIPT_DIR,
		    strerror(errno));
		return -1;
	}

	fdd = dirfd(dir);
	num = scandirat(fdd, drc_type_str[drc_type], &entries,
			is_file_or_link, versionsort);
	closedir(dir);

	if (asprintf(&drc_count_str, "%d", drc_count) == -1) {
		say(ERROR, "Can't allocate new string : %s", strerror(errno));
		free(entries);
		return -1;
	}

	for (i = 0; i < num; i++) {
		struct stat st;
		struct dirent *entry = entries[i];
		char *name;

		if (asprintf(&name, "%s/%s/%s", DR_SCRIPT_DIR,
			     drc_type_str[drc_type], entry->d_name) == -1) {
			say(ERROR,
			    "Can't allocate filename string (%zd bytes)\n",
			    strlen(DR_SCRIPT_DIR) + 1 +
			    strlen(drc_type_str[drc_type]) + 1 +
			    strlen(entry->d_name) + 1);
			rc = 1;
			free(entry);
			continue;
		}

		/*
		 * Report error only in the case the hook itself fails.
		 * Any other error (file is not executable etc.) is ignored.
		 */
		if (stat(name, &st))
			say(WARN, "Can't stat file %s: %s\n",
			    name, strerror(errno));
		else if (S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR) &&
			 run_one_hook(drc_type, action, phase, drc_count_str,
				      name))
			rc = 1;

		free(name);
		free(entry);
	}

	free(drc_count_str);
	free(entries);
	return rc;
}
