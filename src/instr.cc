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

#include <string.h>
#include <libintl.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "settings.h"
#include "util.h"
#include "log.h"
#include "repo.h"

#define _(x) gettext (x)

static void instr_select_packages_next (gpointer data);

static void
annoy_user_with_gerror (const char *filename, GError *error)
{
  add_log ("%s: %s", filename, error->message);
  g_error_free (error);
  annoy_user (_("ai_ni_operation_failed"));
}

static void instr_select_packages_cont (bool res, void * data)
{
  GSList * selected_package_list = (GSList *) data;
  gchar * package_name = NULL;

  /* cancelled installation */
  if (!res) {
    g_slist_foreach(selected_package_list, (GFunc) g_free, NULL);
    return;
  }

  /* after last package */
  if (selected_package_list == NULL) {
    return;
  }

  /* packages remaining */
  package_name = (gchar *) selected_package_list->data;
  selected_package_list = g_slist_delete_link(selected_package_list, selected_package_list);

  install_named_package (package_name, instr_select_packages_next, (void *) selected_package_list);
}

static void
instr_select_packages_next (void * data)
{
  instr_select_packages_cont(TRUE, data);
}

static void
instr_select_packages (char ** packages)
{
  /* This function should prepare a dialog to select the packages to install from the list in
   * packages. Now it simply calls to a stub returning function for that dialog
   */
  char ** current = NULL;
  GSList * selected_packages = NULL;

  for (current = packages; current[0] != NULL; current++)
    selected_packages = g_slist_prepend (selected_packages, current[0]);
  g_free(packages);

  instr_select_packages_cont(TRUE, selected_packages);
}

static void
instr_cont3 (bool res, void *data)
{
  char **package_list = (char **)data;

  if (!res) {
    g_strfreev (package_list);
    return;
  }

  if (package_list != NULL) {
    if (package_list[1] == NULL) {
      install_named_package (package_list[0], NULL, NULL);
      g_strfreev (package_list);
    } else {
       instr_select_packages (package_list);
    }
  }
}

static void
instr_cont2 (bool res, void *data)
{
  char **package_list = (char **)data;

  if (res)
    refresh_package_cache_with_cont (false, instr_cont3, package_list);
  else
    g_strfreev (package_list);
}

void
open_local_install_instructions (const char *filename)
{
  GError *error = NULL;

  GKeyFile *keys = g_key_file_new ();
  if (!g_key_file_load_from_file (keys, filename, GKeyFileFlags(0), &error))
    {
      annoy_user_with_gerror (filename, error);
      g_key_file_free (keys);
      cleanup_temp_file ();
      return;
    }

  cleanup_temp_file ();

  gchar *repo_name = g_key_file_get_value (keys, "install", "repo_name", NULL);
  gchar *repo_deb  = g_key_file_get_value (keys, "install", "repo_deb_3", NULL);
  gchar **package_list   = g_key_file_get_string_list (keys, "install", "package", NULL, NULL);

  if (package_list == NULL && repo_deb == NULL)
    annoy_user (_("ai_ni_operation_failed"));

  if (repo_deb)
    {
      maybe_add_repo (repo_name, repo_deb, package_list != NULL,
		      instr_cont2, package_list);
    }
  else
    annoy_user (_("ai_ni_error_n770package_incompatible"));

  g_free (repo_name);
  g_free (repo_deb);
  g_key_file_free (keys);
}
