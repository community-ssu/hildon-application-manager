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

/* Strings used for parsing available translations */
#define LOC_BEGIN "repo_name["
#define LOC_BEGIN_SIZE 10
#define LOC_END "]"

static void instr_select_packages_next (gpointer data);

static void
annoy_user_with_gerror (const char *filename, GError *error)
{
  add_log ("%s: %s", filename, error->message);
  g_error_free (error);
  annoy_user (_("ai_ni_operation_failed"));
}

static void 
instr_select_packages_cont (bool res, void *data)
{
  GSList *selected_package_list = (GSList *) data;
  gchar *package_name = NULL;

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
instr_select_packages_next (void *data)
{
  instr_select_packages_cont (TRUE, data);
}

static void
instr_select_packages (char **packages)
{
  /* This function should prepare a dialog to select the packages to install from the list in
   * packages. Now it simply calls to a stub returning function for that dialog
   */
  char **current = NULL;
  GSList *selected_packages = NULL;

  for (current = packages; current[0] != NULL; current++)
    {
      selected_packages = g_slist_prepend (selected_packages, current[0]);
    }

  g_free(packages);

  instr_select_packages_cont (TRUE, selected_packages);
}

static void
instr_cont3 (bool res, void *data)
{
  char **package_list = (char **) data;

  if (!res) {
    g_strfreev (package_list);
    return;
  }

  if (package_list != NULL)
    {
      if (package_list[1] == NULL) 
	{
	  install_named_package (package_list[0], NULL, NULL);
	  g_strfreev (package_list);
	} 
      else 
	{
	  instr_select_packages (package_list);
	}
    }
}

static void
instr_cont2 (bool res, void *data)
{
  char **package_list = (char **) data;

  if (res)
    {
      refresh_package_cache_with_cont (false, instr_cont3, package_list);
    }
  else
    {
      g_strfreev (package_list);
    }
}

/* obtains a GSList * of available translations for the file. To do this
 * it allocates a buffer with the whole file contents, and parses it
 * finding translations */
static GSList *
get_repo_name_locs (GKeyFile *keys)
{
  gchar *line = NULL;
  gchar *file_buffer = NULL;
  GSList *loc_list = NULL;

  file_buffer = g_key_file_to_data (keys, NULL, NULL);
  if (file_buffer != NULL)
    {
      line = file_buffer;

      while (line != '\0')
	{
	  /* If the string begins with repo_name[ */
	  if (strncmp (line, LOC_BEGIN, LOC_BEGIN_SIZE) == 0) 
	    {
	      gchar *start_loc = NULL;

	      line = line + LOC_BEGIN_SIZE;
	      /* Finds the corresponding ] */
	      start_loc = strstr (line, LOC_END);
	      
	      /* Extracts the string */
	      if (start_loc != NULL)
		{
		  loc_list = 
		    g_slist_prepend (loc_list, 
				     g_strndup (line, start_loc - line));
		  line = start_loc;
		}
	    }
	  line = strchr (line, '\n');
	  if (line != NULL)
	    line++;
	}
      g_free (file_buffer);
    }
  return loc_list;
}

void
open_local_install_instructions (const char *filename)
{
  GError *error = NULL;
  gsize repo_name_size = 0;
  gsize repo_deb_size = 0;
  GSList *loc_list = NULL;
  GSList *cur_loc = NULL;

  GKeyFile *keys = g_key_file_new ();
  if (!g_key_file_load_from_file (keys, filename, GKeyFileFlags(G_KEY_FILE_KEEP_TRANSLATIONS), &error))
    {
      annoy_user_with_gerror (filename, error);
      g_key_file_free (keys);
      cleanup_temp_file ();
      return;
    }

  cleanup_temp_file ();

  /* Extract the available translation names */
  loc_list = get_repo_name_locs (keys);

  /* We parse the repository names, deb lines and the packages we want to install. */
  gchar **repo_name_list = g_key_file_get_string_list (keys, "install", "repo_name", &repo_name_size, NULL);
  gchar **repo_deb_list  = g_key_file_get_string_list (keys, "install", "repo_deb_3", &repo_deb_size, NULL);
  gchar **package_list   = g_key_file_get_string_list (keys, "install", "package", NULL, NULL);

  /* We obtain the translations array */
  GSList **translation_lists = g_new0 (GSList *, repo_name_size+1);
  gsize trans_index = 0;
  for (cur_loc = loc_list; cur_loc != NULL; cur_loc = g_slist_next (cur_loc))
    {
      if (cur_loc->data != NULL)
	{
	  gchar **cur_name = NULL;
	  gsize trans_count = 0;
	  gchar **translation = NULL;
 
	  translation = 
	    g_key_file_get_locale_string_list (keys, "install", "repo_name", 
					       (gchar *) cur_loc->data, 
					       &trans_count, NULL);
	  if (trans_count == repo_name_size)
	    {
	      cur_name = translation;
	      for (trans_index = 0; trans_index < repo_name_size; trans_index++)
		{
		  if (translation[trans_index] != NULL)
		    {
		      translation_lists[trans_index] = 
			g_slist_prepend (translation_lists[trans_index],
					 g_strdup (translation[trans_index]));
		    }
		}
	    }
	  g_strfreev (translation);
	}
    }

  /* Lists must be reversed in order to match the order in loc_list */
  for (trans_index = 0; trans_index < repo_name_size; trans_index++)
    {
      translation_lists[trans_index] = g_slist_reverse (translation_lists[trans_index]);
    }

  /* If there's no repo name field, then enter a stub name list with a null value */
  if ((repo_name_size == 0) && (repo_deb_size > 0)) 
    {
      repo_name_list = (gchar **) g_malloc0 (sizeof (gchar **));
      repo_name_size = repo_deb_size;
    }

  if ((package_list == NULL && repo_deb_list == NULL)||
      (repo_name_size != repo_deb_size))
    {
      annoy_user (_("ai_ni_operation_failed"));
    }

  if (repo_deb_list)
    {
      maybe_add_repos ((const gchar **) repo_name_list, 
		       (const gchar **) repo_deb_list, 
		       (const GSList *) loc_list, 
		       (const GSList **) translation_lists,
		       package_list != NULL,
		      instr_cont2, package_list);
    }
  else
    {
      annoy_user (_("ai_ni_error_n770package_incompatible"));
    }

  /* Free all alocated memory */
  g_slist_foreach (loc_list, (GFunc) g_free, NULL);
  g_slist_free (loc_list);
  {
    GSList **list = NULL;
    for (list = translation_lists; list != NULL && list[0] != NULL; list++)
      {
	g_slist_foreach (list[0], (GFunc) g_free, NULL);
	g_slist_free (list[0]);
      }
    g_free (translation_lists);
  }
  g_strfreev (repo_name_list);
  g_strfreev (repo_deb_list);
  g_key_file_free (keys);
}
