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

/* X-expressions are a bit like S-expression, but suitable for
   encoding in XML.  They are used for various configuration files and
   the installation instructions.

   An X-expression, or xexp for short, has a 'tag' and is either a
   'list' or 'text'.  Lists have children and text xexps have a
   string.  There are utility functions for treating texts as
   integers, but all types must be inferred from the context.

   The external representation of a xexp uses the subset of XML
   supported by the glib XML parser.  For a text node, the text is
   written with all the necessary escapes between its tag:

       <TAG>TEXT</TAG>

   For a list xexp, the format is
   
       <TAG>CHILD1 CHILD2 ...</TAG>

   with no space between the representations of the children.

   The ambiguity between text xexps with an empty string and empty
   lists is resolved by writing empty strings as

       <TAG></TAG>

   and empty lists as

       <TAG/>

   NULL is not a valid xexp, but NULL is used to designate the end of
   the list of children.

   Using lists and texts, some 'higher-level' constructs are defined:
   a association list is a list that is meant to be used as a
   dictionary using xexp_aref.  An empty list is often used as a flag
   whose presence or absence in a association list is used to encode a
   boolean value.

   Memory management is very simplistic: X-expression can not share
   structure or form cycles.  Every xexp is referenced by at most one
   other xexp, and when that references is broken, the xexp is freed.
   In other words, as soon as you put a xexp into a list, the list
   assumes ownership of that xexp.
*/

#ifndef XEXP_H
#define XEXP_H

#include <stdio.h>
#include <glib.h>

struct xexp;
typedef struct xexp xexp;

/* General
 */
const char *xexp_tag (xexp *x);
int xexp_is (xexp *x, const char *tag);
xexp *xexp_rest (xexp *x);
void xexp_set_rest (xexp *x, xexp *y);
void xexp_free_1 (xexp *x);
void xexp_free (xexp *x);
xexp *xexp_copy (xexp *x);

/* Lists
 */
xexp *xexp_list_new (const char *tag, xexp *first, xexp *rest);
int xexp_is_list (xexp *x);
xexp *xexp_first (xexp *x);
int xexp_length (xexp *x);
void xexp_prepend (xexp *x, xexp *y);
void xexp_append (xexp *x, xexp *y);
void xexp_del (xexp *x, xexp *y);

/* Texts
 */
xexp *xexp_text_new (const char *tag, const char *text, xexp *rest);
int xexp_is_text (xexp *x);
const char *xexp_text (xexp *x);
int xexp_text_as_int (xexp *x);

/* Association lists
 */
xexp *xexp_aref (xexp *x, const char *tag);
const char *xexp_aref_text (xexp *x, const char *tag);
int xexp_aref_bool (xexp *x, const char *tag);
int xexp_aref_int (xexp *x, const char *tag, int def);
void xexp_aset (xexp *x, xexp *val);
void xexp_aset_text (xexp *x, const char *tag, const char *val);
void xexp_aset_bool (xexp *x, const char *tag, int val);
void xexp_adel (xexp *x, const char *tag);
void xexp_adel_all (xexp *x, const char *tag);

/* Reading and writing
 */
xexp *xexp_read (FILE *f, GError **error);
void xexp_write (FILE *f, xexp *x);

xexp *xexp_read_file (const char *filename);
int xexp_write_file (const char *filename, xexp *x);

#endif
