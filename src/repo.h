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

void show_catalogue_dialog_flow ();

/* A 'catalogue' is an association xexp with the following elements:

      - name

      The display name for this catalogue.  This can be either a text,
      in which case it gives the display name directly, or it can be
      an association list tagged with language codes.  If it is such a
      list, the "default" language code should be used as a fallback
      when the current language is not found.

      - uri, dist, components

      The respective parts of the deb line inserted into sources.list.
      When 'dist' is omitted, it defaults to the distribution of the
      IT OS that we are running on.  When 'components' is omitted, it
      defaults to the empty string.  'uri' is mandatory.

      - disabled

      When present, marks this catalogue as disabled.

      - essential

      When present, marks this catalogue as essential.

      - nobackup

      When present, prevents this catalogue from ending up in a
      backup.

  Note that 'file_uri' is not valid for a catalogue xexp; you have to
  resolve this when reading the .install file.
*/

void show_cat_dialog_with_catalogues (xexp *catalogues, void *user_data);

void get_catalogues (void (*cont) (xexp *catalogues, void *data),
		     void *data);

void set_catalogues (xexp *catalogues, bool refresh, bool ask,
		     void (*cont) (bool res, void *data),
		     void *data);

void set_temp_catalogues (xexp *catalogues,
			  void (*cont) (bool res, void *data),
			  void *data);

void add_catalogues (xexp *catalogues, bool ask, bool update,
		     void (*cont) (bool res, void *data),
		     void *data);

GString *render_catalogue_report (xexp *catalogue_report);

xexp *get_failed_catalogues (xexp *catalogue_report);

#endif /* !REPO_H */
