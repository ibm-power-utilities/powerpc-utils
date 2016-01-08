/**
 * @file rtas_calls.h
 *
 * Copyright (C) IBM Corporation 2006
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

#ifndef _RTAS_CALLS_H_
#define _RTAS_CALLS_H_

#include <librtas.h>
#include "drpci.h"

/*  The following definitions are used in the interfaces to various
 *  subroutines.
 */

/* Indicators for rtas_set_indicator */
#define ISOLATION_STATE	 9001	/* value for isolation-state */
#define DR_INDICATOR	 9002	/* value for dr-indicator */
#define ALLOCATION_STATE 9003	/* value for allocation-state */

/* Error status from rtas_set_indicator */
#define HARDWARE_ERROR		-1
#define HARDWARE_BUSY		-2
#define NO_INDICATOR		-3
#define MULTI_LEVEL_ISO_ERROR	-9000
#define VALID_TRANSLATION	-9001

/* Error status from dr-entity-sense(get-sensor-state) */
#define NEED_POWER    -9000	/* Need to turn on power to slot */
#define PWR_ONLY      -9001	/* Power on slot, leave isolated */

/* Sensor values from dr-entity-sense(get-sensor-state) */
#define EMPTY		0	/* No card in slot */
#define PRESENT		1	/* Card in slot */
#define STATE_UNUSABLE	2	/* No DR operation will succeed */
#define EXCHANGE	3	/* resource unlicensed, for sparing only */
#define RECOVERY	4	/* can be recovered by platform */

/* Return status from configure-connector */
#define NOT_THIS_SYSTEM	-9001	/* DR entity not supported on this system */
#define NOT_THIS_SLOT	-9002	/* DR entity not supported in this slot */
#define DR_UNUSABLE	-9003	/* Logical DR connector unusable */

/* Return status from ibm,suspend_me */
#define NOT_SUSPENDABLE  -9004
#define MULTIPLE_THREADS -9005

/* State values for set-indicator dr-indicator */
#define LED_OFF		0
#define LED_ON		1
#define LED_ID		2
#define LED_ACTION	3

/* State values for isolation-state */
#define ISOLATE		0
#define UNISOLATE	1

/* Level values for set-power-level */
#define POWER_OFF	0
#define POWER_ON	100

/* State values for allocation-state */
#define ALLOC_UNUSABLE	0	/* Release Unusable Resource to FW	*/
#define ALLOC_USABLE	1	/* Assign Usable Resource from FW 	*/

/* Tokens for RTAS calls */
#define DR_ENTITY_SENSE	 9003	/* token value for dr-entity-sense */

/* Return status from configure-connector */
#define NEXT_SIB	1	/* Next sibling */
#define NEXT_CHILD	2	/* Next child */
#define NEXT_PROPERTY	3	/* Next property */
#define PREV_PARENT	4	/* Previous parent */
#define MORE_MEMORY	5	/* Need more memory */
#define ERR_CFG_USE     -9003   /* DR connector unusable */

struct of_property {
	struct of_property *next;	/* Ptr to next property for node */
	char	*name;			/* OF property name */
	int	 length;		/* Length of property value in bytes */
	char	*value;			/* Pointer to property value */
};

struct of_node {
	char *name;			/* Node name including unit address */
	struct of_property *properties;	/* Pointer to OF properties */
	struct of_node *parent;		/* Pointer to parent node */
	struct of_node *sibling;	/* Pointer to next sibling node */
	struct of_node *child;		/* Pointer to first child node */
	int added;
};

extern char *hw_error;

int dr_entity_sense(int index);
struct of_node *configure_connector(int index);
int set_power(int domain, int level);
int acquire_drc(uint32_t);
int release_drc(int, uint32_t);
struct of_node *configure_connector(int);

#endif /* _RTAS_CALLS_H_ */
