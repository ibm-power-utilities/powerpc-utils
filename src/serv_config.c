/**
 * @file serv_config.c
 * @brief utility for configuring service policies and settings for 
 * IBM ppc64-based systems.
 */
/**
 * @mainpage serv_config documentation
 * @section Copyright
 * Copyright (c) 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @section Overview
 * This utility can be run in one of two modes: interactive mode, where
 * the user will be prompted for the value of each variable, or macro
 * mode, where the variables will be provided as a comma-delimited string
 * on the command line.
 *
 * For example, to update the surveillance parameters in interactive
 * mode, the user would type:<br>
 *	<tt>serv_config -s</tt><br>
 * To update the variables from the command line (macro mode):<br>
 *	<tt>serv_config --surveillance on,5,10,no</tt><br>
 * or:<br>
 *	<tt>serv_config --surveillance off,,,yes</tt><br>
 * to change only the value of the first and last variables.
 *
 * Macro mode is intended to be used by other utilities for automation.
 * A single variable can be specified for update with the -e option.
 *
 * @author Michael Strosaker <strosake@us.ibm.com>
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <librtas.h>
#define _GNU_SOURCE
#include <getopt.h>

#include "librtas_error.h"

#define NVRAM_PROGRAM     "/usr/sbin/nvram"
#define PATH_GET_SYSPARM  "/proc/device-tree/rtas/ibm,get-system-parameter"
#define PATH_SET_SYSPARM  "/proc/device-tree/rtas/ibm,set-system-parameter"
#define PATH_PARTITION_NO "/proc/device-tree/ibm,partition-no"
#define SURV_INDICATOR	  9000
#define BUF_SIZE	  5000	/* needs to be at least 4K + 2 bytes */
#define ERR_BUF_SIZE	  40
#define CALL_HOME_SYSPARM 30

char *cmd;			/**< argv[0] */
int verbose = 0;		/**< verbose (-v, --verbose) flag */
int no_rtas_get_sysparm = 0;	/**< rtas_get_sysparm call availability */
int no_rtas_set_sysparm = 0;	/**< rtas_set_sysparm call availability */
char *call_home_buffer = NULL;  /**< saved rtas call data */
int nvram_setupcfg = 0;		/**< nvram setupcfg partition availability */
int nvram_common = 0;		/**< nvram common partition availibility */
int nvram_ofconfig = 0;		/**< nvram ofconfig partition availability */

static struct option long_options[] = {
	{"surveillance",		optional_argument, NULL, 'S'},
	{"reboot-policy",		optional_argument, NULL, 'B'},
	{"remote-maint",		optional_argument, NULL, 'M'},
	{"remote-pon",			optional_argument, NULL, 'R'},
	{"scan-dump-policy",		optional_argument, NULL, 'D'},
	{"processor-diagnostics",	optional_argument, NULL, 'P'},
	{"verbose",			optional_argument, NULL, 'v'},
	{"list",			no_argument,       NULL, 'l'},
	{"restore",			required_argument, NULL, 'z'},
	{"help",			no_argument,       NULL, 'h'},
	{"force",			no_argument,       NULL, 'f'},
	{0,0,0,0}
};

/* return codes for serv_config functions */
#define RC_SUCCESS	0	/**< success!! */
#define RC_PERM		1	/**< not authorized (no error message printed
				 *   unless verbose) */
#define RC_NO_VAR	2	/**< variable not found (no error message
				 *   printed unless verbose) */ 
#define RC_HW_ERROR	3	/**< hardware error */
#define RC_PARAM_ERROR	4	/**< parameter error */
#define RC_LIB_ERROR	5	/**< library error */
#define RC_OTHER	6	/**< other error */

/**
 * @struct service_var
 * @brief nvram data variable description and data
 */
struct service_var {
	char *description;	/**< must not be NULL unless it is the
				 *   last entry in an array of entries */
	int type;		/**< one of the TYPE_* defines */
	char *nvram_var;	/**< NULL only for special cases */
	char *nvram_partition;	/**< can be NULL if var is never in nvram */
	int sysparm_num;	/**< either the sysparm number for the
				 *   ibm,{get|set}-system-parameter RTAS call, 
				 *   or NO_SYSPARM_NUM if the var can never be 
				 *   manipulated by those calls, or a -2 for 
				 *   the special case Remote Maintenance vars */
	int default_val;	/**< reserved for future expansion */
	uint16_t special;	/**< usually 0, sometimes on of the TYPE_*
				 *   defines */
};

/* values for the "type" field of service_var */
#define TYPE_STRING		0
#define TYPE_STRING_12		1	/**< string with a max of 12 chars */
#define TYPE_STRING_15		2	/**< string with a max of 15 chars */
#define TYPE_STRING_20		3	/**< string with a max of 20 chars */
#define TYPE_STRING_120		4	/**< string with a max of 120 chars */
#define TYPE_ON_OFF		5
#define TYPE_YES_NO		6
#define TYPE_FIRST_ALL		7
#define TYPE_BAUD		8	/**< one of 11 possible line speeds */
#define TYPE_NUM_0_1		9
#define TYPE_NUM_0_2		10
#define TYPE_NUM_0_3		11
#define TYPE_NUM_1_120		12
#define TYPE_NUM_1_255		13
#define TYPE_NUM_0_65535	14
#define TYPE_NUM_U64		15	/**< a 64-bit unsigned int */
#define TYPE_NUM_GT_1		16	/**< # greater than 1 with 12 chars 
					 *   or less */

#define NO_DEFAULT		INT_MIN
#define NO_SYSPARM_NUM		-1
#define USE_CALL_HOME_SYSPARM	-2

/* values for the "special" field of service_var */
#define SET_INDICATOR_9000	0x0001	/**< the 4th surveillance var */
#define REMOTE_MAINT		0x0002	/**< special handling for remote
					 *   maintenance variables */

/**
 * var surv_vars
 * @brief surveillance sensor service variables
 *
 * The last element is a special case; no NVRAM var or sysparm
 * corresponds with it; if "yes" is chosen, a set-indicator
 * RTAS call is invoked
 */
static struct service_var surv_vars[] = {
	{"Surveillance",
		TYPE_ON_OFF,	"sp-sen",	"ibm,setupcfg",	27, 0,  0},
	{"Surveillance time interval (in minutes)",
		TYPE_NUM_1_255,	"sp-sti",	"ibm,setupcfg",	28, 5,  0},
	{"Surveillance delay (in minutes)",
		TYPE_NUM_1_120,	"sp-sdel",	"ibm,setupcfg",	29, 10, 0},
	{"Changes are to take effect immediately",
		TYPE_YES_NO, NULL, NULL, NO_SYSPARM_NUM, NO_DEFAULT,
		SET_INDICATOR_9000},
	{0,0,0,0,0,0,0}
};

/**
 * @var ri_pon_vars
 * @brief nvram poweron ring variables
 */
static struct service_var ri_pon_vars[] = {
	{"Power On via Ring Indicate",
		TYPE_ON_OFF,	"sp-ri-pon",	"ibm,setupcfg",	23, 0,  0},
	{"Number of Rings Until Power On",
		TYPE_NUM_1_255,	"sp-rb4-pon",	"ibm,setupcfg",	24, 6,  0},
	{0,0,0,0,0,0,0}
};

/**
 * @var proc_diagnostic_vars
 * @brief processor diagnostic service variables.
 *
 */
static struct service_var proc_diagnostic_vars[] = {
	{"Platform Processor Diagnostic Status (0=Disabled, 1=Staggered,"
						"2=Immediate, 3=Periodic)",
		TYPE_NUM_0_3,	"platform-processor-diagnostics-run-mode",
		"ibm,setupcfg",	42, 0, 0},
	/* Will using 42, "0", 0 cause it to be in a disabled state ?? */
	{0,0,0,0,0,0,0}
};

/**
 * @var wol_vars
 * @brief nvram wake-on-lan variables
 */
static struct service_var wol_vars[] = {
	{"Wake On LAN",
		TYPE_ON_OFF,	"sp-remote-pon",	NULL,	23, 0,  0},
	{0,0,0,0,0,0,0}
};

/**
 * @var chosen_remote_pon_vars
 * @brief reference to remote poweron variables for this machine
 *
 * This will refer to either the ri_pon_vars or wol_vars array depending
 * on the type of machine the command is run on.
 */
struct service_var *chosen_remote_pon_vars;

/**
 * @var boot_vars
 * @brief nvram boot variables
 */
static struct service_var boot_vars[] = {
	{"Maximum Number of Reboot Attempts",
		TYPE_NUM_1_120,	"sp-bootrt-limit",	"ibm,setupcfg",
		NO_SYSPARM_NUM, 1, 0},
	{"Use the O/S Defined Restart Policy (1=Yes, 0=No)",
		TYPE_NUM_0_1,	"sp-os-plt-reboot",	"ibm,setupcfg",
		NO_SYSPARM_NUM, 0, 0},
	{"Enable Supplemental Restart Policy (1=Yes, 0=No)",
		TYPE_NUM_0_1,	"sp-plt-reboot","ibm,setupcfg",
		NO_SYSPARM_NUM, 1, 0},
	{"Call Out Before Restart",
		TYPE_ON_OFF,		"sp-dookc",	"ibm,setupcfg",
		NO_SYSPARM_NUM, 0, 0},
	{"Enable Unattended Start Mode (1=Yes, 0=No)",
		TYPE_NUM_0_1,	"sp-ac-reboot",	"ibm,setupcfg",
		NO_SYSPARM_NUM, 0, 0},
	{0,0,0,0,0,0,0}
};

/**
 * @var boot_lpar_vars
 * @brief nvram LPAR boot variables
 */
static struct service_var boot_lpar_vars[] = {
	{"Auto Restart Partition (1=Yes, 0=No)",
		TYPE_NUM_0_1, "partition_auto_restart",		NULL, 21, 1, 0},
	{"Auto Restart Following Power Loss (1=Yes, 0=No)",
		TYPE_NUM_0_1, "platform_auto_power_restart",	NULL, 22, 1, 0},
	{0,0,0,0,0,0,0}
};

/**
 * @var chosen_boot_vars
 * @brief reference to the boot varaibles for this machine
 *
 * This will refer to either boot_vars or boot_lpar_vars depending on the
 * machine the command is run on.
 */
struct service_var *chosen_boot_vars;

/**
 * @var scanlog_vars
 * @brief nvram scanlog variables
 */
static struct service_var scanlog_vars[] = {
	{"Scan Dump Control (0=Never, 1=Platform defined, 2=Always)",
		TYPE_NUM_0_2, 	"sdc",		NULL,	16, NO_DEFAULT, 0},
	{"Scan Dump Size (0=None, 1=Platform determined - Hardware abbreviated,"
		" 2=Platform determined - Software abbreviated, 3=All)",
		TYPE_NUM_0_3,	"sds",		NULL,	17, NO_DEFAULT, 0},
	{0,0,0,0,0,0,0}
};

/**
 * @var maint_vars
 * @brief nvram maintenance variables
 */
static struct service_var maint_vars[] = {
	{"Serial Port 1 - Retry String",
		TYPE_STRING_20,	"sp-rt-s1",	"ibm,setupcfg",	-2, 0, 0},
	{"Serial Port 1 - Protocol Interdata Block Delay (*IC)",
		TYPE_STRING_12,	"sp-ic-s1",	"ibm,setupcfg",	-2, 0, 0},
	{"Serial Port 1 - Protocol Time Out (*DT)",
		TYPE_STRING_12,	"sp-to-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Call Delay (*CD)",
		TYPE_STRING_12,	"sp-cd-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Connect (*CX)",		TYPE_STRING_12,
		"sp-connect-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Disconnect (*DX)",		TYPE_STRING_12,
		"sp-disconnect-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Call-Out Condition (*C0)",	TYPE_STRING_12,
		"sp-condout-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Call-Wait (*C0)",		TYPE_STRING_12,
		"sp-condwait-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Call-In Condition (*C1)",	TYPE_STRING_12,
		"sp-condin-s1",		"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Wait Call (*WC)",		TYPE_STRING_12,
		"sp-waitcall-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Describe How to Page a Beeper",
		TYPE_STRING_20,	"sp-page-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Call In Authorized",
		TYPE_ON_OFF,	"sp-diok-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Call Out Authorized",
		TYPE_ON_OFF,	"sp-dook-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Line Speed",
		TYPE_BAUD,	"sp-ls-s1",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 1 - Filename of Last Modem File Used to "
			"Configure Parameters",
		TYPE_STRING_120, "sp-modemf-s1", "ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Retry String",
		TYPE_STRING_20,	"sp-rt-s2",	"ibm,setupcfg",	-2, 0, 0},
	{"Serial Port 2 - Protocol Interdata Block Delay (*IC)",
		TYPE_STRING_12,	"sp-ic-s2",	"ibm,setupcfg",	-2, 0, 0},
	{"Serial Port 2 - Protocol Time Out (*DT)",
		TYPE_STRING_12,	"sp-to-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Call Delay (*CD)",
		TYPE_STRING_12,	"sp-cd-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Connect (*CX)",		TYPE_STRING_12,
		"sp-connect-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Disconnect (*DX)",		TYPE_STRING_12,
		"sp-disconnect-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Call-Out Condition (*C0)",	TYPE_STRING_12,
		"sp-condout-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Call-Wait (*C0)",		TYPE_STRING_12,
		"sp-condwait-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Call-In Condition (*C1)",	TYPE_STRING_12,
		"sp-condin-s2",		"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Wait Call (*WC)",		TYPE_STRING_12,
		"sp-waitcall-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Describe How to Page a Beeper",
		TYPE_STRING_20,	"sp-page-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Call In Authorized",
		TYPE_ON_OFF,	"sp-diok-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Call Out Authorized",
		TYPE_ON_OFF,	"sp-dook-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Line Speed",
		TYPE_BAUD,	"sp-ls-s2",	"ibm,setupcfg", -2, 0, 0},
	{"Serial Port 2 - Filename of Last Modem File Used to "
			"Configure Parameters",
		TYPE_STRING_120, "sp-modemf-s2", "ibm,setupcfg", -2, 0, 0},
	{"Service Center Telephone Number (*PS)",
		TYPE_STRING_20,	"sp-phsvc",	"ibm,setupcfg", -2, 0, 0},
	{"Customer Administration Center Telephone Number (*PH)",
		TYPE_STRING_20,	"sp-phadm",	"ibm,setupcfg", -2, 0, 0},
	{"Digital Pager Telephone Number",
		TYPE_STRING_20,	"sp-pager",	"ibm,setupcfg", -2, 0, 0},
	{"Customer System Telephone Number (*PY)",
		TYPE_STRING_20,	"sp-phsys",	"ibm,setupcfg", -2, 0, 0},
	{"Customer Voice Telephone Number (*PO)",
		TYPE_STRING_20,	"sp-vox",	"ibm,setupcfg", -2, 0, 0},
	{"Customer Account Number (*CA)",
		TYPE_STRING_12,	"sp-acct",	"ibm,setupcfg", -2, 0, 0},
	{"Call Out Policy (first/all) - Numbers to Call in Case of Failure",
		TYPE_FIRST_ALL,	"sp-cop",	"ibm,setupcfg",	-2, 0, 0},
	{"Customer RETAIN Login Userid (*LI)",
		TYPE_STRING_12,	"sp-retlogid",	"ibm,setupcfg", -2, 0, 0},
	{"Customer RETAIN Login Password (*PW)",
		TYPE_STRING_12,	"sp-retpw",	"ibm,setupcfg", -2, 0, 0},
	{"Remote Timeout (in seconds) (*RT)",
		TYPE_NUM_GT_1,	"sp-rto",	"ibm,setupcfg", -2, 0, 0},
	{"Remote Latency (in seconds) (*RL)",
		TYPE_NUM_GT_1,	"sp-rlat",	"ibm,setupcfg", -2, 0, 0},
	{"Number of Retries (while busy) (*RN)",
		TYPE_NUM_0_65535, "sp-m",	"ibm,setupcfg", -2, 0, 0},
	{"System Name (system administrator aid)",
		TYPE_STRING_15,	"sp-sysname",	"ibm,setupcfg", -2, 0, 0},
	{0,0,0,0,0,0,0}
};

/**
 * @var other_vars
 * @brief other nvram variables that don't fit elsewhere
 */
static struct service_var other_vars[] = {
	{"HMC 0",	TYPE_STRING, "hmc0",	NULL,	0,  NO_DEFAULT, 0},
	{"HMC 1",	TYPE_STRING, "hmc1",	NULL,	1,  NO_DEFAULT, 0},
	{"HMC 2",	TYPE_STRING, "hmc2",	NULL,	2,  NO_DEFAULT, 0},
	{"HMC 3",	TYPE_STRING, "hmc3",	NULL,	3,  NO_DEFAULT, 0},
	{"HMC 4",	TYPE_STRING, "hmc4",	NULL,	4,  NO_DEFAULT, 0},
	{"HMC 5",	TYPE_STRING, "hmc5",	NULL,	5,  NO_DEFAULT, 0},
	{"HMC 6",	TYPE_STRING, "hmc6",	NULL,	6,  NO_DEFAULT, 0},
	{"HMC 7",	TYPE_STRING, "hmc7",	NULL,	7,  NO_DEFAULT, 0},
	{"HMC 8",	TYPE_STRING, "hmc8",	NULL,	8,  NO_DEFAULT, 0},
	{"HMC 9",	TYPE_STRING, "hmc9",	NULL,	9,  NO_DEFAULT, 0},
	{"HMC 10",	TYPE_STRING, "hmc10",	NULL,	10, NO_DEFAULT, 0},
	{"HMC 11",	TYPE_STRING, "hmc11",	NULL,	11, NO_DEFAULT, 0},
	{"HMC 12",	TYPE_STRING, "hmc12",	NULL,	12, NO_DEFAULT, 0},
	{"HMC 13",	TYPE_STRING, "hmc13",	NULL,	13, NO_DEFAULT, 0},
	{"HMC 14",	TYPE_STRING, "hmc14",	NULL,	14, NO_DEFAULT, 0},
	{"HMC 15",	TYPE_STRING, "hmc15",	NULL,	15, NO_DEFAULT, 0},
	{"Memory CUoD Capacity Card Info",	TYPE_STRING,
		"mem-cuod-card-info",		NULL,	19, NO_DEFAULT, 0},
	{"SPLPAR Characteristics",		TYPE_STRING,
		"splpar-characteristics",	NULL,	20, NO_DEFAULT, 0},
	{"Snoop Sequence String",		TYPE_STRING,
		"sp-snoop-str",			NULL,	25, NO_DEFAULT, 0},
	{"Serial Snoop (1=Enabled, 0=Disabled)",TYPE_NUM_0_1,
		"sp-serial-snoop",		NULL,	26, NO_DEFAULT, 0},
	{"Current Flash Image (0=perm, 1=temp)",TYPE_NUM_0_1,
		"sp-current-flash-image",	NULL,	31, NO_DEFAULT, 0},
	{"Platform Dump Max Size",		TYPE_NUM_U64,
		"platform-dump-max-size",	NULL,	32, NO_DEFAULT, 0},
	{"EPOW3 Quiesce Time (in seconds)",	TYPE_NUM_0_65535,
		"epow3-quiesce-time",		NULL,	33, NO_DEFAULT, 0},
	{"Memory Preservation Boot Time (in seconds)",	TYPE_NUM_0_65535,
		"memory-preservation-boot-time",NULL,	34, NO_DEFAULT, 0},
	{"DASD Spin Interval (in seconds)",	TYPE_NUM_1_120,
		"ibm,dasd-spin-interval",	"common", -1, NO_DEFAULT, 0},
	{"Processor Module Information",	TYPE_STRING,
		"processor-module-info",	NULL,	43, NO_DEFAULT, 0},
	{0,0,0,0,0,0,0}
};

/**
 * print_usage
 * @brief print the usage message for serv_config
 */
static void 
print_usage(void) {
	printf ("Usage: %s [-l] [-b] [-s] [-r] [-m] [-z filename]\n"
		"\t-l: list all of the current policy settings\n"
		"\t-b: update the reboot policies\n"
		"\t-s: update the surveillance settings\n"
		"\t-r: update the remote power-on settings\n"
		"\t    (either \"ring indicate power-on\" or \"wake on LAN\")\n"
		"\t-m: update the remote maintenance settings\n"
		"\t-d: update the scan dump settings\n"
		"\t-p: update processor diagnostic settings\n"
		"\t-z: restore the settings saved in the specified "
		"backup file\n"
		"\t(Refer to the man page for advanced options.)\n", cmd);
}

#define ERR_MSG		0	/**< deontes an err_msg that is a true error */
#define WARN_MSG	1	/**< denotes an err_msg that is only a warning*/

/**
 * err_msg
 * brief print a error or warning message to stderr
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
 * update_nvram
 * @brief update the nvram partition
 *
 * This will attempt to fork a child process to execute the nvram
 * command and update the nvram val in the specified partition
 *
 * @param var nvram variable to update
 * @param val value to update "var" to
 * @param partition nvram partition to update
 * @return 0 on success, !0 otherwise
 */
int
update_nvram(char *var, char *val, char *partition) {
	char buf[256];
	pid_t child;
	int status, rc;
	char *nvram_args[] = { "nvram", "--update-config",
			buf, "-p", partition, NULL };

	strcpy(buf, var);
	strcat(buf, "=");
	strcat(buf, val);

	if (verbose > 1)
		printf("Updating NVRAM: %s(%s) = %s\n", var, partition, val);

	/* fork and exec nvram */
	child = fork();
	if (child == -1) {
		err_msg(ERR_MSG, "Cannot fork utility to update NVRAM\n");
		return 0;
	}
	else if (child == 0) {
		/* child process */
		rc = execv(NVRAM_PROGRAM, nvram_args);

		/* shouldn't get here */
		err_msg(ERR_MSG, "Could not exec %s to update NVRAM\n",
				NVRAM_PROGRAM);
		exit(1);
	}
	else {
		/* parent process */
		waitpid (child, &status, 0);
		if (status)
			return 0;
	}

	return 1;
}

/**
 * retrieve_from_nvram
 * @brief retrieve a nvram variable's value
 *
 * @param var nvram variable to retrieve
 * @param partition nvram partition to get "var" from
 * @param buf buffer to put "var"'s value into
 * @param size size of "buffer"
 * @return RC_SUCCESS n success
 * @return one of RC_* on failure
 */
int
retrieve_from_nvram(char *var, char *partition, char *buf, size_t size) {
	char nvram_args[BUF_SIZE];
	FILE *fp;
	int rc, len;

	if ((var == NULL) || (partition == NULL)) return 6;

	/* build the nvram command, checking to ensure the buffer is
	   not exceeded */
	len = strlen(NVRAM_PROGRAM);
	if (len >= BUF_SIZE) return RC_OTHER;
	strcpy(nvram_args, NVRAM_PROGRAM);
	len += strlen(" --print-config=");
	if (len >= BUF_SIZE) return RC_OTHER;
	strcat(nvram_args, " --print-config=");
	len += strlen(var);
	if (len >= BUF_SIZE) return RC_OTHER;
	strcat(nvram_args, var);
	len += strlen(" -p ");
	if (len >= BUF_SIZE) return RC_OTHER;
	strcat(nvram_args, " -p ");
	len += strlen(partition);
	if (len >= BUF_SIZE) return RC_OTHER;
	strcat(nvram_args, partition);

	if (verbose > 1)
		printf("Retrieving from nvram: %s(%s)\n", var, partition);

	/* open a pipe to nvram */
	if ((fp = popen(nvram_args, "r")) == NULL) {
		err_msg(ERR_MSG, "Cannot open a pipe with NVRAM "
				"retrieval utility.\n");
		return RC_OTHER;
	}
	rc = fread(buf, 1, size, fp);
	buf[rc-1] = '\0';
	rc = pclose(fp);

	if ((int8_t)WEXITSTATUS(rc) == (int8_t)-1) {
		if (verbose > 1)
			err_msg(WARN_MSG, "Cannot find the variable %s\n", var);
		return RC_NO_VAR;
	}

	return RC_SUCCESS;
}

/**
 * byte_to_string
 * @brief convert a byte into a ascii string
 *
 * @param num byte to convert
 * @param buf buffer to write conversion to
 * @param size size of "buffer"
 */
void
byte_to_string(uint8_t num, char *buf, size_t size) {
	int i;
	size_t length=1;

        for (i=10; i<=num; i*=10) length++;
        if (length > size-1) {
                buf[0]='\0';
                return;
        }

        for (i=length-1; i>=0; i--) {
                switch(num % 10) {
                        case 0: buf[i] = '0';  break;
                        case 1: buf[i] = '1';  break;
                        case 2: buf[i] = '2';  break;
                        case 3: buf[i] = '3';  break;
                        case 4: buf[i] = '4';  break;
                        case 5: buf[i] = '5';  break;
                        case 6: buf[i] = '6';  break;
                        case 7: buf[i] = '7';  break;
                        case 8: buf[i] = '8';  break;
                        case 9: buf[i] = '9';  break;
                }
                num = num / 10;
        }

	buf[length] = '\0';
	return;
}

/**
 * parse_call_home_buffer
 * @brief parse a call home buffer for a particular variable
 *
 * @param var variable to search for
 * @param buf buffer to search
 * @param size size of "buffer"
 * @return RC_SUCCESS on success
 * @return one of the RC_* on failure
 */
int
parse_call_home_buffer(char *var, char *buf, size_t size) {
	int buf_size;
	char *loc;

	if (!call_home_buffer) return RC_OTHER;	/* should never happen */

	buf_size = *(uint16_t *)call_home_buffer;
	loc = call_home_buffer + sizeof(uint16_t);

	while (loc[0] != '\0') {
		if (!strcmp(var, loc)) {
			loc = strstr(loc, "=");
			loc++;
			strncpy(buf, loc, size);
			return RC_SUCCESS;	/* success */
		}
		loc += (sizeof(loc) + 1);
	}

	if (verbose > 1)
		err_msg(WARN_MSG, "Could not find %s\n", var);

	return RC_NO_VAR;	/* variable not found */
}

/**
 * retrieve_value
 * @brief retrieve a nvram variable
 *
 * @param var service_var struct of variable to retrieve
 * @param buf buffer to write retrieved value to
 * @param size size of "buffer"
 * @return  RC_SUCCESS
 * @return one of RC_* on failure
 */
int
retrieve_value(struct service_var *var, char *buf, size_t size) {
	int rc, sysparm;
	char err_buf[ERR_BUF_SIZE];
	char param[BUF_SIZE];
	uint16_t ret_size;

	if (!no_rtas_get_sysparm && (var->sysparm_num != NO_SYSPARM_NUM))
	{ 
		sysparm = var->sysparm_num;
		if (sysparm == USE_CALL_HOME_SYSPARM) {
			if (call_home_buffer) {
				/* The call home parameters have already
				 * been retrieved; obtain the specific value
				 * from the saved call_home_buffer.
				 */
				return parse_call_home_buffer(var->nvram_var,
					buf, size);
			}
			sysparm = CALL_HOME_SYSPARM;
		}

		if (verbose > 1)
			printf("Retrieving sysparm: %d\n", sysparm);

		rc = rtas_get_sysparm(sysparm, BUF_SIZE, param);
		switch(rc) {
		case 0:
			if (var->sysparm_num == USE_CALL_HOME_SYSPARM)
				break;

			ret_size = *(uint16_t *)param;
			if (!strcmp(var->nvram_var, "sp-ri-pon") ||
				!strcmp(var->nvram_var, "sp-remote-pon") ||
				!strcmp(var->nvram_var, "sp-sen")) {

				if (param[2] == 0)
					strncpy(buf, "off", size);
				if (param[2] == 1)
					strncpy(buf, "on", size);
				return RC_SUCCESS;
			}

			if ((var->type == TYPE_NUM_0_1) ||
			    (var->type == TYPE_NUM_0_2) ||
			    (var->type == TYPE_NUM_0_3) ||
			    (var->type == TYPE_NUM_1_120) ||
			    (var->type == TYPE_NUM_1_255)) {
				byte_to_string(param[2], buf, size);
			}
			else {
				strncpy(buf, param+2, ((size>ret_size)?
					ret_size:size));
				buf[ret_size] = '\0';
			}
			return RC_SUCCESS;
		case -1:	/* hardware error */
			err_msg(ERR_MSG, "Hardware error retrieving %d (%s)\n",
					sysparm, var->nvram_var);
			return RC_HW_ERROR;
		case RTAS_UNKNOWN_OP:	/* RTAS call not available */
			no_rtas_get_sysparm = 1;
		case -3:	/* system parameter not supported */
			break;
		case -9002:	/* not authorized */
			if (verbose > 1)
				err_msg(ERR_MSG, "Not authorized to retrieve "
					"%d (%s)\n", sysparm, var->nvram_var);
			return RC_PERM;
		case -9999:	/* parameter error */
			err_msg(ERR_MSG, "Parameter error retrieving %d (%s)\n",
					sysparm, var->nvram_var);
			return RC_PARAM_ERROR;
		default:
			librtas_error (rc, err_buf, ERR_BUF_SIZE);
			err_msg(ERR_MSG, "Error retrieving %d (%s)\n%s\n",
					sysparm, var->nvram_var, err_buf);
			return RC_LIB_ERROR;
		}

		if ((rc == 0) && (var->sysparm_num == USE_CALL_HOME_SYSPARM)) {
			/* Save the call home parameters so that retrievals
			 * of additional varables in the future won't result
			 * in additional RTAS calls.
			 */
			if (call_home_buffer)
				free(call_home_buffer);
			call_home_buffer = malloc(BUF_SIZE);
			memcpy(call_home_buffer, param, BUF_SIZE);

			/* Parse the actual value out of the call home string
			 * to be returned.
			 */
			return parse_call_home_buffer(var->nvram_var,
				buf, size);
		}
	}

	if (var->nvram_partition) {
		if ((!strcmp(var->nvram_partition,"ibm,setupcfg") && nvram_setupcfg) ||
			(!strcmp(var->nvram_partition,"common") && nvram_common) ||
			(!strcmp(var->nvram_partition,"of-config") && nvram_ofconfig)) {

			return retrieve_from_nvram(var->nvram_var,
				var->nvram_partition, buf, size);
		}
	}

	if (verbose > 1)
		err_msg(WARN_MSG, "Cannot find the variable %s\n",
				var->nvram_var);

	return RC_NO_VAR;
}

/**
 * update_value
 * @brief Update a value in vram
 *
 * @param var variable to update in nvram
 * @param val value to update "var" to
 * @return 0 on success, !0 otherwise
 */
int
update_value(struct service_var *var, char *val) {
	int rc, setting=0;
	char err_buf[ERR_BUF_SIZE];
	char param[BUF_SIZE];

	if (var->special == SET_INDICATOR_9000) {
		if (!strcmp(val, "no"))
			return 1;

		/* this is the 4th surveillance var; need to call
		   set-indicator with token 9000, if it exists */
		if (!retrieve_value(&(surv_vars[0]), param, BUF_SIZE)) {
			if (!strncmp(param, "on", 2)) {
				if (!retrieve_value(&(surv_vars[1]), param,
							BUF_SIZE)) {
					setting = atoi(param);
				}
			}
		}

		if (verbose > 1)
		printf("Calling set-indicator(9000, 0, %d)\n", setting);

		rc = rtas_set_indicator(SURV_INDICATOR, 0, setting);
		switch(rc) {
		case 0:
			return 1;
		case -1:	/* hardware error */
			err_msg(ERR_MSG, "Hardware error setting the"
					" surveillance indicator\n");
			break;
		case -3:	/* no such indicator */
			err_msg(ERR_MSG, "The surveillance indicator does"
					" not exist on this system\n");
			break;
		default:
			err_msg(ERR_MSG, "General error setting the"
					" surveillance indicator\n");
			break;
		}
		return 0;
	}

	if (!no_rtas_set_sysparm && (var->sysparm_num != NO_SYSPARM_NUM))
	{ 
		if (verbose > 1)
			printf("Updating sysparm: %d = %s\n", 
					var->sysparm_num, val);

		if (!strcmp(var->nvram_var, "sp-ri-pon") ||
				!strcmp(var->nvram_var, "sp-remote-pon") ||
				!strcmp(var->nvram_var, "sp-sen")) {
			*(uint16_t *)param = 1;
			if (!strcmp(val, "on"))
				param[2] = (uint8_t)(1);
			if (!strcmp(val, "off"))
				param[2] = (uint8_t)(0);
		}
		else if ((var->type == TYPE_NUM_0_1) || 
			 (var->type == TYPE_NUM_0_2) ||
			 (var->type == TYPE_NUM_0_3) || 
			 (var->type == TYPE_NUM_1_120) ||
			 (var->type == TYPE_NUM_1_255)) {

			*(uint16_t *)param = 1;
			param[2] = (uint8_t)atoi(val);
		}
		else {
			*(uint16_t *)param = sizeof(val);
			strncpy(param+2, val, BUF_SIZE-2);
		}

		rc = rtas_set_sysparm(var->sysparm_num, param);
		switch(rc) {
		case 0:
			return 1;
		case -1:	/* hardware error */
			err_msg(ERR_MSG, "Hardware error updating %d\n",
					var->sysparm_num);
			return 0;
		case RTAS_UNKNOWN_OP:	/* RTAS call not available */
			no_rtas_set_sysparm = 1;
		case -3:	/* system parameter not supported */
			break;
		case -9002:	/* not authorized */
			err_msg(ERR_MSG, "Not authorized to update %d\n",
					var->sysparm_num);
			return 0;
		case -9999:	/* parameter error */

			/*SPECIAL CASE processor diagnostic "3" check */
			if (var->sysparm_num == 42)
				err_msg(ERR_MSG, "Currently this option is only"
					" supported through ASM menu.\n");

			err_msg(ERR_MSG, "Parameter error updating %d\n",
					var->sysparm_num);
			return 0;
		default:
			librtas_error (rc, err_buf, ERR_BUF_SIZE);
			err_msg(ERR_MSG, "Error updating %d\n%s\n",
					var->sysparm_num, err_buf);
			return 0;
		}
	}

	return update_nvram(var->nvram_var, val, var->nvram_partition);
}

/**
 * @var tokenizer_position
 * @brief current position in string to be tokenized.
 */
char *tokenizer_position = NULL;

/**
 * tokenize
 * @brief tokenize a string
 *
 * strtok() doesn't quite hack it; need to be able to tokenize ,,, into
 * 4 0-length strings.
 *
 * @param string string to tokenize
 * @param separator token seperator
 * @return pointer to next token
 */
char *
tokenize(char *string, char separator) {
	char *begin, *end;

	if ((string == NULL) && (tokenizer_position == NULL))
		return NULL;

	/* If string is NULL, act like strtok() */
	if (string == NULL)
		begin = end = tokenizer_position;
	else
		begin = end = string;

#if 0
	if (*end == '\0') {
		tokenizer_position = NULL;	* finished tokenizing *
		if (string == NULL) {
			if (*(end-1) == '\0')
				return end;
			else
				return NULL;
		}
		else
			return end;
	}
#endif
	if (*end == '\0') {
		tokenizer_position = NULL;
		return end;
	}

	while((*end != '\0') && (*end != separator))
		end++;

	if (*end == '\0')
		tokenizer_position = NULL;
	else {
		*end = '\0'; 
		tokenizer_position = end + 1;
	}

	return begin;
}

/**
 * validate_input
 * @brief validate an input parameter against the specified type
 *
 * @param in input to validate
 * @param type input type (one of the TYPE_* defines)
 * @return 0 on success, !0 otherwise
 */
int
validate_input(char *in, int type) {
	int num;
	uint64_t bignum;

	if (!in) return 0;

	switch(type) {
	case TYPE_STRING:
		break;
	case TYPE_STRING_12:
		if (strlen(in) > 12) {
			printf("Please limit your input to 12 characters");
			return 0;
		}
		break;
	case TYPE_STRING_15:
		if (strlen(in) > 15) {
			printf("Please limit your input to 15 characters");
			return 0;
		}
		break;
	case TYPE_STRING_20:
		if (strlen(in) > 20) {
			printf("Please limit your input to 20 characters");
			return 0;
		}
		break;
	case TYPE_STRING_120:
		if (strlen(in) > 120) {
			printf("Please limit your input to 120 characters");
			return 0;
		}
		break;
	case TYPE_ON_OFF:
		if (strcmp(in, "on") && strcmp(in, "off")) {
			printf("Please input either \"on\" or \"off\"");
			return 0;
		}
		break;
	case TYPE_YES_NO:
		if (strcmp(in, "yes") && strcmp(in, "no")) {
			printf("Please input either \"yes\" or \"no\"");
			return 0;
		}
		break;
	case TYPE_FIRST_ALL:
		if (strcmp(in, "first") && strcmp(in, "all")) {
			printf("Please input either \"first\" or \"all\"");
			return 0;
		}
		break;
	case TYPE_BAUD:
		if (strcmp(in, "300") && strcmp(in, "600") &&
				strcmp(in, "1200") && strcmp(in, "2000") &&
				strcmp(in, "2400") && strcmp(in, "3600") &&
				strcmp(in, "4800") && strcmp(in, "7200") &&
				strcmp(in, "9600") && strcmp(in, "19200") &&
				strcmp(in, "38400")) {
			printf("Please input a valid line speed: "
				"300, 600, 1200, 2000, 2400, 3500, 4800, "
				"7200, 9600, 19200, or 38400");
			return 0;
		}
		break;
	case TYPE_NUM_0_1:
		num = atoi(in);
		if ((num > 1) || (num < 0)) {
			printf("Please input either a 0 or a 1");
			return 0;
		}
		break;
	case TYPE_NUM_0_2:
		num = atoi(in);
		if ((num > 2) || (num < 0)) {
			printf("Please input a 0, 1 or 2");
			return 0;
		}
		break;
	case TYPE_NUM_0_3:
		num = atoi(in);
		if ((num > 3) || (num < 0)) {
			printf("Please input a 0, 1, 2 or 3");
			return 0;
		}
		break;
	case TYPE_NUM_1_120:
		num = atoi(in);
		if ((num > 120) || (num < 1)) {
			printf("Please input a number in the range "
				"of 1 to 120");
			return 0;
		}
		break;
	case TYPE_NUM_1_255:
		num = atoi(in);
		if ((num > 255) || (num < 1)) {
			printf("Please input a number in the range "
				"of 1 to 255");
			return 0;
		}
		break;
	case TYPE_NUM_0_65535:
		num = atoi(in);
		if ((num > 65535) || (num < 0)) {
			printf("Please input a number in the range "
				"of 0 to 65535");
			return 0;
		}
		break;
	case TYPE_NUM_U64:
		bignum = strtol(in, NULL, 0);
		if (bignum == ULONG_MAX) {
			printf("Please input an unsigned 64-bit number");
			return 0;
		}
		break;
	case TYPE_NUM_GT_1:
		num = atoi(in);
		if (num < 2) {
			printf("Please input a number greater than 1");
			return 0;
		}
		if (strlen(in) > 12) {
			printf("Please limit your input to 12 characters");
			return 0;
		}
		break;
	}

	return 1;
}

/**
 * interactive_prompts
 * @brief main routine using serv_config with an interactive prompt
 *
 * @param vars array of service_var structs for this interactive mode
 * @param buf buffer to write results of any requests to
 * @param size size of "buffer"
 * @return 0 on success, !0 otherwise
 */
int
interactive_prompts(struct service_var vars[], char *buf, size_t size) {
	int i=0, s, found_one=0;
	char input[BUF_SIZE], buffer[BUF_SIZE];

	buf[0] = '\0';

	while(vars[i].description) {
		if (vars[i].special == SET_INDICATOR_9000) {
			/* If this is the last variable, and no other variable
			 * was found, exit with a failure.
			 */
			if (!(vars[i+1].description) && !found_one) {
				printf("This category of service policies "
					"does not exist on this system.\n");
				return 0;
			}

			/* Handles the special surveillance case */
			strcpy(buffer, "no");
			goto prompt;
		}

		if (retrieve_value(&(vars[i]), buffer, BUF_SIZE)) {
			/* This variable is not supported; append a comma to
			 * the list in progress.
			 */
			if ((strlen(buf)+1) < size) {
				if (i != 0) strcat(buf, ",");
				i++;
				continue;
			}
			else {
				err_msg(ERR_MSG, "An internal buffer "
					"has been filled; exiting to "
					"avoid overflow.\n");
				return 0;
			}

			/* If these are the remote maintenance sysparms, and
			 * the get-system-param call failed, return
			 * immediately
			 *
			 * If this is the last variable, and no other variable
			 * was found, exit with a failure.
			 */
			if ((!(vars[i+1].description) && !found_one) ||
				vars[i].sysparm_num == USE_CALL_HOME_SYSPARM) {

				printf("This category of service policies "
					"does not exist on this system.\n");
				return 0;
			}
		}
		else {
			found_one = 1;
		}

prompt:
		printf("%s [%s]:  ", vars[i].description, buffer);

		if (fgets(input, BUF_SIZE, stdin)) {
			s = strlen(input) - 1;
			input[s] = '\0';
			if (s == 0 || !strcmp(input, buffer)) {
				if ((strlen(buf)+1) < size) {
					if (i != 0) strcat(buf, ",");
					i++;
				}
				else {
					err_msg(ERR_MSG, "An internal buffer "
						"has been filled; exiting to "
						"avoid overflow.\n");
					return 0;
				}
			}
			else if (validate_input(input, vars[i].type)) {
				if ((strlen(buf)+strlen(input)+1) < size) {
					if (i != 0) strcat(buf, ",");
					strcat(buf, input);
					i++;
				}
				else {
					err_msg(ERR_MSG, "An internal buffer "
						"has been filled; exiting to "
						"avoid overflow.\n");
					return 0;
				}
			}
			else
				printf(".\n");
			
		}
		else {
			err_msg(ERR_MSG, "An error has been encountered "
				"while retrieving user input.\n");
			return 0;
		}
	}

	if (!found_one) {
		printf("This category of service policies "
			"does not exist on this system.\n");
		return 0;
	}

	return 1;
}

/**
 * macro
 * @brief parse the results of an interactive mode session
 *
 * @param vars service_var struct array from the interactive session
 * @param input input from interactive session
 * @param input_validated has the input been validated?
 * @param force do we want to force any action
 * @return 0 on success, !0 otherwise
 */
int
macro(struct service_var vars[], char *input, int input_validated, int force) {
	int num=0, i=0, ret=0;
	char *tok, temp[BUF_SIZE];

	if (verbose > 2)
		printf("%s\n", input);
	strcpy(temp, input);

	while(vars[num].description)
		num++;
	
	tok = (char *)tokenize(temp, ',');
	do {
		if (i > num-1) {
			err_msg(ERR_MSG, "Too many variables specified; "
				"there are %d variables in the selected "
				"category.\n", num);
			return 0;
		}

		if (!input_validated)
			if ((*tok != '\0') &&
					!validate_input(tok, vars[i].type)) {
				printf(" for variable number %d (%s).\n",
					i+1, vars[i].nvram_var?
					vars[i].nvram_var:vars[i].description);
				ret = 1;
			}

		i++;
	} while ((tok = (char *)tokenize(NULL, ',')) != NULL);

	if (i < num) {
		err_msg(ERR_MSG, "Too few variables specified; there are "
			"%d variables in the selected category.\n", num);
		return 0;
	}

	if (ret) return 0;

	if (!force) do {
		printf("Are you certain you wish to update the system "
			"configuration\n\tto the specified values? "
			"(yes/no) [no]:  ");
		if (fgets(temp, BUF_SIZE, stdin)) {
			i = strlen(temp) -1;
			temp[i] = '\0';
			if (i==0)
				return 0;
			else {
				if ((i = validate_input(temp, TYPE_YES_NO)) == 1) {
					if (strcmp(temp, "yes"))
						return 0;
				}
				else
					printf(".\n");
			}
		}
	} while (!i);

	tok = (char *)tokenize(input, ',');
	i = 0;
	while(vars[i].description) {
		if (*tok != '\0') {
			if (!update_value(&(vars[i]), tok)) {
				err_msg(ERR_MSG, "Could not update a value "
					"necessary to continue: %s\n",
					vars[i].nvram_var);
				err_msg(ERR_MSG, "This category of service "
					"policies does not exist on this "
					"system.\n");
				return 0;
			}
		}

		tok = (char *)tokenize(NULL, ',');
		i++;
	}

	return 1;
}

/**
 * output_vars
 * @brief print the value of a set of service variables
 *
 * @param vars array of service vars to print out
 * @param out stream to print to
 */
void
output_vars(struct service_var vars[], FILE *out) {
	int i = 0, rc;
	char buf[BUF_SIZE];

	while(vars[i].description) {
		if (vars[i].nvram_var) {
			rc = retrieve_value(&(vars[i]), buf, BUF_SIZE);
			if (!rc)
				fprintf(out, "%s=%s\n", vars[i].nvram_var, buf);
			else if (verbose) {
				err_msg(WARN_MSG, "Could not retrieve the "
						"value for the variable %s\n",
						vars[i].nvram_var);
			}
		}
		i++;
	}
}

/**
 * find_and_update_var
 * @brief find a given nvram variable and update its value
 * 
 * If val is NULL, the existing value of the variable will be printed to
 * stdout  If include_others is non-zero, also check the other_vars array
 * of variables. If print is non-zero, print the value of the variable.
 *
 * @param name nvram variable name to update
 * @param val value to update the nvram variable to
 * @param include_others flag to search others array
 * @param print flag to print the value of the nvram variable
 * @return RC_SUCCESS on success
 * @return one of RC_* on failure
 */
int
find_and_update_var(char *name, char *val, int include_others, int print) {
	int i, found = 0;
	struct service_var var;
	char buffer[BUF_SIZE];

	/* Find the specified variable in the arrays */
	i = 0;
	while (surv_vars[i].description) {
		if (surv_vars[i].nvram_var && !strcmp(
				surv_vars[i].nvram_var, name)) {
			found = 1;
			var = surv_vars[i];
			break;
		}
		i++;
	}

	if (!found) {
		i = 0;
		while (chosen_remote_pon_vars[i].description) {
			if (!strcmp(chosen_remote_pon_vars[i].nvram_var,
			    name)) {
				found = 1;
				var = chosen_remote_pon_vars[i];
				break;
			}
			i++;
		}
	}

	if (!found) {
		i = 0;
		while (chosen_boot_vars[i].description) {
			if (!strcmp(chosen_boot_vars[i].nvram_var,
					name)) {
				found = 1;
				var = chosen_boot_vars[i];
				break;
			}
			i++;
		}
	}

	if (!found) {
		i = 0;
		while (maint_vars[i].description) {
			if (!strcmp(maint_vars[i].nvram_var, name)) {
				found = 1;
				var = maint_vars[i];
				break;
			}
			i++;
		}
	}

	if (!found) {
		i = 0;
		while (proc_diagnostic_vars[i].description) {
			if (!strcmp(proc_diagnostic_vars[i].nvram_var, name)) {
				found = 1;
				var = proc_diagnostic_vars[i];
				break;
			}
			i++;
		}
	}

	if (!found) {
		i = 0;
		while (scanlog_vars[i].description) {
			if (!strcmp(scanlog_vars[i].nvram_var, name)) {
				found = 1;
				var = scanlog_vars[i];
				break;
			}
			i++;
		}
	}

	if (!found && include_others) {
		i = 0;
		while (other_vars[i].description) {
			if (!strcmp(other_vars[i].nvram_var, name)) {
				found = 1;
				var = other_vars[i];
				break;
			}
			i++;
		}
	}

	if (!found)
		return RC_NO_VAR;

	if (!val) {
		if (retrieve_value(&var, buffer, BUF_SIZE))
			return RC_NO_VAR;
		if (print) {
			if (verbose)
				printf("%s=", name);
			printf("%s\n", buffer);
		}
	}
	else {
		if (validate_input(val, var.type)) {
			if (!update_value(&var, val))
				return RC_PARAM_ERROR;
		}
		else {
			printf(" for the value of %s.\n", name);
			return RC_LIB_ERROR;
		}
	}

	return RC_SUCCESS;
}

int 
main(int argc, char *argv[]) {
	int rc, option_index, validated = 0, i, j, s;
	int interactive_mode=0, macro_mode=0;
	int force_flag=0, l_flag=0, e_flag=0, z_flag=0;
	int surv_flag=0, boot_flag=0, maint_flag=0;
	int ri_pon_flag=0, scanlog_flag=0, proc_diag_flag=0;
	char *surv=NULL, *boot=NULL, *maint=NULL, *ri_pon=NULL;
	char *scanlog=NULL, *arg=NULL, *diag=NULL;
	char *val=NULL;
	char buffer[BUF_SIZE];
	FILE *fp;

#if 0
	printf("Tokenizing: on,5,10,yes\n");
	strcpy(buffer, "on,5,10,yes");
	val = tokenize(buffer, ',');
	i = 1;
	while (val) {
		printf("Value %d: %s\n", i++, val);
		val = tokenize(NULL, ',');
	}
	printf("Tokenizing: ,5,,\n");
	strcpy(buffer, ",5,,");
	val = tokenize(buffer, ',');
	i = 1;
	while (val) {
		printf("Value %d: %s\n", i++, val);
		val = tokenize(NULL, ',');
	}
	return 0;
		memset(buffer, 0, BUF_SIZE);
		rc = rtas_get_sysparm(0, BUF_SIZE, buffer);
		printf("hmc0(0) = %d, %s\n",  rc, buffer+2);
		memset(buffer, 0, BUF_SIZE);
		rc = rtas_get_sysparm(23, BUF_SIZE, buffer);
		printf("sp-ri-pon(23) = %d, %s\n",  rc, buffer+2);
		memset(buffer, 0, BUF_SIZE);
		rc = rtas_get_sysparm(27, BUF_SIZE, buffer);
		printf("sp-sen(27) = %d, %s\n",  rc, buffer+2);
		memset(buffer, 0, BUF_SIZE);
		rc = rtas_get_sysparm(30, BUF_SIZE, buffer);
		printf("sp-call-home(30) = %d, %s\n",  rc, buffer+2);
		return 0;

	memset(buffer, 0, BUF_SIZE);
	rc = rtas_get_sysparm(30, BUF_SIZE, buffer);
	printf("sp-call-home(30) = %d, %s\n\n",  rc, buffer+2);
#endif

	cmd = argv[0];
	if (argc == 1) {
		print_usage();
		return 1;
	}

	for (;;) {
		option_index = 0;
		rc = getopt_long(argc, argv, "e:bdsmrplz:fhv", long_options,
			       &option_index);
		if (rc == -1)
			break;
		switch (rc) {
		    case 'h':
			print_usage();
			exit(0);
		    case 'v':
			verbose += (optarg ? atoi(optarg) : 1);
			break;
		    case 'S':	/* surveillance macro */
			surv_flag = macro_mode = 1;
			surv = optarg;
			break;
		    case 's':	/* surveillance interactive */
			surv_flag = interactive_mode = 1;
			break;
		    case 'B':	/* reboot policy macro */
			boot_flag = macro_mode = 1;
			boot = optarg;
			break;
		    case 'b':	/* reboot policy interactive */
			boot_flag = interactive_mode = 1;
			break;
		    case 'M':	/* remote maint macro */
			maint_flag = macro_mode = 1;
			maint = optarg;
			break;
		    case 'm':	/* remote maint interactive */
			maint_flag = interactive_mode = 1;
			break;
		    case 'R':	/* ring indicate macro */
			ri_pon_flag = macro_mode = 1;
			ri_pon = optarg;
			break;
		    case 'r':	/* ring indicate interactive */
			ri_pon_flag = interactive_mode = 1;
			break;
		    case 'D':	/* scan dump macro */
			scanlog_flag = macro_mode = 1;
			scanlog = optarg;
			break;
		    case 'd':	/* scan dump interactive */
			scanlog_flag = interactive_mode = 1;
			break;
		    case 'P': /* Processor diagnostic macro */
			proc_diag_flag = macro_mode = 1;
			diag = optarg;
			break;
		    case 'p': /* Processor diagnostic interactive */
			proc_diag_flag = interactive_mode = 1;
			break;
		    case 'f':	/* force */
			force_flag = 1;
			break;
		    case 'l':	/* save policies */
			l_flag = 1;
			break;
		    case 'z':	/* restore policies */
			z_flag = 1;
			arg = optarg;
			break;
		    case 'e':	/* set a specified variable */
			e_flag = 1;
			arg = optarg;
			break;
		    case '?':
			exit(1);
			break;
		    default:
			printf("huh?\n");
			break;
		}
	}

	/* Command-line verification */
	for (i = optind; i < argc; i++) {
		err_msg(ERR_MSG,
			"Invalid argument %s\n", argv[i]);
		print_usage();
		return 1;
	}

	if (macro_mode && interactive_mode) {
		err_msg(ERR_MSG,
			"Macro mode options cannot be mixed with"
			"interactive options\n");
		print_usage();
		return 1;
	}

	if (force_flag &&
			((surv_flag && !surv) || (boot_flag && !boot)
			|| (maint_flag && !maint) ||
			(ri_pon_flag && !ri_pon) ||
			(proc_diag_flag && !diag) ||
			(scanlog_flag && !scanlog))) {
		err_msg(WARN_MSG,
			"--force ignored on interactive options; "
			"continuing...\n");
		force_flag = 0;
	}

	if (l_flag && (surv_flag || boot_flag || maint_flag || scanlog_flag ||
	    ri_pon_flag || proc_diag_flag || e_flag || z_flag || force_flag)) {
		err_msg(ERR_MSG,
			"The -l option cannot be used with any "
			"other options\n");
		print_usage();
		return 1;
	} 

	if (z_flag && (surv_flag || boot_flag || maint_flag || scanlog_flag ||
			ri_pon_flag || proc_diag_flag|| e_flag || l_flag)) {
		err_msg(ERR_MSG,
			"The -z option cannot be used with any options "
			"except --force\n");
		print_usage();
		return 1;
	} 

	if (e_flag && (surv_flag || boot_flag || maint_flag || scanlog_flag ||
	    ri_pon_flag || proc_diag_flag || l_flag || z_flag || force_flag)) {
		err_msg(ERR_MSG,
			"The -e option cannot be used with any "
			"other options\n");
		print_usage();
		return 1;
	} 

	/* Check for the availability of ibm,get-system-parameter and
	   ibm,set-system-parameter RTAS calls */
	/* Also check which boot vars struct is the correct one to use */
	chosen_boot_vars = boot_vars;
	if ((rc = open(PATH_GET_SYSPARM, O_RDONLY)) < 0) {
		no_rtas_get_sysparm = 1;
		if (verbose > 1)
			printf("ibm,get-system-parameter is not supported\n");
	}
	else {
		close(rc);
		if (verbose > 1)
			printf("ibm,get-system-parameter is supported\n");

		rc = rtas_get_sysparm(boot_lpar_vars[0].sysparm_num, 0, buffer);
		if (rc == 0)
			chosen_boot_vars = boot_lpar_vars;
	}
	if ((rc = open(PATH_SET_SYSPARM, O_RDONLY)) < 0) {
		no_rtas_set_sysparm = 1;
		if (verbose > 1)
			printf("ibm,set-system-parameter is not supported\n");
	}
	else {
		close(rc);
		if (verbose > 1)
			printf("ibm,set-system-parameter is supported\n");
	}

	/* Check for the existence of the NVRAM partitions */
	if ((fp = popen("/usr/sbin/nvram --partitions", "r")) == NULL) {
		err_msg(ERR_MSG, "Cannot open a pipe with NVRAM "
				"retrieval utility.\n");
		return 2;
	}
	rc = fread(buffer, 1, BUF_SIZE, fp);
	if (!ferror(fp)) {
		buffer[rc] = '\0';
		if (strstr(buffer, "ibm,setupcfg"))
			nvram_setupcfg = 1;
		if (strstr(buffer, "common"))
			nvram_common = 1;
		if (strstr(buffer, "of-config"))
			nvram_ofconfig = 1;
	}
	pclose(fp);

	if (verbose > 1) {
		printf("ibm,setupcfg NVRAM partition %s.\n",
			nvram_setupcfg?"exists":"does not exist");
		printf("common NVRAM partition %s.\n",
			nvram_common?"exists":"does not exist");
		printf("of-config NVRAM partition %s.\n",
			nvram_ofconfig?"exists":"does not exist");
	}

	/* Check which of remote ring indicate or wake on LAN is supported */
	if (retrieve_value(&(ri_pon_vars[1]), buffer, BUF_SIZE))
		chosen_remote_pon_vars = wol_vars;
	else
		chosen_remote_pon_vars = ri_pon_vars;

	if (l_flag) {
		output_vars(surv_vars, stdout);
		output_vars(chosen_boot_vars, stdout);
		output_vars(chosen_remote_pon_vars, stdout);
		output_vars(maint_vars, stdout);
		output_vars(scanlog_vars, stdout);
		output_vars(proc_diagnostic_vars, stdout);
		output_vars(other_vars, stdout);

		if (call_home_buffer) free(call_home_buffer);
		return 0;
	}

	if (z_flag) {
		if ((fp = fopen(arg, "r")) == NULL) {
			printf("Could not open %s for reading\n", arg);
			return 5;
		}

		j = 0;
		while (fgets(buffer, BUF_SIZE, fp)) {
			s = strlen(buffer) - 1;
			buffer[s] = '\0';

			if ((val = strstr(buffer, "=")) != NULL) {
				*val = '\0';
				val++;
			}

			rc = find_and_update_var(buffer, val, 0, 0);

			switch (rc) {
			case RC_SUCCESS:
				j++;
				break;
			case RC_NO_VAR:
				err_msg(ERR_MSG, "No service configuration "
					"variable named %s could be found.\n",
					buffer);
				break;
			case RC_HW_ERROR:
				err_msg(ERR_MSG, "Could not assign the value "
					"%s to %s.\n", val, buffer);
				break;
			case RC_PARAM_ERROR:
				err_msg(ERR_MSG, "Could not update %s to %s.\n",
					buffer, val);
				break;
			case RC_LIB_ERROR:
				break;
			default:
				err_msg(ERR_MSG, "Unexpected error "
					"manipulating %s.\n", buffer);
				break;
			}
		}

		fclose(fp);
		printf("%d service variables successfully restored from %s\n",
				j, arg);
		if (call_home_buffer) free(call_home_buffer);
		return 0;
	}

	if (e_flag) {
		if ((val = strstr(arg, "=")) != NULL) {
			*val = '\0';
			val++;
		}

		rc = find_and_update_var(arg, val, 1, 1);

		switch (rc) {
		case RC_SUCCESS:
			break;
		case RC_NO_VAR:
			err_msg(ERR_MSG, "No service configuration variable "
				"named %s could be found.\n", arg);
			break;
		case RC_HW_ERROR:
			err_msg(ERR_MSG, "Could not retrieve the value of "
				"%s.\n", arg);
			break;
		case RC_PARAM_ERROR:
			err_msg(ERR_MSG, "Could not update %s to %s.\n",
				arg, val);
			break;
		case RC_LIB_ERROR:
			break;
		default:
			err_msg(ERR_MSG, "Unexpected error manipulating %s.\n",
				arg);
			break;
		}

		if (call_home_buffer) free(call_home_buffer);
		return rc;
	}

	if (surv_flag) {
		if (!surv && interactive_mode) {
			printf("Surveillance Settings:\n"
			       "---------------------\n");
			validated = 1;
			if (interactive_prompts(surv_vars, buffer, BUF_SIZE))
				surv = buffer;
		}
		if (surv)
			macro(surv_vars, surv, validated, force_flag);
		if (!surv && macro_mode)
			output_vars(surv_vars, stdout);
	}

	if (ri_pon_flag) {
		if (!ri_pon && interactive_mode) {
			if (retrieve_value(&(ri_pon_vars[1]), buffer, BUF_SIZE)) {
				printf("Wake On LAN Settings:\n"
				       "--------------------\n");
			}
			else {
				printf("Ring Indicate Power On Settings:\n"
				       "-------------------------------\n");
			}

			validated = 1;
			if (interactive_prompts(chosen_remote_pon_vars,
					buffer, BUF_SIZE))
				ri_pon = buffer;
		}
		if (ri_pon)
			macro(chosen_remote_pon_vars, ri_pon, validated,
				force_flag);
		if (!ri_pon && macro_mode)
			output_vars(chosen_remote_pon_vars, stdout);
	}

	if (boot_flag) {
		if (!boot && interactive_mode) {
			printf("Reboot Policy Settings:\n"
                               "----------------------\n");
			validated = 1;
			if (interactive_prompts(chosen_boot_vars, buffer,
					BUF_SIZE))
				boot = buffer;
		}
		if (boot)
			macro(chosen_boot_vars, boot, validated, force_flag);
		if (!boot && macro_mode)
			output_vars(chosen_boot_vars, stdout);
	}

	if (maint_flag) {
		if (!maint && interactive_mode) {
			printf("Remote Maintenance Settings:\n"
                               "---------------------------\n");
			validated = 1;
			if (interactive_prompts(maint_vars, buffer, BUF_SIZE))
				maint = buffer;
		}
		if (maint)
			macro(maint_vars, maint, validated, force_flag);
		if (!maint && macro_mode)
			output_vars(maint_vars, stdout);
	}

	if (proc_diag_flag) {
		if (!diag && interactive_mode) {
			printf("Platform Processor Diagnostic Settings:\n"
                               "---------------------------\n");
			validated = 1;
			if (interactive_prompts(proc_diagnostic_vars, buffer, BUF_SIZE))
				diag = buffer;
		}
		if (diag)
			macro(proc_diagnostic_vars, diag, validated, force_flag);
		if (!diag && macro_mode)
			output_vars(proc_diagnostic_vars, stdout);
	}

	if (scanlog_flag) {
		if (!scanlog && interactive_mode) {
			printf("Scanlog Dump Settings:\n"
                               "---------------------\n");
			validated = 1;
			if (interactive_prompts(scanlog_vars, buffer, BUF_SIZE))
				scanlog = buffer;
		}
		if (scanlog)
			macro(scanlog_vars, scanlog, validated, force_flag);
		if (!scanlog && macro_mode)
			output_vars(scanlog_vars, stdout);
	}

	if (call_home_buffer) free(call_home_buffer);
	return 0;
}
