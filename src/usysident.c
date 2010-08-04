/**
 * @file usysident.c
 * @brief usysident/usysattn/usysfault 
 */
/**
 * @mainpage usysident/usysattn documentation
 * @section Copyright
 * Copyright (c) 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @section Overview
 * Manipulate LEDs on IBM ppc64 platforms.
 *
 * @section usysident
 * The usysident utility is used to view and manipulate the indicators
 * (LEDs) that identify certain devices on PowerPC-64 machines.  These
 * identifiers are specified by location code; location codes can be retrieved
 * by running lscfg.
 *
 * When run without arguments, usysident will print a list of all of the
 * identification indicators on the system along with their current status (on
 * or off).  The <tt>-l</tt> or <tt>-d</tt> options can be used to specify a
 * particular indicator, by location code or logical device name respectively.
 * If <tt>-l</tt> or <tt>-d</tt> is the only argument, the status of that 
 * indicator will be printed.  If the <tt>-s</tt> argument is specified in 
 * addition, the indicator may be turned on or off.
 *
 * @section usysattn
 * The usysattn utility is used to view and manipulate the attention
 * indicators (LEDs) on PowerPC-64 machines.  These indicators are turned on
 * automatically when a serviceable event is received.  These identifiers are
 * specified by location code.
 * 
 * When run without arguments, usysattn will print a list of all of the
 * attention indicators on the system along with their current status (on or
 * off).  The <tt>-l</tt> option can be used to specify a particular indicator.
 * If <tt>-l</tt> is the only argument, the status of that indicator will be
 * printed.  If <tt>-s normal</tt> is specified along with the <tt>-l</tt>
 * argument, the indicator will be turned off.
 * 
 * <b>NOTE:</b> Ensure that there are no outstanding service actions to be 
 * taken on this system before turning off the system attention indicators.
 *
 * @author Michael Strosaker <strosake@us.ibm.com>
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <librtas.h>
#include <ctype.h>
#include <errno.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

#include "librtas_error.h"

#define BUF_SIZE	4096
#define IDENT_INDICATOR	9007	/**< usysident indicator */
#define ATTN_INDICATOR	9006	/**< usysattn indicator */

#define TYPE_RTAS	1
#define TYPE_SES	2

#define INDICATOR_TYPE(x)	(((x) == IDENT_INDICATOR) ? "identification" \
							  : "attention")

/**
 * @struct loc_code
 * @brief location code definition
 *
 * The order of the fields in loc_code is important! 
 */
struct loc_code
{
	/* These first three fields must be first, and in this order */
	/* These fields are used for RTAS-controlled indicators */
	uint32_t length;	/**< length includes null terminator (RTAS) */
	char code[120];		/**< location code of identifier */
	uint32_t index;		/**< RTAS index, if an RTAS indicator */

	/* Fields after this point can be reordered */

	int type;		/**< one of TYPE_ defines */
	struct loc_code *next;

	/* These fields are for hard drive indicators */
	char dev[8];		/**< sd* device name */
	char file[32];		/**< /dev/sg* file for owning enclosure */
	int host;		/**< host from HBTL */
	int bus;		/**< bus from HBTL */
	int target;		/**< target from HBTL */
};

/**
 * @struct sg_map
 * @brief contains the data from on line of "sg_map -x" output
 */
struct sg_map
{
	char generic[32];
	int host;
	int bus;
	int target;
	int lun;
	int type;
	char dev[32];
	struct sg_map *next;
};

/**
 * @struct sense_data_t
 * @brief sense data structure, as defined in SCSI specification
 */
struct sense_data_t
{
	uint8_t error_code;
	uint8_t segment_numb;
	uint8_t sense_key;
	uint8_t info[4];
	uint8_t add_sense_len;
	uint8_t cmd_spec_info[4];
	uint8_t add_sense_code;
	uint8_t add_sense_code_qual;
	uint8_t field_rep_unit_code;
	uint8_t sense_key_spec[3];
	uint8_t add_sense_bytes[0];
};

/**
 * @struct ses_drive_elem_status
 * @brief SES drive status
 */
struct ses_drive_elem_status
{
	uint8_t select:1;
	uint8_t predictive_fault:1;
	uint8_t reserved:1;
	uint8_t swap:1;
	uint8_t status:4;
	uint8_t reserved2:4;
	uint8_t scsi_id:4;
	uint8_t reserved3:4;
	uint8_t insert:1;
	uint8_t remove:1;
	uint8_t identify:1;
	uint8_t reserved4:1;
	uint8_t reserved5:1;
	uint8_t fault_requested:1;
	uint8_t fault_sensed:1;
	uint8_t reserved6:5;
};

/**
 * @struct ses_encl_status_ctl_pg
 * @brief SES diagnostics page
 */
struct ses_encl_status_ctl_pg
{
	uint8_t page_code;
	uint8_t health_status;
	uint16_t byte_count;
	uint8_t reserved1[4];
	uint8_t overall_status_select:1;
	uint8_t overall_status_predictive_fault:1;
	uint8_t overall_status_reserved:1;
	uint8_t overall_status_swap:1;
	uint8_t overall_status_reserved2:4;
	uint8_t overall_status_reserved3;
	uint8_t overall_status_reserved4:4;
	uint8_t overall_status_insert:1;
	uint8_t overall_status_remove:1;
	uint8_t overall_status_identify:1;
	uint8_t overall_status_reserved5:1;
	uint8_t overall_status_reserved6:2;
	uint8_t overall_status_fault_requested:1;
	uint8_t overall_status_reserved7:4;
	uint8_t overall_status_disable_resets:1;
	struct ses_drive_elem_status elem_status[15];
};

#ifdef DEBUG
/**
 * dump_raw_data
 * @brief dump the raw contents of a buffer
 *
 * @param data buffer to dump
 * @param data_len length of the data buffer
 */ 
void 
dump_raw_data(char *data, int data_len)
{
	int i, j;
	int offset = 0;
	char *h, *a;
	char *end = data + data_len;

	h = a = data;

	while (h < end) {
		printf("0x%08x  ", offset);
		offset += 16;

		for (i = 0; i < 4; i++) {
			for (j = 0; j < 4; j++) {
				if (h <= end)
					printf("%02x", *h++);
				else
					printf("  ");
			}
			printf(" ");
		}

		printf("|");
		for (i = 0; i < 16; i++) {
			if (a <= end) {
				if ((*a >= ' ') && (*a <= '~'))
					printf("%c", *a);
				else
					printf(".");
				a++;
			} else
				printf(" ");
		}
		printf("|\n");
	}
}
#endif

/**
 * truncate_loc_code
 * @brief truncate the last few characters of a location code
 *
 * Truncates the last few characters off of a location code; if an
 * indicator doesn't exist at the orignal location, perhaps one exists
 * at a location closer to the CEC.
 *
 * @param loccode location code to truncate
 * @return 1 - successful truncation
 * @return 0 - could not be truncated further
 */
int
truncate_loc_code (char *loccode)
{
	int i;

	for (i = strlen(loccode) - 1; i >= 0; i--) {
		if (loccode[i] == '-') {
			loccode[i] = '\0';
			return 1;	/* successfully truncated */
		}
	}
	return 0;	/* could not be further truncated */
}

/**
 * print_usage
 * @brief print the usage statement
 *
 * @param cmd command we are running
 */
void
print_usage (char *cmd)
{
	if (strstr(cmd, "usysident"))
		printf("Usage: %s"
			" [-s {normal|identify}] [-l location_code]\n",
			cmd);
	else
		printf("Usage: %s"
			"  [-s normal] [-l location_code]\n", cmd);
}

/**
 * get_rtas_sensor
 * @brief retrieve a sensor value from rtas
 * 
 * Call the rtas_get_sensor or rtas_get_dynamic_sensor librtas calls,
 * depending on whether the index indicates that the sensor is dynamic.
 *
 * @param indicator identification or attention indicator
 * @param loc location code of the sensor
 * @param state return location for the sensor state
 * @return rtas call return code
 */
int
get_rtas_sensor (int indicator, struct loc_code *loc, int *state)
{
	int rc;
	char err_buf[40];

	if (loc->index == 0xFFFFFFFF)
		rc = rtas_get_dynamic_sensor (indicator,
				(void *)loc, state);
	else
		rc = rtas_get_sensor (indicator, loc->index, state);

	if (rc == -1) {
		fprintf(stderr,
			"Hardware error retrieving the indicator at %s\n",
			loc->code);
		return -1;
	}
	else if (rc == -3) {
		fprintf(stderr,
			"The indicator at %s is not implemented.\n",
			loc->code);
		return -2;
	}
	else if (rc != 0) {
		librtas_error(rc, err_buf, 40);

		fprintf(stderr, "Could not get %ssensor %s indicators,\n%s.\n",
			(loc->index == 0xFFFFFFFF) ? "dynamic " : "",
			INDICATOR_TYPE(indicator), err_buf);

		return -3;
	}

	return rc;
}

/**
 * set_rtas_indicator
 * @brief set an rtas indicator
 * 
 * Call the rtas_set_indicator or rtas_set_dynamic_indicator librtas calls,
 * depending on whether the index indicates that the indicator is dynamic.
 *
 * @param indicator identification or attention indicator
 * @param loc location code of rtas indicator
 * @param new_value value to update indicator to
 * @return rtas call return code
 */
int
set_rtas_indicator (int indicator, struct loc_code *loc, int new_value)
{
	int rc;
	char err_buf[40];

	if (loc->index == 0xFFFFFFFF)
		rc = rtas_set_dynamic_indicator(indicator,
			new_value, (void *)loc);
	else
		rc = rtas_set_indicator(indicator, loc->index, new_value);

	if (rc == -1) {
		fprintf(stderr,
			"Hardware error retrieving the indicator at %s\n",
			loc->code);
		return -1;
	}
	else if (rc == -3) {
		fprintf(stderr,
			"The indicator at %s is not implemented.\n",
			loc->code);
		return -2;
	}
	else if ( rc!= 0) {
		librtas_error(rc, err_buf, 40);

		fprintf(stderr, "Could not set %ssensor %s indicator,\n%s.\n",
			(loc->index == 0xFFFFFFFF) ? "dynamic " : "",
			INDICATOR_TYPE(indicator), err_buf);

		return -3;
	}

	return rc;
}

/**
 * get_diagnostic_page
 * @brief Make the necessary sg ioctl() to retrieve a diagnostic page
 *
 * @todo A lot of code is common between send_ and get_; should merge the two
 *
 * @param fd a file descriptor to the appropriate /dev/sg<x> file
 * @param diag_page the page to be retrieved
 * @param buf a buffer to contain the contents of the diagnostic page
 * @param buf_len the length of the previous parameter
 * @return 0 on success, -EIO on invalid I/O status, or CHECK_CONDITION
 */
int
get_diagnostic_page(int fd, uint8_t diag_page, void *buf, int buf_len)
{
	uint8_t scsi_cmd_buf[16] = {
		RECEIVE_DIAGNOSTIC,
		0x01,			/* set PCV bit to 1 */
		diag_page,		/* page to be retrieved */
		(buf_len >> 8) & 0xff,	/* most significant byte */
		buf_len & 0xff,		/* least significant byte */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct sense_data_t sense_data;
	sg_io_hdr_t hdr;
	int i, rc;

	for (i = 0; i < 3; i++) {
		memset(&hdr, 0, sizeof(hdr));
		memset(&sense_data, 0, sizeof(struct sense_data_t));

		hdr.interface_id = 'S';
		hdr.dxfer_direction = SG_DXFER_FROM_DEV;
		hdr.cmd_len = 16;
		hdr.mx_sb_len = sizeof(sense_data);
		hdr.iovec_count = 0;	/* scatter gather not necessary */
		hdr.dxfer_len = buf_len;
		hdr.dxferp = buf;
		hdr.cmdp = scsi_cmd_buf;
		hdr.sbp = (unsigned char *)&sense_data;
		hdr.timeout = 120 * 1000;	/* set timeout to 2 minutes */
		hdr.flags = 0;
		hdr.pack_id = 0;
		hdr.usr_ptr = 0;

		rc = ioctl(fd, SG_IO, &hdr);

		if (rc == 0) {
			if (hdr.masked_status == CHECK_CONDITION) {
				rc = CHECK_CONDITION;
				if (sense_data.sense_key == ILLEGAL_REQUEST) {
					printf("Oh, no!  Illegal request!!\n");
				}
			}
			else if (hdr.host_status || hdr.driver_status) {
				rc = -EIO;
			}
			else
				break;
		}

		if (hdr.host_status == 1) {
			break;
		}
	}

	return rc;
}

/**
 * send_diagnostic_page
 * @brief Make the necessary sg ioctl() to send a diagnostic page
 *
 * @todo A lot of code is common between send_ and get_; should merge the two
 *
 * @param fd a file descriptor to the appropriate /dev/sg<x> file
 * @param buf a buffer containing the contents of the diagnostic page
 * @param buf_len the length of the previous parameter
 * @return 0 on success, -EIO on invalid I/O status, or CHECK_CONDITION
 */
int
send_diagnostic_page(int fd, void *buf, int buf_len)
{
	unsigned char scsi_cmd_buf[16] = {
		SEND_DIAGNOSTIC,
		0x10,			/* set PCV bit to 0x10 */
		(buf_len >> 8) & 0xff,	/* most significant byte */
		buf_len & 0xff,		/* least significant byte */
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	struct sense_data_t sense_data;
	sg_io_hdr_t hdr;
	int i, rc;

	for (i = 0; i < 3; i++) {
		memset(&hdr, 0, sizeof(hdr));
		memset(&sense_data, 0, sizeof(struct sense_data_t));

		hdr.interface_id = 'S';
		hdr.dxfer_direction = SG_DXFER_TO_DEV;
		hdr.cmd_len = 16;
		hdr.mx_sb_len = sizeof(sense_data);
		hdr.iovec_count = 0;	/* scatter gather not necessary */
		hdr.dxfer_len = buf_len;
		hdr.dxferp = buf;
		hdr.cmdp = scsi_cmd_buf;
		hdr.sbp = (unsigned char *)&sense_data;
		hdr.timeout = 120 * 1000;	/* set timeout to 2 minutes */
		hdr.flags = 0;
		hdr.pack_id = 0;
		hdr.usr_ptr = 0;

		rc = ioctl(fd, SG_IO, &hdr);

		if ((rc == 0) && (hdr.masked_status == CHECK_CONDITION))
			rc = CHECK_CONDITION;
		else if ((rc == 0) && (hdr.host_status || hdr.driver_status))
			rc = -EIO;

		if (rc == 0 || hdr.host_status == 1)
			break;
	}

	return rc;
}

/**
 * get_ses_indicator
 * @brief retrieve the current state for a SES-controlled hard drive indicator
 * 
 * @param loc location code of the sensor
 * @param state return location for the sensor state
 * @return 0 on success
 */
int
get_ses_indicator(struct loc_code *loc, int *state)
{
	int rc, len, fd, i, found=0;
	struct ses_encl_status_ctl_pg dp;

	if (strlen(loc->file) == 0)
		return -1;

	fd = open(loc->file, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "Could not open %s\n", loc->file);
		return -1;
	}

	rc = get_diagnostic_page(fd, 2, (void *)&dp,
			sizeof(struct ses_encl_status_ctl_pg));
	if (rc != 0) {
		close(fd);
		fprintf(stderr, "Could not retrieve diagnostic page for %s, "
				"error %d\n", loc->code, rc);
		return rc;
	}

	/* retrieve data from the page */
	len = (ntohs(dp.byte_count)-8) / sizeof(struct ses_drive_elem_status);
	for (i=0; i<len; i++) {
		if (dp.elem_status[i].scsi_id == loc->target) {
			found = 1;
			*state = dp.elem_status[i].identify;
			break;
		}
	}

	if (found == 0) {
		fprintf(stderr, "Could not retrieve data from diagnostic page "
				"for %s\n", loc->code);
		return -2;
	}

	close(fd);
	return 0;
}

/**
 * set_ses_indicator
 * @brief set a new state for a SES-controlled hard drive indicator
 *
 * @param loc location code of the sensor
 * @param new_value value to update indicator to
 * @return 0 on success
 */
int
set_ses_indicator(struct loc_code *loc, int new_value)
{
	int rc, len, fd, i, found=0;
	struct ses_encl_status_ctl_pg dp;

	if (strlen(loc->file) == 0)
		return -1;

	fd = open(loc->file, O_RDWR);
	if (fd == -1) {
		fprintf(stderr, "Could not open %s\n", loc->file);
		return -1;
	}

	rc = get_diagnostic_page(fd, 2, (void *)&dp,
			sizeof(struct ses_encl_status_ctl_pg));
	if (rc != 0) {
		close(fd);
		fprintf(stderr, "Could not retrieve diagnostic page for %s, "
				"error %d\n", loc->code, rc);
		return rc;
	}

	/* update data in the page */
	len = (ntohs(dp.byte_count)-8) / sizeof(struct ses_drive_elem_status);
	for (i=0; i<len; i++) {
		if (dp.elem_status[i].scsi_id == loc->target) {
			found = 1;
			dp.elem_status[i].select = 1;
			dp.elem_status[i].identify = new_value;
			break;
		}
	}
	if (found == 0) {
		fprintf(stderr, "Could not retrieve data from diagnostic page "
				"for %s\n", loc->code);
		return -2;
	}

	rc = send_diagnostic_page(fd, (void *)&dp,
			sizeof(struct ses_encl_status_ctl_pg));
	if (rc != 0) {
		close(fd);
		fprintf(stderr, "Could not send diagnostic page to %s, "
				"error %d\n", loc->code, rc);
		return rc;
	}

	close(fd);
	return 0;
}

/**
 * get_indicator_state
 * @brief retrieve the current state for an indicator
 * 
 * Call the appropriate routine for retrieving indicator values based on the
 * type of indicator.
 *
 * @param indicator identification or attention indicator
 * @param loc location code of the sensor
 * @param state return location for the sensor state
 * @return 0 on success
 */
int
get_indicator_state(int indicator, struct loc_code *loc, int *state)
{
	switch (loc->type) {
	case TYPE_RTAS:
		return get_rtas_sensor(indicator, loc, state);
	case TYPE_SES:
		return get_ses_indicator(loc, state);
	default:
		break;
	}

	return 0;
}

/**
 * set_indicator_state
 * @brief set an indicator to a new state (on or off)
 * 
 * Call the appropriate routine for setting indicators based on the type
 * of indicator.
 *
 * @param indicator identification or attention indicator
 * @param loc location code of rtas indicator
 * @param new_value value to update indicator to
 * @return 0 on success
 */
int
set_indicator_state(int indicator, struct loc_code *loc, int new_value)
{
	switch (loc->type) {
	case TYPE_RTAS:
		return set_rtas_indicator(indicator, loc, new_value);
	case TYPE_SES:
		return set_ses_indicator(loc, new_value);
	default:
		break;
	}

	return 0;
}

/**
 * legalize_path
 * @brief Removes any ".." directories from a path
 * 
 * Removes any ".." directories from a path, so that
 * /proc/device-tree/rtas/../../ppc64/rtas/error_log would become
 * /proc/ppc64/rtas/error_log.
 *
 * @param s path to legalize
 */
void
legalize_path(char *s)
{
	int p1 = 0, p2 = 0;

	while(s[p1]) {
		if (s[p1] == '.' && s[p1+1] == '.' && s[p1-1] == '/') {
			/* move p2 back one directory */
			p2 -= 2;
			while (s[p2] != '/')
				p2--;
			p1 += 2;
		}
		s[p2++] = s[p1++];
	}
	s[p2] = '\0';

	return;
}

/**
 * parse_work_area
 * @brief parse the working buffer into alist of loc_code structs
 *
 * @param c list to add new data to
 * @param buf worlking area to parse
 * @return pointer to new loc_code list
 */
struct loc_code *
parse_workarea(struct loc_code *c, char *buf)
{
	int i;
	struct loc_code *ret = c, *curr = c;
	int num = *(int *)buf;

	if (curr)
		while (curr->next)
			curr=curr->next;

	buf += sizeof(uint32_t);
	for (i=0; i<num; i++) {
		if (!curr) {
			curr =
			  (struct loc_code *)malloc(sizeof(struct loc_code));
			ret = curr;
		}
		else {
			curr->next =
			  (struct loc_code *)malloc(sizeof(struct loc_code));
			curr = curr->next;
		}
		curr->index = *(uint32_t *)buf;
		buf += sizeof(uint32_t);
		curr->length = *(uint32_t *)buf;
		buf += sizeof(uint32_t);
		strncpy(curr->code, buf, curr->length);
		buf += curr->length;
		curr->code[curr->length] = '\0';
		curr->length = strlen(curr->code) + 1;
		curr->type = TYPE_RTAS;
		curr->next = NULL;
	}

	return ret;
}

/**
 * add_scsi_drives
 * @brief append the list of SCSI drives to the list of loc_code structures
 *
 * @param l current list, with already-added RTAS entries
 */
void
add_scsi_drives(struct loc_code *l)
{
	char cmd[128], buf[1024], *pos, *end;
	int host, bus;
	struct loc_code *curr = l;
	struct sg_map *sg_head = NULL, *sg_curr = NULL;
	FILE *fp;
	DIR *dir;
	struct dirent *entry;
	struct stat statbuf;

	/* Check for lscfg and sg_map, both required for this routine */
	if (stat("/usr/bin/sg_map", &statbuf) < 0)
		return;
	if (stat("/usr/sbin/lscfg", &statbuf) < 0)
		return;

	if (curr)
		while (curr->next)
			curr=curr->next;

	/* Generate a linked list of sg_map structs */
	snprintf(cmd, 128, "/usr/bin/sg_map -x");
	if ((fp = popen(cmd, "r")) == NULL)
		return;
	while (fgets(buf, 1024, fp)) {
		if (!sg_head) {
			sg_head = (struct sg_map *)malloc(
					sizeof(struct sg_map));
			sg_curr = sg_head;
		}
		else {
			sg_curr->next = (struct sg_map *)malloc(
					sizeof(struct sg_map));
			sg_curr = sg_curr->next;
		}
		memset(sg_curr, 0, sizeof(struct sg_map));

		/* get the /dev/sg* field */
		pos = buf;
		end = strchr(pos, ' ');
		if (!end) break;
		*end = '\0';
		strncpy(sg_curr->generic, pos, 32);
		pos = end+1;

		/* get the host */
		while (*pos == ' ') pos++;
		end = strchr(pos, ' ');
		if (!end) break;
		*end = '\0';
		sg_curr->host = atoi(pos);
		pos = end+1;

		/* get the bus */
		while (*pos == ' ') pos++;
		end = strchr(pos, ' ');
		if (!end) break;
		*end = '\0';
		sg_curr->bus = atoi(pos);
		pos = end+1;

		/* get the target */
		while (*pos == ' ') pos++;
		end = strchr(pos, ' ');
		if (!end) break;
		*end = '\0';
		sg_curr->target = atoi(pos);
		pos = end+1;

		/* get the lun */
		while (*pos == ' ') pos++;
		end = strchr(pos, ' ');
		if (!end) break;
		*end = '\0';
		sg_curr->lun = atoi(pos);
		pos = end+1;

		/* get the type */
		while (*pos == ' ') pos++;
		end = strchr(pos, ' ');
		if (!end) {
			end = strchr(pos, '\n');
			if (!end) break;
		}
		*end = '\0';
		sg_curr->type = atoi(pos);
		pos = end+1;

		if (*pos != '\0') {
			/* get the /dev/sd* field */
			while (*pos == ' ') pos++;
			end = strchr(pos, '\n');
			if (!end) break;
			*end = '\0';
			strncpy(sg_curr->dev, pos, 32);
			pos = end+1;
		}
	}
	pclose(fp);

	if ((dir = opendir("/sys/block")) != NULL) {
		/* Find /sys/block/sd* */
		while ((entry = readdir(dir)) != NULL) {
			if (!strncmp(entry->d_name, "sd", 2)) {
				if (!curr) {
					curr = (struct loc_code *)malloc(
						sizeof(struct loc_code));
				}
				else {
					curr->next = (struct loc_code *)malloc(
						sizeof(struct loc_code));
					curr = curr->next;
				}
				strncpy(curr->dev, entry->d_name, 8);
				curr->type = TYPE_SES;
				curr->next = NULL;

				/* Add location code (from lscfg) */
				snprintf(cmd, 128, "/usr/sbin/lscfg | grep %s",
						entry->d_name);
				if ((fp = popen(cmd, "r")) == NULL) {
					goto err_out;
				}
				else {
					if (fgets(buf, 1024, fp) == NULL) {
						pclose(fp);
						goto err_out;
					}
					pos = strstr(buf, entry->d_name) +
						strlen(entry->d_name);
					while(isspace(*pos)) pos++;
					strncpy(curr->code, pos, 120);
					/* remove trailing newline/whitespace */
					curr->code[strlen(curr->code)-1] = '\0';
					pos = strchr(curr->code, ' ');
					if (pos) *pos = '\0';
					pclose(fp);
				}

				/* Obtain the host and bus from sg_map */
				host = bus = 0;
				sg_curr = sg_head;
				while (sg_curr != NULL) {
					if (!strcmp(curr->dev,
							sg_curr->dev+5)) {
						host = sg_curr->host;
						bus = sg_curr->bus;
						curr->target = sg_curr->target;
						break;
					}
					sg_curr = sg_curr->next;
				}

				/* Add /dev/sg file of enclosure */
				sg_curr = sg_head;
				while (sg_curr != NULL) {
					if ((host == sg_curr->host) &&
						(bus == sg_curr->bus) &&
						(sg_curr->type == 13)) {

						strncpy(curr->file,
							sg_curr->generic, 32);
						break;
					}
					sg_curr = sg_curr->next;
				}
			}
		}

		closedir(dir);
	}

err_out:
	/* Free the list of sg_map structs */
	while (sg_head) {
		sg_curr = sg_head;
		sg_head = sg_head->next;
		free(sg_curr);
	}

	return;
}

/**
 * delete_list
 * @brief free the memory for list of loc_code structures
 *
 * @param l list to free
 */
void
delete_list(struct loc_code *l)
{
	if (!l) return;

	if (l->next) delete_list(l->next);
	free(l);

	return;
}

int
main (int argc, char **argv)
{
	char *dvalue=NULL, *lvalue=NULL, *svalue=NULL, *othervalue=NULL;
	int c, index, next_index, indicator, rc, state;
	char workarea[BUF_SIZE], err_buf[40], loc[120];
	char cmd[128], temp[128], buf[1024], *pos;
	struct loc_code *current = NULL, *list = NULL;
	FILE *fp;
	int trunc = 0, truncated = 0;
	struct stat statbuf;

	while ((c = getopt (argc, argv, "td:l:s:-:")) != -1) {

		switch (c) {
		case 'd':
			dvalue = optarg;
			break;
		case 'l':
			lvalue = optarg;
			break;
		case 's':
			svalue = optarg;
			break;
		case 't':
			trunc = 1;
			break;
		case '-':
			othervalue = optarg;
			break;
		case '?':
			if (isprint (optopt))
				fprintf(stderr,
					"Unrecognized option: -%c\n",
					optopt);
			else
				fprintf(stderr,
					"Unrecognized option character %x\n",
					optopt);
			print_usage (argv[0]);
			return 1;
		default:
			abort ();
		}
	}

	/* Option checking */
	for (index = optind; index < argc; index++) {
		fprintf(stderr,
			"Unrecognized argument %s\n", argv[index]);
		print_usage (argv[0]);
		return 1;
	}

	if (dvalue && !strstr(argv[0], "usysident")) {
		fprintf(stderr,
			"Unrecognized option: -d\n");
		print_usage (argv[0]);
		return 1;
	}

	if (dvalue && lvalue) {
		fprintf(stderr,
			"The -d and -l options cannot be used together.\n");
		print_usage (argv[0]);
		return 1;
	}

	if (strstr(argv[0], "usysident") && svalue
			&& (strcmp(svalue, "identify")
			&& (strcmp(svalue, "normal")))) {
		fprintf(stderr,
			"The -s option must be either "
			"\"identify\" or \"normal\".\n");
		print_usage (argv[0]);
		return 1;
	}
	else if ((strstr(argv[0],"usysattn") || strstr(argv[0],"usysfault"))
		&& svalue &&  strcmp(svalue, "normal")) {
		fprintf(stderr,
			"The -s option must be \"normal\".\n");
		print_usage (argv[0]);
		return 1;
	
	}
	
	if (svalue && !(dvalue || lvalue)) {
		if (strstr(argv[0],"usysident"))
			fprintf(stderr,
				"The -s option requires the -d or -l "
				"option to also be used.\n");
		else
			fprintf(stderr,
				"The -s option required the -l option "
				"to also be used.\n");
		print_usage (argv[0]);
		return 1;
	}

	if (othervalue && strcmp(othervalue, "all-on")
			&& strcmp(othervalue, "all-off")) {
		fprintf(stderr,
			"Unrecognized option: --%s\n", othervalue);
		print_usage (argv[0]);
		return 1;
	}

	if (othervalue && !strcmp(othervalue, "all-on") &&
			!strstr(argv[0], "usysident")) {
		fprintf(stderr,
			"Unrecognized option: --all-on\n");
		print_usage (argv[0]);
		return 1;
	}

	if (othervalue && argc > 2) {
		fprintf(stderr,
			"--%s cannot be used with any other options.\n",
			othervalue);
		print_usage (argv[0]);
		return 1;
	}

	if (strstr(argv[0], "usysident"))
		indicator=IDENT_INDICATOR;
	else if (strstr(argv[0],"usysattn") || strstr(argv[0],"usysfault"))
		indicator= ATTN_INDICATOR;
	else
		return(1);

	/* gather the list of RTAS indicators */
	index = 1;
	do
	{
		rc = rtas_get_indices (0, indicator, workarea, BUF_SIZE,
				index, &next_index);
		switch(rc) {
		case 1:		/* more data available */
			index = next_index;
			/* fall through */
			
		case 0:		/* success */
			list = parse_workarea(list, workarea);
			break;
			
		case -1:	/* hardware error */
			fprintf(stderr, "Hardware error retrieving "
				"indicator indices\n");
			delete_list(list);
			return -1;
			
		case RTAS_UNKNOWN_OP:
			/* Yes, this is a librtas return code but it should
			 * be treated the same as a -3 return code, both
			 * indicate that functionality is not supported
			 */
			librtas_error(rc, err_buf, 40);
			/* fall through */

		case -3:	/* indicator type not supported. */ 
			fprintf(stderr, "The %s indicators are not supported "
				"on this system", INDICATOR_TYPE(indicator));
			
			if (rc == RTAS_UNKNOWN_OP)
				fprintf(stderr, ",\n%s\n", err_buf);
			else
				fprintf(stderr, ".\n");

			delete_list(list);
		
			return -2;

		case -4:	/* list changed; start over */
			delete_list(list);
			list = NULL;
			index = 1;
			break;

		default:
			librtas_error(rc, err_buf, 40);
			fprintf(stderr, "Could not retrieve data for %s "
				"indicators,\n%s.\n", 
				INDICATOR_TYPE(indicator), err_buf);
			delete_list(list);
			return -3;
		}

	} while (((rc == 1) || (rc == -4)));

	if (indicator == IDENT_INDICATOR) {
		/* Gather the list of SCSI hard drive indicators */
		add_scsi_drives(list);
	}

	if (!svalue && !lvalue && !dvalue && !othervalue) {
		/* Print all location codes and indicator status */
		current = list;
		while (current) {
			if (!get_indicator_state(indicator, current, &state))
				printf("%s\t[%s]\n", current->code,
					state?"on":"off");
				fflush(stdout);
			current	= current->next;
		}
	}

	if (dvalue) {
		/* Print/update indicator status for the specified device */

		if (stat("/usr/sbin/lscfg", &statbuf) < 0) {
			fprintf(stderr, "lsvpd must be installed for the "
					"-d option to work\n");
			return 2;
		}

		/* Add location code (from lscfg) */
		snprintf(cmd, 128, "/usr/sbin/lscfg | grep %s", dvalue);
		if ((fp = popen(cmd, "r")) == NULL) {
			fprintf(stderr, "Could not run lscfg to retrieve "
					"the location code\n");
			return 2;
		}
		else {
			if (fgets(buf, 1024, fp) == NULL) {
				fprintf(stderr, "Could not run lscfg to "
						"retrieve the location code\n");
				return 2;
			}
			pos = strstr(buf, dvalue) + strlen(dvalue);
			while(isspace(*pos)) pos++;
			strncpy(loc, pos, 120);
			/* remove trailing newline/whitespace */
			loc[strlen(loc)-1] = '\0';
			pos = strchr(loc, ' ');
			if (pos) *pos = '\0';
			pclose(fp);
		}

#if 0
		/* Retrieve the devspec from sysfs */
		/* Look in /sys/class/net and /sys/block */
		if (((dir1 = opendir("/sys")) == NULL) ||
				((dir2 = opendir("/proc")) == NULL)) {
			fprintf(stderr, "The -d option requires a version "
				"2.6 or later kernel with both /sys and "
				"/proc mounted.\n");
			delete_list(list);
			return 1;
		}
		closedir(dir1);
		closedir(dir2);

		workarea[0] = '\0';

		/* Look in /sys/class/net and /sys/block */
		strcpy(path, "/sys/class/net/");
		strcat(path, dvalue);
		strcat(path, "/device/devspec");
		fd = open(path, O_RDONLY);
		if (fd > 0) {
			c = read(fd, workarea, BUF_SIZE);
			close(fd);
			workarea[c] = '\0';
		}
		else {
			strcpy(path, "/sys/block/");
			strcat(path, dvalue);
			fd = open(path, O_RDONLY);
			if (fd > 0) {
				close(fd);
				strcpy(temp, path);
				strcat(temp, "/device");
				if((c = readlink(temp, sym, 128)) > 0) {
					sym[c] = '\0';
					strcat(path, "/");
					strcat(path, sym);
					strcat(path, "/../../devspec");
					legalize_path(path);

					fd = open(path, O_RDONLY);
					if(fd > 0) {
						c = read(fd, workarea,
							BUF_SIZE);
						close(fd);
						workarea[c] = '\0';
					}
				}
			}
		}

		if (workarea[0] == '\0') {
			fprintf(stderr, "Could not determine the location "
				"code for the specified device %s.\n", dvalue);
			delete_list(list);
			return -2;
		}

		/* Retrieve the location code from procfs */
		strcpy(path, "/proc/device-tree");
		strcat(path, workarea);
		strcat(path, "/ibm,loc-code");
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "Could not determine the location "
				"code for the specified device %s.\n", dvalue);
			delete_list(list);
			return -2;
		}
		c = read(fd, loc, 120);
		close(fd);
		loc[c] = '\0';
#endif

		/* Set lvalue and fall through to the next conditional */
		lvalue = loc;
		printf("%s is at location code %s.\n", dvalue, lvalue);
	}

	if (lvalue) {
		/* Print/update indicator status at the specified location */
		if (svalue) {
			if (!strcmp(svalue, "normal"))
				c = 0;
			else if (!strcmp(svalue, "identify"))
				c = 1;
			else {
				fprintf(stderr, "oops! the -s value was "
					"neither \"normal\" nor "
					"\"identify\"!\n");
				delete_list(list);
				return 1;
			}
		}

retry:
		current = list;

		while (current) {
			if (!strcmp(current->code, lvalue)) {
				if (truncated)
					printf("Truncated the specified "
						"location code to %s\n",
						lvalue);

				if (svalue) {
					rc = get_indicator_state(indicator,
						current, &state);
					if (rc) {
						delete_list(list);
						return rc;
					}

					if (state != c) {
						rc = set_indicator_state(
							indicator, current, c);
						if (rc) {
							delete_list(list);
							return rc;
						}
					}
				}

				if (!(rc = get_indicator_state(indicator,
						current, &state))) {
					if (dvalue)
						printf("%s\t[%s]\n", lvalue,
							state?"on":"off");
					else
						printf("%s\n",
							state?"on":"off");
				}
				delete_list(list);
				return rc;
			}

			current = current->next;
		}

		strcpy(temp, lvalue);

		if(trunc) {
			if(truncate_loc_code(lvalue)) {
				truncated = 1;
				goto retry;
			}
		}

		printf("There is no indicator at location code %s\n",
			temp);
		delete_list(list);
		return 2;
	}

	if (othervalue) {
		/* Turn all indicators either on or off */
		if (!strcmp(othervalue, "all-on"))
			c = 1;
		else if (!strcmp(othervalue, "all-off"))
			c = 0;
		else {
			fprintf(stderr, "oops! neither all-on nor all-off"
				"was specified!\n");
			delete_list(list);
			return 1;
		}

		current = list;
		while (current) {
			if (!get_indicator_state(indicator, current, &state)) {
				if (state == c)
					printf("%s\t[%s]\n", current->code,
						c?"on":"off");
				else {
					set_indicator_state(indicator,
						current, c);
					if (!get_indicator_state(indicator,
							current, &state)) {
						printf("%s\t[%s]\n",
							current->code,
							state?"on":"off");
					}
				}
				fflush(stdout);
			}
			current	= current->next;
		}
	}

	delete_list(list);
	return 0;
}
