/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007, 2008 Nokia Corporation.  All Rights reserved.
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

extern "C" {
#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>
}

#include "catalogues.h"
#include "confutils.h"

static gboolean
package_catalogue_is_valid (xexp *cat)
{
  const char *id = xexp_aref_text (cat, "id");
  return id && catalogue_is_valid (cat);
}

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

/* this function is the same as merge_package_catalogues without
 * avoiding duplicated catalogues */
static void
add_package_catalogues (xexp *pkgcat, xexp *cat, gchar* file)
{
  while (xexp *m = xexp_pop (cat))
  {
    if (!package_catalogue_is_valid (m))
    {
      DBG ("Ignoring", m);
      xexp_free (m);
      continue;
    }

    /* this is not an elegant place to do this */
    if (file)
      xexp_aset_text (m, "file", file);

    xexp_aset_bool (m, "nobackup", TRUE);

    DBG ("Adding", m);
    xexp_append_1 (pkgcat, m);
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

xexp*
read_package_catalogues (void)
{
  GDir* catdir;
  gboolean ok;
  xexp *pkgcat;

  pkgcat = xexp_list_new ("catalogues");
  
  catdir = g_dir_open (PACKAGE_CATALOGUES, 0, NULL);
  if (!catdir)
    return pkgcat;

  ok = TRUE;
  while (ok)
  {
    const gchar *dirent;
    gchar *cat_path;
    struct stat file_stat;
    xexp *cat;
    gchar *ext;

    dirent = g_dir_read_name (catdir);
    if (dirent == NULL)
      break;

    ext = g_strrstr (dirent, ".");
    if (ext == NULL)
      continue;

    if (g_ascii_strcasecmp (ext + 1, CATALOGUES_EXT))
      continue; /* not valid file extension */

    cat_path = g_strconcat (PACKAGE_CATALOGUES, dirent, NULL);

    /* is it a file? */
    if (g_stat (cat_path, &file_stat) != 0 || !S_ISREG (file_stat.st_mode))
    {
      g_free (cat_path);
      continue;
    }

    cat = xexp_read_file (cat_path);

    if (cat)
    {
      gchar* filename;

      filename = g_strndup (dirent, ext - dirent);

      //merge_package_catalogues (packcat, cat, filename);
      add_package_catalogues (pkgcat, cat, filename);

      if (filename)
        g_free (filename);

      xexp_free (cat);
    }

    g_free (cat_path);
  }

  g_dir_close (catdir);

  return pkgcat;
}

xexp*
read_catalogues (void)
{
  xexp *syscat;
  xexp *pkgcat;
  xexp *global;

  global = xexp_list_new ("catalogues");
  pkgcat = read_package_catalogues ();
  xexp_append (global, pkgcat);   

  /* add the user's catalogues */
  syscat = xexp_read_file (CATALOGUE_CONF);

  if (!syscat)
    goto beach;
  
  while (xexp *m = xexp_pop (syscat))
  {
    const gchar *sfile = xexp_aref_text (m, "file");
    const gchar *sid   = xexp_aref_text (m, "id");
    gboolean sdisabled = xexp_aref_bool (m, "disabled");
    
    if ((!sfile || !sid) &&          /* is a reference to a pkg catalogue? */
        catalogue_is_valid (m))      /* is a valid catalogue? */
    {
      DBG ("Adding user's cat to global", m);
      xexp_append_1 (global, m);
    }
    else if (sdisabled)
    {
      xexp* x = find_package_catalogue (sid, sfile, global);
      if (x)
        xexp_aset_bool (x, "disabled", TRUE);
      xexp_free (m);
    }
    else  
      xexp_free (m);
  }

  if (syscat)
    xexp_free (syscat);

beach:
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

      if (xexp_aref_text (x, "file") &&       /* it is a reference and it is */
          xexp_aref_text (x, "id") &&         /* disabled */
          xexp_aref_bool (x, "disabled"))
      {
        newrep = xexp_list_new ("catalogue");
        xexp_aset_bool (newrep, "disabled", 1);
        xexp_aset_text (newrep, "id", xexp_aref_text (x, "id"));
        xexp_aset_text (newrep, "file", xexp_aref_text (x, "file"));
      }
      else if (!xexp_aref_text (x, "file") && /* it is not a reference to a */
               !xexp_aref_text (x, "id"))     /* package catalogue */
        newrep = xexp_copy (x);
      else
        continue;

      if (newrep)
        xexp_append_1 (usercat, newrep);
    }
  }

  gint retval = xexp_write_file (CATALOGUE_CONF, usercat);
  xexp_free (usercat);

  return retval;
}
