/**
 * @file uesensor.c
 * @brief view status of environmental sensors on IBM ppc64 platforms.
 */

/**
 * @mainpage uesensor documentation
 * @section Copyright
 * Copyright (c) 2004 International Business Machines
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
 * The uesensor utility is used to view the state of environmental sensors 
 * on PowerPC-64 machines.
 *
 * There are 4 types of system sensors that can be retrieved with
 * uesensor; each sensor has an identifying token:<br>
 * 3 - Thermal sensor<br>
 * 9001 - Fan speed sensor<br>
 * 9002 - Voltage sensor<br>
 * 9004 - Power supply sensor
 *
 * Each sensor is uniquely identified by a combination of token and
 * index number; index numbers start at 0 and are contiguous.  For example,
 * the second fan on the system would be identified by token number 9001
 * and index 1.
 *
 * The state of each sensor consists of a status and a measured value.
 * The status value is one of the following:<br>
 * 9 - Critical low<br>
 * 10 - Warning low<br>
 * 11 - Normal<br>
 * 12 - Warning high<br>
 * 13 - Critical high
 *
 * The measured value depends on the type of sensor.  Thermal sensors are
 * measured in degrees Celcius; fan speed is measured in revolutions per
 * minute; voltage is measured in millivolts; power supply measurements are
 * defined as follows:<br>
 * 0 - Not present<br>
 * 1 - Present and not operational<br>
 * 2 - Status unknown<br>
 * 3 - Present and operational
 *
 * Each sensor is also associated with a location code; this location code
 * may not be unique (for example, there may be multiple voltage sensors on
 * a planar).
 *
 * @author Michael Strosaker <strosake@us.ibm.com>
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <librtas.h>

#include "librtas_error.h"
#include "pseries_platform.h"

#define BUF_SIZE		1024
#define PATH_RTAS_SENSORS	"/proc/device-tree/rtas/rtas-sensors"

/**
 * @var status_text
 * @brief ascii strings corresponding to status levels
 */
char *status_text[] = {"Critical low", "Warning low", "Normal",
			"Warning high", "Critical high"};
/**
 * power_supply_text
 * @brief ascii stringss corresponding to power supply status
 */
char *power_supply_text[] = {"Not present", "Present and not operational",
			"Status unknown", "Present and operational"};

/**
 * cmd
 * @brief command name (argv[0])
 */
char *cmd;

/**
 * print_usage
 * @brief print the uesensor usage message
 *
 * @param cmd argv[0]
 */
void
print_usage (char *cmd) {
	printf("Usage: %s [-l | -a] [-t token -i index [-v]]\n"
		"\t-l: list all sensor values in a text format\n"
		"\t-a: list all sensor values in a tabular format\n"
		"\t-t: specify the token of the sensor to query\n"
		"\t-i: specify the index of the sensor to query\n"
		"\t-v: return the measured value of the sensor, rather than\n"
		"\t    the sensor status which is returned by default\n",
		cmd);
	return;
}

#define ERR_MSG		0	/**< denotes an actual error message */
#define WARN_MSG	1	/**< denotes a warning message */

/**
 * err_msg
 * @brief print an error or warning message to stderr 
 *
 * @param msg_type eith ERR_MSG or WARN_MSG
 * @param fmt formatted strinf a la printf()
 * @param ... additional args a la printf()
 */
void
err_msg(int msg_type, const char *fmt, ...) {
	va_list ap;
	int n;
	char buf[BUF_SIZE];

	n = sprintf(buf, "%s: %s", cmd, 
		(msg_type == WARN_MSG ? "WARNING: " : "ERROR: "));

	va_start(ap, fmt);
	vsprintf(buf + n, fmt, ap);
	va_end(ap);

	fflush(stderr);
	fputs(buf, stderr);
	fflush(NULL);
}

/**
 * get_sensor
 * @brief retrieve a sensors status
 * 
 * Call the rtas_get_sensor librtas call.  The sensor state is returned
 * as the "state" parameter, while the sensor status is the return value.
 *
 * @param token rtas sensor token
 * @param index rtas sensor index
 * @param state place to return rtas sensor state
 * @return return code from rtas call
 */
int
get_sensor (int token, int index, int *state) {
	int rc;
	char err_buf[40];

	rc = rtas_get_sensor (token, index, state);

	if (rc == -1) {
		err_msg(ERR_MSG,
			"Hardware error retrieving a sensor: token %04d, "
			"index %d\n", token, index);
		return -1;
	}
	else if (rc == -3) {
		err_msg(ERR_MSG,
			"The sensor at token %04d, index %d is not "
			"implemented.\n", token, index);
		return -2;
	}
	else if (rc <= RTAS_KERNEL_INT) {
		librtas_error(rc, err_buf, 40);

		err_msg(ERR_MSG,
			"Library call (rtas_get_sensor) failure for the "
			"sensor at token %04d, index %d:\n%s\n",
			token, index, err_buf);

		return -3;
	}

	return rc;
}

/**
 * get_location_code
 * @brief retrieve the locations code for a specified token and index
 *
 * @param token rtas token for the location code
 * @param index rtas index for the location code
 * @param buffer buffer to put location code into
 * @param size size of "buffer"
 * @return 1 on success, 0 on failure
 */
int
get_location_code (int token, int index, char *buffer, size_t size) {
	int fd, i, len;
	char filename[45], temp[4096], *pos;

	if ((size == 0) || (buffer == NULL))
		return 0;

	buffer[0] = '\0';
	sprintf(filename, "/proc/device-tree/rtas/ibm,sensor-%04d", token);

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return 0;

	if ((len = read(fd, temp, 4096)) < 0) {
		close(fd);	
		return 0;
	}

	pos = temp;

	for (i=0; i<index; i++) {
		pos += strlen(pos)+1;
		if (pos >= temp + len) {
			close(fd);
			return 0;
		}
	}

	strncpy(buffer, pos, size);
	close(fd);

	return 1;
}

#define PRINT_STATUS	0	/**< print the sensor status only */
#define PRINT_VALUE	1	/**< print the measured value only */
#define PRINT_TABULAR	2	/**< print the sensor/values as a table */
#define PRINT_TEXT	3	/**< print the sensor/values verbosely */

/**
 * print_sensor
 * @brief print the status of a sensor
 *
 * @param token rtas token of sensor to print
 * @param index rtas index of sensor to print
 * @param verbosity verbose level
 * @return 1 on success, 0 on failure
 */
int
print_sensor (uint32_t token, uint32_t index, int verbosity) {
	int status, state;
	char loc_buf[80];

	status = get_sensor (token, index, &state);

	if (status < 0)
		return 0;

	switch (verbosity) {
	case PRINT_STATUS:
		printf("%d\n", status);
		break;
	case PRINT_VALUE:
		printf("%d\n", state);
		break;
	case PRINT_TABULAR:
		printf("%d %d %d %d ", token, index, status, state);
		get_location_code(token, index, loc_buf, 80);
		printf("%s\n", loc_buf);
		break;
	case PRINT_TEXT:
		switch (token) {
		case 3:
			printf("Sensor Token = Thermal\n");
			printf("Status = %s\n", status_text[status-9]);
			printf("Value = %d%c C (%d%c F)\n", state, 176,
					((9*state)/5) + 32, 176);
			break;
		case 9001:
			printf("Sensor Token = Fan Speed\n");
			printf("Status = %s\n", status_text[status-9]);
			printf("Value = %d RPM\n", state);
			break;
		case 9002:
			printf("Sensor Token = Voltage\n");
			printf("Status = %s\n", status_text[status-9]);
			printf("Value = %d mv\n", state);
			break;
		case 9004:
			printf("Sensor Token = Power Supply\n");
			printf("Status = %s\n", status_text[status-9]);
			printf("Value = %s\n", power_supply_text[state]);
			break;
		default:
			printf("Sensor Token = (unknown)\n");
			printf("Status = %d\n", status);
			printf("Value = %d\n", state);
		}
		get_location_code(token, index, loc_buf, 80);
		printf("Location Code = %s\n\n", loc_buf);
		break;
	default:
		return 0;
	}

	return 1;
}

int
main (int argc, char **argv)
{
	int c, text=0, numerical=0, measured=0, i;
	int fd, rc;
	uint32_t tok, max_index;
	char *token=NULL, *index=NULL;

	cmd = argv[0];
	if (get_platform() != PLATFORM_PSERIES_LPAR) {
		fprintf(stderr, "%s: is not supported on the %s platform\n",
							cmd, platform_name);
		return 1;
	}

	while ((c = getopt (argc, argv, "hlat:i:v")) != -1) {

		switch (c) {
		case 'h':
			print_usage (argv[0]);
			return 0;
		case 'l':
			text = 1;
			break;
		case 'a':
			numerical = 1;
			break;
		case 't':
			token = optarg;
			break;
		case 'i':
			index = optarg;
			break;
		case 'v':
			measured = 1;
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
	for (i = optind; i < argc; i++) {
		fprintf(stderr,
			"Unrecognized argument %s\n", argv[i]);
		print_usage (argv[0]);
		return 1;
	}

	if (argc == 1) {
		print_usage (argv[0]);
		return 1;
	}

	if ((token && !index) || (index && !token)) {
		fprintf(stderr,
			"The -t and -i options must be used together.\n");
		print_usage (argv[0]);
		return 1;
	}

	if (!token && !text && !numerical) {
		print_usage (argv[0]);
		return 1;
	}

	if (text && numerical) {
		fprintf(stderr,
			"The -l and -a options cannot be used together.\n");
		print_usage (argv[0]);
		return 1;
	}

	if (token && (text || numerical)) {
		fprintf(stderr,
			"The -t and -i options cannot be used with either "
			"-l or -a.\n");
		print_usage (argv[0]);
		return 1;
	}
	
	if (measured && !token) {
		fprintf(stderr,
			"The -v option requires the -t and -i options to "
			"also be used.\n");
		print_usage (argv[0]);
		return 1;
	}

	if (token) {
		rc = print_sensor(atoi(token), atoi(index),
			measured?PRINT_VALUE:PRINT_STATUS);
		if (!rc) {
			err_msg(ERR_MSG,
				"Could not print the value of the "
				"requested sensor.\n");
			return 2;
		}
	}

	if (text || numerical) {
		unsigned i;

		/* Print the status/value of all sensors */
		fd = open(PATH_RTAS_SENSORS, O_RDONLY);

		while (read(fd, (char *)&tok, sizeof(uint32_t)) ==
					sizeof(uint32_t)) {
			tok = be32toh(tok);
			rc = read(fd, (char *)&max_index, sizeof(uint32_t));
			max_index=be32toh(max_index);
			if (rc != sizeof(uint32_t)) {
				err_msg(ERR_MSG, "Error reading the "
						"list of sensors.\n");
				return 2;
			}

			if ((tok == 3) || (tok == 9001) ||
					(tok == 9002) || (tok == 9004)) {
				for (i = 0; i <= max_index; i++) {
					rc = print_sensor(tok, i,
						text?PRINT_TEXT:PRINT_TABULAR);
				}
			}

		}

		close(fd);
	}

	return 0;
}
