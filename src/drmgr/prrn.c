#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dr.h"
#include "drmem.h"
#include "drcpu.h"

int handle_prrn(char *filename)
{
	struct options opts;
	char fmt_drc[11];
	char type[4];
	char drc[9];
	int rc = 0;
	FILE *fd;

	fd = fopen(filename, "r");
	if (!fd) {
		say(ERROR, "Failed to open the file %s\n", filename);
		return -1;
	}

	set_output_level(4);

	memset(&opts, 0, sizeof(opts));

	while (fscanf(fd, "%3s %8s\n", type, drc) == 2) {
		/* Set up options struct */
		opts.ctype = type;
		sprintf(fmt_drc, "0x%s", drc);
		usr_drc_name = fmt_drc;

		set_timeout(PRRN_TIMEOUT);

		if (!strcmp(type, "mem")) {
			usr_action = REMOVE;
			rc = drslot_chrp_mem(&opts);
			if (rc)
				continue;

			usr_action = ADD;
			drslot_chrp_mem(&opts);
		} else if (!strcmp(type, "cpu")) {
			usr_action = REMOVE;
			rc = drslot_chrp_cpu(&opts);
			if (rc)
				continue;

			usr_action = ADD;
			drslot_chrp_cpu(&opts);
		} else {
			say(ERROR, "Device type \"%s\" not recognized.\n",
			    type);
			continue;
		}
	}

	return 0;
}
