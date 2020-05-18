/**
 * @file ioa_bus_error.c
 * @brief Hardware Error Injection Tool IO Adapter Error module
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Linas Vepstas <nfont@us.ibm.com>
 *
 * Inject errors into an IO Adapter (PCI) bus slot.
 *
 * Copyright (c) 2004 IBM Corporation
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

#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <librtas.h>

#include "errinjct.h"

static int function = -1;    /**< type of ioa bus error to inject */
static uint32_t phb_id_lo;   /**< lo bits of PHB id */
static uint32_t phb_id_hi;   /**< hi bits of PHB id */
static uint64_t bus_addr;    /**< bus address to inject error on */
static uint32_t config_addr; /**< config address of adapter */
static uint64_t mask = 0x0;  /**< mask */
static char *sysfsname;      /**< sysfs name of adapter to inject to */
static char *loc_code;       /**< location code of adapter to inject to */

#define IOA_BUSERR_MAXFUNC		19

#define BUFSZ 4000

/**
 * ioa_buserr_fnames
 * @brief list of IOA bus error functionalities
 *
 * List of types of errors to inject.  Note that this list
 * must correspond 1-1 with the RPA numeric values sent into RTAS.
 * Do not reorder this list.
 */
static char *ioa_buserr_fnames[] = {
	"Load to PCI Memory Address Space - inject an Address Parity Error",
	"Load to PCI Memory Address Space - inject a Data Parity Error",
	"Load to PCI I/O Address Space - inject an Address Parity Error",
	"Load to PCI I/O Address Space - inject a Data Parity Error",
	"Load to PCI Configuration Space - inject an Address Parity Error",
	"Load to PCI Configuration Space - inject a Data Parity Error",
	"Store to PCI Memory Address Space - inject an Address Parity Error",
	"Store to PCI Memory Address Space - inject a Data Parity Error",
	"Store to PCI I/O Address Space - inject an Address Parity Error",
	"Store to PCI I/O Address Space - inject a Data Parity Error",
	"Store to PCI Configuration Space - inject an Address Parity Error",
	"Store to PCI Configuration Space - inject a Data Parity Error",
	"DMA read to PCI Memory Address Space - inject an Address Parity Error",
	"DMA read to PCI Memory Address Space - inject a Data Parity Error",
	"DMA read to PCI Memory Address Space - inject a Master Abort Error",
	"DMA read to PCI Memory Address Space - inject a Target Abort Error",
	"DMA write to PCI Memory Address Space - inject an Address Parity Error",
	"DMA write to PCI Memory Address Space - inject a Data Parity Error",
	"DMA write to PCI Memory Address Space - inject a Master Abort Error",
	"DMA write to PCI Memory Address Space - inject a Target Abort Error"
};


/**
 * ioa_bus_error_usage
 * @brief print the "IOA bus error" error injection usage message
 *
 * @param ei_func errinjct functionality
 * @param show_codes flag to print the IOA bus error function names
 * @param is64bit Is this for a ioa-bus-error-64 call
 */
static void
ioa_bus_error_usage(ei_function *ei_func, int show_codes, int is64bit)
{
	int i;

	printf("Usage: %s %s [OPTIONS]\n", progname, ei_func->name);
	printf("       %s %s [OPTIONS]\n", progname, ei_func->alt_name);
	printf("%s\n", ei_func->desc);
	printf("This will inject an EEH bus error to the slot\n");
	printf("A freeze condition should trigger on the next access to the adapter.\n\n");

	printf("Mandatory arguments:\n");
	printf(HELP_FMT, "-f function", "IOA bus error to inject");
	printf("\n  Specify a device either with the -s flag, or -p flag,\n");
	printf("  or use the explicit BUID/config address flags.\n");

	printf(HELP_FMT, "-s classpath",
	       "look up device by sysfs classpath");
	printf(HELP_FMT, "", "for example -s net/eth3 or -s scsi_host/host0\n");
	printf(HELP_FMT, "-p loc-code",
	       "look up device by location code");
	printf(HELP_FMT, "", "for example -p \"U0.1-P2-I1\"");
	printf("\n");

	printf("  Explicit BUID/config mandatory arguments:\n");
	printf(HELP_FMT, "-c config_addr", "configure address of the IOA");
	printf(HELP_FMT, "-h high_bits", "high bits of PHB unit id");
	printf(HELP_FMT"\n", "-l lo_bits", "lo bits of PHB unit id");

	print_optional_args();
	if (is64bit) {
		printf(HELP_FMT, "-a addr",
		       "64-bit address at which to report the error");
		printf(HELP_FMT, "-m mask",
		       "64-bit address mask (defaults to 0x0)");
	} else {
		printf(HELP_FMT, "-a addr",
		       "32-bit address at which to report the error");
		printf(HELP_FMT, "-m mask",
		       "32-bit address mask (defaults to 0x0)");
	}

	print_cpu_arg();
	print_token_arg();

	if (show_codes) {
		printf("\nFunctions for %s:\n", ei_func->name);
		for (i = 0; i <= IOA_BUSERR_MAXFUNC; i++)
			printf("%3d - %s\n", i, ioa_buserr_fnames[i]);
	}
}

/**
 * ioa_bus_error_arg
 * @brief check for "IOA bus errors" cmdline args
 *
 * @param arg cmdline arg to check
 * @param optarg optional cmdline argument to 'arg'
 * @return 0 - indicates this is a IOA bus error arg
 * @return 1 - indicates this is not an IOA bus error arg
 */
int ioa_bus_error_arg(char arg, char *optarg)
{
	switch (arg) {
	case 'a':
		bus_addr = strtoull(optarg, NULL, 16);
		break;
	case 'c':
		config_addr = strtoul(optarg, NULL, 16);
		break;
	case 'f':
		function = atoi(optarg);
		break;
	case 'h':
		phb_id_hi = strtoul(optarg, NULL, 16);
		break;
	case 'l':
		phb_id_lo = strtoul(optarg, NULL, 16);
		break;
	case 'm':
		mask = strtoull(optarg, NULL, 16);
		break;
	case 'p':
		loc_code = optarg;
		break;
	case 's':
		sysfsname = optarg;
		break;
	default:
		return 1;
	}

	return 0;
}

/**
 * get_config_addr_from_reg
 * @brief retrieve the config address from device-tree reg file
 *
 * Given the directory /proc/device-tree/pci@...,
 * yank out the config address out of the reg file
 *
 * @param devpath device-tree path of the reg file
 * @return 0 on failure, config address (!0) on success
 */
static uint32_t get_config_addr_from_reg(char *devpath)
{
	char path[BUFSZ];
	char *buf;
	uint32_t *be_caddr;
	uint32_t caddr = 0;

	strncpy(path, devpath, BUFSZ-5);
	strcat(path, "/reg");

	buf = read_file(path, NULL);
	if (!buf)
		return 1;

	be_caddr = (uint32_t *)buf;
	caddr = be32toh(*be_caddr);

	free(buf);
	return caddr;
}

/**
 * parse_sysfsname
 * @brief parse a sysfs name for IOA bus error injections
 *
 * Users can specify a sysfs name on the cmdline for the adapter they want
 * to inject IOA bus errors into.  This routine will parse the sysfsname
 * and retrieve the required data from sysfs to perform an IOA bus error
 * injection
 *
 * @return 0 on success, !0 otherwise
 */
static int parse_sysfsname(void)
{
	char path[BUFSZ];
	char *devspec;
	char *at;
	uint32_t addr;
	uint64_t phb_id;

	strcpy(path, "/sys/class/");
	strcat(path, sysfsname);
	strcat(path, "/device");
	if (!strncmp(sysfsname, "scsi_host", 9))
		strcat(path, "/..");
	strcat(path, "/devspec");

	devspec = read_file(path, NULL);
	if (!devspec)
		return 1;

	/* Now we parse something like /pci@400000000112/pci@2/ethernet@1 for
	 * BUID HI =4000 and LOW 00000112 */
	at = strchr(devspec, '@');
	if (!at || 0 == *at || 0 == *(++at)) {
		perr(errno, "Unable to parse devspec = %s\n", devspec);
		free(devspec);
		return 1;
	}

	phb_id = strtoull(at, NULL, 16);
	phb_id_hi = phb_id >> 32;
	phb_id_lo = phb_id & 0xFFFFFFFF;

	/* Obtain the config address from the device-tree reg file */
	strcpy(path, "/proc/device-tree/");
	strcat(path, devspec);
	addr = get_config_addr_from_reg(path);
	if (addr) {
		config_addr = addr;
		free(devspec);
		return 0;
	}

	free(devspec);
	return 1;
}

/**
 * recurse_hunt_file_contents
 * @brief search for a device with a given location code
 *
 * Walk directory structure recursively,
 * and try to find a device with a matchine ibm,loc-code.
 * If found, then copy the device-tree path into 'base_path'
 * and return a non-null pointer.
 *
 * (This return mechanism results in a few extra copies,
 * but so  what, its perf critical, and memory management
 * is a whole lot easier this way.)
 *
 * @param base_path base path to begin search at
 * @param filename filename we are searchhing for
 * @param desired_file_contents contents we are searching for in "filename"
 * @param chase_link_cnt specify how far to chase links
 * @return pointer to base path to "filename" on success
 * @return NULL on failure
 */
static char *recurse_hunt_file_contents(char *base_path, const char *filename,
					const char *desired_file_contents,
					int chase_link_cnt)
{
	char path[BUFSZ];
	char *loco;

	strcpy(path, base_path);
	strcat(path, filename);

	loco = read_file(path, NULL);
	if (loco) {
		int ndesire = strlen(desired_file_contents);

		if (0 == strncmp(loco, desired_file_contents, ndesire)) {
			free(loco);
			return base_path;
		}
	}

	if (loco)
		free(loco);

	/* Either this dir did not contain a "filename" file,
	 * or it did but the contents didn't match.  Now, search the subdirs.
	 */
	DIR *dir = opendir(base_path);

	if (!dir) {
		perr(errno, "Couldn't open %s\n", base_path);
		return NULL;
	}

	while (1) {
		struct dirent *de = readdir(dir);

		if (!de)
			break;

		if (((DT_DIR == de->d_type) && ('.' != de->d_name[0])) ||
		    ((DT_LNK == de->d_type) && (0 < chase_link_cnt))) {

			/* Don't chase links forever, only go so deep. */
			int depth = chase_link_cnt;

			if (DT_LNK == de->d_type)
				depth--;

			strcpy(path, base_path);
			strcat(path, "/");
			strcat(path, de->d_name);
			char *found = recurse_hunt_file_contents(path,
				      filename, desired_file_contents, depth);
			if (found) {
				closedir(dir);
				strcpy(base_path, found);
				return base_path;
			}
		}
	}
	closedir(dir);

	return NULL;
}

/**
 * hunt_loc_code
 * @brief Search for a specific location code
 *
 * Look for a specific IBM location code.
 * These are typically of the form U0.1-P2-I1/E1 or something like that.
 * Fill in the config addr, etc. based on what we find.
 *
 * @return 0 on success, !0 otherwise
 */
static int hunt_loc_code(void)
{
	char path[BUFSZ];
	char *match_dir;
	char *devspec;
	char *at;
	uint32_t addr;
	uint64_t phb_id;

	/* Try to find a device with a matching ibm,loc-code */
	strcpy(path, "/proc/device-tree");
	match_dir = recurse_hunt_file_contents(path, "/ibm,loc-code",
					       loc_code, 0);
	if (NULL == match_dir) {
		perr(0, "Unable to find location code %s in device tree\n",
		     loc_code);
		return 1;
	}

	devspec = path + strlen("/proc/device-tree");

	/* Now we parse something like /pci@400000000112/pci@2/ethernet@1 for
	 * BUID HI =4000 and LOW 00000112 */
	at = strchr(devspec, '@');
	if (!at || 0 == *at || 0 == *(++at)) {
		perr(errno, "Unable to parse devspec = %s\n", devspec);
		return 1;
	}

	phb_id = strtoull(at, NULL, 16);
	phb_id_hi = phb_id >> 32;
	phb_id_lo = phb_id & 0xFFFFFFFF;

	/* Try to get the config address from the dev-tree reg file. */
	addr = get_config_addr_from_reg(path);
	if (addr) {
		config_addr = addr;
		return 0;
	}
	return 1;
}

/**
 * ioa_bus_error
 * @brief "IOA bus error" error injection handler
 *
 * @param ei_func errinjct functionality
 * @return 0 on success, !0 otherwise
 */
int ioa_bus_error(ei_function *ei_func, int is64bit)
{
	int rc;

	if (ext_help) {
		ioa_bus_error_usage(ei_func, 1, is64bit);
		return 1;
	}

	/* Validate the function number */
	if ((function < 0) || (function > IOA_BUSERR_MAXFUNC)) {
		ioa_bus_error_usage(ei_func, 1, is64bit);
		return 1;
	}

	if ((loc_code != NULL) && (sysfsname != NULL)) {
		perr(0, "Only specify one of the -p or -s options\n");
		ioa_bus_error_usage(ei_func, 0, is64bit);
	}

	if (loc_code) {
		if (sysfs_check() != 0)
			return 1;

		rc = hunt_loc_code();
		if (rc) {
			printf("Unable to find info for %s:\n", loc_code);
			if (is64bit)
				printf("ADDR MASK:\t\t%.16lx\n", mask);
			else
				printf("ADDR MASK:\t\t%.8lx\n", mask);
			printf("CONFIG ADDR:\t\t%x\n", config_addr);
			printf("PHB UNIT_ID:\t\t%x%.8x\n", phb_id_hi,
			       phb_id_lo);
			printf("\nPlease try again or use the -s, -c, -h, and -l flags\n");
			return 1;
		}
	}

	if (sysfsname) {
		if (sysfs_check() != 0)
			return 1;

		rc = parse_sysfsname();
		if (rc) {
			printf("Unable to find info for %s:\n", sysfsname);
			if (is64bit)
				printf("ADDR MASK:\t\t%.16lx\n", mask);
			else
				printf("ADDR MASK:\t\t%.8lx\n", mask);
			printf("CONFIG ADDR:\t\t%x\n", config_addr);
			printf("PHB UNIT_ID:\t\t%x%.8x\n", phb_id_hi,
			       phb_id_lo);
			printf("\nPlease try again or use the -p, -c, -h, and -l flags\n");
			return 1;
		}
	}

	if ((config_addr == 0) || (phb_id_hi == 0) || (phb_id_lo == 0)) {
		printf("A sysfs device, slot location code, or\n");
		printf("config address and PHB Unit ID are required inputs.\n");
		printf("\nPlease try again, using the -s, -p or the -c, -h, and -l flags\n\n");
		ioa_bus_error_usage(ei_func, 0, is64bit);
		return 1;
	}

	/* Get the "slot mode" config address, for DDR and PCI-E slots
	 * that do not have an EADS bridge.
	 */
	uint32_t info;
	uint64_t phb_id = phb_id_hi;

	phb_id <<= 32;
	phb_id |= phb_id_lo;
	rc = rtas_get_config_addr_info2(config_addr, phb_id, 0, &info);
	if (rc == 0)
		config_addr = info;

	if (!be_quiet) {
		printf("Injecting an ioa-bus-error");
		if (verbose || dryrun) {
			printf(" with the following data:\n\n");

			if (is64bit) {
				printf("BUS ADDR:\t\t%.16lx\n", bus_addr);
				printf("ADDR MASK:\t\t%.16lx\n", mask);
			} else {
				printf("BUS ADDR:\t\t%.8lx\n", bus_addr);
				printf("ADDR MASK:\t\t%.8lx\n", mask);
			}
			printf("CONFIG ADDR:\t\t%x\n", config_addr);
			printf("PHB UNIT_ID:\t\t%x%.8x\n", phb_id_hi,
			       phb_id_lo);
			printf("FUNCTION:\t\t%d\n", function);
			printf("%s\n", ioa_buserr_fnames[function]);
		} else {
			printf("...\n");
		}
	}

	if (dryrun)
		return 0;

	if (is64bit) {
		err_buf[0] = htobe32(bus_addr >> 32);
		err_buf[1] = htobe32(bus_addr & 0xffffffff);
		err_buf[2] = htobe32(mask >> 32);
		err_buf[3] = htobe32(mask & 0xffffffff);
		err_buf[4] = htobe32(config_addr);
		err_buf[5] = htobe32(phb_id_hi);
		err_buf[6] = htobe32(phb_id_lo);
		err_buf[7] = htobe32(function);
	} else {
		err_buf[0] = htobe32(bus_addr);
		err_buf[1] = htobe32(mask);
		err_buf[2] = htobe32(config_addr);
		err_buf[3] = htobe32(phb_id_hi);
		err_buf[4] = htobe32(phb_id_lo);
		err_buf[5] = htobe32(function);
	}

	rc = do_rtas_errinjct(ei_func);
	if ((rc == 0) && verbose)
		printf("If the correct information was provided and there is\nactivity on the bus, the hardware should hit the error\nHowever, if incorrect information was provided or there\nis no bus activity, you may not get a hit.\n\n");

	return rc;
}

int ioa_bus_error32(ei_function *ei_func)
{
	return ioa_bus_error(ei_func, 0);
}

int ioa_bus_error64(ei_function *ei_func)
{
	return ioa_bus_error(ei_func, 1);
}
