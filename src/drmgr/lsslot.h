/**
 * @file lsslot.h
 * @brief Common data for lsslot* commands
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
 *
 * @author Nathan Fontenot <nfont@linux.vnet.ibm.com>
 */

#ifndef _H_LSSLOT
#define _H_LSSLOT

/* command options */
struct cmd_opts {
	int	slot_type;
#define PCI	0
#define SLOT	1
#define PHB	2
#define CPU	3
#define MEM	4
#define PORT	5

	int	a_flag;
	int	o_flag;
	int	b_flag;
	int	p_flag;
	int	timeout;
	char	*delim;
	char	*s_name;
};

#define MAX(x,y)	(((x) > (y)) ? (x) : (y))

/* lsslot.c */
int lsslot(struct cmd_opts *);

/* lsslot_chrp_phb.c */
int lsslot_chrp_phb(struct cmd_opts *);

/* lsslot_chrp_cpu.c */
int lsslot_chrp_cpu(struct cmd_opts *);

#endif
