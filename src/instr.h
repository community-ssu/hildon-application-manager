/*
 * This file is part of osso-application-installer
 *
 * Copyright (C) 2005, 2006 Nokia Corporation.  All Right reserved.
 *
 * Contact: Marius Vollmer <marius.vollmer@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef INSTR_H
#define INSTR_H

/* Carry out the instruction in FILENAME, which is assumed to refer to
   a local file as produced by 'localize_file_and_keep_it_open'.
   OPEN_LOCAL_INSTALL_INSTRUCTIONS will call 'cleanup_temp_file' when
   it is done with FILENAME.
*/
void open_local_install_instructions (const char *filename);

#endif /* !INSTR_H */
