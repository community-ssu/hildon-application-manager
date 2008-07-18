/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007, 2008 Nokia Corporation.  All Rights reserved.
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

#ifndef CATALOGUES_H
#define CATALOGUES_H

extern "C" {
#include "xexp.h"
}

/* The directory where packages store their catalogues
 */
#define PACKAGE_CATALOGUES "/usr/share/hildon-application-manager/catalogues/"

/* The file extension for package catalogues
 */
#define CATALOGUES_EXT "xexp"

/* Only reads the package catalogues
 */
xexp *read_package_catalogues (void);

/* Reads the package catalogues and merges them with the system catalogue
 */
xexp *read_catalogues (void);

/* Writes the user catalogues filtering the packages catalogues
 */
int write_user_catalogues (xexp *catalogues);

/* Retrieves a package catalogue entry given the id and the file
 */
xexp* find_package_catalogue (const gchar *id, const gchar *file, xexp* pkgcat);

#endif /* !CATALOGUES_H */
