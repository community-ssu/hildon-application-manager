/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2005, 2006, 2007 Nokia Corporation.  All Rights reserved.
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

#ifndef REPO_H
#define REPO_H

extern "C" {
#include "xexp.h"
}

void show_repo_dialog ();

void maybe_add_repos (const char **name, 
		      const char **deb_line, 
		      const GSList *loc_list,
		      const GSList **translation_lists,
		      bool for_install,
		      void (*cont) (bool res, void *data), 
		      void *data);

void temporary_set_repos (const char **deb_line_list,
			  void (*cont) (bool res, void *data),
			  void *data);

bool parse_quoted_word (char **start, char **end, bool term);

/* XXX - emerging modernized catalogue handling below.  Stay tuned...
 */

void with_catalogues (void (*cont) (xexp *catalogues, void *data),
		      void *data);

void set_catalogues (xexp *catalogues, bool refresh = true, bool ask = true);

#endif /* !REPO_H */
