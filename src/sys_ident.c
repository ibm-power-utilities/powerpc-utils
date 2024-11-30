/**
 * @file sys_ident.c
 * @brief generates unique system identification numbers
 */
/**
 * @mainpage sys_ident documentation
 * @section Copyright
 * Copyright (c) 2006 International Business Machines
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
 *
 * @section Overview
 * Generate unique identification numbers for IBM 64-bit powerpc platforms.
 *
 * @section usysident
 * The sys_ident utility implements algorithms for generating identification
 * numbers that are unique to each system.  These numbers are generated with
 * the same algorithm as the numbers generated by <tt>uname -f</tt> on AIX.
 *
 * When invoked with the <tt>-s</tt> option, a 64-bit identification number
 * will be printed (as a 16-character hexadecimal number).  The number will
 * also be unique for each partition on a partitioned system.
 *
 * When invoked with the <tt>-p</tt> option, a 32-bit processor serial
 * number will be printed (as an 8-character hexadecimal number).
 *
 * @author Michael Strosaker <strosake@us.ibm.com>
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <librtas.h>
#include "pseries_platform.h"

#define POW_36_4	(36*36*36*36)
#define POW_36_3	(36*36*36)
#define POW_36_2	(36*36)
#define POW_16_4	(16*16*16*16)
#define POW_16_3	(16*16*16)
#define POW_16_2	(16*16)

/**
 * @struct sys_part_id_0
 * @brief unique system/partition identification; 16 hex characters
 *
 * This struct is used for the following OF prefixes:  "IBM,01", "IBM,03",
 * "IBM,05", and "IBM,02" if TTTT begins with a 7.
 */
struct sys_part_id_0
{
	uint64_t firstbit:1;	/* always 0 in this one */
	uint64_t RV:4;
	uint64_t TF:14;
	uint64_t CF:11;
	uint64_t SF:26;
	uint64_t PF:8;
};

/**
 * @struct sys_part_id_1
 * @brief unique system/partition identification; 16 hex characters
 *
 * This struct is used for the following OF prefixes:  "IBM,04" and "IBM,02"
 * if TTTT does not begin with a 7.
 */
struct sys_part_id_1
{
	uint64_t firstbit:1;	/* always 1 in this one */
	uint64_t RV:17;
	uint64_t SF:26;
	uint64_t PF:20;
};

/**
 * @struct proc_sn_110
 * @brief VPD processor serial number value; 8 hex characters
 *
 * Used for OF prefix "IBM,02" if all characters in SSSSS are in "0-9,A-F"
 */
struct proc_sn_110
{
	uint32_t bit1:1;	/* always 1 */
	uint32_t bit2:1;	/* always 1 */
	uint32_t bit3:1;	/* always 0 */
	uint32_t RV:1;
	uint32_t SF:20;
	uint32_t constant:8;	/* always 0x4C */
};

/**
 * @struct proc_sn_111
 * @brief VPD processor serial number value; 8 hex characters
 *
 * Used for OF prefix "IBM,02" if any characters in SSSSS are in the set "G-Z"
 */
struct proc_sn_111
{
	uint32_t bit1:1;	/* always 1 */
	uint32_t bit2:1;	/* always 1 */
	uint32_t bit3:1;	/* always 1 */
	uint32_t SF:21;
	uint32_t constant:8;	/* always 0x4C */
};

/**
 * print_usage
 * @brief print the usage statement
 *
 * @param cmd command we are running
 */
void
print_usage (char *cmd)
{
	printf("Usage: %s -p | -s\n", cmd);
	printf("\t-p: print 32-bit VPD processor serial number value\n");
	printf("\t-s: print 64-bit unique system identifier\n");
	printf("\t(all values are printed in hex, with no 0X or 0x prefix)\n");
	return;
}

/**
 * dump_hex
 * @brief dumps a character string as a series of hex characters
 *
 * @param data the data to dump
 * @param len the length of the data buffer
 */
void
dump_hex(char *data, int len)
{
	int i = 0;

	while (i < len) {
		printf("%02X", data[i]);
		i++;
	}

	printf("\n");
	return;
}

/**
 * char_to_enum
 * @brief Converts a character to an integer value
 *
 * @param c character to convert
 * @return 0 - 35 for characters in 0-9,A-Z; exits the program otherwise
 */
int
char_to_enum(char c)
{
	if ((c >= 48) && (c <= 57))	/* 0 through 9 */
		return (c-48);

	if ((c >= 65) && (c <= 90))	/* A through Z */
		return (c-55);

	printf("0\n");
	exit(2);
}

/**
 * print_proc_sn_value
 * @brief Prints the VPD processor serial number value
 *
 * @return 0 on success
 */
int
print_proc_sn_value(void)
{
	int i, prefix, fd, rc;
	char temp[32], sys_id[13], sssss[6], buf[5000], *pos;
	struct proc_sn_110 p110;
	struct proc_sn_111 p111;

	fd = open("/proc/device-tree/system-id", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Could not open /proc/device-tree/system-id\n");
		return 2;
	}

	i = read(fd, sys_id, 13);
	if (i != 13) {
		fprintf(stderr, "Unexpected contents in "
				"/proc/device-tree/system-id\n");
		close(fd);
		return 2;
	}

	close(fd);

	strncpy(temp, sys_id, 4);
	temp[4] = '\0';

	if (strcmp(temp, "IBM,")) {
		fprintf(stderr, "This command does not work on OEM systems\n");
		return 2;
	}

	temp[0] = sys_id[4];
	temp[1] = sys_id[5];
	temp[2] = '\0';
	prefix = atoi(temp);

	strncpy(sssss, sys_id+8, 5);
	sssss[5] = '\0';

	switch(prefix) {
		case 1:
			/* BUG: not sure what to do for this case */
			fprintf(stderr, "Cannot currently generate the serial "
					"number for IBM,01 systems\n");
			break;
		case 2:
			if (sssss[0] >= 71 || sssss[1] >= 71 ||
					sssss[2] >= 71 || sssss[3] >= 71 ||
					sssss[4] >= 71) {
				p111.bit1 = 1;
				p111.bit2 = 1;
				p111.bit3 = 1;
				p111.SF = char_to_enum(sssss[0])*POW_36_4 +
					char_to_enum(sssss[1])*POW_36_3 +
					char_to_enum(sssss[2])*POW_36_2 +
					char_to_enum(sssss[3])*36 +
					char_to_enum(sssss[4]);
				p111.constant = 0x4c;
				dump_hex((char *)&p111,
					sizeof(struct proc_sn_111));
			}
			else {
				p110.bit1 = 1;
				p110.bit2 = 1;
				p110.bit3 = 0;
				p110.RV = 0;
				p110.SF = char_to_enum(sssss[0])*POW_16_4 +
					char_to_enum(sssss[1])*POW_16_3 +
					char_to_enum(sssss[2])*POW_16_2 +
					char_to_enum(sssss[3])*36 +
					char_to_enum(sssss[4]);
				p110.constant = 0x4c;
				dump_hex((char *)&p110,
					sizeof(struct proc_sn_110));
			}
			break;
		case 3:
			rc = rtas_get_sysparm(36, 5000, buf);
			if (rc != 0) {
				fprintf(stderr, "Unable to retrieve "
						"parameter from RTAS\n");
				return 2;
			}
			/* Ignore length field (first 2 bytes) */
			pos = strstr(buf + 2, "uid=");
			if (pos == NULL) {
				fprintf(stderr, "Parameter from RTAS does "
						"not contain uid\n");
				return 2;
			}
			strncpy(temp, pos+4, 8);
			temp[8] = '\0';
			printf("%s\n", temp);
			break;
		case 4:
		case 5:
			printf("00000000\n");
			break;
		case 6:
			if (sssss[4] >= 71) {
				p111.bit1 = 1;
				p111.bit2 = 1;
				p111.bit3 = 1;
				p111.SF = char_to_enum(sssss[4])*POW_36_4 +
					char_to_enum(sssss[0])*POW_36_3 +
					char_to_enum(sssss[1])*POW_36_2 +
					char_to_enum(sssss[2])*36 +
					char_to_enum(sssss[3]);
				p111.constant = 0x4b;
				dump_hex((char *)&p111,
					sizeof(struct proc_sn_111));
			}
			else {
				p110.bit1 = 1;
				p110.bit2 = 1;
				p110.bit3 = 0;
				p110.RV = 0;
				p110.SF = char_to_enum(sssss[0])*POW_16_4 +
					char_to_enum(sssss[1])*POW_16_3 +
					char_to_enum(sssss[2])*POW_16_2 +
					char_to_enum(sssss[3])*36 +
					char_to_enum(sssss[4]);
				p110.constant = 0x4b;
				dump_hex((char *)&p110,
					sizeof(struct proc_sn_110));
			}
			break;
		default:
			fprintf(stderr, "Unknown OF prefix: IBM,%02d\n",
					prefix);
			return 2;
	}

	return 0;
}

/**
 * print_sys_part_id
 * @brief Prints the unique system identification number
 *
 * @return 0 on success
 */
int
print_sys_part_id(void)
{
	int i, prefix, fd;
	uint32_t par_no;
	char temp[32], sys_id[13], model[12], tttt[5], sssss[6], cc[3];
	struct sys_part_id_0 id0;
	struct sys_part_id_1 id1;

	fd = open("/proc/device-tree/system-id", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Could not open /proc/device-tree/system-id\n");
		return 2;
	}

	i = read(fd, sys_id, 13);
	if (i != 13) {
		fprintf(stderr, "Unexpected contents in "
				"/proc/device-tree/system-id\n");
		close(fd);
		return 2;
	}

	close(fd);

	strncpy(temp, sys_id, 4);
	temp[4] = '\0';

	if (strcmp(temp, "IBM,")) {
		fprintf(stderr, "This command does not work on OEM systems\n");
		return 2;
	}

	temp[0] = sys_id[4];
	temp[1] = sys_id[5];
	temp[2] = '\0';
	prefix = atoi(temp);

	strncpy(cc, sys_id+6, 2);
	cc[2] = '\0';

	strncpy(sssss, sys_id+8, 5);
	sssss[5] = '\0';

	fd = open("/proc/device-tree/model", O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Could not open /proc/device-tree/model\n");
		return 2;
	}

	i = read(fd, model, 12);
	if (i != 12) {
		fprintf(stderr, "Unexpected contents in "
				"/proc/device-tree/model\n");
		close(fd);
		return 2;
	}

	close(fd);

	memcpy(tttt, model+4, 4);
	tttt[4] = '\0';

	fd = open("/proc/device-tree/ibm,partition-no", O_RDONLY);
	if (fd < 0)
		par_no = 0;
	else {
		i = read(fd, &par_no, sizeof(uint32_t));
		if (i != sizeof(uint32_t)) {
			fprintf(stderr, "Unexpected contents in "
					"/proc/device-tree/partition-no\n");
			close(fd);
			return 2;
		}
		close(fd);
	}

	switch(prefix) {
		case 1:
		case 2:
		case 3:
		case 5:
			if ((prefix == 2) && (tttt[0] != '7')) {
				/* fall through */
			}
			else {
				id0.firstbit = 0;
				id0.RV = 0;
				id0.TF = (tttt[0]-48)*1000 + (tttt[1]-48)*100 +
					(tttt[2]-48)*10 + (tttt[3]-48);
				id0.CF = char_to_enum(cc[0])*36 +
					char_to_enum(cc[1]);
				id0.SF = char_to_enum(sssss[0])*POW_36_4 +
					char_to_enum(sssss[1])*POW_36_3 +
					char_to_enum(sssss[2])*POW_36_2 +
					char_to_enum(sssss[3])*36 +
					char_to_enum(sssss[4]);
				id0.PF = par_no;
				dump_hex((char *)&id0,
					sizeof(struct sys_part_id_0));
				break;
			}
		case 4:
		case 6:
			id1.firstbit = 1;
			id1.RV = 0;
			id1.SF = char_to_enum(sssss[0])*POW_36_4 +
				char_to_enum(sssss[1])*POW_36_3 +
				char_to_enum(sssss[2])*POW_36_2 +
				char_to_enum(sssss[3])*36 +
				char_to_enum(sssss[4]);
			id1.PF = par_no;
			dump_hex((char *)&id1,
				sizeof(struct sys_part_id_1));
			break;
		default:
			fprintf(stderr, "Unknown OF prefix: IBM,%02d\n",
					prefix);
			return 2;
	}

	return 0;
}

int
main (int argc, char **argv)
{
	int c, s_flag = 0, p_flag = 0;

	if (get_platform() != PLATFORM_PSERIES_LPAR) {
		fprintf(stderr, "%s: is not supported on the %s platform\n",
							argv[0], platform_name);
		return 1;
	}

	while ((c = getopt(argc, argv, "hps")) != -1) {

		switch (c) {
		case 'h':
			print_usage(argv[0]);
			return 0;
		case 'p':
			p_flag = 1;
			break;
		case 's':
			s_flag = 1;
			break;
		case '?':
			if (isprint(optopt))
				fprintf(stderr, "Unrecognized option: -%c\n",
					optopt);
			else
				fprintf(stderr,
					"Unrecognized option character %x\n",
					optopt);
			print_usage(argv[0]);
			return 1;
		default:
			abort();
		}
	}

	if ((s_flag) && (p_flag)) {
		fprintf(stderr, "Only one of -s or -p may be used\n");
		return 1;
	}

	if (s_flag)
		return print_sys_part_id();
	else if (p_flag)
		return print_proc_sn_value();
	else {
		fprintf(stderr, "No option specified\n");
		print_usage(argv[0]);
		return 1;
	}

	return 0;
}
