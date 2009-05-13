/**
 * @file nvram.h
 * @brief nvram access utility for powerpc platforms.
 *
 * Copyright (c) 2003, 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 * @author Michael Strosaker <strosake@us.ibm.com>
 * @author Todd Inglett <tinglett@us.ibm.com>
 */

#ifndef _DEV_NVRAM_H_
#define _DEV_NVRAM_H_

#define NVRAM_SIG_SP	0x02	/**< support processor signature */
#define NVRAM_SIG_OF	0x50	/**< open firmware config signature */
#define NVRAM_SIG_FW	0x51	/**< general firmware signature */
#define NVRAM_SIG_HW	0x52	/**< hardware (VPD) signature */
#define NVRAM_SIG_SYS	0x70	/**< system env vars signature */
#define NVRAM_SIG_CFG	0x71	/**< config data signature */
#define NVRAM_SIG_ELOG	0x72	/**< error log signature */
#define NVRAM_SIG_VEND	0x7e	/**< vendor defined signature */
#define NVRAM_SIG_FREE	0x7f	/**< Free space signature */
#define NVRAM_SIG_OS	0xa0	/**< OS defined signature */

/**
 * @def printmap(ch)
 * @brief dertermines if 'ch' is a printable character
 */
#define printmap(ch)	(isgraph(ch) ? (ch) : '.')

#define NVRAM_BLOCK_SIZE	16
#define NVRAM_FILENAME1		"/dev/nvram"
#define NVRAM_FILENAME2		"/dev/misc/nvram"

#define DEVICE_TREE "/proc/device-tree"
#define NVRAM_DEFAULT DEVICE_TREE "/nvram"
#define NVRAM_ALIAS DEVICE_TREE "/aliases/nvram"

#define DEFAULT_NVRAM_SZ	(1024 * 1024)

/**
 * @def MAX_CPUS
 * @brief maximum number of CPUS for errlog dumps
 */
#define MAX_CPUS 128

/**
 * @struct partition_header
 * @brief nvram partition header data
 */
struct partition_header {
    unsigned char	signature;  /**< partition signature */
    unsigned char 	checksum;   /**< partition checksum */
    unsigned short 	length;     /**< partition length */
    char 		name[12];   /**< partition name */
};

/* Internal representation of NVRAM. */
#define MAX_PARTITIONS 50
/**
 * @struct nvram
 * @brief internal representation of nvram data
 */
struct nvram {
    char 	*filename;		/**< original filename */
    int		fd;			/**< file descriptor */
    int 	nparts;			/**< number of partitions */
    int 	nbytes;			/**< size of data in bytes.  This 
                                         *   cannot be changed 
                                         *   (i.e. hardware size) 
					 */
    struct partition_header *parts[MAX_PARTITIONS]; 
                                        /**< partition header pointers 
                                         *   into data 
                                         */
    char	*data;                  /**< nvram contents */
};

/**
 * @var descs
 * @brief Array of VPD field names and descriptions
 */
static struct { char *name; char *desc; } descs[] = {
    {"PN", "Part Number"},
    {"FN", "FRU Number"},
    {"EC", "EC Level"},
    {"MN", "Manufacture ID"},
    {"SN", "Serial Number"},
    {"LI", "Load ID"},
    {"RL", "ROM Level"},
    {"RM", "Alterable ROM Level"},
    {"NA", "Network Address"},
    {"DD", "Device Driver Level"},
    {"DG", "Diagnostic Level"},
    {"LL", "Loadable Microcode Level"},
    {"VI", "Vendor ID/Device ID"},
    {"FU", "Function Number"},
    {"SI", "Subsystem Vendor ID/Device ID"},
    {"VK", "Platform"},			/**< "RS6K" => VPD is present */
    {"TM", "Model"},			/**< IBM specific? */
    {"YL", "Location Code"},		/**< IBM specific? */
    {"BR", "Brand"},			/**< IBM specific */
    {"CI", "CEC ID"},			/**< IBM specific */
    {"RD", "Rack ID"},			/**< IBM specific */
    {"PA", "Op Panel Installed"},	/**< IBM specific */
    {"NN", "Node Name"},		/**< IBM specific */
};

#endif
