#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dr.h"
#include "drmem.h"
#include "drcpu.h"

int handle_prrn(void)
{
	char fmt_drc[11];
	char type[4];
	char drc[9];
	int rc = 0;
	FILE *fd;

	fd = fopen(prrn_filename, "r");
	if (!fd) {
		say(ERROR, "Failed to open the file %s\n", prrn_filename);
		return -1;
	}

	set_output_level(4);

	while (fscanf(fd, "%3s %8s\n", type, drc) == 2) {
		usr_drc_type = to_drc_type(type);
		sprintf(fmt_drc, "0x%s", drc);
		usr_drc_name = fmt_drc;

		set_timeout(PRRN_TIMEOUT);

		if (!strcmp(type, "mem")) {
			usr_action = REMOVE;
			rc = drslot_chrp_mem();
			if (rc)
				continue;

			usr_action = ADD;
			drslot_chrp_mem();
		} else if (!strcmp(type, "cpu")) {
			usr_action = REMOVE;
			rc = drslot_chrp_cpu();
			if (rc)
				continue;

			usr_action = ADD;
			drslot_chrp_cpu();
		} else {
			say(ERROR, "Device type \"%s\" not recognized.\n",
			    type);
			continue;
		}
	}

	return 0;
}
