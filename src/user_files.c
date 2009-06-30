/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2005, 2006, 2007, 2008 Nokia Corporation.  All Rights reserved.
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

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "user_files.h"

gchar *
user_file_get_state_dir_path ()
{
  gchar *required_dir = NULL;

  required_dir = g_strdup_printf ("%s/%s",
				  getenv ("HOME"),
				  HAM_STATE_DIR);

  if (mkdir (required_dir, 0777) && errno != EEXIST)
    return NULL;

  return required_dir;
}

FILE *
user_file_open_for_read (const gchar *name)
{
  FILE *f = NULL;
  struct stat buf;
  int stat_result;
  gboolean old_file = FALSE;
  gchar *old_path = NULL;
  gchar *new_path = NULL;
  gchar *full_state_dir = NULL;

  if (name == NULL)
    return NULL;
  
  full_state_dir = user_file_get_state_dir_path ();
  if (full_state_dir == NULL)
    return NULL;

  /* Look for the old file */
  old_path = g_strdup_printf ("%s-%s", full_state_dir, name);
  stat_result = stat (old_path, &buf);

  if (!stat_result)
    old_file = TRUE;

  /* Look for the file under HAM_STATE_DIR first */
  new_path = g_strdup_printf ("%s/%s", full_state_dir, name);
  stat_result = stat (new_path, &buf);

  if (old_file)
    {
      if (stat_result) 
        {
          /* If new file was not found, but an old one was, move the old one */
          rename (old_path, new_path);
        }
      else
        {
          /* Remove the old file if it was found */
          unlink (old_path);
        }
    }

  f = fopen (new_path, "r");

  g_free (new_path);
  g_free (old_path);
  g_free (full_state_dir);

  return f;
}

FILE *
user_file_open_for_write (const gchar *name)
{
  FILE *f = NULL;
  gchar *full_state_dir = NULL;

  full_state_dir = user_file_get_state_dir_path ();
  if (full_state_dir != NULL)
    {
      /* Save the file under HAM_STATE_DIR (create it, if needed) */
      struct stat buf;
      int stat_result;
      gchar *current_path = NULL;
      gchar *old_path = NULL;

      /* Delete, if present, the previous file under HOME dir */
      old_path = g_strdup_printf ("%s-%s", full_state_dir, name);

      stat_result = stat (old_path, &buf);
      if (!stat_result)
	unlink (old_path);

      g_free (old_path);

      /* Get the current full path and return a FILE pointer */
      current_path = g_strdup_printf ("%s/%s", full_state_dir, name);

      f = fopen (current_path, "w");
      g_free (current_path);

      g_free (full_state_dir);
    }

  return f;
}

int
user_file_remove (const gchar *name)
{
  gchar *full_state_dir = NULL;
  int result = -1;

  full_state_dir = user_file_get_state_dir_path ();
  if (full_state_dir != NULL)
    {
      gchar *current_path = NULL;

      /* Delete, if present, the previous file under HOME dir */
      current_path = g_strdup_printf ("%s/%s", full_state_dir, name);

      result = unlink (current_path);

      g_free (current_path);
      g_free (full_state_dir);
    }

  return result;
}

xexp *
user_file_read_xexp (const gchar *name)
{
  FILE *f = NULL;
  xexp *x = NULL;

  f = user_file_open_for_read (name);
  if (f != NULL)
    {
      x = xexp_read (f, NULL);
      fclose (f);
    }
  return x;
}

void
user_file_write_xexp (const gchar *name, xexp *x)
{
  FILE *f = NULL;

  if (x != NULL)
    {
      f = user_file_open_for_write (name);
      if (f != NULL)
        {
          xexp_write (f, x);
          if (fflush (f) || fsync (fileno (f)) || fclose (f))
            return;
        }
    }
}
