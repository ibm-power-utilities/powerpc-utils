/**
 * @file set_poweron_time.c
 * @brief set a time to automatically power-on an IBM ppc64 system.
 */
/**
 * @mainpage set_poweron_time documentation
 * @section Copyright
 * Copyright (c) 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @section Overview
 * The set_poweron_time utility is used to set a time in the future to 
 * power on the system.  The utility uses firmware interfaces provided by 
 * IBM ppc64 systems to provide this functionality.
 *
 * When used with the <tt>-t</tt> option, the utility will configure the 
 * system to power-on at the specified date and time.  This is usefule for 
 * specifying that the sysetm should be restarted at 6 AM on Monday morning, 
 * for example.
 * 
 * When used with the <tt>-d</tt> option, the utility will treat the specified 
 * time as a delta from the present. This is useful for specifying that the 
 * system should be restarted in 2 days, for example.
 *
 * Times for the <tt>-t</tt> and <tt>-d</tt> options should be specified in 
 * the following format:<br>
 * <b>Y</b>&lt;year&gt;<b>M</b>&lt;month&gt;<b>D</b>&lt;day&gt;<b>h</b>&lt;hour&gt;<b>m</b>&lt;minute&gt;<b>s</b>&lt;second&gt;<br>
 * The month, if specified, should be in the range of 1-12.<br>
 * The day, if specified, should be in the range of 1-31.<br>
 * The hour, if specified, should be in the range of 0-23.<br>
 * The minute and second, if specified, should be in the range of 0-59.<br>
 *
 * For the <tt>-t</tt> option:<br>
 * Year, month, and day default to the current date if not specified.<br>
 * Hour, minute, and second default to 0 if not specified.
 *
 * For the <tt>-d</tt> option:<br>
 * Year, month, day, hour, minute, and second default to 0 if not specified.
 *
 * When used with the <tt>-m</tt> option, the utility will print the maximum 
 * amount of time in the future that the power-on time can be set (in days).
 * This option cannot be used with any others.
 * 
 * When used with the <tt>-s</tt> option, the utility will shut down the 
 * system with <tt>shutdown -h now</tt> after the power-on time has been set. 
 * If the utility is unable to set the power-on time for any reason, the 
 * system will not be shut down.
 * 
 * @author Michael Strosaker <strosake@us.ibm.com>
 */

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <librtas.h>
#include "librtas_error.h"
#include "pseries_platform.h"

#define PROC_FILE_RTAS_CALL "/proc/device-tree/rtas/set-time-for-power-on"
#define PROC_FILE_MAX_LATENCY "/proc/device-tree/rtas/power-on-max-latency"
#define ERROR_BUF_SIZE 40

#define HARDWARE_ERROR -1	/**< RTAS hardware error return code */
#define PARAMETER_ERROR -3	/**< RTAS parameter error return code */

#define SECS_IN_HOUR 60*60
#define SECS_IN_DAY SECS_IN_HOUR*24
#define SECS_IN_MONTH SECS_IN_DAY*30	/**< estimated */
#define SECS_IN_YEAR 365*SECS_IN_DAY	/**< estimated */

/**
 * print_usage
 * @brief print the set_poweron_time usage message
 *
 * @param cmd argv[0]
 */
void print_usage(char *cmd) {
	printf ("Usage: %s [-d delta | -t time] [-s] [-m] [-h]\n", cmd);
}

/**
 * print_help
 * @brief print the help message for set_poweron_time
 *
 * @param cmd argv[0]
 */
void print_help(char *cmd) {
	print_usage(cmd);
	printf ("  -d delta    specify the time to restart the system as a delta from the present\n");
	printf ("  -t time     specify a date and time to restart the system\n");
	printf ("  -s          shutdown the system in one minute if scheduling the time for\n");
	printf ("              power-on succeeded\n");
	printf ("  -m          show the maximum amount of time in the future (in days) the\n");
	printf ("              power-on time can be scheduled\n");
	printf ("  -h          print this help message\n");
	printf ("Specifying dates:\n");
	printf ("  Y<year>M<month>D<day>h<hour>m<minute>s<second>n<nanosecond>\n");
	printf ("  For the -d option:\n");
	printf ("    Year, month, and day default to 0 if not specified.\n");
	printf ("    Hour, minute, second, and nanosecond default to 0 if not specified.\n");
	printf ("  For the -t option:\n");
	printf ("    Year, month, and day default to the current date if not specified.\n");
	printf ("    Hour, minute, second, and nanosecond default to 0 if not specified.\n");
	printf ("Examples:\n");
	printf ("  Shut down the system and schedule it to restart in 12 hours and 10 minutes:\n");
	printf ("    %s -d h12m10 -s\n", cmd);
	printf ("  Schedule the system to restart at noon on June 15th of this year:\n");
	printf ("    %s -t M6D15h12\n", cmd);
}

/**
 * check_rtas_call
 * @brief Ensure that the set-time-for-poweron property is in the OF tree
 *
 * @return 0 on success, !0 otherwise
 */
int check_rtas_call(void) {
	int fd;

	if ((fd = open(PROC_FILE_RTAS_CALL, O_RDONLY, 0)) == -1) {
		return 0;
	}
	close(fd);
	return 1;
}

/**
 * get_max_latency
 * @brief retrieve the maximum poweron latency from OF device tree
 *
 * @return latency
 */
uint32_t get_max_latency(void) {
	int fd, n;
	uint32_t max;

	if ((fd = open(PROC_FILE_MAX_LATENCY, O_RDONLY, 0)) == -1) {
		return 28;	/* assumed default of 28 days, per RPA */
	}
	n = read(fd, &max, 4);
	close(fd);

	if (n != 4)
		return 28;
	else
		return max;
}

/**
 * get_current_epoch
 * @brief get the current time
 * @return current time
 */
time_t get_current_epoch(void) {
	return time(NULL);
}

/**
 * conv_epoch_to_local_tm
 * @brief conver the epoch time to a local time
 *
 * @param e epoch time
 * @return converted local time
 */
struct tm *conv_epoch_to_local_tm(time_t *e) {
	return localtime(e);
}

/**
 * conv_local_tm_to_utc_tm
 * @brief convert local time to UTC time
 *
 * @param l local time
 * @return converted UTC time
 */
struct tm *conv_local_tm_to_utc_tm(struct tm *l) {
	time_t e = mktime(l);
	return gmtime(&e);
}

/**
 * conv_epoch_to_utc_tm
 * @brief convert epoch time to UTC time
 *
 * @param e epoch time
 * @return converted UTC time
 */
struct tm *conv_epoch_to_utc_tm(time_t *e) {
	return gmtime(e);
}

/**
 * conv_epoch_to_utc_string
 * @brief convert epoch time to an ascii string
 *
 * @param e epoch time
 * @return ascii string 
 */
char *conv_epoch_to_utc_string(time_t *e) {
	struct tm *t = gmtime(e);
	return asctime(t);
}

int main(int argc, char **argv) {
	char *date = NULL;
	int c, mflag = 0, sflag = 0, dflag = 0, tflag = 0, rc;
	size_t i;
	pid_t child;
	uint32_t max_latency;
	uint32_t year = 0, month = 0, day = 0;
	uint32_t hour = 0, min = 0, sec = 0, nsec = 0;
	time_t epoch, delta;
	struct tm *loc, *utc;
	char *shutdown_args[] = { "shutdown", "-h", "+1", NULL };
	char error_buf[ERROR_BUF_SIZE];

	switch (get_platform()) {
	case PLATFORM_UNKNOWN:
	case PLATFORM_POWERKVM_HOST:
		fprintf(stderr, "%s: is not supported on the %s platform\n",
							argv[0], platform_name);
		return 1;
	}

	/* Parse command line options */
	opterr = 0;
	while ((c = getopt (argc, argv, "d:t:hsm")) != -1) {
		switch (c) {
		case 'd':
			date = optarg;
			dflag = 1;
			break;
		case 't':
			date = optarg;
			tflag = 1;
			break;
		case 'h':
			print_help(argv[0]);
			return 0;
		case 'm':
			mflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case '?':
			if (isprint (optopt))
				printf ("Unrecognized option: -%c.\n", optopt);
			else
				printf ("Unrecognized option character \\x%x.\n", optopt);
			print_usage(argv[0]);
			return 1;
		default:
			abort();
		}
	}

	/* Option checking */
	if (mflag && argc > 2) {
		fprintf (stderr, "-m cannot be used with any other options.\n");
		print_usage(argv[0]);
		return 1;
	}

	if (dflag && tflag) {
		fprintf (stderr, "The -d and -t options cannot be used together.\n");
		print_usage(argv[0]);
		return 1;
	}

	if (!dflag && !tflag && !mflag) {
		fprintf (stderr, "One of the following options must be provided: -m, -d, or -t.\n");
		print_usage(argv[0]);
		return 1;
	}

	if (!check_rtas_call()) {
		fprintf (stderr, "The option to set a time for power-on is not available on this system.\n");
		return 2;
	}

	max_latency = get_max_latency();
	if (mflag) {
		printf ("The power-on time may be set up to %u days in the future.\n",
				max_latency);
		return 0;
	}

	/* Parse the date string */
	if (!date) {
		/* Should not get here */
		fprintf (stderr, "No date string available.\n");
		return -1;
	}
	for (i=0; i<strlen(date); i++) {
		switch (date[i]) {
		case 'Y':
			sscanf(date+i+1, "%d", &year);
			break;
		case 'M':
			sscanf(date+i+1, "%d", &month);
			break;
		case 'D':
			sscanf(date+i+1, "%d", &day);
			break;
		case 'h':
			sscanf(date+i+1, "%d", &hour);
			break;
		case 'm':
			sscanf(date+i+1, "%d", &min);
			break;
		case 's':
			sscanf(date+i+1, "%d", &sec);
			break;
		case 'n':
			sscanf(date+i+1, "%d", &nsec);
			break;
		default:
			if((date[i] < '0') || (date[i] > '9')) {
				printf("Invalid date specification: %s\n",
					date);
				return 1;
			}
			break;
		}
	}

	epoch = get_current_epoch();
	if (dflag) {

		/*
		 * A delta from the current time was specified;
		 * retrieve the current time and add the delta.
		 */

		delta = epoch + sec + (min * 60);
		delta += (hour * SECS_IN_HOUR);
		delta += (day * SECS_IN_DAY);
		delta += (month * SECS_IN_MONTH);
		delta += (year * SECS_IN_YEAR);

		utc = conv_epoch_to_utc_tm(&delta);

	}
	else {

		/*
		 * An absolute time was specified; if the year,
		 * month, or day was not specified, set it to the
		 * current year, month, or day as appropriate.
		 */

		if (year==0 || month==0 || day==0) {
			loc = conv_epoch_to_local_tm(&epoch);
			if (year==0)
				year = loc->tm_year + 1900;
			if (month==0)
				month = loc->tm_mon + 1;
			if (day==0)
				day = loc->tm_mday;
		}

		loc = malloc(sizeof(struct tm));
		if (!loc) {
			fprintf(stderr, "Out of memory");
			exit(3);
		}

		loc->tm_year = year-1900;
		loc->tm_mon = month-1;
		loc->tm_mday = day;
		loc->tm_hour = hour;
		loc->tm_min = min;
		loc->tm_sec = sec;
		utc = conv_local_tm_to_utc_tm(loc);
		free(loc);

	}

	/* Make the set-time-for-power-on RTAS call */
        rc = rtas_set_poweron_time(utc->tm_year+1900,
			utc->tm_mon+1, utc->tm_mday, utc->tm_hour,
			utc->tm_min, utc->tm_sec, 0);
        if (rc) {
		if (is_librtas_error(rc)) {
			librtas_error(rc, error_buf, ERROR_BUF_SIZE);
			fprintf(stderr, "Could not set power-on time\n%s\n",
				error_buf);
		} else {
			fprintf(stderr, "Could not set power-on time\n");
		}

                return 4;
        }

	/* Print the current time, and the newly set power-on time */
	printf("The power-on time was successfully set to:\n\t  UTC: %s",
			asctime(utc));
	printf("The current time is:\n\t  UTC: %s",
			conv_epoch_to_utc_string(&epoch));
	printf("\tLocal: %s", ctime(&epoch));

	/* Shut down if requested to do so */
	if (sflag) {
		child = fork();
		if (child == -1) {
			fprintf(stderr, "Could not begin shutdown.  System must be shut down manually.\n");
		}
		else if (child == 0) {
			/* child process */
			rc = execv("/sbin/shutdown", shutdown_args);

			/* shouldn't get here */
			fprintf(stderr, "Could not execute shutdown.  System must be shut down manually.\n");
			exit(-1);
		}
	}

	return 0;
}

