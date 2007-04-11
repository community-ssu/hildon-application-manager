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

#include "util.h"
#include "main.h"

/* INSTALL_PACKAGES - Overview

   0. Filter out already installed packages.  When the list is empty
      after this, an appropriate note is shown and the process aborts.

   1. Confirm packages.  When this is a 'restore' or 'card' flow, the
      multi-package selection dialog is used, otherwise all packages
      except the first are ignored and a single package confirmation
      dialog is used.

   2. Check for certified packages.  For each of the selected
      packages, a 'check_install' operation is performed and when one
      of them would install non-certified packages, the Notice dialog
      is shown.

   The following is repeated for each selected package, as indicated.
   "Aborting this package" means that an error message is shown and
   when there is another package to install, the user is asked whether
   to continue or not.

   3. Check if enough storage is available and decide where to
      download the packages to.

   4. Download the packages.

   5. If the package has the 'suggest-backup' flag, suggest a backup
      to be taken.

   6. Check the free storage again.

   7. If the package doesn't have the 'close-apps' flag, run the
      'checkrm' scripts of the updated packages and abort this package
      if the scripts asks for it.  Otherwise close all applications.

   8. Do the actual install, aborting this package if it fails.

   9. If the package has the 'reboot' flag, reboot here and now.

  10. If there are more packages to install, go back to 3.
*/

struct ip_clos {
  int install_type;

  GList *all_packages;   // all packages given to install_packages
  GList *packages;       // the ones that are not installed or uptodate

  void (*cont) (void *);
  void *data;
};

static void ip_confirm_install_response (gboolean res, void *data)
static void ip_select_package_response (gboolean res, GList *selected_packages,
					void *data)
static void ip_install_loop (ip_clos *c);
static void ip_end (ip_clos *c);

void
xxx_install_package (package_info *pi,
		     void (*cont) (void *), void *data)
{
  xxx_install_packages (g_list_prepend (NULL, pi), INSTALL_TYPE_STANDARD,
			cont, data);
}

void
xxx_install_packages (GList *packages, int install_type,
		      void (*cont) (void *), void *data)
{
  ip_clos *c = new ip_clos;

  c->install_type = install_type;
  c->all_packages = packages;
  c->cont = cont;
  c->data = data;

  // Filter packages, stopping after the first when this is a standard
  // install.

  c->packages = NULL;
  for (GList *p = c->all_packages; p; p = p->next)
    {
      package_info *pi = (package_info *)p->data;
      if (pi->available_version != NULL)
	{
	  c->packages = g_list_append (c->packages, pi);
	  if (c->install_type == INSTALL_TYPE_STANDARD)
	    break;
	}
    }

  if (c->packages == NULL)
    {
      annoy_user_with_cont ("Nothing to install", ip_end, c);  // XXX-L10N
      return;
    }

  // Bring up the appropriate confirmation dialog.

  if (c->install_type == INSTALL_TYPE_RESTORE
      || c->install_type == INSTALL_TYPE_MEMORY_CARD)
    {
      select_package_list (c->package_list, 
			   _("ai_ti_install_apps"), 
			   (c->install_type == INSTALL_TYPE_RESTORE
			    ? _("ai_ti_restore")
			    : _("ai_ti_memory")),
			   ip_select_package_response, c);
    }
  else
    {
      GString *text = g_string_new ("");
      char download_buf[20];
      package_info *pi;

      c->cur = c->packages;
      pi = (package_info *)(c->cur->data);

      size_string_general (download_buf, 20, c->cur_pi->info.download_size);

      g_string_printf (text,
		       (pi->installed_version
			? _("ai_nc_update")
			: _("ai_nc_install")),
		       pi->name, pi->available_version, download_buf);
  
      ask_yes_no_with_details ((pi->installed_version
				? _("ai_ti_confirm_update")
				: _("ai_ti_confirm_install")),
			       text->str, pi, install_details,
			       ip_install_cur, c);
      g_string_free (text, 1);
    }
}

static void
ip_select_package_response (gboolean res, GList *selected_packages,
			    void *data)
{
  ip_clos *c = (ip_close *)data;

  if (!res || selected_packages == NULL)
    ip_end (c);
  else
    {
      g_list_free (c->packages);
      c->packages = selected_packages;
      c->cur = c->packages;
      ip_install_loop (c);
    }
}

static void
ip_confirm_install_response (gboolean res, void *data)
{
  ip_clos *c = (ip_close *)data;

  if (res)
    ip_install_loop (c);
  else
    ip_end (c);
}

static void
ip_install_loop (ip_clos *c)
{
  package_info *pi = (package_info *)c->cur->data;

  fprintf (stderr, "INSTALL: %s\n", pi->name);

  c->cur = c->cur->next;:
  
  if (c->cur)
    ip_install_loop (c);
  else
    ip_end (c);
}

static void
ip_end (ip_clos *c)
{
  void (*cont) (void *) = c->cont;
  void *data = c->data;
  
  delete c;

  cont (data);
}
