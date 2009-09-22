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

#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "confutils.h"

#ifdef DEBUG
static void
DBG (const char *str, xexp *cat)
{
  fprintf (stderr, "%s:\n", str);
  xexp_write (stderr, cat);
}
#else
static void
DBG (const char *str, xexp *cat)
{
}
#endif

xexp *system_settings = NULL;

const char *default_distribution;

void
load_system_settings ()
{
  xexp *settings = xexp_read_file (SYSTEM_SETTINGS_FILE);
  xexp *defaults = xexp_read_file (SYSTEM_SETTINGS_DEFAULTS_FILE);

  if (settings)
    {
      if (defaults)
	xexp_append (settings, defaults);
    }
  else if (defaults)
    {
      settings = defaults;
    }
  else
    settings = xexp_list_new ("settings");

  system_settings = settings;

  default_distribution = xexp_aref_text (settings, "distribution");
  if (default_distribution == NULL)
    default_distribution = "unknown";
}

static const char *
skip_whitespace (const char *str)
{
  while (isspace (*str))
    str++;
  return str;
}

/* NULL and empty strings are considered equal.  Whitespace at the
   beginning and end is ignored.  Sequences of white spaces are
   equal to every other sequence of white space.
*/

bool
tokens_equal (const char *str1, const char *str2)
{
  if (str1 == NULL)
    str1 = "";

  if (str2 == NULL)
    str2 = "";

  str1 = skip_whitespace (str1);
  str2 = skip_whitespace (str2);

  while (*str1 && *str2)
    {
      if (isspace (*str1) && isspace (*str2))
	{
	  str1 = skip_whitespace (str1);
	  str2 = skip_whitespace (str2);
	}
      else if (*str1 == *str2)
	{
	  str1++;
	  str2++;
	}
      else
	break;
    }

  str1 = skip_whitespace (str1);
  str2 = skip_whitespace (str2);

  return *str1 == '\0' && *str2 == '\0';
}

/* This function modifies the string in place */
static void
uri_remove_trailing_slashes (gchar *uri)
{
  if (uri != NULL)
    {
      while (g_str_has_suffix (uri, "/"))
        {
          char *lastchar = g_strrstr (uri, "/");
          *lastchar = '\0';
        }
    }
}

bool
catalogue_equal (xexp *cat1, xexp *cat2)
{
  const gchar *cat1_file = xexp_aref_text (cat1, "file");
  const gchar *cat1_id = xexp_aref_text (cat1, "id");
  const gchar *cat2_file = xexp_aref_text (cat2, "file");
  const gchar *cat2_id = xexp_aref_text (cat2, "id");
  bool is_pkg_cat1;
  bool is_pkg_cat2;
  bool result;

  /* Check whether they're package catalogues */
  is_pkg_cat1 = cat1_file && cat1_id;
  is_pkg_cat2 = cat2_file && cat2_id;

  /* Check whether they're actually equal or not */
  result = false;
  if (is_pkg_cat1 && is_pkg_cat2)
    {
      /* Package catalogues */
      result =
        (tokens_equal (xexp_aref_text (cat1, "file"),
                       xexp_aref_text (cat2, "file"))
         && tokens_equal (xexp_aref_text (cat1, "id"),
                          xexp_aref_text (cat2, "id")));
    }
  else
    {
      gchar *cat1_uri = g_strdup (xexp_aref_text (cat1, "uri"));
      gchar *cat2_uri = g_strdup (xexp_aref_text (cat2, "uri"));

      /* Remove trailing "/" from uris, if present */
      uri_remove_trailing_slashes (cat1_uri);
      uri_remove_trailing_slashes (cat2_uri);

      /* User catalogues */
      result =
        (tokens_equal (cat1_uri, cat2_uri)
         && tokens_equal (xexp_aref_text (cat1, "dist"),
                          xexp_aref_text (cat2, "dist"))
         && tokens_equal (xexp_aref_text (cat1, "components"),
                          xexp_aref_text (cat2, "components")));

      g_free (cat1_uri);
      g_free (cat2_uri);
    }

  return result;
}

xexp *
find_catalogue (xexp *catalogues, xexp *cat)
{
  for (xexp *c = xexp_first (catalogues); c; c = xexp_rest (c))
    {
      if (xexp_is (c, "catalogue") && catalogue_equal (c, cat))
	return c;
    }

  return NULL;
}

bool
catalogue_is_valid (xexp *cat)
{
  const char *filter_dist = xexp_aref_text (cat, "filter_dist");
  return (filter_dist == NULL
	  || strcmp (filter_dist, default_distribution) == 0);
}

bool
write_sources_list (const char *filename, xexp *catalogues)
{
  FILE *f = fopen (filename, "w");
  if (f)
    {
      for (xexp *x = xexp_first (catalogues); x; x = xexp_rest (x))
	if (xexp_is (x, "catalogue")
	    && !xexp_aref_bool (x, "disabled"))
	  {
	    const char *uri = xexp_aref_text (x, "uri");
	    const char *dist = xexp_aref_text (x, "dist");
	    const char *comps = xexp_aref_text (x, "components");

	    if (uri == NULL)
	      continue;
	    if (dist == NULL)
	      dist = default_distribution;
	    if (comps == NULL)
	      comps = "";

	    fprintf (f, "deb %s %s %s\n", uri, dist, comps);
	  }
    }

  if (f == NULL || ferror (f) || fflush (f) || fsync (fileno (f)) || fclose (f))
    {
      fprintf (stderr, "%s: %s\n", filename, strerror (errno));
      return false;
    }

  return true;
}

static xexp *
get_backup_catalogues ()
{
  /* We backup all the information in the CATALOGUE_CONF file, but
     nothing from the PACKAGE_CATALOGUES directory.  That will lead to
     all the user data being backedup, and none of the data controlled
     by packages.
  */

  return xexp_read_file (CATALOGUE_CONF);
}

void
backup_catalogues ()
{
  /* We write the catalogues to two backup files since the catalogues
     are in two backup categories ("Settings" and "List of
     Applications") and osso-backup can not put one file into two
     categories.

     When restoring, we use the one that has been restored.  When both
     have been restored, we use either one since they will be identical.
  */

  xexp *catalogues = get_backup_catalogues ();
  if (catalogues)
    {
      xexp_write_file (BACKUP_CATALOGUES, catalogues);
      xexp_write_file (BACKUP_CATALOGUES2, catalogues);
      xexp_free (catalogues);
    }
}

xexp*
find_package_catalogue (const gchar *id, const gchar *file, xexp* pkgcat)
{
  if (!file || !id || !pkgcat)
    return NULL;

  for (xexp *m = xexp_first (pkgcat); m; m = xexp_rest (m))
  {
    const gchar *pfile = xexp_aref_text (m, "file");
    const gchar *pid   = xexp_aref_text (m, "id");

    if (!pfile || !pid)
      continue;

    if (!g_ascii_strcasecmp (file, pfile) &&
        !g_ascii_strcasecmp (id, pid))
      return m;
  }

  return NULL;
}

static bool
file_is_valid (gchar *filepath)
{
  g_return_val_if_fail (filepath, false);

  struct stat filestat;
  return (g_stat (filepath, &filestat) != 0 || !S_ISREG (filestat.st_mode));
}

static void
read_package_config_files (xexp *pkgcat, const gchar* dir,
			   void (*add_config) (xexp *, xexp *, gchar *))
{
  g_return_if_fail (pkgcat && dir);

  GDir* catdir = g_dir_open (dir, 0, NULL);
  if (!catdir)
    return;

  while (true)
    {
      const gchar *dirent = g_dir_read_name (catdir);
      if (dirent == NULL)
	break;

      gchar *ext = g_strrstr (dirent, ".");
      if (ext == NULL)
	continue;

      if (g_ascii_strcasecmp (ext + 1, SYSTEM_CONFIG_EXT))
	continue; /* not valid file extension */

      gchar *filepath = g_strconcat (dir, dirent, NULL);
      if (file_is_valid (filepath))
	{
	  g_free (filepath);
	  continue;
	}

      xexp *cat = xexp_read_file (filepath);
      g_free (filepath);

      if (cat)
	{
	  if (add_config)
	    {
	      gchar* filename;
	      filename = g_strndup (dirent, ext - dirent);
	      add_config (pkgcat, cat, filename);
	      if (filename)
		g_free (filename);
	      xexp_free (cat);
	    }
	  else
	    xexp_append (pkgcat, cat);
	}
    }

  g_dir_close (catdir);
}

/* this function is the same as merge_package_catalogues without
 * avoiding duplicated catalogues */
static void
add_package_catalogues (xexp *pkgcat, xexp *cat, gchar* file)
{
  while (xexp *m = xexp_pop (cat))
    {
      if (xexp_aref_text (m, "id") == NULL)
	{
	  fprintf (stderr, "%s.%s: catalogues must have 'id' element.\n",
		   file, SYSTEM_CONFIG_EXT);
	  continue;
	}

      if (!catalogue_is_valid (m))
	{
	  DBG ("Ignoring", m);
	  xexp_free (m);
	  continue;
	}

      xexp_aset_text (m, "file", file);

      DBG ("Adding", m);
      xexp_append_1 (pkgcat, m);
    }
}

static void
add_user_catalogues (xexp *global)
{
  g_return_if_fail (global);

  xexp *syscat = xexp_read_file (CATALOGUE_CONF);

  if (syscat)
    {
      while (xexp *m = xexp_pop (syscat))
	{
	  const gchar *sfile = xexp_aref_text (m, "file");
	  const gchar *sid   = xexp_aref_text (m, "id");

	  if ((!sfile || !sid) /* is a reference to a pkg catalogue? */
	      && catalogue_is_valid (m)) /* is a valid catalogue? */
	    {
	      DBG ("Adding user's cat to global", m);
	      xexp_append_1 (global, m);
	    }
	  else
	    {
	      gboolean sdisabled = xexp_aref_bool (m, "disabled");
	      xexp* x = find_package_catalogue (sid, sfile, global);
	      if (x)
		xexp_aset_bool (x, "disabled", sdisabled);
	      xexp_free (m);
	    }
	}
      xexp_free (syscat);
    }
}

xexp*
read_catalogues (void)
{
  xexp* global = xexp_list_new ("catalogues");
  read_package_config_files (global, PACKAGE_CATALOGUES,
			     add_package_catalogues);
  add_user_catalogues (global);

  return global;
}

int
write_user_catalogues (xexp *catalogues)
{
  if (catalogues == NULL)
    return 0;

  xexp *usercat = xexp_list_new ("catalogues");

  /* lets' filter the current catalogues list */
  for (xexp *x = xexp_first (catalogues); x; x = xexp_rest (x))
    {
      if (xexp_is (x, "catalogue"))
	{
	  xexp* newrep = NULL;

	  if (xexp_aref_text (x, "file") &&       /* it is package catalogue */
	      xexp_aref_text (x, "id"))
	    {
	      newrep = xexp_list_new ("catalogue");
	      xexp_aset_bool (newrep, "disabled",
			      xexp_aref_bool (x, "disabled"));
	      xexp_aset_text (newrep, "id", xexp_aref_text (x, "id"));
	      xexp_aset_text (newrep, "file", xexp_aref_text (x, "file"));
	      xexp_append_1 (usercat, newrep);
	    }
	  else
	    xexp_append_1 (usercat, xexp_copy (x));
	}
    }

  gint retval = xexp_write_file (CATALOGUE_CONF, usercat);
  xexp_free (usercat);

  return retval;
}

/* Domains
 */

bool
domain_equal (xexp *a, xexp *b)
{
  const char *a_name = xexp_aref_text (a, "name");
  const char *b_name = xexp_aref_text (b, "name");

  if (a_name == NULL || b_name == NULL)
    return false;
  return strcmp (a_name, b_name) == 0;
}

xexp*
read_domains (void)
{
  xexp *global = xexp_list_new ("domains");
  read_package_config_files (global, PACKAGE_DOMAINS, NULL);

  return global;
}
