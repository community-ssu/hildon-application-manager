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

#include "xexp.h"

typedef enum {
  XEXP_TAG,
  XEXP_TEXT
} xexp_type;

struct xexp {
  xexp_type type;
  const char *string;
  xexp *first;
  xexp *rest;
};

xexp *
xexp_rest (xexp *x)
{
  return x->rest;
}

void
xexp_set_rest (xexp *x, xexp *rest)
{
  x->rest = rest;
}

void
xexp_free_1 (xexp *x)
{
  g_free (x->string);
  g_free (x);
}

void
xexp_free (xexp *x);
{
  while (x)
    {
      xexp *r = xexp_rest (x);
      xexp_free (x->first);
      xexp_free_1 (x);
      x = r;
    }
}

xexp *
xexp_tag_new (const char *tag, xexp *first, xexp *rest)
{
  g_assert (tag);

  xexp *x = g_new0 (xexp, 1);
  x->type = XEXP_TAG;
  x->string = tag;
  x->first = first;
  x->rest = rest;
  return x;
}

bool
xexp_is_tag (xexp *x)
{
  return x->type == XEXP_TAG;
}

const char *
xexp_get_tag (xexp *x)
{
  g_assert (xexp_is_tag (x));
  return x->string;
}

bool
xexp_is (xexp *x, const char *tag)
{
  return xexp_is_tag (x) && strcmp (xexp_get_tag (x), tag) == 0;
}

xexp *
xexp_first (xexp *x)
{
  g_assert (xexp_is_tag (x));
  return x->first;
}

void
xexp_set_first (xexp *x, xexp *first)
{
  g_assert (xexp_is_tag (x));
  x->first = first;
}

xexp *
xexp_text_new (const char *text, xexp *rest)
{
  g_assert (text);

  xexp *x = g_new0 (xexp, 1);
  x->type = XEXP_TEXT;
  x->string = text;
  x->first = NULL;
  x->rest = rest;
  return x;
}

bool
xexp_is_text (xexp *x)
{
  return x->type == XEXP_TEXT;
}

const char *
xexp_get_text (xexp *x)
{
  g_assert (xexp_is_text (x));
  return x->string;
}

int
xexp_get_text_as_int (xexp *x)
{
  return strtod (xexp_get_text (x));
}
