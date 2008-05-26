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

/* HILDON-APPLICATION-MANAGER-MERGE-CATALOGUES

   Merge the catalogues provided on stdin with the already configured
   catalogues and update the APT configuration to match.

   The APT configuration is updated in any case, so you can just merge
   zero catalogues if you just want to resyncronize.
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

extern "C" {
#include "xexp.h"
}

/* Catalogue utilities.  XXX - move them to their own module.
 */

/* The file where we store our catalogues for ourselves.
 */
#define CATALOGUE_CONF "/etc/hildon-application-manager/catalogues"

/* The file where we store our ctalogues for apt-pkg to read
 */
#define CATALOGUE_APT_SOURCE "/etc/apt/sources.list.d/hildon-application-manager.list"

const char *
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

bool
catalogues_are_equal (xexp *cat1, xexp *cat2)
{
  return (tokens_equal (xexp_aref_text (cat1, "uri"),
			xexp_aref_text (cat2, "uri"))
	  && tokens_equal (xexp_aref_text (cat1, "dist"),
		       xexp_aref_text (cat2, "dist"))
	  && tokens_equal (xexp_aref_text (cat1, "components"),
			   xexp_aref_text (cat2, "components")));
}

xexp *
find_catalogue (xexp *catalogues, xexp *cat)
{
  for (xexp *c = xexp_first (catalogues); c; c = xexp_rest (c))
    {
      if (xexp_is (c, "catalogue")
	  && catalogues_are_equal (c, cat))
	return c;
    }

  return NULL;
}

bool
catalogue_is_valid (xexp *cat)
{
  const char *filter_dist = xexp_aref_text (cat, "filter_dist");
  return filter_dist == NULL || strcmp (filter_dist, DEFAULT_DIST) == 0;
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

void
merge_catalogues (xexp *sys_cats, xexp *merge_cats)
{
  while (xexp *m = xexp_pop (merge_cats))
    {
      if (!catalogue_is_valid (m))
	{
	  DBG ("Ignoring", m);
	  xexp_free (m);
	  continue;
	}

      xexp *s = find_catalogue (sys_cats, m);
      if (s)
	{
	  DBG ("Deleting", s);
	  xexp_del (sys_cats, s);
	}
      DBG ("Adding", m);
      xexp_append_1 (sys_cats, m);
    }
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
	      dist = DEFAULT_DIST;
	    if (comps == NULL)
	      comps = "";
	    
	    fprintf (f, "deb %s %s %s\n", uri, dist, comps);
	  }
    }
  
  if (f == NULL || ferror (f) || fclose (f) < 0)
    {
      fprintf (stderr, "%s: %s\n", filename, strerror (errno));
      return false;
    }

  return true;
}

int
main (int argc, char **argv)
{
  GError *error = NULL;

  if (argc != 2)
    {
      fprintf (stderr,
	       "Usage: "
	       "hildon-application-manager-merge-catalogues FILE\n");
      exit (1);
    }

  xexp *merge_cats;
  if (strcmp (argv[1], "-") == 0)
    {
      merge_cats = xexp_read (stdin, &error);
      if (merge_cats == NULL)
	{
	  fprintf (stderr, "stdin: %s\n", error->message);
	  exit (1);
	}
    }
  else
    {
      merge_cats = xexp_read_file (argv[1]);
      if (merge_cats == NULL)
	exit (1);
    }

  xexp *sys_cats =
    xexp_read_file (CATALOGUE_CONF);
  if (sys_cats == NULL)
    exit (1);

  merge_catalogues (sys_cats, merge_cats);

  if (!xexp_write_file (CATALOGUE_CONF, sys_cats))
    exit (1);

  if (!write_sources_list (CATALOGUE_APT_SOURCE, sys_cats))
    exit (1);

  return 0;
}
