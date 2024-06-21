/**
 * @file drslot_chrp_pci.c
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

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <locale.h>
#include <librtas.h>
#include <stdbool.h>

#include "rtas_calls.h"
#include "dr.h"
#include "drpci.h"
#include "ofdt.h"

static char *sw_error = "Internal software error. Contact your service "
			"representative.\n";
static char *speed_error = "Add operation failed. The 33MHz PCI card may\n"
			   "not be added to the PCI bus with another adapter\n"
			   "running at 66 MHz.\n";

#define HW_ERROR       -1	/* RTAS hardware error */

/* Error status from set-power */
#define SPEED_ERROR    -9000	/* 33MHz card on bus running at 66MHz */

#define USER_QUIT  0		/* user wants to bail out of operation	 */
#define USER_CONT  1		/* user wants to continue with operation */

static char *usagestr = 	"-c pci -s <drc_name | drc_index> {-i | -a [-I] | -r [-I] | -R [-I]}";
/**
 * pci_usage
 *
 */
void
pci_usage(char **pusage)
{
	*pusage = usagestr;
}

/**
 * process_led
 *
 * sets the given slot's led to the specified state. If the
 * return from set_led is bad, call the error_exit function to clean up
 * and exit the command.
 *
 * @param lslot pointer to slot we're setting
 * @param led setting for led
 */
static int
process_led(struct dr_node *node, int led)
{
	int rtas_rc;

	rtas_rc = rtas_set_indicator(DR_INDICATOR, node->drc_index, led);
	if (rtas_rc) {
		if (rtas_rc == HW_ERROR)
			say(ERROR, "%s", hw_error);
		else
			say(ERROR, "%s", sw_error);

		return -1;
	}

	return 0;
}

/**
 * identify_slot
 *
 * Given a slot structure, set the led of the slot to the identify
 * state and print a message to the user. Read the user input and return it.
 *
 * @param islot pointer to slot to be set
 * @returns USER_CONT if user pushes enter key, USER_QUIT otherwise
 */
static int
identify_slot(struct dr_node *node)
{
	if (process_led(node, LED_ID))
		return USER_QUIT;

	printf("The visual indicator for the PCI slot <%s>\n"
		"has been set to the identify state. Press Enter to\n"
		"continue or enter x to exit.\n", node->drc_name);

	if (getchar() == '\n')
		return (USER_CONT);
	else
		return (USER_QUIT);
}

static char *
find_drc_name(uint32_t drc_index, struct dr_node *all_nodes)
{
	struct dr_node *node;

	node = all_nodes;
	while (node != NULL) {
		say(DEBUG, "%#x =? %#x\n", drc_index, node->drc_index);
		if (node->drc_index == drc_index) {
			say(DEBUG, "Found drc_name %s\n", node->drc_name);
			return node->drc_name;
		} else
			node = node->next;
	}

	if ((node == NULL))
		say(ERROR, "Could not find drc_name for index %#x\n", drc_index);

	return NULL;
}


/**
 * find_slot
 *
 * Searches through all slots and returns a pointer to the slot
 * whose location code matches the user input. If no slot is found,
 * an error message is displayed and the entire command is exited.
 *
 * This routine uses a common function "cmp_drcname" so that all routines
 * validating a slot name will compare on the same basis. A slot could be
 * found, but if the "skip" field of the slot structure is set, we
 * shouldn't use it.
 *
 * @returns pointer to slot on success, NULL otherwise
 */
static struct dr_node *
find_slot(char *drc_name, uint32_t drc_index,
		struct dr_node *all_nodes, bool partner)
{
	struct dr_node *node;	/* working pointer */

	/* Search through all_nodes to see if the
	 * user-specified location exists.
	 */
	node = all_nodes;
	while (node != NULL) {
		if (cmp_drcname(node->drc_name, drc_name))
			break;
		else if (drc_index && (node->drc_index == drc_index))
			break;
		else
			node = node->next;
	}

	/*
	 * Partner path may not be assigned to LPAR.
	 * So ignore if can not find the node for the partner.
	 */
	if ((!partner && (node == NULL)) || (node && (node->skip)))
		say(ERROR, "The specified PCI slot is either invalid\n"
		    "or does not support hot plug operations.\n");

	return node;
}

static int check_card_presence(struct dr_node *node)
{
	int state = EMPTY;
	int i, keep_working;

	say(DEBUG, "Waiting for the card to appear...\n");
	do {
		keep_working = 0;

		for (i = 0; i < 30; i++) {
			state = dr_entity_sense(node->drc_index);
			if (state != EMPTY)
				return state;

			sleep(1);
		}

		if (usr_prompt) {
			printf("The card still does not appear to be present"
			       "\npress Enter to continue to wait or enter "
			       "'x' to exit.\n");

			if ((getchar() == '\n'))
				keep_working = 1;
                }

        } while (keep_working);

	return state;
}

/**
 * card_present
 * @brief Determines if there is a card present in the specified slot
 *
 * Attempt to check if a card is present. If we get a definite answer,
 * return that and indicate that we did not turn the power on. Depending
 * on the adapter, we may have to power on and we may have to unisolate
 * to determine if a card is present. If that's the case, power on
 * maybe isolate and try sensing again. If we hit fatal errors, call
 * error_exit to clean up and exit the command.
 *
 * @param slot pointer to slot we're checking
 * @param power_state state of power when we leave this routine
 * @param isolate_state state of isolation when we leave this routine
 * @returns EMPTY or PRESENT
 */
static int card_present(struct dr_node *node, int *power_state,
			int *isolate_state)
{
	int state, rc;

	*power_state = POWER_OFF;	/* initialize */
	*isolate_state = ISOLATE;

	state = check_card_presence(node);
	if ((state == EMPTY) || (state == PRESENT))
		return state;

	else if (state == HW_ERROR) {
		say(ERROR, "%s", hw_error);
		return -1;
	}
	else if ((state == NEED_POWER) || (state == PWR_ONLY)) {
		/* set power on, and if needed, unisolate too */
		rc = set_power(node->drc_power, POWER_ON);
		if (rc) {
			if (rc == HW_ERROR) {
				say(ERROR, "%s", hw_error);
				return -1;
			}
			else if (rc == SPEED_ERROR) {
				say(ERROR, "%s", speed_error);
				set_power(node->drc_power, POWER_OFF);
				return -1;
			}
			/* Catch any other errors.  */
			else {
				say(ERROR, "%s", sw_error);
				set_power(node->drc_power, POWER_OFF);
				return -1;
			}
		}

		*power_state = POWER_ON;
		if (state == NEED_POWER) {
			/* If we get here, we have power on now
			 * but we still need to unisolate
			 */
			rc = rtas_set_indicator(ISOLATION_STATE,
						node->drc_index, UNISOLATE);
			if (rc) {
				if (rc == HW_ERROR)
					say(ERROR, "%s", hw_error);
				else
					say(ERROR, "%s", sw_error);

				rtas_set_indicator(ISOLATION_STATE,
						   node->drc_index, ISOLATE);
				set_power(node->drc_power, POWER_OFF);
				return -1;
			}
			*isolate_state = UNISOLATE;
		}

		/* Now we have power on, and the unisolate is done
		 * if it was needed. check for card again.
		 */
		state = check_card_presence(node);
		if ((state == EMPTY) || (state == PRESENT))
			return state;

		if (state) {
			if (rc == HW_ERROR)
				say(ERROR, "%s", hw_error);
			else
				say(ERROR, "%s", sw_error);

			rtas_set_indicator(ISOLATION_STATE, node->drc_index,
					   ISOLATE);
			set_power(node->drc_power, POWER_OFF);
			return -1;
		}
	}
	else {
  		/* catch any other errors from first dr_entity_sense */
		say(ERROR, "%s", sw_error);
	}

	return state;
}

/**
 * do_identify
 * @brief  Main processor for the drslot_chrp_pci -i command
 *
 * Validate the user input,  a slot name. Call the routine which actually
 * does the call to set the LED. When we're done identifying, reset the
 * LED based on whether or not there's an OF node representing an adapter
 * connected to the slot.  If an adapter node exists, turn the LED on,
 * else turn if off.
 */
static int do_identify(struct dr_node *all_nodes)
{
	struct dr_node *node;
	int usr_key;
	int led_state;

	node = find_slot(usr_drc_name, 0, all_nodes, 0);
	if (node == NULL)
		return -1;

	if (is_display_adapter(node)) {
		say(ERROR, "Display adapters are not supported by drmgr.\n");
		return -1;
	}

	usr_key = identify_slot(node);

	/* when we're done with identify, put the LED back
	 * where we think it ought to be. ON if an adapter is
	 * connected, OFF if not
	 */
	if (node->children == NULL)
		led_state = LED_OFF;
	else
		led_state = LED_ON;

	if (process_led(node, led_state))
		return -1;

	if (usr_key == USER_QUIT)
		return -1;

	return 0;	/* don't return anything on identify */
}

/**
 * add_work
 *
 * Prepares a PCI hot plug slot for adding an adapter, then
 * configures the adapter and any PCI adapters below into the
 * kernel's and /proc's Open Firmware device tree.
 *
 * If there are any errors from the RTAS routines,
 * the slot is powered off, isolated, and the LED is turned off. No
 * configuration is completed.
 * If the OF tree cannot be updated, the slot is powered
 * off, isolated, and the LED is turned off.
 *
 * @param slot
 * @param partner path or not
 * @returns 0 on success, !0 on failure
 */
static int add_work(struct dr_node *node, bool partner_device)
{
	int pow_state = POWER_OFF;	/* Tells us if power was turned */
					/* on when	 */
	int iso_state = ISOLATE;	/* Tells us isolation state after */
	int rc;
	struct of_node *new_nodes;/* nodes returned from configure_connector */

	/*
	 * Already checked the card presence for the original device
	 * and both multipaths use the same card. So do not need to
	 * check the card presence again for the partner device.
	 */
	if (!partner_device) {
		/* if we're continuing, set LED_ON and see if a card */
		/* is really there. */
		if (process_led(node, LED_ON))
			return -1;

		say(DEBUG, "is calling card_present\n");
		rc = card_present(node, &pow_state, &iso_state);
		if (!rc) {
			say(ERROR, "No PCI card was detected in the specified "
					"PCI slot.\n");
			rtas_set_indicator(ISOLATION_STATE, node->drc_index,
					ISOLATE);
			set_power(node->drc_power, POWER_OFF);
			return -1;
		}
	}

	if (!pow_state) {
		say(DEBUG, "is calling set_power(POWER_ON index 0x%x, "
		    "power_domain 0x%x\n", node->drc_index, node->drc_power);

		rc = set_power(node->drc_power, POWER_ON);
		if (rc) {
			if (rc == HW_ERROR)
				say(ERROR, "%s", hw_error);
			else if (rc == SPEED_ERROR)
				say(ERROR, "%s", speed_error);
			else
				say(ERROR, "%s", sw_error);

			rtas_set_indicator(ISOLATION_STATE, node->drc_index,
					   ISOLATE);
			set_power(node->drc_power, POWER_OFF);
			return -1;
		}
	}

	if (!iso_state) {
		say(DEBUG, "calling rtas_set_indicator(UNISOLATE index 0x%x)\n",
		    node->drc_index);

		rc = rtas_set_indicator(ISOLATION_STATE, node->drc_index,
					UNISOLATE);
		if (rc) {
			if (rc == HW_ERROR)
				say(ERROR, "%s", hw_error);
			else
				say(ERROR, "%s", sw_error);

			rtas_set_indicator(ISOLATION_STATE, node->drc_index,
					   ISOLATE);
			set_power(node->drc_power, POWER_OFF);
			return -1;
		}
	}

	/* Now go get all the new nodes for this adapter. If
	 * the return status requires a message, print it out
	 * and exit, otherwise, add the nodes to the OF tree.
	 */
	new_nodes = configure_connector(node->drc_index);
	if (new_nodes == NULL) {
		rtas_set_indicator(ISOLATION_STATE, node->drc_index, ISOLATE);
		set_power(node->drc_power, POWER_OFF);
		return -1;
	}

	say(DEBUG, "Adding %s to %s\n", new_nodes->name, node->ofdt_path);
	rc = add_device_tree_nodes(node->ofdt_path, new_nodes);
	if (rc) {
		say(DEBUG, "add_device_tree_nodes failed at %s\n",
		    node->ofdt_path);
		say(ERROR, "%s", sw_error);
		rtas_set_indicator(ISOLATION_STATE, node->drc_index, ISOLATE);
		set_power(node->drc_power, POWER_OFF);
		return -1;
	}

	return 0;
}

/**
 * do_insert_card_work
 *
 * Performs the steps required to set the isolation state and turn the
 * power to a slot off. The prompts the user to insert the new card
 * into the slot.
 */
static int do_insert_card_work(struct dr_node *node, bool partner_device)
{
	int rc;

	/* Now it's time to isolate, power off, set the action LED, and
	 * prompt the user to put the card in.
	 */
	say(DEBUG, "is calling rtas_set_indicator(ISOLATE index 0x%x)\n",
	    node->drc_index);

	rc = rtas_set_indicator(ISOLATION_STATE, node->drc_index, ISOLATE);
	if (rc) {
		if (rc == HW_ERROR)
			say(ERROR, "%s", hw_error);
		else
			say(ERROR, "%s", sw_error);

		set_power(node->drc_power, POWER_OFF);
		return -1;
	}

	say(DEBUG, "is calling set_power(POWER_OFF index 0x%x, "
	    "power_domain 0x%x)\n", node->drc_index, node->drc_power);

	rc = set_power(node->drc_power, POWER_OFF);
	if (rc) {
		if (rc == HW_ERROR)
			say(ERROR, "%s", hw_error);
		else
			say(ERROR, "%s", sw_error);

		return -1;
	}

	if (usr_prompt && !partner_device) {
		/* Prompt user to put in card and to press
		 * Enter to continue or other key to exit.
		 */
		if (process_led(node, LED_ACTION))
			return -1;

		printf("The visual indicator for the PCI slot <%s>\n"
			"has been set to the action state. Insert the PCI\n"
			"card into the identified slot, connect any devices\n"
			"to be configured and press Enter to continue.\n"
			"Enter x to exit.\n", node->drc_name);

		if (!(getchar() == '\n')) {
			process_led(node, LED_OFF);
			return 0;
		}
	}

	return 0;
}

/**
 * find_partner_node
 *
 * Find the partner DRC index and retrieve the partner node.
 */
static struct dr_node *
find_partner_node(struct dr_node *node, struct dr_node *all_nodes)
{
	struct dr_node *partner_node = NULL;
	uint32_t partner_index = 0;
	int rc;

	/*
	 * Expect the partner device only for the PCI node
	 */
	if (!node->children)
		return NULL;

	/* Find the multipath partner device index if available */
	rc = get_my_partner_drc_index(node->children, &partner_index);
	if (!rc)
		partner_node = find_slot(NULL, partner_index, all_nodes, 1);

	return partner_node;
}

static int insert_add_work(struct dr_node *node, bool partner_device)
{
	int usr_key = USER_CONT;
	int rc;

	if (!partner_device) {
		/* Prompt user only if in interactive mode. */
		if (usr_prompt) {
			if (usr_slot_identification)
				usr_key = identify_slot(node);

			if (usr_key == USER_QUIT) {
				if (node->children == NULL)
					process_led(node, LED_OFF);
				else
					process_led(node, LED_ON);
				return 0;
			}
		}

		if (node->children != NULL) {
			/* If there's already something here, turn the
			 * LED on and exit with user error.
			 */
			process_led(node, LED_ON);
			say(ERROR, "The specified PCI slot is already occupied.\n");
			return -1;
		}
	}

	if (!pci_hotplug_only) {
		rc = do_insert_card_work(node, partner_device);
		if (rc)
			return rc;
	}

	/* Call the routine which determines
	 * what the user wants and does it.
	*/
        rc = add_work(node, partner_device);
        if (rc)
                return rc;

	/*
	 * Need to populate w/ children to retrieve partner device
	 */
	if (init_node(node))
		return -1;

	return 1;
}

/**
 * do_add
 *
 * Prepares a PCI hot plug slot for adding an adapter, then
 * configures the adapter and any PCI adapters below into
 * the Open Firmware device tree.
 *
 * Verifies that a given hot plug PCI slot does not have an adapter
 * already connected, identifies the slot to the user unless requested not
 * to with the -i flag, prompts the user to connect the adapter, powers
 * the slot on, and calls configure connector. When configure connector
 * completes and returns the new node(s) for the new PCI adapter and any
 * attached devices then the Open Firmware device tree is
 * updated to reflect the new devices.
 */
static int do_add(struct dr_node *all_nodes)
{
	struct dr_node *node, *partner_node = NULL;
	int rc;

	node = find_slot(usr_drc_name, 0, all_nodes, 0);
	if (node == NULL)
		return -1;

	if (is_display_adapter(node)) {
		say(ERROR, "DLPAR of display adapters is not supported.\n");
		return -1;
	}

	rc = insert_add_work(node, false);
	if (rc <= 0)
		return rc;

	partner_node = find_partner_node(node, all_nodes);
	if (partner_node) {
		printf("<%s> and <%s> are\nmultipath partner devices. "
			"So <%s> is\nalso added.\n", node->drc_name,
			partner_node->drc_name, partner_node->drc_name);
		rc = insert_add_work(partner_node, true);
		if (rc <= 0)
			return rc;
	}

	say(DEBUG, "is calling enable_slot to config adapter\n");

	/* Try to config the adapter. The rpaphp module doesn't play well with
	 * qemu pci slots so we let the generic kernel pci code probe the device
	 * by rescanning the bus in the qemu virtio case.
	 */
	if (!pci_virtio) {
		set_hp_adapter_status(PHP_CONFIG_ADAPTER, node->drc_name);
		if (partner_node)
			set_hp_adapter_status(PHP_CONFIG_ADAPTER,
					partner_node->drc_name);
	} else
		pci_rescan_bus();

	return 0;
}

/**
 * remove_work
 *
 * Removes nodes associated a PCI slot from the
 * Open Firmware device tree and isolates and powers off the PCI slot.
 *
 * A remove may be specified by the location code of the  PCI slot.
 * Unless the user specifies the -I flag, the slot is identified to
 * the user.
 * Nodes representing the device(s) are removed from the
 * Open Firmware device tree. The slot is isolated and powered off,
 * and the LED is turned off.
 *
 * @returns 0 on success, -1 on failure
 */
static int remove_work(struct dr_node *node, bool partner_device)
{
	struct dr_node *child;
	int rc;
	int usr_key = USER_CONT;

	if (node == NULL)
		return -1;

	say(DEBUG, "found node: drc name=%s, index=0x%x, path=%s\n",
	     node->drc_name, node->drc_index, node->ofdt_path);

	if (is_display_adapter(node)) {
		say(ERROR, "DLPAR of display adapters is not supported.\n");
		return -1;
	}

	if (!partner_device) {
		if (usr_prompt) {
			if (usr_slot_identification)
				usr_key = identify_slot(node);

			if (usr_key == USER_QUIT) {
				if (node->children == NULL)
					process_led(node, LED_OFF);
				else
					process_led(node, LED_ON);
				return -1;
			}
		}

		/* Turn on the LED while we go do some work. */
		if (process_led(node, LED_ON))
			return -1;

		/* Make sure there's something there to remove. */
		if (node->children == NULL) {
			process_led(node, LED_OFF);
			say(ERROR, "There is no configured card to remove "
				"from the specified PCI slot.\n");
			return -1;
		}
	}

	if (!pci_virtio) {
		/* Make sure all the devices are
		 * not configured before proceeding
		 */
		if (get_hp_adapter_status(node->drc_name) == CONFIG) {
			say(DEBUG, "unconfiguring adapter in slot[%s]\n",
				node->drc_name);
			set_hp_adapter_status(PHP_UNCONFIG_ADAPTER, node->drc_name);

			int rc = get_hp_adapter_status(node->drc_name);
			if (rc != NOT_CONFIG) {
				say(ERROR, "Unconfig adapter failed.\n");
				return -1;
			}
		} else {
			/* In certain cases such as a complete failure of the
			 * adapter there may not have been the possibility to clean
			 * up everything. Mark these adapaters for additional
			 * processing later.
			 */
			node->post_replace_processing = 1;
		}
	} else {
		/* The rpaphp module fails to work with qemu pci slots so
		 * so we rely on the devices remove attribute in sysfs to
		 * inform the generic kernel pci code to remove the qemu virtio
		 * device
		 */
		for (child = node->children; child; child = child->next)
			pci_remove_device(child);

		/* The kernel adds the pci device removal to a work queue which
		 * makes the previous pci_remove_device call asynchronous. Need
		 * to let kernel finish cleanup before informing qemu to isolate
		 * and power down virtio pci device.
		 */
		sleep(3);
	}

	/* Call subroutine to remove node(s) from
	 * the device tree.
	 */
	for (child = node->children; child; child = child->next) {
		rc = remove_device_tree_nodes(child->ofdt_path);
		if (rc) {
			say(ERROR, "%s", sw_error);
			rtas_set_indicator(ISOLATION_STATE, node->drc_index,
					   ISOLATE);
			set_power(node->drc_power, POWER_OFF);
			return -1;
		}
	}

	if (pci_hotplug_only)
		return 0;

	/* We have to isolate and power off before
	 * allowing the user to physically remove
	 * the card.
	 */
	say(DEBUG, "is calling rtas_set_indicator(ISOLATE index 0x%x)\n",
	    node->drc_index);

	rc = rtas_set_indicator(ISOLATION_STATE, node->drc_index, ISOLATE);
	if (rc) {
		if (rc == HW_ERROR)
			say(ERROR, "%s", hw_error);
		else
			say(ERROR, "%s", sw_error);

		set_power(node->drc_power, POWER_OFF);
		return rc;
	}

	say(DEBUG, "is calling set_power(POWER_OFF index 0x%x, power_domain "
	    "0x%x)\n", node->drc_index, node->drc_power);

	rc = set_power(node->drc_power, POWER_OFF);
	if (rc) {
		if (rc == HW_ERROR)
			say(ERROR, "%s", hw_error);
		else
			say(ERROR, "%s", sw_error);

		set_power(node->drc_power, POWER_OFF);
		return rc;
	}

	return 0;
}

/**
 * do_remove
 *
 * Removes nodes associated a PCI slot from the Open Firmware
 * device tree and isolates and powers off the PCI slot.
 *
 * A remove may be specified by the location code of the PCI slot.
 * Unless the user specifies the -I flag, the slot is identified
 * the user. Nodes representing the device(s) are removed from the
 * Open Firmware device tree. The slot is isolated and powered off,
 * and the LED is turned off.
 *
 * If there are any errors from the RTAS routines,
 * the slot is powered off, isolated, and the LED is turned off. No
 * unconfiguration is completed.
 * If  the OF tree cannot be updated, the slot is powered
 * off, isolated, and the LED is turned off.
 */
static int do_remove(struct dr_node *all_nodes)
{
	struct dr_node *node, *partner_node = NULL;

	node = find_slot(usr_drc_name, 0, all_nodes, 0);
	if (node == NULL)
		return -1;

	partner_node = find_partner_node(node, all_nodes);
        if (partner_node)
		printf("<%s> and <%s> are\nmultipath partner devices. "
			"So <%s> will\nbe also removed.\n", node->drc_name,
			partner_node->drc_name, partner_node->drc_name);

	/* Remove the specified slot and update the device-tree */
	if (remove_work(node, false))
		return -1;

	if (partner_node) {
		if (remove_work(partner_node, true))
			return -1;
	}

	/* Prompt user to remove card and to press
	 * Enter to continue. Can't exit out of here.
	 */
	if (usr_prompt) {
		if (process_led(node, LED_ACTION))
			return -1;

		printf("The visual indicator for the specified PCI "
			"slot has\nbeen set to the action state. "
			"Remove the PCI card\nfrom the identified "
			"slot and press Enter to continue.\n");
		getchar();
		if (process_led(node, LED_OFF))
			return -1;
	}

	return 0;
}

static int replace_add_work(struct dr_node *node, bool partner_device)
{

	say(DEBUG, "repl_node:path=%s node:path=%s\n",
	    node->ofdt_path, node->children->ofdt_path);

	/* Prompt user to replace card and to press
	 * Enter to continue or x to exit. Exiting here
	 * means the original card has been removed.
	 */
	if (usr_prompt && !partner_device) {
		if (process_led(node, LED_ACTION))
			return -1;

		printf("The visual indicator for the specified PCI slot <%s>\n"
			"has been set to the action state. Replace the PCI\n"
			"card in the identified slot and press Enter to "
			"continue.\nEnter x to exit. Exiting now leaves the "
			"PCI slot\nin the removed state.\n",
			node->drc_name);

		if (!(getchar() == '\n')) {
			process_led(node, LED_OFF);
			return 0;
		}
	}

	if (add_work(node, partner_device))
		return -1;

	say(DEBUG, "CONFIGURING the card in node[name=%s, path=%s]\n",
	    node->drc_name, node->ofdt_path);

	set_hp_adapter_status(PHP_CONFIG_ADAPTER, node->drc_name);

	return 1;
}

/**
 * do_replace
 * @brief Allows the replacement of an adapter connected to a
 *        PCI hot plug slot
 *
 * A replace may be specified by the location code of the PCI slot.
 * Unless the user specifies the -I flag, the slot is identified to
 * the user.
 * Nodes representing the old device(s) are removed from the
 * Open Firmware device tree. The slot is isolated and powered off,
 * and the LED is set to the ACTION state. The user is prompted to replace
 * the adpater. The slot is powered on and unisolated and configure
 * connector is executed.
 *
 * If there are any errors from the RTAS routines,
 * the slot is powered off, isolated, and the LED is turned off. If the
 * original adapter has been removed, it is left in that state.
 * If the OF tree cannot be updated, the slot is powered
 * off, isolated, and the LED is turned off.
 */
static int do_replace(struct dr_node *all_nodes)
{
	struct dr_node *repl_node, *partner_node = NULL;
	int rc;

	repl_node = find_slot(usr_drc_name, 0, all_nodes, 0);
	if (repl_node == NULL)
		return -1;

	partner_node = find_partner_node(repl_node, all_nodes);
	if (partner_node)
		printf("<%s> and <%s> are\nmultipath partner devices. "
			"So <%s> will\nbe also replaced.\n",
			repl_node->drc_name, partner_node->drc_name,
			partner_node->drc_name);

	/* Call the routine which does the work of getting the node info,
	 * then removing it from the OF device tree.
	 */
	if (remove_work(repl_node, false))
		return -1;

	if (partner_node) {
		if (remove_work(partner_node, true))
			return -1;
	}

	if (!repl_node->children) {
		say(ERROR, "Bad node struct.\n");
		return -1;
	}

	rc = replace_add_work(repl_node, false);
	if (rc <= 0)
		return rc;

	rc = replace_add_work(partner_node, true);
	if (rc <= 0)
		return rc;

	if (repl_node->post_replace_processing) {
		int prompt_save = usr_prompt;
		struct dr_node *node = repl_node;

		say(DEBUG, "Doing post replacement processing...\n");
		/* disable prompting for post-processing */
		usr_prompt = 0;;

		repl_node = find_slot(usr_drc_name, 0, node, 0);
		if (remove_work(repl_node, false))
			return -1;

		partner_node = find_partner_node(repl_node, node);
		if (partner_node) {
			if (remove_work(partner_node, true))
				return -1;
		}

		rc = add_work(repl_node, false);
		if (!rc)
			set_hp_adapter_status(PHP_CONFIG_ADAPTER,
					      repl_node->drc_name);

		if (partner_node) {
			rc = add_work(partner_node, true);
			if (!rc)
				set_hp_adapter_status(PHP_CONFIG_ADAPTER,
					partner_node->drc_name);
		}

		usr_prompt = prompt_save;
	}

	return rc;
}

int valid_pci_options(void)
{
	if ((usr_action == IDENTIFY) && (!usr_slot_identification)) {
		say(ERROR, "Cannot specify the -i and -I option together\n");
		return -1;
	}

	/* The -s option can specify a drc name or drc index */
	if (usr_drc_name && !strncmp(usr_drc_name, "0x", 2)) {
		usr_drc_index = strtoul(usr_drc_name, NULL, 16);
		usr_drc_name = NULL;
	}

	if (!usr_drc_name  && !usr_drc_index) {
		say(ERROR, "A drc name or index must be specified\n");
		return -1;
	}

	if ((usr_action != ADD) && (usr_action != REMOVE)
	    && (usr_action != IDENTIFY) && (usr_action != REPLACE)) {
		say(ERROR, "The '-r', '-a', '-R' or '-i' option must be spcified "
	 	    "for PCI operations\n");
		return -1;
	}

	return 0;
}

int drslot_chrp_pci(void)
{
	int rc;
	struct dr_node *all_nodes;

	all_nodes = get_hp_nodes();
	if (all_nodes == NULL) {
		say(ERROR, "There are no PCI hot plug slots on this system.\n");
		return -1;
	}

#ifdef DBG_HOT_PLUG
	print_slots_list(all_nodes);
#endif

	if (!usr_drc_name)
		usr_drc_name = find_drc_name(usr_drc_index, all_nodes);

	switch (usr_action) {
	    case ADD:
		rc = do_add(all_nodes);
		break;
	    case REMOVE:
		rc = do_remove(all_nodes);
		break;
	    case REPLACE:
		rc = do_replace(all_nodes);
		break;
	    case IDENTIFY:
		rc = do_identify(all_nodes);
		break;
	    default:
		say(ERROR, "Invalid operation specified!\n");
		rc = -1;
		break;
	}

	free_node(all_nodes);
	return rc;
}
