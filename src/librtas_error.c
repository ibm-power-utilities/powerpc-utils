/**
 * @file librtas_error.c
 * @brief Common librtas_error routine for powerpc-utils-papr commands
 *
 * Copyright (c) 2004 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @author Nathan Fontenot <nfont@austin.ibm.com>
 */

#include <stdio.h>
#include <librtas.h>

/**
 * librtas_error
 * @brief check for librtas specific return codes
 *
 * This will check the erro value for a librtas specific return code
 * and fill in the buffer with the appropraite error message
 *
 * @param error return code from librtas
 * @param buf buffer to fill with error string
 * @param size size of "buffer"
 */
void librtas_error(int error, char *buf, size_t size)
{
	switch (error) {
	    case RTAS_KERNEL_INT:
		snprintf(buf, size, "No kernel interface to firmware");
		break;
	    case RTAS_KERNEL_IMP:
		snprintf(buf, size, "No kernel implementation of function");
		break;
	    case RTAS_PERM:
		snprintf(buf, size, "Non-root caller");
		break;
	    case RTAS_NO_MEM:
		snprintf(buf, size, "Out of heap memory");
		break;
	    case RTAS_NO_LOWMEM:
		snprintf(buf, size, "Kernel out of low memory");
		break;
	    case RTAS_FREE_ERR:
		snprintf(buf, size, "Attempt to free nonexistant RMO buffer");
		break;
	    case RTAS_TIMEOUT:
		snprintf(buf, size, "RTAS delay exceeded specified timeout");
		break;
	    case RTAS_IO_ASSERT:
		snprintf(buf, size, "Unexpected librtas I/O error");
		break;
	    case RTAS_UNKNOWN_OP:
		snprintf(buf, size, "No firmware implementation of function");
		break;
	    default:
		snprintf(buf, size, "Unknown librtas error %d", error);
	}

	return;
}

int is_librtas_error(int error)
{
	int rc = 0;

	switch (error) {
	    case RTAS_KERNEL_INT:
	    case RTAS_KERNEL_IMP:
	    case RTAS_PERM:
	    case RTAS_NO_MEM:
	    case RTAS_NO_LOWMEM:
	    case RTAS_FREE_ERR:
	    case RTAS_TIMEOUT:
	    case RTAS_IO_ASSERT:
	    case RTAS_UNKNOWN_OP:
		rc = 1;
	}

	return rc;
}
