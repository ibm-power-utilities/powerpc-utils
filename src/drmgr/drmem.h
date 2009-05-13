#include "drpci.h"

struct lmb_list_head {
	struct dr_node	*lmbs;
	char		*drconf_buf;
	int		drconf_buf_sz;
	int		lmbs_modified;
};

struct drconf_mem {
	uint64_t	address;
	uint32_t	drc_index;
	uint32_t	reserved;
	uint32_t	assoc_index;
	uint32_t	flags;
};

#define DRMEM_ASSIGNED		0x00000008
#define DRMEM_DRC_INVALID	0x00000020

#define MEM_PROBE_FILE		"/sys/devices/system/memory/probe"
#define MEM_BLOCK_SIZE_BYTES	"/sys/devices/system/memory/block_size_bytes"
#define DYNAMIC_RECONFIG_MEM	"/proc/device-tree/ibm,dynamic-reconfiguration-memory"

struct lmb_list_head *get_lmbs(void);
void free_lmbs(struct lmb_list_head *);
