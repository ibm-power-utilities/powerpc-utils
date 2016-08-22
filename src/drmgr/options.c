/**
 * @file options.c
 *
 * Copyright (C) IBM Corporation 2016
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

/* Global User Specifications */
enum drmgr_action usr_action = NONE;

/* default is to do slot identification, unless the drmgr -I
 * option is specified.
 */
int usr_slot_identification = 1;

/* timeout specified with the -w <timeout> option */
int usr_timeout = 0;

/* user specified drc name */
char *usr_drc_name = NULL;

/* user specified drc index */
uint32_t usr_drc_index = 0;

/* default to prompting the user for pci hotplug operations
 * unless the drmgr -n option is specified.
 */
int usr_prompt = 1;

/* user specified number of devices to add/remove */
int usr_drc_count = 0;

/* user specified drc type to use */
enum drc_type usr_drc_type = DRC_TYPE_NONE;

/* user specified -p option, meaning varies depending on usr_drc_type */
char *usr_p_option = NULL;

/* user specified workaround for qemu pci dlpar */
int pci_virtio = 0;

/* user specified file for handling prrn events */
char *prrn_filename = NULL;

/* lsslot specific options */
int show_available_slots = 0;
int show_cpus_and_caches = 0;
int show_occupied_slots = 0;
int show_caches = 0;
char *usr_delimiter = NULL;
