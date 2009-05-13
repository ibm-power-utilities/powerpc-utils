/**
 * @file drmig_chrp_pmig.c
 *
 * Copyright (C) IBM Corporation
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <librtas.h>
#ifdef HAVE_SERVICELOG
#include <servicelog-1/servicelog.h>
#endif
#include "dr.h"
#include "ofdt.h"
#include "drpci.h"

struct pmap_struct {
	struct pmap_struct	*next;
	unsigned int		phandle;
	int			ibmphandle;
	char			*name;
};

static struct pmap_struct *plist;

static char *usagestr = "-m -p {check | pre} -s <stream_id>";

/**
 * pmig_usage
 *
 */
void
pmig_usage(char **pusage)
{
	*pusage = usagestr;
}
/**
 * add_phandle
 *
 * @param name
 * @param phandle
 * @param ibmphandle
 */
static void
add_phandle(char *name, unsigned int phandle, int ibmphandle)
{
	struct pmap_struct *pm = zalloc(sizeof(struct pmap_struct));

	if (strlen(name) == 0)
		name = "/";

	pm->name = zalloc(strlen(name)+strlen(OFDT_BASE)+1);
	sprintf(pm->name, "%s%s", OFDT_BASE, name);

	pm->phandle = phandle;
	pm->ibmphandle = ibmphandle;
	pm->next = plist;
	plist = pm;
}

/**
 * find_phandle
 *
 * @param ph
 * @returns
 */
static char *
find_phandle(unsigned int ph)
{
	struct pmap_struct *pms = plist;

	while (pms && pms->phandle != ph)
		pms = pms->next;

	return pms ? pms->name : NULL;
}

/**
 * add_phandles
 *
 * @param parent
 * @param p
 * @returns
 */
static int
add_phandles(char *parent, char *p)
{
	DIR *d;
	struct dirent *de;
	char path[PATH_MAX];
	unsigned int phandle;
	char *pend;
	FILE *fd;

	strcpy(path,parent);
	if (strlen(p)) {
		strcat(path,"/");
		strcat(path,p);
	}
	pend = path + strlen(path);

	d = opendir(path);
	if (d == NULL) {
		perror(path);
		return 1;
	}

	while ((de = readdir(d))) {
		if ((de->d_type == DT_DIR) &&
		    (strcmp(de->d_name,".")) &&
		    (strcmp(de->d_name,"..")))
			add_phandles(path,de->d_name);
	}

	strcpy(pend,"/linux,phandle");
	fd = fopen(path,"r");
	if (fd != NULL) {
		if (fread(&phandle,sizeof(phandle),1,fd) != 1) {
			perror(path);
			dbg("Error reading phandle data!\n");
			return 1;
		}
		*pend = '\0';
		add_phandle(path + strlen("/proc/device-tree"),phandle, 0);
		fclose(fd);
	}

	strcpy(pend,"/ibm,phandle");
	fd = fopen(path,"r");
	if (fd != NULL) {
		if (fread(&phandle,sizeof(phandle),1,fd) != 1) {
			perror(path);
			dbg("Error reading phandle data!\n");
			return 1;
		}
		*pend = '\0';
		add_phandle(path + strlen("/proc/device-tree"), phandle, 1);
		fclose(fd);
	}

	closedir(d);
	return 0;
}

/**
 * do_update
 *
 * @param cmd
 * @param len
 * @returns 0 on success, !0 otherwise
 */
static int
do_update(char *cmd, int len)
{
	int rc;
	int i, fd;

	fd = open(OFDTPATH, O_WRONLY);
	if (fd <= 0) {
		err_msg("Failed to open %s: %s\n", OFDTPATH, strerror(errno));
		rc = errno;
		return rc;
	}

	dbg("len %d\n", len);

	if ((rc = write(fd, cmd, len)) != len)
		dbg("Error writing to ofdt file! rc %d errno %d\n", rc, errno);

	for (i = 0; i < len; i++) {
		if (! isprint(cmd[i]))
			cmd[i] = '.';
		if (isspace(cmd[i]))
			cmd[i] = ' ';
	}
	cmd[len-1] = 0x00;

	dbg("<%s>\n", cmd);

	close(fd);
	return rc;
}

/**
 * del_node
 *
 * @param phandle
 */
static void
del_node(unsigned int phandle)
{
	char *name = find_phandle(phandle);
	char delcmd[128] = "remove_node ";

	if (name == NULL)
		dbg("Delete node error: Invalid phandle %8.8x", phandle);
	else {
		strcat(delcmd,name);
		do_update(delcmd, strlen(delcmd));
	}
}

/**
 * update_properties
 *
 * @param phandle
 * @returns 0 on success, !0 otherwise
 */
static int
update_properties(unsigned int phandle)
{
	int rc;
	char cmd[DR_PATH_MAX];
	char *longcmd = NULL;
	char *newcmd;
	int cmdlen = 0;
	int proplen = 0;
	unsigned int wa[1024];
	unsigned int *op;
	unsigned int nprop;
	unsigned int vd;
	int lenpos = 0;
	char *pname;
	unsigned int i;
	int more = 0;
	char *name = find_phandle(phandle);

	memset(wa, 0x00, 16);
	wa[0] = phandle;

	do {
		dbg("about to call rtas_update_properties.  work area:\n"
		    "phandle %8.8x, node %s\n"
		    " %8.8x %8.8x %8.8x %8.8x\n",
		    phandle, name ? name : "NULL", wa[0], wa[1], wa[2], wa[3]);

		rc = rtas_update_properties((char *)wa, 1);
		if (rc && rc != 1) {
			dbg("Error %d from rtas_update_properties()\n", rc);
			return 1;
		}

		dbg("successful rtas_update_properties (more %d)\n", rc);

		op = wa+4;
		nprop = *op++;

		for (i = 0; i < nprop; i++) {
			pname = (char *)op;
			op = (unsigned int *)(pname + strlen(pname) + 1);
			vd = *op++;

			switch (vd) {
			    case 0x00000000:
				dbg("%s - name only property %s\n",
				    name, pname);
				break;

			    case 0x80000000:
				dbg("%s - delete property %s\n", name, pname);
				sprintf(cmd,"remove_property %u %s",
					phandle, pname);
				do_update(cmd, strlen(cmd));
				break;

			default:
				if (vd & 0x80000000) {
					dbg("partial property!\n");
					/* twos compliment of length */
					vd = ~vd + 1;
					more = 1;
				}
				else {
					more = 0;
				}

				dbg("%s - updating property %s length %d\n",
				    name, pname, vd);

				/* See if we have a partially completed
				 * command
				 */
				if (longcmd) {
					newcmd = zalloc(cmdlen + vd);
					memcpy(newcmd, longcmd, cmdlen);
					free(longcmd);
					longcmd = newcmd;
				}
				else {
					longcmd = zalloc(vd+128);
					/* Build the command with a length
					 * of six zeros 
					 */
					lenpos = sprintf(longcmd, 
							 "update_property %u "
							 "%s ", phandle, 
							 pname);
					strcat(longcmd, "000000 ");
					cmdlen = strlen(longcmd);
				}

				memcpy(longcmd + cmdlen, op, vd);
				cmdlen += vd;
				proplen += vd;

				if (! more) {
				        /* Now update the length to its actual
					 * value  and do a hideous fixup of 
					 * the new trailing null 
					 */
					sprintf(longcmd+lenpos,"%06d",proplen);
					longcmd[lenpos+6] = ' ';

					do_update(longcmd, cmdlen);
					free(longcmd);
					longcmd = NULL;
					cmdlen = 0;
					proplen = 0;
				}

				op = (unsigned int *)(((char *)op) + vd);
			}
		}
	} while (rc == 1);

	return 0;
}

/**
 * add_new_node
 *
 * @param phandle
 * @param drcindex
 */
static void
add_new_node(unsigned int phandle, unsigned int drcindex)
{
	char *path;
	int rtas_rc;
	struct of_node *new_nodes;/* nodes returned from configure_connector */

	new_nodes = configure_connector(drcindex);
	
	path = find_phandle(phandle);
	if (path == NULL) {
		dbg("Cannot find pnahdle %x\n", phandle);
		return;
	}

	rtas_rc = add_device_tree_nodes(path, new_nodes);
	if (rtas_rc)
		dbg("add_device_tree_nodes failed at %s\n", path);
}

/**
 * del_nodes
 *
 * @param op
 * @param n
 */
static void
del_nodes(unsigned int *op, unsigned int n)
{
	unsigned int i, phandle;

	for (i = 0; i < n; i++) {
		phandle = *op++;
		dbg("Delete node with phandle %8.8x\n", phandle);
		del_node(phandle);
	}
}

/**
 * update_nodes
 *
 * @param op
 * @param n
 */
static void
update_nodes(unsigned int *op, unsigned int n)
{
	unsigned int i, phandle;

	for (i = 0; i < n; i++) {
		phandle = *op++;
		dbg("Update node with phandle %8.8x\n", phandle);
		update_properties(phandle);
	}
}

/**
 * add_nodes
 *
 * @param op
 * @param n
 */
static void
add_nodes(unsigned int *op, unsigned int n)
{
	unsigned int i, pphandle, drcindex;

	for (i = 0; i < n; i += 2) {
		pphandle = *op++;
		drcindex = *op++;
		dbg("Add node with parent phandle %8.8x and drc index %8.8x\n",
		    pphandle, drcindex);
		add_new_node(pphandle, drcindex);
	}
}

/**
 * devtree_update
 *
 */
static void
devtree_update(void)
{
	int rc;
	unsigned int wa[1024];
	unsigned int *op;

	dbg("Updating device_tree\n");
	if (add_phandles("/proc/device-tree",""))
		return;

	/* First 16 bytes of work area must be initialized to zero */
	memset(wa, 0x00, 16);

	do {
		rc = rtas_update_nodes((char *)wa, 1);
		if (rc && rc != 1) {
			dbg("Error %d from rtas_update_nodes()\n", rc);
			return;
		}

		dbg("successful rtas_update_nodes (more %d)\n", rc);

		op = wa+4;

		while (*op & 0xFF000000) {
			unsigned int i;
			dbg("op %p, *op %8.8x\n", op, *op);

			for (i = 0; i < (*op & 0x00FFFFFF); i++)
				dbg("   %8.8x\n",op[i+1]);

			switch (*op & 0xFF000000) {
			    case 0x01000000:
				del_nodes(op+1, *op & 0x00FFFFFF);
				break;

			    case 0x02000000:
				update_nodes(op+1, *op & 0x00FFFFFF);
				break;

			    case 0x03000000:
				add_nodes(op+1, *op & 0x00FFFFFF);
				break;

			    case 0x00000000:
				/* End */
				break;

			    default:
				dbg("Unknown update_nodes op %8.8x\n", *op);
			}
			op += 1 + (*op & 0x00FFFFFF);
		}
	} while (rc == 1);

	dbg("leaving\n");
}

#ifdef HAVE_SERVICELOG
/**
 * servicelog_update
 * @brief Logs a migration event in servicelog, if it is installed
 *
 * @param sys_src serial number of the source machine
 */
static void
servicelog_update(char *sys_src)
{
	struct servicelog *slog;
	struct sl_event event;
	char msg[128], refcode[64], sys_dest[20];
	int rc;
	uint64_t event_id;

	/* Build the servicelog event */
	memset(&event, 0, sizeof(event));
	event.type = SL_TYPE_BASIC;
	event.time_event = time(NULL);
	event.severity = SL_SEV_INFO;

	snprintf(refcode, 9, "#MIGRATE");
	event.refcode = refcode;

	get_str_attribute(OFDT_BASE, "system-id", sys_dest, 20);
	snprintf(msg, 128, "Partition migration completed.  Source: %s "
		 "Destination: %s", sys_src, sys_dest); /* 56 */
	event.description = msg;

	rc = servicelog_open(&slog, 0);
	if (rc) {
		dbg("Couldn't open the servicelog database: %s\n",
		    servicelog_error(slog));
		return;
	}

	rc = servicelog_event_log(slog, &event, &event_id);
	if (rc) {
		dbg("Couldn't log an event to the servicelog database: %s\n",
		    servicelog_error(slog));
	}

	servicelog_close(slog);
}
#endif

int
valid_pmig_options(struct options *opts)
{
	if (opts->action != MIGRATE) {
		err_msg("The only valid action is -m\n");
		return -1;
	}

	if (opts->p_option  == NULL) {
		err_msg("A command must be specified\n");
		return -1;
	}

	return 0;
}
	
int
drmig_chrp_pmig(struct options *opts)
{
	int rc;
	char *cmd = opts->p_option;
	char sys_src[20];
	uint64_t stream_val;

	/* Ensure that this partition is migratable/mobile */
	if (! pmig_capable()) {
		fprintf(stderr, "drmig_chrp_pmig: Partition Mobility is not "
			"supported on this kernel.\n");
		return -1;
	}

	/* Today we do no pre-checks for migratability. The only check
	 * we could do is whether the "ibm,suspend-me" RTAS call exists.
	 * But if it doesn't, the firmware level doesn't support migration,
	 * in which case why the heck are we being invoked anyways.
	 */
	if (strcmp(cmd, "check") == 0) {
		dbg("check: Nothing to do...\n");
		return 0;
	}

	/* The only other command is pre, any other command is invalid */
	if (strcmp(cmd, "pre")) {
		dbg("Invalid command \"%s\" specified\n", cmd);
		return 1;
	}

	if (opts->usr_drc_name == NULL) {
		err_msg("No streamid specified\n");
		return -1;
	}

	errno = 0;
	stream_val = strtoull(opts->usr_drc_name, NULL, 16);
	if (errno != 0) {
		err_msg("Invalid streamid specified: %s\n", strerror(errno));
		return -1;
	}
	
	/* Get the ID of the original system, for later logging */
	get_str_attribute(OFDT_BASE, "system-id", sys_src, 20);

	/* Now do the actual migration */
	do {
		dbg("about to issue ibm,suspend-me(%llx)\n", stream_val);
	
		rc = rtas_suspend_me(stream_val);
		if (rc == NOT_SUSPENDABLE) {
			dbg("rtas_suspend_me() returned NOT_SUSPENDABLE, "
			    "sleeping...\n");
			sleep(1);
		} else
			dbg("ibm,suspend-me() returned %d\n", rc);

	} while (rc == NOT_SUSPENDABLE);

	syslog(LOG_LOCAL0 | LOG_INFO, "drmgr: ibm,suspend-me rc %d\n", rc);
	if (rc)
		return rc;

	devtree_update();
#ifdef HAVE_SERVICELOG
	servicelog_update(sys_src);
#endif

	return 0;
}
