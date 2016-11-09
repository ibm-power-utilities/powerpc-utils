/**
 * @file nvram.c
 * @brief nvram access utility for powerpc platforms.
 */
/**
 * @mainpage nvram documentation
 * @section Copyright
 * Copyright (c) 2003, 2004, 2005 International Business Machines
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
 * The nvram command is used to print and modify data stored in the 
 * non-volatile RAM (NVRAM) on powerpc systems.  NVRAM on powerpc systems 
 * is split into several partitions, each with their own format.  
 *
 * The print options allow you to view the available partitions in NVRAM
 * and print their contents.
 *
 * The update options allow you to update certain partitions of NVRAM, 
 * namely those containing name=value pairs.  On many systems, the 
 * following NVRAM partitions contain data formatted as name=value pairs: 
 * common, of-config, and ibm,setupcfg.
 *
 * @author Nathan Fontenot <nfont@linux.vnet.ibm.com>
 * @author Michael Strosaker <strosake@us.ibm.com>
 * @author Todd Inglett <tinglett@us.ibm.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <dlfcn.h>
#include <netinet/in.h> /* for ntohs */
#include <glob.h>
#include <getopt.h>
#include <inttypes.h>
#include <zlib.h>
#if defined(__FreeBSD__)
#include <sys/endian.h>
#else
#include <endian.h>
#endif
#include <stdbool.h>

#include "nvram.h"

/**
 * @var nvram_cmdname
 * @brief name used to invoke thre nvram command (argv[0]) 
 */
char *nvram_cmdname;
static int verbose;

static struct option long_options[] = {
    {"verbose", 		optional_argument, NULL, 'v'},
    {"print-config",		optional_argument, NULL, 'o'},
    {"zero",			optional_argument, NULL, '0'},
    {"print-vpd", 		optional_argument, NULL, 'V'},
    {"print-all-vpd", 		optional_argument, NULL, 'W'},
    {"print-err-log", 		no_argument, 	   NULL, 'e'},
    {"print-event-scan", 	no_argument, 	   NULL, 'E'},
    {"partitions", 		no_argument, 	   NULL, 'P'},
    {"dump", 			required_argument, NULL, 'd'},
    {"ascii",			required_argument, NULL, 'a'},
    {"unzip", 			required_argument, NULL, 'z'},
    {"nvram-file", 		required_argument, NULL, 'n'},
    {"nvram-size", 		required_argument, NULL, 's'},
    {"update-config",		required_argument, NULL, 'u'},
    {"help", 			no_argument, 	   NULL, 'h'},
    {"partition",               required_argument, NULL, 'p'},
    {0,0,0,0}
};

/**
 * help
 * @brief print the help/usage message for nvram
 */
static void 
help(void)
{
    printf("nvram options:\n"
    "  --print-config[=var]\n"
    "          print value of a config variable, or print all variables in\n"
    "          the specified (or all) partitions\n"
    "  --zero | -0\n"
    "          terminate config pairs with a NUL character\n"
    "  --update-config <var>=<value>\n"
    "          update the config variable in the specified partition; the -p\n"
    "          option must also be specified\n"
    "  -p <partition>\n"
    "          specify a partition; required with --update-config option,\n"
    "          optional with --print-config option\n"
    "  --print-vpd\n"
    "          print VPD\n"
    "  --print-all-vpd\n"
    "          print VPD, including vendor specific data\n"
    "  --print-err-log\n"
    "          print checkstop error log\n"
    "  --print-event-scan\n"
    "          print event scan log\n"
    "  --partitions\n"
    "          print NVRAM paritition header info\n"
    "  --dump <name>\n"
    "          raw dump of partition (use --partitions to see names)\n"
    "  --ascii <name>\n"
    "          print partition contents as ASCII text\n"
    "  --unzip <name>\n"
    "          decompress and print compressed data from partition\n"
    "  --nvram-file <path>\n"
    "          specify alternate nvram data file (default is /dev/nvram)\n"
    "  --nvram-size\n"
    "          specify size of nvram data, must in multiples of 16 Bytes\n"
    "          (for repair operations)\n"
    "  --verbose (-v)\n"
    "          be (more) verbose\n"
    "  --help\n"
    "          print what you are reading right now.\n"
    );
}

/**
 * @def ERR_MSG
 * @brief define to denote error messages
 */
#define ERR_MSG		0
/**
 * @def WARN_MSG
 * @brief define to denote warning messages
 */
#define WARN_MSG	1
/**
 * @def MAXLINE
 * @brief maximum line length
 */
#define MAXLINE		512

/**
 * _msg
 * @brief print a message to stderr with the specified prefix
 *
 * @param msg_type either ERR_MSG or WARN_MSG
 * @param fmt formatted string a la printf()
 * @param ap initialized varaiable arg list
 */
void
_msg(int msg_type, const char *fmt, va_list ap)
{
    int	n;
    char buf[MAXLINE];

    n = snprintf(buf, sizeof(buf), "%s: %s", nvram_cmdname,
		(msg_type == WARN_MSG ? "WARNING: " : "ERROR: "));

    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);

    fflush(stderr);
    fputs(buf, stderr);
    fflush(NULL);
}

/**
 * err_msg
 * @brief print an error message to stderr
 *
 * @param fmt formatted string a la printf()
 */
void err_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void
err_msg(const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    _msg(ERR_MSG, fmt, ap);
    va_end(ap);
}

/**
 * warn_msg
 * @brief print a warning message to stderr
 *
 * @param fmt formatted string a la printf()
 */
void warn_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void
warn_msg(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    _msg(WARN_MSG, fmt, ap);
    va_end(ap);
}

/**
 * nvram_read
 * @brief read in the contents of nvram
 *
 * @param nvram nvram struct to read data into
 * @return 0 on success, !0 on failure
 */
int 
nvram_read(struct nvram *nvram)
{
    int len, remaining, chunk;
    char *p;

    /* read in small chunks */
    p = nvram->data;
    remaining = nvram->nbytes;
    chunk = (NVRAM_READ_SIZE < remaining) ? NVRAM_READ_SIZE : remaining;

    while ((len = read(nvram->fd, p, chunk)) > 0) {
        p+=len;
        remaining -= len;
        chunk = (NVRAM_READ_SIZE < remaining) ? NVRAM_READ_SIZE : remaining;
    }

    if (len == -1) {
        err_msg("cannot read \"%s\": %s\n", nvram->filename, strerror(errno));
        return -1;
    }
   
    /* If we are using the DEFAULT_NVRAM_SZ value we to do a small bit of
     * fixup here.  All of the remaining code assumes that nbytes contains
     * the actual size of nvram, not a guess-timated amount and bad things
     * ensue if it is not correct.
     */
    if (nvram->nbytes == DEFAULT_NVRAM_SZ) {
	    nvram->nbytes = nvram->nbytes - remaining;
	    remaining = DEFAULT_NVRAM_SZ - (remaining + nvram->nbytes);
    }

    if (remaining) {
	warn_msg("expected %d bytes, but only read %d!\n", 
	    	 nvram->nbytes, nvram->nbytes - remaining);
	/* preserve the given nbytes, but zero the rest in case someone cares */
	memset(p, 0, remaining);
    }

    if (verbose)
	printf("NVRAM size %d bytes\n", nvram->nbytes);

    return 0;
}

/**
 * checksum
 * @brief calculate the checksum for a partition header
 *
 * @param p pointer to partition header
 * @return calculated checksum
 */
static unsigned char 
checksum(struct partition_header *p)
{
    unsigned int c_sum, c_sum2;
    unsigned short *sp = (unsigned short *)p->name; /* assume 6 shorts */

    c_sum = p->signature + p->length + sp[0] + sp[1] + sp[2] + sp[3] 
	    		 + sp[4] + sp[5];

    /* The sum probably may have spilled into the 3rd byte.  Fold it back. */
    c_sum = ((c_sum & 0xffff) + (c_sum >> 16)) & 0xffff;

    /* The sum cannot exceed 2 bytes.  Fold it into a checksum */
    c_sum2 = (c_sum >> 8) + (c_sum << 8);
    c_sum = ((c_sum + c_sum2) >> 8) & 0xff;
    
    return c_sum;
}

/**
 * dump_raw_data
 * @brief raw data dump of a partition.
 * 
 * Note that data_len must be a multiple of 16 bytes which makes
 * for a cheap implementation.
 *
 * @param data pointer to data to be dumped
 * @param data_len length of data buffer to be dumped
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

/**
 * parse_of_common
 * @brief parse a config definition
 *
 * Parse an Open Firmware common config definition which
 * is of the form name=value and return its length.
 *
 * Note that the name will always be < 32 chars.  OF does
 * not specify the max value length, but the value is binary
 * and must be unquoted.  We assume 4k is enough.
 *
 * @param data pointer to start of raw data (NULL terminated)
 * @param data_len length of remaining raw data
 * @param outname buffer to write parsed name
 * @param outval buffer to write parsed output value
 * @return number of bytes parsed 
 */
int 
parse_of_common(char *data, int data_len, char *outname, char *outval)
{
    char *p, *np;
    char *p_end = data + data_len;

    for (p = data, np = outname; *p && *p != '='; p++) {
	*np++ = *p;
	if (np - outname > 32)
	    break;	/* don't overrun */
	if (p >= p_end) {
	    err_msg("partition corrupt:  ran off end parsing name\n");
	    return 0;
	}
    }
	
    *np = '\0';
    if (*p != '=') {
	err_msg("corrupt data:  no = sign found or name > 31 chars\n");
	return 0;
    }
    p++;
	
    /* Value needs to be unquoted */
    for (np = outval; *p; p++) {
	if (*p == (char)0xff) {
	    char ch, num;
	    p++;
		
	    if (p >= p_end) {
		err_msg("partition corrupt: ran off end parsing "
                        "quoted value\n");
		return 0;
	    }
			
	    num = *p & 0x7f;
	    ch = (*p & 0x80) ? 0xff : 0;	/* hi bit chooses ff or 00. */
	    if (np + num - outval > 4096)
		break;	/* don't overrun */
	
	    /* repeat char */
	    while (num > 0) {
		*np++ = ch;
		num--;
	    }

	} 
        else {
	    *np++ = *p;
	    if (np - outval > 4096)
		break;	/* don't overrun */
	}
	    
	if (p >= p_end) {
	    err_msg("partition corrupt:  ran off end parsing value\n");
	    return 0;
	} 
    }

    *np = '\0';
    if (*p) {
	err_msg("data value too long for this utility (>4k)\n");
	return 0;
	/* ToDo: recover */
    }
	
    return p - data;
}

/**
 * nvram_parse_partitions
 * @brief fill in the nvram structure with data from nvram 
 *
 * Fill in the partition parts of the struct nvram.
 * This makes handling partitions easier for the rest of the code.
 *
 * The spec says that partitions are made up of 16 byte blocks and
 * the partition header must be 16 bytes.  We verify that here.
 *
 * @param nvram pointer to nvram struct to fill out
 * @return 0 on success, !0 otherwise
 */
static int
nvram_parse_partitions(struct nvram *nvram)
{
    char *nvram_end = nvram->data + nvram->nbytes;
    char *p_start = nvram->data; 
    struct partition_header *phead;
    unsigned char c_sum;

    if (sizeof(struct partition_header) != 16) {
	err_msg("partition_header struct is not 16 bytes\n");
	return -1;
    }

    while (p_start < nvram_end) {
	phead = (struct partition_header *)p_start;
	nvram->parts[nvram->nparts++] = phead;
	c_sum = checksum(phead);
	if (c_sum != phead->checksum)
	    warn_msg("this partition checksum should be %02x!\n", c_sum);
	phead->length = be16toh(phead->length);
	p_start += phead->length * NVRAM_BLOCK_SIZE;
    }

    if (verbose)
	printf("NVRAM contains %d partitions\n", nvram->nparts);

    return 0;
}

bool part_name_valid(const char *name)
{
    if (strlen(name) > MAX_PART_NAME) {
        err_msg("partition name maximum length is %d\n", MAX_PART_NAME);
        return false;
    }

    return true;
}

/**
 * nvram_find_fd_partition
 * @brief Find a particular nvram partition using a file descriptor
 *
 * @param name name of the partition to find
 * @param nvram pointer to nvram struct to search
 * @return 0 on success, !0 otherwise
 */
static int
nvram_find_fd_partition(struct nvram *nvram, char *name)
{
    struct partition_header	phead;
    int				len;
    int				found = 0;

    if (!part_name_valid(name))
	    return -1;

    if (lseek(nvram->fd, SEEK_SET, 0) == -1) {
        err_msg("could not seek to beginning of file %s\n", nvram->filename);
	return -1;
    }
    
    while (! found) {
        len = read(nvram->fd, &phead, sizeof(phead));
	if (len == 0) { /* EOF */
	    err_msg("could not find %s partition in %s\n", 
	    	    name, nvram->filename);
	    return -1;
	} 
        else if (len != sizeof(phead)) {
	    err_msg("Invalid read from %s: %s\n", nvram->filename,
	    	    strerror(errno));
	    return -1;
	}

	if (! strncmp(phead.name, name, sizeof(phead.name)))
	    found = 1;
	else {
	    int offset =
		    be16toh(phead.length) * NVRAM_BLOCK_SIZE - len;
	    if (lseek(nvram->fd, offset, SEEK_CUR) == -1) {
	        err_msg("seek error in file %s: %s\n", nvram->filename,
			strerror(errno));
		return -1;
	    }
	}
    }

    if (! found) {
        err_msg("could not find %s partition in %s\n", name, nvram->filename);
	return -1;
    }

    /* we found the correct partition seek back to the beginning of it */
    if (lseek(nvram->fd, -len, SEEK_CUR) == -1) {
        err_msg("could not seek to %s partition\n", name);
	return -1;
    }

    return 0;
}
	
/**
 * nvram_find_partition
 * @brief Find a partition given a signature and name.
 * 
 * If signature is zero (invalid) it is not used for matching.
 * If name is NULL it is ignored.
 * start is the partition in which to resume a search (NULL starts at the first
 * partition).
 *
 * @param signature partition signature to find
 * @param name partition name to find
 * @param start partition header to start search at
 * @param nvram nvram struct to search
 * @return pointer to partition header on success, NULL otherwise 
 */
static struct partition_header *
nvram_find_partition(struct nvram *nvram, unsigned char signature, char *name, 
		     struct partition_header *start)
{
    struct partition_header *phead;
    int i;

    /* Get starting partition.  This is not terribly efficient... */
    if (start == NULL) {
	i = 0;
	if (verbose > 1)
	    printf("find partition starts with zero\n");
    } 
    else {
	for (i = 0; i < nvram->nparts; i++)
	    if (nvram->parts[i] == start)
		break;
	i++;	/* start at next partition */
	if (verbose > 1)
	    printf("find partition starts with %d\n", i);
    }

    /* Search starting with partition i... */
    while (i < nvram->nparts) {
	phead = nvram->parts[i];
	if (signature == '\0' || signature == phead->signature) {
	    if (name == NULL 
		|| strncmp(name, phead->name, sizeof(phead->name)) == 0) {
		return phead;
	    }
	}
	i++;
    }
	
    return NULL;
}

/**
 * print_partition_table
 * @brief print a table of available partitions
 *
 * @param nvram nvram struct of partitions
 */ 
static void 
print_partition_table(struct nvram *nvram)
{
    struct partition_header *phead;
    int i = 0;

    printf(" # Sig Chk  Len  Name\n");
    for (i = 0; i < nvram->nparts; i++) {
        phead = nvram->parts[i];
    	printf("%2d  %02x  %02x  %04x %.12s\n", i, phead->signature, 
	       phead->checksum, phead->length, phead->name);
    }
}

/**
 * getvalue
 * @brief Copy a value into a buf.
 * The first two bytes of the value is a length.
 * Return pointer to byte after the value.
 *
 * @param p pointer to value to copy
 * @param buf buffer to copy value into
 * @return pointer past the copied value in p
 */
static char *
getvalue(char *p, char *buf)
{
    int len = *p++;
    len |= ((*p++) << 8);
    memcpy(buf, p, len);
    buf[len] = '\0';
    return p+len;
}

/**
 * getsmallvlaue
 * @brief Copy a value into a buf.
 * The first one bytes of the value is a length.
 *
 * @param p pointer to value to copy
 * @param buf buffer to copy value into
 * @return pointer past the copied value in p
 */
static char *
getsmallvalue(char *p, char *buf)
{
    int len = *p++;
    memcpy(buf, p, len);
    buf[len] = '\0';
    return p+len;
}

/**
 * lookupfield
 * @brief translate a VPD field to human readable string
 *
 * Lookup a VPD field name (always 2 chars) and return a human 
 * readable string.
 *
 * @param p VPD field name
 * @return pointer to human readable string on success, NULL otherwise
 */
static char *
lookupfield(char *p)
{
    int i;

    for (i = 0; (i < sizeof(descs) / sizeof(descs[0])); i++) {
	if (strcmp(p, descs[i].name) == 0)
	    return descs[i].desc;
     }

    return NULL;
}

/**
 * printVPDfield
 * @brief Format and print a VPD field and return a ptr to the next field. 
 *
 * @param p pointer to VPD field
 * @param show_all verbosity level
 * @return pointer to next VPD field
 */
static char *
printVPDfield(char *p, int show_all)
{
    char field[3];
    char value[256];
    char *fname;

    field[0] = p[0]; field[1] = p[1]; field[2] = 0;
    p+=2;
    p = getsmallvalue(p, value);
    if ((fname = lookupfield(field)) != NULL)
	printf("	%-20s %s\n", fname, value);
    else if (show_all)
	printf("	%-20s %s\n", field, value);

    return p;
}

/**
 * dump_vpd
 * @brief Dump Vital Product Data
 * 
 * See Chapter 18: Expansion ROMs of the PCI spec.
 *
 * @param nvram nvram struct to retrieve VPD data from
 * @param show_all verbosity level
 */
int 
dump_vpd(struct nvram *nvram, int show_all)
{
    struct partition_header *phead;
    char *p, *p_end;
    char value[4096];

    phead = nvram_find_partition(nvram, NVRAM_SIG_HW, "ibm,vpd", NULL);
    if (!phead) {
	err_msg("there is no ibm,vpd partition!\n");
	return -1;
    }

    p = (char *)(phead + 1);
    p_end = (char *)(phead + phead->length);
	
    while (*p && p < p_end) {
	if (*p == (char)0x82) {	/* Identification string descriptor. */
	    p++;
	    p = getvalue(p, value);
	    printf("%s\n", value);	/* print id string */
	
	    while (*p != 0x79) {	/* loop until VPD end tag */
		int vpdlen;
		char *vpd_endp;
		p++;
		vpdlen = *p++;
		vpdlen |= ((*p++) << 8);
		vpd_endp = p + vpdlen;
		while (p < vpd_endp)
		    p = printVPDfield(p, show_all);
	    }
		
	    p++;
	    /* printf("checksum byte=0x%x\n", *p); */
	    p++;
	} 
        else if (*p == 0) {	/* end tag */
	    break;
	}
    }
	
    if (*p && p < p_end) {
	warn_msg("found unknown descriptor byte 0x%x\n", *p);
    }

    return 0;
}

/**
 * dump_errlog
 * @brief Dump ibm,err-log partition which contains checkstop info.
 * 
 * ToDo: this code needs more work.
 * See IBM RPA (IBM internal use only -- sorry).
 *
 * @param nvram nvram struct to dump errlog from
 * @return 0 on success, !0 otherwise
 */
int
dump_errlog(struct nvram *nvram)
{
    struct partition_header *phead;
    uint16_t *p, *p_end;    /* Note: data is organized into 16bit big 
                             * endian (network byte order) */
    int p_max;		/* max index to go out of bounds of the partition */
    int i, cpu;
    char checkstop_count;
    int offset;
    int num_cpus;
    int num_memctrls;
    int num_ioctrls;
    uint16_t *sys_regs;	    /* System specific registers 
                             * (e.g. bus arbitration chips, etc */
    uint16_t *cpu_regs[MAX_CPUS+1];
    uint16_t *ioctrl_data;

    phead = nvram_find_partition(nvram, NVRAM_SIG_SP, "ibm,err-log", NULL);
    if (!phead) {
	err_msg("there is no ibm,err-log partition!\n");
	return -1;
    }
	
    p = (uint16_t *)(phead + 1);
    p_end = (uint16_t *)(phead + phead->length);
    p_max = p_end - p;	/* max in 16bit values */
    if (p_max < 4) {
	err_msg("Corrupt ibm,err-log partition in nvram\n");
	return -1;
    }
	
    /* index 0 is checkstop count (high byte), semaphores (low byte) */
    i = 0;	/* index through short words */
    checkstop_count = p[i] >> 8;
    if (checkstop_count)
	printf("Checkstops detected: %d\n", checkstop_count);
    else
	printf("No checkstops have been detected.\n");

    /* index 1 is system specific register offset */
    i++;
    offset = ntohs(p[i])/2+1;
    sys_regs = offset + i < p_max ? p + offset + i : 0;

    /* index 2 is number of cpus */
    i++;
    num_cpus = ntohs(p[i]);
    printf("CPUS: %d\n", num_cpus);

    /* Next indexes are offsets to cpu specific regs */
    for (cpu = 0; cpu < num_cpus; cpu++) {
	i++;
	if (cpu < MAX_CPUS) {
	    offset = ntohs(p[i])/2+1;
	    cpu_regs[cpu] = offset + i < p_max ? p + offset + i : 0;
	}
    }
    
    if (num_cpus > MAX_CPUS)
	num_cpus = MAX_CPUS;	/* just in case... */

    /* next index is number of memory controllers */
    i++;
    num_memctrls = ntohs(p[i]);
    printf("Memory Controllers: %d\n", num_memctrls);

    /* next index is offset of memory controller data, we don't use
     * this so no need to save it.
     */
    i++;	/* ToDo: this may be a list of offsets...manual doesn't show 
		   that but only 1 seems odd */

    /* next index is number of I/O Subsystem controllers */
    i++;
    num_ioctrls = ntohs(p[i]);
    printf("I/O Controllers: %d\n", num_ioctrls);

    /* next index is offset of I/O Subsystem controller data */
    i++;	/* ToDo: this may be a list of offsets...manual doesn't show 
		   that but only 1 seems odd */
    offset = ntohs(p[i])/2+1;
    ioctrl_data = offset + i < p_max ? p + offset + i : 0;

    /*** End of header ***/

    /* Now dump sections collected by the header. */
    if (sys_regs && num_cpus > 0) {
	/* ToDo: what is the length of the data?  We dump until the 
	   first cpu data. */
	printf("System Specific Registers\n");
	dump_raw_data((char *)sys_regs, cpu_regs[0] - sys_regs);
    }

    /* artificial "next cpu" data for length */
    cpu_regs[num_cpus] = ioctrl_data;
	
    for (cpu = 0; cpu < num_cpus; cpu++) {
	/*char buf[64];*/
	int len;
	/* ToDo: what is the length of the data?  We dump until the 
	   next cpu data. */
	len = cpu_regs[cpu+1] - cpu_regs[cpu];
	printf("CPU %d Register Data (len=%x, offset=%"PRIx64")\n", cpu, len,
		cpu_regs[cpu]-p);
	if (len < 4096)	/* reasonable bound */
	    dump_raw_data((char *)cpu_regs[cpu], len);
    }

    return 0;
}

/**
 * dump_rtas_event_entry
 * @brief Dump event-scan data.
 *
 * Note: This is really only valid for PAPR machines.  To ensure 
 * the nvram command can run on all powerpc machines we dlopen the
 * the librtasevent library to dump the rtas event.
 *
 * @param data pointer to rtas error to dump
 * @param len length of data buffer
 * @return 0 on success, !0 otherwise 
 */
int
dump_rtas_event_entry(char *data, int len)
{
    void *rtas_event;
    void *handle;
    void *(*parse_rtas_event)();
    void (*rtas_print_event)();
    void (*cleanup_rtas_event)();

    handle = dlopen("/usr/lib/librtasevent.so", RTLD_LAZY);
    if (handle == NULL)
        return 1;

    parse_rtas_event = dlsym(handle, "parse_rtas_event");
    if (parse_rtas_event == NULL) {
        dlclose(handle);
        return 1;
    }

    rtas_print_event = dlsym(handle, "rtas_print_event");
    if (rtas_print_event == NULL) {
        dlclose(handle);
        return 1;
    }

    cleanup_rtas_event = dlsym(handle, "cleanup_rtas_event");
    if (cleanup_rtas_event == NULL) {
        dlclose(handle);
        return 1;
    }

    rtas_event = parse_rtas_event(data, len);
    if (rtas_event == NULL) {
        dlclose(handle);
        return 1;
    }

    rtas_print_event(stdout, rtas_event, 0);

    cleanup_rtas_event(rtas_event);

    dlclose(handle);
    return 0;
}

/**
 * dump_eventscanlog
 * @brief Dump ibm,es-logs partition, which contains a service processor log
 * 
 * See IBM RPA (IBM internal use only -- sorry).
 *
 * @param nvram nvram struct to get eventscan log from 
 * @return 0 on success, !0 otherwise
 */
int 
dump_eventscanlog(struct nvram *nvram)
{
    struct partition_header *phead;
    uint32_t *p, *p_end;	/* Note: data is organized into 32bit big 
                                 * endian (network byte order) */
    int p_max;		/* max index to go out of bounds of the partition */
    int lognum;
    int num_logs;
    int rc;
#define MAX_EVENTLOGS 100
    uint32_t loghdr[MAX_EVENTLOGS+1];

    phead = nvram_find_partition(nvram, NVRAM_SIG_SP, "ibm,es-logs", NULL);
    if (!phead) {
	err_msg("there is no ibm,es-logs partition!\n");
	return -1;
    } 
	
    p = (uint32_t *)(phead + 1);
    p_end = (uint32_t *)(phead + phead->length);
    p_max = p_end - p;	/* max in 32bit values */
    if (p_max < 1) {
	err_msg("Corrupt ibm,es-logs partition in nvram\n");
	return -1;
    }

    num_logs = ntohl(*p);
    printf("Number of Logs: %d\n", num_logs);

    if (num_logs > MAX_EVENTLOGS) {
	num_logs = MAX_EVENTLOGS;
	warn_msg("limiting to %d log entries (program limit)\n", num_logs);
    }
    
    if (num_logs > p_max-1) {
	/* of course this leaves no room for log data 
	   (i.e. corrupt partition) */
	num_logs = p_max-1;
	warn_msg("limiting to %d log entries (partition limit)\n", num_logs);
    }
	
    for (lognum = 0; lognum < num_logs; lognum++) {
	loghdr[lognum] = ntohl(p[lognum+1]);
    }

    /* artificial log entry (offset) to put a limit on the last log */
    loghdr[num_logs] = p_max * sizeof(uint32_t);

    for (lognum = 0; lognum < num_logs; lognum++) {
	uint32_t hdr = loghdr[lognum];
	int flags = (hdr >> 24) & 0xff;
	int logtype = (hdr >> 16) & 0xff;
	int start = hdr & 0xffff;
	int end = loghdr[lognum+1] & 0xffff;
	printf("Log Entry %d:  flags: 0x%02x  type: 0x%02x\n", lognum, 
	       flags, logtype);
	rc = dump_rtas_event_entry(((char *)p) + start, end - start);
        if (rc) {
            printf("==== Log %d ====\n", lognum);
            dump_raw_data(((char *)p) + start, end - start);
        }
    }

    return 0;
}


/**
 * dump_raw_partition
 * @brief Dump raw data of a partition.  Mainly for debugging.
 *
 * @param nvram nvram struct containing partition
 * @param name name of partition to dump
 * @return 0 on success, !0 otherwise
 */
int
dump_raw_partition(struct nvram *nvram, char *name)
{
    struct partition_header *phead;

    phead = nvram_find_partition(nvram, 0, name, NULL);
    if (!phead) {
	err_msg("there is no %s partition!\n", name);
	return -1;
    }
    
    dump_raw_data((char *)phead, phead->length * NVRAM_BLOCK_SIZE);

    return 0;
}

/**
 * dump_ascii_partition
 * @brief ASCII data dump of a partition, excluding header
 *
 * @param nvram nvram struct containing partition
 * @param name name of partition to dump
 * @return 0 on success, !0 otherwise
 *
 * Partition subheaders, if any, are dumped along with the rest of the data.
 * We substitute periods for unprintable characters.
 */
int
dump_ascii_partition(struct nvram *nvram, char *name)
{
    struct partition_header *phead;
    char *start, *end, *c;

    phead = nvram_find_partition(nvram, 0, name, NULL);
    if (!phead) {
	err_msg("there is no %s partition!\n", name);
	return -1;
    }
    
    start = (char*) phead;
    end = start + phead->length * NVRAM_BLOCK_SIZE;
    start += sizeof(*phead);	/* Skip partition header. */
    for (c = start; c < end; c++) {
	if (isprint(*c) || isspace(*c))
	    putchar(*c);
	else
	    putchar('.');
    }
    /* Always end with a newline.*/
    putchar('\n');
    return 0;
}

int
dump_zipped_text(char *zipped_text, unsigned int zipped_length)
{
    z_stream strm;
    int result;
    char unzipped_text[4096];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = zipped_length;
    strm.next_in = (Bytef*) zipped_text;
    result = inflateInit(&strm);
    if (result != Z_OK) {
    	err_msg("can't decompress text: inflateInit() returned %d\n", result);
	return -1;
    }

    do {
	strm.avail_out = 4096;
	strm.next_out = (Bytef*) unzipped_text;
    	result = inflate(&strm, Z_NO_FLUSH);
	switch (result) {
	case Z_STREAM_ERROR:
	case Z_NEED_DICT:
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
	    err_msg("can't decompress text: inflate() returned %d\n", result);
	    (void) inflateEnd(&strm);
	    return -1;
	}
	if (fwrite(unzipped_text, 4096 - strm.avail_out, 1, stdout) != 1) {
	    err_msg("can't decompress text: fwrite() failed\n");
	    (void) inflateEnd(&strm);
	    return -1;
	}
    } while (strm.avail_out == 0);

    (void) inflateEnd(&strm);
    return 0;
}

/**
 * unzip_partition
 * @brief Uncompress and print compressed data from a partition.
 *
 * @param nvram nvram struct containing partition
 * @param name name of partition to dump
 * @return 0 on success, !0 otherwise
 */
int
unzip_partition(struct nvram *nvram, char *name)
{
    struct partition_header *phead;
    char *start, *next;
    unsigned short zipped_length;

    phead = nvram_find_partition(nvram, 0, name, NULL);
    if (!phead) {
	err_msg("there is no %s partition!\n", name);
	return -1;
    }
    
    start = (char*) phead;
    next = start + sizeof(*phead);	/* Skip partition header. */
    next += sizeof(struct err_log_info);	/* Skip sub-header. */
    zipped_length = be16toh(*((unsigned short*) next));
    next += sizeof(unsigned short);

   /* New format oops header, zipped_length > OOPS_PARTITION_SZ
    * signifies the version of new header. Find out new zipped length
    * and from where the compressed data starts.
    */
   if (zipped_length > OOPS_PARTITION_SZ) {
        zipped_length = be16toh(*((unsigned short*) next));
        next += sizeof(struct oops_log_info) - sizeof(unsigned short);
   }

    if ((next-start) + zipped_length > phead->length * NVRAM_BLOCK_SIZE) {
    	err_msg("bogus size for compressed data in partition %s: %u\n", name,
	    zipped_length);
	return -1;
    }

    return dump_zipped_text(next, zipped_length);
}

/**
 * print_of_config_part
 * @brief Print the name/value pairs of a partition
 *
 * @param pname partition name containing name/value pairs
 * @param nvram nvram struct containing partition to dump
 * @return 0 on success, !0 otherwise
 */
static int
print_of_config_part(struct nvram *nvram, char *pname)
{
    struct partition_header	*phead;
    char	*data;
    int		i;

    phead = nvram_find_partition(nvram, 0, pname, NULL);
    if (phead == NULL) 
	return -1;

    data = (char *)phead + sizeof(*phead);

    printf("\"%s\" Partition\n", pname);
    for (i = 0; i <= (strlen(pname) + 14); i++)
	printf("-");
    printf("\n");

    while (*data != '\0') {
        printf("%s\n", data);
	data += strlen(data) + 1;
    }

    printf("\n");

    return 0;
}

/* Print a single OF var...or all if "" is used */
/**
 * @var name_value_parts
 * @brief List of partition names that contain name/value pairs
 */
/**
 * @var num_name_value_parts
 * @brief number of names in the name_vlaue_parts array
 */
static char	*name_value_parts[] = {
    "common", "ibm,setupcfg", "of-config"
};
static int num_name_value_parts = 3;

/**
 * print_of_config
 * @brief Print the contents of an Open Firmware config partition
 *
 * This will print the name/value pair for a specified Open
 * Firmware config variable or print all of the name/value pairs
 * in the partition if the name is NULL.
 *
 * @param config_var config variable to print
 * @param pname partition name containing config_var
 * @param nvram nvram struct containing pname
 * @return 0 on success, !0 otherwise
 */
static int 
print_of_config(struct nvram *nvram, char *config_var, char *pname,
	int zero_terminator)
{
    struct partition_header *phead;
    char *data, terminator;
    int  i, varlen;
    int  rc = -1;

    terminator = '\n';
    if (zero_terminator)
	terminator = '\0';

    /* if config_var is NULL , print the data from the
     * partition specified by pname or all of the
     * name/value pair partitions if pname is NULL. 
     */
    if (config_var == NULL) {
	if (pname == NULL) {
            for (i = 0; i < num_name_value_parts; i++)
	    	(void)print_of_config_part(nvram, name_value_parts[i]);
	} 
        else {
	    for (i = 0; i < num_name_value_parts; i++) {
	    	if (strcmp(pname, name_value_parts[i]) == 0) {
	            (void)print_of_config_part(nvram, name_value_parts[i]);
		    rc = 0;
		}
	    }
	    if (rc)
	    	err_msg("There is no Open Firmware \"%s\" partition!\n", pname);
	}
	return rc;
    } 
    
    /* the config_var is a variable name */
    varlen = strlen(config_var);

    if (pname == NULL) {
    	for (i = 0; i < num_name_value_parts; i++) {
	    phead = nvram_find_partition(nvram, 0, name_value_parts[i], NULL);
   	    if (phead == NULL) 
		continue;

	    data = (char *)phead + sizeof(*phead);

	    while (*data != '\0') {
	    	if ((data[varlen] == '=') && 
		    strncmp(config_var, data, varlen) == 0) {
    	            printf("%s%c", data + varlen + 1, terminator);
		    rc = 0;
		}
	    	data += strlen(data) + 1;
            }
        }
    } 
    else {
	phead = nvram_find_partition(nvram, 0, pname, NULL);
	if (phead == NULL) {
	    err_msg("There is no Open Firmware \"%s\" partition.\n", pname);
	    return -1;
	}

	data = (char *)phead + sizeof(*phead);
	while (*data != '\0') {
	    if ((data[varlen] == '=') && 
		strncmp(config_var, data, varlen) == 0) {
		printf("%s%c", data + varlen + 1, terminator);
		rc = 0;
	    }
	    data += strlen(data) + 1;
	}
    }

    return rc;
}

/**
 * update_config_var
 * @brief Update an Open Firmware config variable in nvram
 *
 * This will attempt to update the value half of a name/value
 * pair in the nvram config partition.  If the name/value pair
 * is not found in the partition then the specified name/value pair
 * is added to the end of the data in the partition.
 *
 * @param config_var OF config variable to update
 * @param pname partition containing config_var
 * @param nvram nvram struct containing pname
 * @return 0 on success, !0 otherwise
 */
int
update_of_config_var(struct nvram *nvram, char *config_var, char *pname)
{
    struct partition_header *phead, *new_phead;
    char *new_config_value;
    char *data_offset;
    char *new_part;
    char *new_part_offset, *new_part_end;
    char *tmp_offset;
    int	config_name_len;
    int	len, rc, part_size;

    new_config_value = strchr(config_var, '=');
    if (!new_config_value) {
	err_msg("config variables must be in the format \"name=value\"");
	return -1;
    }
    new_config_value++;

    phead = nvram_find_partition(nvram, 0, pname, NULL);
    if (phead == NULL) {
        err_msg("there is no \"%s\" partition!\n", pname);
	return -1;
    }

    part_size = phead->length * NVRAM_BLOCK_SIZE;
    data_offset = (char *)((unsigned long)phead + sizeof(*phead));

    new_part = malloc(part_size);
    if (new_part == NULL) {
        err_msg("cannot allocate space to update \"%s\" partition\n", pname);
	return -1;
    }

    memset(new_part, 0, part_size);

    /* get the length of then name of the config variable we are updating */
    config_name_len = strstr(config_var, "=") - config_var;
    config_name_len++;
    
    /* now find this config variable in the partition */
    while (*data_offset != '\0') {
	if (strncmp(data_offset, config_var, config_name_len) == 0)
	    break;
	data_offset += strlen(data_offset) + 1;
    }

    /* Copy everything up to the config name we are modifying 
     * to the new partition 
     */
    memcpy(new_part, phead, data_offset - (char *)phead);

    /* make sure the new config var will fit into the partition and add it */
    new_phead = (struct partition_header *)new_part;
    new_phead->length = be16toh(new_phead->length);
    new_part_offset = new_part + (data_offset - (char *)phead);
    new_part_end = new_part + part_size;

    if ((new_part_offset + strlen(config_var) + 1) >= new_part_end) {
        err_msg("cannot update config var to\"%s\".\n"
		"\tThere is not enough room in the \"%s\" partition\n", 
		config_var, pname);
	free(new_part);
	return -1;
    }

    if (strlen(new_config_value)) {
	strncpy(new_part_offset, config_var, strlen(config_var));
	new_part_offset += strlen(config_var);
	*new_part_offset++ = '\0';
    }

    /* Find the end of the name/value pairs in the partition so we
     * can copy them over to the new partition.
     */
    data_offset += strlen(data_offset) + 1;
    tmp_offset = data_offset;
    while (*data_offset != '\0') {
    	data_offset += strlen(data_offset) + 1;
    }

    /* we should now be pointing to a double NULL, verify this */
    if ((data_offset[-1] != '\0') && (data_offset[0] != '\0')) {
        err_msg("the \"%s\" partition appears to be corrupt\n", pname);
	free(new_part);
	return -1;
    }

    /* go past double NULL */
    data_offset++;

    /* verify that this will fit into the new partition */
    if ((new_part_offset + (data_offset - tmp_offset)) > new_part_end) {
        err_msg("cannot update open firmware config var to \"%s\".\n"
		"\tThere is not enough room in the \"%s\" partition\n", 
		config_var, pname);
	free(new_part);
	return -1;
    }

    memcpy(new_part_offset, tmp_offset, data_offset - tmp_offset);

    /* recalculate the checksum */
    new_phead->checksum = checksum(new_phead);

    /* seek the position in the /dev/nvram for the common partition */
    if (nvram_find_fd_partition(nvram, new_phead->name) != 0) {
    	free(new_part);
	return -1;
    }
    
    /* write the partition out to nvram */
    for (rc = 0, len = 0; len < part_size; len += rc) {
	rc = write(nvram->fd, new_part + len, part_size - len);
	if (rc <= 0)
	    break;
    }

    if (len != part_size) {
        err_msg("only wrote %d bytes of the \"%s\" partition back\n"
		"\tto %s, expected to write %d bytes\n",
		len, pname, nvram->filename, part_size);
    }

    free(new_part);
    return 0;
}

int 
main (int argc, char *argv[])
{
    struct nvram nvram;
    struct stat sbuf;
    int ret = 0; 
    int	option_index;
    char *endp;
    char *of_config_var = NULL;
    int print_partitions = 0;
    int print_vpd = 0;
    int print_errlog = 0;
    int print_event_scan = 0;
    int	print_config_var = 0;
    int zero_terminator = 0;
    char *dump_name = NULL;
    char *ascii_name = NULL;
    char *zip_name = NULL;
    char *update_config_var = NULL;
    char *config_pname = "common";

    nvram_cmdname = argv[0];
    if (argc == 1) {
	help();
	exit(1);
    }

    /* initialize nvram struct */
    memset(&nvram, 0, sizeof(struct nvram));
    nvram.fd = -1;
	
    for (;;) {
	option_index = 0;
	ret = getopt_long(argc, argv, "+p:Vv::0", long_options, &option_index);
	if (ret == -1)
		break;
	switch (ret) {
	    case 'h':
		help();
		exit(0);
	    case 'v':
		verbose += (optarg ? atoi(optarg) : 1);
		break;
	    case 'd':	/* dump */
		dump_name = optarg;
                if (!part_name_valid(dump_name))
                    exit(1);
		break;
	    case 'a':	/* ASCII dump */
	    	ascii_name = optarg;
                if (!part_name_valid(ascii_name))
                    exit(1);
		break;
	    case 'z':	/* dump compressed data */
                zip_name = optarg;
                if (!part_name_valid(zip_name))
                    exit(1);
		break;
	    case 'n':	/* nvram-file */
		nvram.filename = optarg;
		break;
	    case 'o':	/*print-config */
		print_config_var = 1;
		of_config_var = optarg;
		break;
	    case '0':
		zero_terminator = 1;
		break;
	    case 'P':	/* partitions */
		print_partitions = 1;
		break;
	    case 's':	/* nvram-size */
		nvram.nbytes = strtoul(optarg, &endp, 10);
		if (!*optarg || *endp) {
		    err_msg("specify nvram-size as an integer\n");
		    exit(1);
		}
		if (nvram.nbytes % NVRAM_BLOCK_SIZE) {
		    err_msg("nvram-size must be a multiple of 16 Bytes\n");
		    exit(1);
		}
		break;
	    case 'V':	/* print-vpd */
		print_vpd = 1;
		break;
	    case 'W':	/* print-all-vpd */
		print_vpd = 2;
		break;
	    case 'e':	/* print-err-log */
		print_errlog = 1;
		break;
	    case 'E':	/* print-event-scan */
		print_event_scan = 1;
		break;
	    case 'u':	/* update-config */
	        update_config_var = optarg;
		break;
	    case 'p':	/* update-config partition name */
	        config_pname = optarg;
                if (!part_name_valid(config_pname))
                    exit(1);
		break;
	    case '?':
		exit(1);
		break;
	    default:
		printf("huh?\n");
		break;
	}
    }

    if (optind < argc) {
	    err_msg("Could not parse the option %s correctly.\n", 
		    argv[optind]);
	    help();
	    exit(-1);
    }

    ret = 0;

    if (nvram.filename) {
        nvram.fd = open(nvram.filename, O_RDWR);
        if (nvram.fd == -1) {
	    err_msg("cannot open \"%s\": %s\n", 
  		    nvram.filename, strerror(errno));
              ret = -1;
  	    goto err_exit;
        }
    } else {
        nvram.filename = NVRAM_FILENAME1;
        nvram.fd = open(nvram.filename, O_RDWR);
        if (nvram.fd == -1) {
	    int errno1 = errno;

            nvram.filename = NVRAM_FILENAME2;
            nvram.fd = open(nvram.filename, O_RDWR);
            if (nvram.fd == -1) {
                err_msg("cannot open \"%s\": %s\n", 
                        NVRAM_FILENAME1, strerror(errno1));
                err_msg("cannot open \"%s\": %s\n", 
                        NVRAM_FILENAME2, strerror(errno));
                ret = -1;
                goto err_exit;
            }
        }
    }

    if (fstat(nvram.fd, &sbuf) < 0) {
        err_msg("cannot stat %s: %s\n", nvram.filename, strerror(errno));
	ret = -1;
	goto err_exit;
    }

    if (!nvram.nbytes) {
	ret = lseek(nvram.fd, 0, SEEK_END);
	if (ret < 0) {
	    err_msg("cannot seek(END) %s: %s\n", nvram.filename,
		    strerror(errno));
	    goto err_exit;
	}

	nvram.nbytes = ret;

	ret = lseek(nvram.fd, 0, SEEK_SET);
	if (ret < 0) {
	    err_msg("cannot seek(SET) %s: %s\n", nvram.filename,
		    strerror(errno));
	    goto err_exit;
	}
    }

    nvram.data = malloc(nvram.nbytes);
    if (nvram.data == NULL) {
        err_msg("cannot allocate space for nvram of %d bytes\n", nvram.nbytes);
    	ret = -1;
	goto err_exit;
    }

    if (nvram_read(&nvram) != 0) {
        ret = -1;
        goto err_exit;
    }

    if (nvram_parse_partitions(&nvram) != 0) {
        ret = -1;
	goto err_exit;
    }

    if (print_partitions)
	print_partition_table(&nvram);

    if (update_config_var) {
        if (config_pname == NULL) {
	    err_msg("you must specify the partition name with the -p option\n"
	    	    "\twhen using the --update-config option\n");
	    goto err_exit;
	}
    	if (update_of_config_var(&nvram, update_config_var, config_pname) != 0) 
	    ret = -1; 
    }
    if (print_config_var)
	if (print_of_config(&nvram, of_config_var, config_pname,
		    zero_terminator) != 0)
	    ret = -1;
    if (print_vpd)
	if (dump_vpd(&nvram, print_vpd == 2) != 0)
	    ret = -1;
    if (print_errlog)
	if (dump_errlog(&nvram) != 0)
	    ret = -1;
    if (print_event_scan)
	if (dump_eventscanlog(&nvram) != 0)
	    ret = -1;
    if (dump_name)
	if (dump_raw_partition(&nvram, dump_name) != 0)
	    ret = -1;
    if (ascii_name)
	if (dump_ascii_partition(&nvram, ascii_name) != 0)
	    ret = -1;
    if (zip_name)
	if (unzip_partition(&nvram, zip_name) != 0)
	    ret = -1;
   
err_exit:   
   if (nvram.data)
	free(nvram.data);
   if (nvram.fd != -1)
   	close(nvram.fd);
	
   return ret;
}
