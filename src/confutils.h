/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007 Nokia Corporation.  All Rights reserved.
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

/* The file where we store our catalogues for ourselves.
 */
#define CATALOGUE_CONF "/etc/hildon-application-manager/catalogues"

/* The file where we store our ctalogues for apt-pkg to read
 */
#define CATALOGUE_APT_SOURCE "/etc/apt/sources.list.d/hildon-application-manager.list"

/* The file where we store our domain information
 */
#define DOMAIN_CONF "/etc/hildon-application-manager/domains"

/* NULL and empty strings are considered equal.  Whitespace at the
   beginning and end is ignored.  Sequences of whitespaces are equal
   to each other.
*/
bool tokens_equal (const char *str1, const char *str2);

/* Catalogues
 */

bool catalogue_equal (xexp *cat1, xexp *cat2);
xexp *find_catalogue (xexp *catalogues, xexp *cat);
bool catalogue_is_valid (xexp *cat);

bool write_sources_list (const char *filename, xexp *catalogues);

/* Domains
 */

bool domain_equal (xexp *a, xexp *b);

#endif /* !CONFUTILS_H */
