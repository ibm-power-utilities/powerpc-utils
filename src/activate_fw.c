/**
 * @file activate_fw.c
 * @brief Activate Firmware command
 */
/**
 * @mainpage activate_firmware documentation
 * @section Copyright
 * Copyright (c) 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @section Overview
 * Simple command to call the "ibm,activate-firmware" rtas call via librtas.so
 *
 * The return codes for this command are as follows:<br>
 *      0 - Success!!<br>
 *      1 - This platform doesn't support concurrent activation of firmware.<br>
 *      2 - There's no new firmware ready to activate (RTAS returned -9001).<br>
 *      3 - You must have root authority to run this command.<br>
 *	4 - Hardware failure (RTAS returned -1).<br>
 *	5 - Memory/resource allocation error.<br>
 *	6 - General error.<br>
 *
 * For the specific mappings of librtas and rtas_call return codes (librtas
 * return codes are in all caps) to the return codes listed above see the
 * switch statement in the code.  There are two values that can be returned
 * by the rtas call but are not explicitly handled below and are handled by
 * the default case statement. These are -2 (busy, try again) and 990x
 * (extended delay).  The librtas module intercepts these return codes and
 * handles them itself, they should never be returned from librtas.
 *
 * @author Nathan Fontenot <nfont@linux.vnet.ibm.com>
 */

#include <stdio.h>
#include <librtas.h>
#include "pseries_platform.h"

/**
 * @def say(_f, _a...)
 * @brief DEBUG definition of printf
 */
#ifdef DEBUG
#define say(_f, _a...)	printf(_f, ##_a)
#else
#define say(_f, _a...)
#endif

static int activate_firmware(void)
{
	int rc;

	rc = rtas_activate_firmware();

	/* Map 'rc' to valid return code listed above */
	switch (rc) {
	/* 0 - Success!! */
	case 0:
		say("activate_firmware: rtas call succeeded\n");
		break;

	/* 1 - activate-firmware not supported */
	case RTAS_KERNEL_INT:  /* No kernel interface to firmware */
	case RTAS_KERNEL_IMP:  /* No kernel implementation of function */
	case RTAS_UNKNOWN_OP:  /* No firmware implementation of function */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 1);
		rc = 1;
		break;

	/* 2 - no new firmware to activate */
	case -9001:	   /* No valid firmware to activate */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 2);
		rc = 2;
		break;

	/* 3 - no root authority  */
	case RTAS_PERM:	   /* No root authority */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 3);
		rc = 3;
		 break;

	/* 4 - hardware error */
	case -1:	   /* Hardware error */
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 4);
		rc = 4;
		break;

	/* 5 - Memory/resource allocation error */
	case RTAS_NO_MEM:
	case RTAS_NO_LOWMEM:
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 5);
		rc = 5;
		break;

	/* 6 - catch all other return codes here */
	default:
		say("activate_fw: rtas call returned %d, converting to %d\n",
		    rc, 6);
		rc = 6;
		break;
	}

	return rc;
}

int main(void)
{
	if (get_platform() != PLATFORM_PSERIES_LPAR) {
		fprintf(stderr,
			"activate_firmware: is not supported on the %s platform\n",
			platform_name);
		return 1;
	}

	return activate_firmware();
}
