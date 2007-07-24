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

#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "confutils.h"

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

bool
catalogue_equal (xexp *cat1, xexp *cat2)
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
      if (xexp_is (c, "catalogue") && catalogue_equal (c, cat))
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
