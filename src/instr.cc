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

#include <string.h>
#include <libintl.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "settings.h"
#include "util.h"
#include "log.h"
#include "repo.h"

#define _(x) gettext (x)

static void
annoy_user_with_gerror (const char *filename, GError *error)
{
  add_log ("%s: %s", filename, error->message);
  g_error_free (error);
  annoy_user (_("ai_ni_operation_failed"));
}

static void
instr_cont3 (bool res, void *data)
{
  char *package = (char *)data;

  if (res && package)
    install_named_package (package);

  g_free (package);
}

static void
instr_cont2 (bool res, void *data)
{
  char *package = (char *)data;

  if (res)
    refresh_package_cache_with_cont (false, instr_cont3, package);
  else
    g_free (package);
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
  gchar *package   = g_key_file_get_value (keys, "install", "package", NULL);

  if (package == NULL && repo_deb == NULL)
    annoy_user (_("ai_ni_operation_failed"));

  if (repo_deb)
    {
      maybe_add_repo (repo_name, repo_deb, package != NULL,
		      instr_cont2, package);
    }
  else
    annoy_user (_("ai_ni_error_n770package_incompatible"));

  g_free (repo_name);
  g_free (repo_deb);
  g_key_file_free (keys);
}
