/**
 * @file rtas_calls.c
 *
 *
 * Copyright (C) IBM Corporation 2006
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <memory.h>
#include <errno.h>
#include "rtas_calls.h"
#include "dr.h"
#include "ofdt.h"

char *hw_error = "Hardware error. You must correct this error before\n"
		 "attempting any further dynamic reconfiguration "
		 "operations.\nCheck the system error log for more "
		 "information.\n";

/**
 * get_node
 * @brief Allocates and initializes a node structure.
 *
 * @param workarea work area returned by "ibm,configure-connector" RTAS call.
 * @returns pointer to allocated node on success, NULL otherwise
 */
static struct of_node *
get_node(char *workarea)
{
	struct of_node *node;	/* Pointer to new node structure */
	int *work_int;		/* Pointer to workarea */
	char *node_name;	/* Pointer to memory for node name */

	/* Allocate new node structure */
	node = zalloc(sizeof(*node));
	if (node == NULL)
		return NULL;

	work_int = (int *)workarea;
	node_name = workarea + work_int[2];
	node->name = (char *)zalloc(strlen(node_name)+1);
	if (node->name == NULL) {
		/* Malloc error */
		free(node);
		return NULL;
	}
	strcpy(node->name, node_name);

	return node;
}

/**
 * free_of_node
 * @brief Free all memory allocated by the configure_connector()
 *
 * @param node node returnd by configure_connector()
 * @returns 0 on success, !0 otherwise
 */
void
free_of_node(struct of_node *node)
{
	struct of_property *prop;	/* Used in freeing property memory */
	struct of_property *next_prop;	/* Used in freeing property memory */

	/* If node has a child, make recursive call to free child memory */
	if (node->child)
		free_of_node(node->child);

	/* If node has a sibling, make recursive call to free its memory */
	if (node->sibling)
		free_of_node(node->sibling);

	/* Now loop through and free all property related memory */
	prop = node->properties;
	while (prop) {
		next_prop = prop->next;
		free(prop->name);
		free(prop->value);
		free(prop);
		prop = next_prop;
	}

	/* Finally free the memory for the name and the node itself */
	if (node->name)
		free(node->name);

	free(node);
}
/**
 * get_rtas_property
 * @brief Allocates and initializes a property structure.
 *
 * @param Pointer to work area returned by "ibm,configure-connector" RTAS call.
 * @returns pointer to of_property_t on success, NULL otherwise
 */
static struct of_property *
get_rtas_property(char *workarea)
{
	struct of_property *prop;  /* Pointer to new property strucutre */
	int *work_int;		/* Pointer to workarea */
	char *name;		/* Pointer to memory for property name */
	char *value;		/* Pointer to memory for property value */


	/* Allocate a new property structure */
	prop = zalloc(sizeof(*prop));
	if (prop != NULL) {
		/* Initialize the new property structure */
		work_int = (int *)workarea;
		prop->next = NULL;
		name = workarea + work_int[2];
		prop->name = (char *)zalloc(strlen(name)+1);
		if (prop->name == NULL) {
			/* Malloc error */
			free(prop);
			return NULL;
		}
		strcpy(prop->name, name);
		prop->length = work_int[3];
		value = workarea + work_int[4];
		prop->value = (char *)zalloc(prop->length);
		if (prop->value == NULL) {
			/* Malloc error */
			free(prop->name);
			free(prop);
			return NULL;
		}
		memcpy(prop->value, value, prop->length);
	}

	return prop;
}

/**
 * dr_entity_sense
 * @brief Determine if a PCI card is present in a hot plug slot.
 *
 * @param index slot index to check
 * @returns EMPTY if no card detected in slotor not a valid state for logical
 *		  resources
 * @returns PRESENT if a card was detected in the slot or logical connector
 *		    is owned by the partition.
 * @returns EXCHANGE Logical resource is unlicensed and is available for
 *		     sparing operations.
 * @returns NEED_POWER	Must power on slot before checking for card presence.
 * @returns PWR_ONLY	Power on slot, but leave isolated.
 * @returns STATE_UNUSABLE No DR operation will succeed
 * @returns HW_ERROR	Hardware error
 * @returns SW_ERROR	Other errors.
 */
int
dr_entity_sense(int index)
{
	int state;
	int rc;

	rc = rtas_get_sensor(DR_ENTITY_SENSE, index, &state);
	say(DEBUG, "get-sensor for %x: %d, %d\n", index, rc, state);

	return (rc >= 0) ? state : rc;
}

/*
 * entity_sense_error
 * @brief provide a detailed error message for dr_entity_sense() errors
 *
 * @param error error returned from dr_entity_sense()
 * @return pointer to message string, empty string if error is invalid
 */
char *
entity_sense_error(int error)
{
	char *empty = "Unable to allocate the resource to the partition.";
	char *present = "Resource is already assigned to the partition.";
	char *unusable = "Resource is not available to the partition.";
	char *exchange = "Resource is available for exchange.";
	char *recovery = "Resource is available for recovery by partition.";

	char *rc = "";

	switch (error) {
	    case EMPTY:
		rc = empty;
		break;

	    case PRESENT:
		rc = present;
		break;

	    case STATE_UNUSABLE:
		rc = unusable;
		break;

	    case EXCHANGE:
		rc = exchange;
		break;

	    case RECOVERY:
		rc = recovery;
		break;
	}

	return rc;
}

/*
 * set_indicator_error
 * @brief provide a detailed error message for rtas_set_indicator() errors
 *
 * @param error error returned from rtas_set_indicator()
 * @return pointer to message string, empty string if error is invalid
 */
char *
set_indicator_error(int error)
{
	char *hw_error = "Hardware error.";
	char *hw_busy = "Hardware busy, try again later.";
	char *no_ind = "No such indicator implemented.";
	char *iso_error = "Multi-level isolation error.";
	char *vot = "Valid outstanding translations exist.";

	char *rc = "";

	switch (error) {
	    case HARDWARE_ERROR:
		rc = hw_error;
		break;

	    case HARDWARE_BUSY:
		rc = hw_busy;
		break;

	    case NO_INDICATOR:
		rc = no_ind;
		break;

	    case MULTI_LEVEL_ISO_ERROR:
		rc = iso_error;
		break;

	    case VALID_TRANSLATION:
		rc = vot;
		break;
	}

	return rc;
}

#define WORK_SIZE 4096		/* RTAS work area is 4K page size   */

/**
 * configure_connector
 *
 * Obtain all of the Open Firmware properties for nodes associated
 * with the hot plug entitiy.
 *
 * @param index slot index from "ibm,drc-indexes" property
 * @returns pointer to node on success, NULL on failure.
 */
struct of_node *
configure_connector(int index)
{
	char workarea[WORK_SIZE];
	struct of_node *node;
	struct of_node *first_node = NULL;
	struct of_node *last_node = NULL;	/* Last node processed */
	struct of_property *property;
	struct of_property *last_property = NULL; /* Last property processed */
	int *work_int;
	int rc;

	say(DEBUG, "Configuring connector for drc index %x\n", index);

	/* initialize work area and args structure */
	work_int = (int *) &workarea[0];
	work_int[0] = index;
	work_int[1] = 0;

	while (1) {
		rc = rtas_cfg_connector(workarea);
		if (rc == 0)
			break; /* Success */

		if (rc == NEXT_SIB) {
			if (last_node == NULL) {
				say(ERROR, "unexpected sibling returned from "
				    "configure_connector\n");
				break;
			}

			/* Allocate and initialize the node */
			node = get_node(workarea);
			if (node == NULL) {
				say(ERROR, "failed to allocate sibling node "
				    "for drc index %x\n", index);
				break;
			}

			/* Set parent node to same as that of last node */
			node->parent = last_node->parent;

			/* Chain to last node */
	        	last_node->sibling = node;

			/* This node becomes the last node */
			last_node = node;
		} else if (rc == NEXT_CHILD) {
			/* Allocate and initialize the node */
			node = get_node(workarea);
			if (node == NULL) {
				say(ERROR, "Failed to allocate child node for "
				    "drc index %x\n", index);
				break;
			}

			if (first_node == NULL) {
				first_node = node;
			} else {
				node->parent = last_node;
				if (last_node)
					last_node->child = node;
			}

			/* This node becomes the last node */
			last_node = node;
		} else if (rc == NEXT_PROPERTY){
			if (last_node == NULL) {
				say(ERROR, "Configure_connector returned a "
				    "property before returning a node\n");
				break;
			}
			/* Allocate and initialize the property structure */
			property = get_rtas_property(workarea);
			if (property == NULL)
				break;

			if (last_node->properties == NULL)
				last_node->properties = property;
			else
				last_property->next = property;

			/* This property becomes last property for node */
			last_property = property;
		} else if (rc == PREV_PARENT) {
			/* Need to back up to parent device */
			last_node = last_node->parent;
		} else if (rc == MORE_MEMORY) {
			say(ERROR, "Configure_connector called with "
			    "insufficient memory.\n");
			break;
		} else if (rc == NOT_THIS_SYSTEM) {
			/* this card is not supported in this system */
			say(ERROR, "This adapter cannot be attached to this "
			    "system at this\ntime. You may have to remove "
			    "other adapters before this\nadapter can be "
			    "successfully attached.  Consult the hardware"
			    "\ndocumentation for your system to find an "
			    "explanation of\nthe supported combinations "
			    "of adapters that may be attached\nat one "
			    "time.\n");
			break;
		} else if (rc == NOT_THIS_SLOT) {
			/* this card is not supported in this slot */
			say(ERROR, "This adapter is not supported in the "
				"specified slot,\nbut there may be other "
				"slots where it is supported. Consult\nthe "
				"hardware documentation for your system to "
				"find the\nappropriate slots for this "
				"adapter.\n");
			break;
		} else if (rc == ERR_CFG_USE) {
			/* This entity is not usable */
			say(ERROR, "This adapter is currently unusable, available "
			    "for exchange or available for recovery\n");
			break;
		} else if (rc == HARDWARE_ERROR) {
			say(ERROR, "%s\n", hw_error);
			break;
		} else {
			say(ERROR, "Unexpected error (%d) returned from "
			    "configure_connector\n", rc);
			break;
		}
	} /* end while */

	if (rc) {
		say(ERROR, "Configure_connector failed for drc index %x\n"
		    "Data may be out of sync and the system may require "
		    "a reboot.\n", index);

		if (first_node) {
			free_of_node(first_node);
			first_node = NULL;	/* Indicates error condition */
		}
	}

	return first_node;
}

/**
 * set_power
 * @brief Sets the power level for the specified slot.
 *
 * @param domain power domain for the slot from "ibm,drc-power-domains"
 * @param level POWER_ON or POWER_OFF
 * @returns 0		Successful completion
 * @returns SPEED_ERROR	Inserted a PCI 33 MHz IOA into a PCIbus which is
 *                      operating at 66 MHz.
 * @returns HW_ERROR	Hardware error.
 * @returns SW_ERROR	Other errors.
 */
int
set_power(int domain, int level)
{
	int ret_level;

	return rtas_set_power_level(domain, level, &ret_level);
}

/**
 * acquire_drc
 *
 */
int
acquire_drc(uint32_t drc_index)
{
	int rc;

	say(DEBUG, "Acquiring drc index 0x%x\n", drc_index);

	rc = dr_entity_sense(drc_index);
	if (rc != STATE_UNUSABLE) {
		say(ERROR, "Entity sense failed for drc %x with %d\n%s\n",
		    drc_index, rc, entity_sense_error(rc));
		return -1;
	}

	say(DEBUG, "setting allocation state to alloc usable\n");
	rc = rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_USABLE);
	if (rc) {
		say(ERROR, "Allocation failed for drc %x with %d\n%s\n",
		    drc_index, rc, set_indicator_error(rc));
		return -1;
	}

	say(DEBUG, "setting indicator state to unisolate\n");
	rc = rtas_set_indicator(ISOLATION_STATE, drc_index, UNISOLATE);
	if (rc) {
		int ret;
		rc = -1;

		say(ERROR, "Unisolate failed for drc %x with %d\n%s\n",
		    drc_index, rc, set_indicator_error(rc));
		ret = rtas_set_indicator(ALLOCATION_STATE, drc_index,
					 ALLOC_UNUSABLE);
		if (ret) {
			say(ERROR, "Failed recovery to unusable state after "
			    "unisolate failure for drc %x with %d\n%s\n",
			    drc_index, ret, set_indicator_error(ret));
		}
	}

	return rc;
}

int
release_drc(int drc_index, uint32_t dev_type)
{
	int rc;

	say(DEBUG, "Releasing drc index 0x%x\n", drc_index);

	rc = dr_entity_sense(drc_index);
	if (rc != PRESENT)
		say(DEBUG, "drc_index %x sensor-state: %d\n%s\n", drc_index, rc,
		    entity_sense_error(rc));

	say(DEBUG, "setting isolation state to isolate\n");
	rc = rtas_set_indicator(ISOLATION_STATE, drc_index, ISOLATE);
	if (rc) {
		if (dev_type == PHB_DEV) {
			/* Workaround for CMVC 508114, where success returns
			 * too quickly
			 */
			int i = 0;
			while ((rc != 0) && (i < 20)) {
				rc = rtas_set_indicator(ISOLATION_STATE,
							drc_index,
							ISOLATE);
				sleep(1);
				i++;
			}
		}

		if (rc) {
			say(ERROR, "Isolation failed for %x with %d\n%s\n",
			    drc_index, rc, set_indicator_error(rc));
			return -1;
		}
	}

	say(DEBUG, "setting allocation state to alloc unusable\n");
	rc = rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_UNUSABLE);
	if (rc) {
		say(ERROR, "Unable to un-allocate drc %x from the partition "
		    "(%d)\n%s\n", drc_index, rc, set_indicator_error(rc));
		rc = rtas_set_indicator(ISOLATION_STATE, drc_index, UNISOLATE);
		say(DEBUG, "UNISOLATE for drc %x, rc = %d\n", drc_index, rc);
		return -1;
	}

	rc = dr_entity_sense(drc_index);
	say(DEBUG, "drc_index %x sensor-state: %d\n%s\n", drc_index, rc,
	    entity_sense_error(rc));

	return 0;
}
