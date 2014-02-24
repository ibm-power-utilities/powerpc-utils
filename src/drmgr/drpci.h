/**
 * @file drpci.h
 *
 *
 * Copyright (C) IBM Corporation
 */

#ifndef _DRPCI_H_
#define _DRPCI_H_

#include "rtas_calls.h"
#include "ofdt.h"

/* PCI Hot Plug  defs  */
#define PHP_SYSFS_ADAPTER_PATH	"/sys/bus/pci/slots/%s/adapter"
#define PHP_SYSFS_POWER_PATH	"/sys/bus/pci/slots/%s/power"
#define PHP_CONFIG_ADAPTER	1
#define PHP_UNCONFIG_ADAPTER	0

#define PCI_RESCAN_PATH         "/sys/bus/pci/rescan"

/* The following defines are used for adapter status */
#define EMPTY		0
#define CONFIG		1
#define NOT_CONFIG	2

/* Device type definitions */
#define PCI_HP_DEV	1
#define PCI_DLPAR_DEV	2
#define VIO_DEV		3
#define HEA_DEV		4
#define HEA_PORT_DEV	5
#define PHB_DEV		7
#define CPU_DEV		8
#define MEM_DEV		9

#define ADD_SLOT_FNAME    	"/sys/bus/pci/slots/control/add_slot"
#define ADD_SLOT_FNAME2    	"/sys/bus/pci/slots/control/\"add_slot\""
#define REMOVE_SLOT_FNAME    	"/sys/bus/pci/slots/control/remove_slot"
#define REMOVE_SLOT_FNAME2    	"/sys/bus/pci/slots/control/\"remove_slot\""

#define IGNORE_HP_PO_PROP	"/proc/device-tree/ibm,ignore-hp-po-fails-for-dlpar"

extern char *add_slot_fname;
extern char *remove_slot_fname;

#define HEA_ADD_SLOT		"/sys/bus/ibmebus/probe"
#define HEA_REMOVE_SLOT		"/sys/bus/ibmebus/remove"
/* %s is the loc-code of the HEA adapter for *_PORT defines */
#define HEA_ADD_PORT		"/sys/bus/ibmebus/devices/%s/probe_port"
#define HEA_REMOVE_PORT		"/sys/bus/ibmebus/devices/%s/remove_port"

#define PCI_NODES	0x00000001
#define VIO_NODES	0x00000002
#define HEA_NODES	0x00000004
#define PHB_NODES	0x00000010

struct dr_node *get_hp_nodes(void);
struct dr_node *get_dlpar_nodes(uint32_t);
struct dr_node *get_node_by_name(const char *, uint32_t);
void free_node(struct dr_node *);

/* Function prototypes for subroutines  */
int get_hp_adapter_status(char *);
int set_hp_adapter_status(uint, char *);
int pci_rescan_bus();
int pci_remove_device(struct dr_node *);
int release_hp_children(char *);
int dlpar_remove_slot(const char *);
int dlpar_add_slot(const char *);
int cmp_drcname(char *, char *);
int acquire_hp_children(char *, int *);
int enable_hp_children(char *);
int disable_hp_children(char *);

void print_node_list(struct dr_node *);

#endif				/* _DRPCI_H_ */
