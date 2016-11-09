/*
 * Copyright (C) 1998 Paul Mackerras.
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#if defined(__FreeBSD__)
#include <sys/endian.h>
#else
#include <endian.h>
#endif

int recurse;
int maxbytes = 128;
int words_per_line = 0;
unsigned char *buf;

void lsprop(FILE *f, char *name);
void lsdir(char *name);

int main(int ac, char **av)
{
    FILE *f;
    int i;
    struct stat sb;
    char *endp;

    while ((i = getopt(ac, av, "Rm:w:")) != EOF) {
	switch (i) {
	case 'R':
	    recurse = 1;
	    break;
	case 'm':
	    maxbytes = strtol(optarg, &endp, 0);
	    if (endp == optarg) {
		fprintf(stderr, "%s: bad argument (%s) to -m option\n", av[0],
			optarg);
		exit(1);
	    }
	    maxbytes = (maxbytes + 15) & -16;
	    break;
	case 'w':
	    words_per_line = strtol(optarg, &endp, 0);
	    if (endp == optarg) {
		fprintf(stderr, "%s: bad argument (%s) to -w option\n",
			av[0], optarg);
		exit(1);
	    }
	    break;
	}
    }

    buf = malloc(maxbytes);
    if (buf == 0) {
	fprintf(stderr, "%s: virtual memory exhausted\n", av[0]);
	exit(1);
    }

    if (optind == ac)
	lsdir(".");
    else
	for (i = optind; i < ac; ++i) {
	    if (stat(av[i], &sb) < 0) {
		perror(av[i]);
		continue;
	    }
	    if (S_ISREG(sb.st_mode)) {
		f = fopen(av[i], "r");
		if (f == NULL) {
		    perror(av[i]);
		    continue;
		}
		lsprop(f, av[i]);
		fclose(f);
	    } else if (S_ISDIR(sb.st_mode)) {
		lsdir(av[i]);
	    }
	}
    exit(0);
}

void lsdir(char *name)
{
    DIR *d;
    struct dirent *de;
    char *p, *q;
    struct stat sb;
    FILE *f;
    int np = 0;

    d = opendir(name);
    if (d == NULL) {
	perror(name);
	return;
    }

    p = malloc(strlen(name) + 520);
    if (p == 0) {
	fprintf(stderr, "%s: virtual memory exhausted\n", name);
	closedir(d);
	return;
    }
    strcpy(p, name);
    q = p + strlen(p);
    while (q > p && q[-1] == '/')
	--q;
    if (q == p + 1 && p[0] == '.')
	q = p;
    else
	*q++ = '/';

    while ((de = readdir(d)) != NULL) {
	if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
	    continue;
	strcpy(q, de->d_name);
	if (stat(p, &sb) < 0) {
	    perror(p);
	    continue;
	}
	if (S_ISREG(sb.st_mode)) {
	    f = fopen(p, "r");
	    if (f == NULL) {
		perror(p);
	    } else {
		lsprop(f, de->d_name);
		fclose(f);
		++np;
	    }
	}
    }
    rewinddir(d);
    while ((de = readdir(d)) != NULL) {
	if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
	    continue;
	strcpy(q, de->d_name);
	if (lstat(p, &sb) < 0) {
	    perror(p);
	    continue;
	}
	if (S_ISDIR(sb.st_mode)) {
	    if (np)
		printf("\n");
	    printf("%s:\n", p);
	    lsdir(p);
	    ++np;
	}
    }
    free(p);
    closedir(d);
}

void lsprop(FILE *f, char *name)
{
    int n, nw, npl, i, j;

    n = fread(buf, 1, maxbytes, f);
    if (n < 0) {
	printf("%s: read error\n", name);
	return;
    }
    printf("%-16s", name);
    if (strlen(name) > 16)
	printf("\n\t\t");
    for (i = 0; i < n; ++i)
	if (buf[i] >= 0x7f ||
	    (buf[i] < 0x20 && buf[i] != '\r' && buf[i] != '\n'
	     && buf[i] != '\t' && buf[i] != 0))
	    break;
    if (i == n && n != 0 && (n == 1 || buf[0] != 0) && buf[n-1] == 0) {
	printf(" \"");
	for (i = 0; i < n - 1; ++i)
	    if (buf[i] == 0)
		printf("\"\n\t\t \"");
	    else if (buf[i] == '\r' || buf[i] == '\n')
		printf("\n\t\t ");
	    else
		putchar(buf[i]);
	putchar('"');
    } else if ((n & 3) == 0) {
	nw = n >> 2;
	if (nw == 1) {
	    i = be32toh(*(int *)buf);
	    printf(" %.8x", i);
	    if (i > -0x10000 && !(i >= 0 && i <= 9))
		printf(" (%d)", i);
	} else {
	    npl = words_per_line;
	    if (npl <= 0) {
		if ((nw % 6) == 0)
		    npl = 6;
		else if ((nw % 5) == 0)
		    npl = 5;
		else
		    npl = 4;
	    }
	    for (i = 0; i < nw; i += npl) {
		if (i != 0)
		    printf("\n\t\t");
		for (j = 0; j < npl && i + j < nw; ++j)
		    printf(" %.8x", be32toh(((unsigned int *)buf)[i+j]));
	    }
	}
    } else {
	for (i = 0; i < n; i += 16) {
	    if (i != 0)
		printf("\n\t\t");
	    for (j = 0; j < 16 && i + j < n; ++j)
		printf(" %.2x", buf[i+j]);
	    for (; j < 16; ++j)
		printf("   ");
	    for (j = 0; j < 16 && i + j < n; ++j)
		if (buf[i+j] > 0x20 && buf[i+j] <= 0x7e)
		    putchar(buf[i+j]);
		else
		    putchar('.');
	}
    }
    printf("\n");
    if (n == maxbytes) {
	while ((i = fread(buf, 1, maxbytes, f)) > 0)
	    n += i;
	if (n > maxbytes)
	    printf("\t\t [%d bytes total]\n", n);
    }
}
