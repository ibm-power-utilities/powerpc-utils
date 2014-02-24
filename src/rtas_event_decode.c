/**
 * @file rtas_event_decode.c
 * @brief decode RTAS event messages into human readable text
 *
 * Copyright (C) 2005 International Business Machines
 * Common Public License Version 1.0 (see COPYRIGHT)
 *
 * @author Nathan Fonetenot <nfont@linux.vnet.ibm.com>
 * @author Jake Moilanen  <moilanen@us.ibm.com>
 *
 * RTAS messages are placed in the syslog encoded in a binary
 * format, and are unreadable.  This tool will take exactly one
 * message, parse it, and spit out the human-readable equivalent.
 * This program expects ascii data on stdin.
 *
 * This tool is mostly meant to be used in conjuction with the
 * 'rtas_dump' shell script, which provides a suitable user 
 * interface.
 *
 * Bug fixes June 2004 by Linas Vepstas <linas@linas.org>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <librtasevent.h>
#include "pseries_platform.h"

#define RTAS_BUF_SIZE   3000
#define RTAS_STR_SIZE   1024
char rtas_buf[RTAS_BUF_SIZE];

/**
 * get_buffer
 * @brief read an RTAS event in from the specified input
 *
 * @param fh file to read RTAS event from
 * @param msgbuf buffer to write RTAS event into
 * @param buflen length of "msgbuf"
 * @return amount read into msgbuf
 */
int
get_buffer(FILE *fh, char *msgbuf, size_t buflen)
{
    char tmpbuf[RTAS_STR_SIZE];
    unsigned int val;
    size_t j = 0;
    int high = 1;
    char *p, *line;
   
    memset(msgbuf, 0, buflen);

    line = fgets(tmpbuf, RTAS_STR_SIZE, fh);

    while (line) {
        /* Skip over any obviously busted input ... */
        if (strstr (tmpbuf, "event begin")) goto next;
        if (strstr (tmpbuf, "eventbegin")) goto next;
        if (strstr (tmpbuf, "event end")) goto done;
        if (strstr (tmpbuf, "eventend")) goto done;

        /* Skip over the initial part of the line */
        p = strstr (line, "RTAS");
        if (p) 
           p = strchr (p, ':');
        else 
            p = line;
        
        while (p && *p) {
            val = 0xff;

	    if (*p >= '0' && *p <= '9') 
                val = *p - '0';
            else if (*p >= 'a' && *p <= 'f')
                val = *p - 'a' + 0xa;
            else if (*p >= 'A' && *p <= 'F')
                val = *p - 'A' + 0xa;

            if (val != 0xff) {
		if (high) {
		    msgbuf[j] = val << 4;
		    high = 0;
		} else { 
		    msgbuf[j++] |= val; 
		    high = 1;
		}
	    }

            /* Don't overflow the output buffer */
            if (j >= buflen) 
                goto done;
            p++;
	}
next:
        line = fgets (tmpbuf, RTAS_STR_SIZE, fh);
    }
            
done:
    return j;
}

/**
 * usage
 * @brief print the event_decode usage statement
 *
 * @param progname argv[0]
 */
void 
usage (const char *progname)
{
    printf("Usage: %s [-dv] [-n eventnum]\n", progname);
    printf("-d              dump the raw RTAS event\n");
    printf("-n eventnum     event number of the RTAS event being dumped\n");
    printf("-v              verbose, print all details, not just header\n");
    printf("-w width        limit the output to the specified width, default\n"
           "                  width is 80 characters. The width must be > 0\n"
           "                  and < 1024.\n");
}

int 
main(int argc , char *argv[])
{
    struct rtas_event *re;
    int     event_no = -1;
    int     verbose = 0;
    int     dump_raw = 0;
    int     len = 0;
    int     c, rtas_buf_len;

    switch (get_platform()) {
    case PLATFORM_UNKNOWN:
    case PLATFORM_POWERKVM_HOST:
	fprintf(stderr, "%s: is not supported on the %s platform\n",
						argv[0], platform_name);
	exit(1);
    }
    /* Suppress error messages from getopt */
    opterr = 0;

    while ((c = getopt(argc, argv, "dn:vw:")) != EOF) {
        switch (c) {
            case 'd':
                dump_raw = 1;
                break;
	    case 'n':
		event_no = atoi(optarg);
		break;
            case 'v':
                verbose++;
                break;
            case 'w':
                if (rtas_set_print_width(atoi(optarg))) {
                    fprintf(stderr, "rtas_dump: (%d) is not a valid print "
                            "width\n", atoi(optarg));
                    usage(argv[0]);
                    exit(1);
                }
                break;
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    rtas_buf_len = get_buffer(stdin, rtas_buf, RTAS_BUF_SIZE);

    re = parse_rtas_event(rtas_buf, rtas_buf_len);

    while (re != NULL) {
        if (event_no != -1)
            re->event_no = event_no;

        if (dump_raw) { 
            len += rtas_print_raw_event(stdout, re);
            fprintf(stdout, "\n");
        }

        len += rtas_print_event(stdout, re, verbose);
        fflush(stdout);

        cleanup_rtas_event(re);

        rtas_buf_len = get_buffer(stdin, rtas_buf, RTAS_BUF_SIZE);
        re = parse_rtas_event(rtas_buf, rtas_buf_len);
    }
            
    return len;
}
