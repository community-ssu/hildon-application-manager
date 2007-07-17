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

#ifndef OPERATIONS_H
#define OPERATIONS_H

#include "main.h"

/* These are the interaction flows implemented by the Application
   Manager.

   INSTALL_PACKAGES installs or updates a list of packages, and
   UNINSTALL_PACKAGE uninstalls a single package.  INSTALL_DEB_FILE
   installs a single .deb file.

   These functions do not call START_INTERACTION_FLOW or
   END_INTERACTION_FLOW.

   These functions take care of all the error reporting.
*/

enum {
  INSTALL_TYPE_STANDARD,
  INSTALL_TYPE_MULTI,
  INSTALL_TYPE_BACKUP,
  INSTALL_TYPE_MEMORY_CARD,
  INSTALL_TYPE_UPDATE_SYSTEM
};

/* PACKAGES is a list of package_info pointers.  The list and the
   package_info structures must remain valid until CONT is called.
   AUTOMATIC should be true if the installation has been initiated
   from a ".auto.install" file.
*/
void install_packages (GList *packages,
		       int state, int install_type,
		       bool automatic,
		       const char *title, const char *desc,
		       void (*cont) (int n_successful, void *data),
		       void *data);

/* Calls INSTALL_PACKAGES with a single package, APTSTATE_DEFAULT and
   INSTALL_TYPE_STANDARD
*/
void install_package (package_info *pi,
		      void (*cont) (int n_successful, void *data),
		      void *data);

/* PI must remain valid until CONT is called.
 */
void uninstall_package (package_info *pi,
			void (*cont) (void *data), void *data);

/* FILENAME must remain valid until CONT is called.
 */
void install_local_deb_file (const char *filename,
			     void (*cont) (void *data), void *data);

#endif /* !OPERATIONS_H */
