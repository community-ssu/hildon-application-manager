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

/* S-expression like things encoded with XML.  This is used for
   various configuration files and the installation instructions.

   A X-expression, or xexp for short, is either a 'tag' or 'text'.
   Tags have a symbolic tag and children.  A text is just a string.
   There are utility functions for treating texts as integers, but all
   types must be inferred from the context.

   All strings must be in UTF-8.  In the list of children of a tag
   node, no text node can follow another text node.

   The external representation of a xexp uses the subset of XML
   supported by the glib XML parser.  For a text node, the text is
   written with all the necessary escapes.  For a tag node, the
   format is
   
       <TAG>CHILD1 CHILD2 ...</TAG>

   with no space between the representations of the children.  If the
   node has no has children, an alternative encoding is

       <TAG/>
*/

#ifndef XEXP_H
#define XEXP_H

#include <stdio.h>

struct xexp;
typedef struct xexp xexp;

xexp *xexp_rest (xexp *x);
void xexp_set_rest (xexp *x, xexp *rest);
void xexp_free_1 (xexp *x);
void xexp_free (xexp *x);

/* Tags
 */
xexp *xexp_tag_new (const char *tag, xexp *first, xexp *rest);
bool xexp_is_tag (xexp *x);
const char *xexp_get_tag (xexp *x);
bool xexp_is (xexp *x, const char *tag);
xexp *xexp_first (xexp *x);
void xexp_set_first (xexp *x, xexp *first);

/* Texts
 */
xexp *xexp_text_new (const char *text, xexp *rest);
bool xexp_is_text (xexp *x);
const char *xexp_get_text (xexp *x);
int xexp_get_text_as_int (xexp *x);

/* Reading and writing
 */
xexp *xexp_read (FILE *f, const char *line_prefix);
void xexp_write (FILE *f, xexp *x, const char *line_prefix);

#endif
