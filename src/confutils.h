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

#ifndef CONFUTILS_H
#define CONFUTILS_H

extern "C" {
#include "xexp.h"
}

/* The file with the current values of the system-wide settings.
 */
#define SYSTEM_SETTINGS_FILE "/etc/hildon-application-manager/settings"

/* The file with the default values of the system-wide settings.
 */
#define SYSTEM_SETTINGS_DEFAULTS_FILE "/usr/share/hildon-application-manager/defaults"

/* The file where we store our catalogues for ourselves.
 */
#define CATALOGUE_CONF "/etc/hildon-application-manager/catalogues"

/* The file where we store our catalogues for apt-pkg to read.
 */
#define CATALOGUE_APT_SOURCE "/etc/apt/sources.list.d/hildon-application-manager.list"

/* The directory where packages store their catalogues.
 */
#define PACKAGE_CATALOGUES "/usr/share/hildon-application-manager/catalogues/"

/* The file extension for system-wide configuration files.
 */
#define SYSTEM_CONFIG_EXT "xexp"

/* The file where we store our domain information.
 */
#define DOMAIN_CONF "/etc/hildon-application-manager/domains"

/* The directory where packages store their domains.
 */
#define PACKAGE_DOMAINS "/usr/share/hildon-application-manager/domains/"

/* The files where we store our backup data.
 *
 * See backup_catalogues for an explanation why we have two backups of
 * the catalogues.
 *
 */
#define BACKUP_CATALOGUES "/var/lib/hildon-application-manager/catalogues.backup"
#define BACKUP_CATALOGUES2 "/var/lib/hildon-application-manager/catalogues2.backup"
#define BACKUP_PACKAGES "/var/lib/hildon-application-manager/packages.backup"

/* NULL and empty strings are considered equal.  Whitespace at the
   beginning and end is ignored.  Sequences of whitespaces are equal
   to each other.
*/
bool tokens_equal (const char *str1, const char *str2);

/* System settings
 */

void load_system_settings ();

extern xexp *system_settings;
extern const char *default_distribution;

/* Catalogues
 */

/* Only reads the package catalogues
 */
xexp *read_package_catalogues (void);

/* Reads the package catalogues and merges them with the user's catalogue.
 */
xexp *read_catalogues (void);

/* Writes the user catalogues filtering the packages catalogues.
 */
int write_user_catalogues (xexp *catalogues);

/* Retrieves a package catalogue entry given the id and the file.
 */
xexp* find_package_catalogue (const gchar *id, const gchar *file, xexp* pkgcat);

/* Returns true if both catalogue are equal.
 */
bool catalogue_equal (xexp *cat1, xexp *cat2);

/* Given a list of catalogues return the catalogue which is equal to
 * the specified.
 */
xexp *find_catalogue (xexp *catalogues, xexp *cat);

/* Verify if the catalogue is valid in the current distribution.
 */
bool catalogue_is_valid (xexp *cat);

/* Generate an apt source list file given a catalogue list.
 */
bool write_sources_list (const char *filename, xexp *catalogues);

/* Backup the user's catalogues.
 */
void backup_catalogues ();

/* Domains
 */

/* Reads the package domains and merges them with the user's domains.
 */
xexp *read_domains (void);

/* Returns true if both domians are equal.
 */
bool domain_equal (xexp *a, xexp *b);

#endif /* !CONFUTILS_H */
