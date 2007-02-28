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

/* X-expressions are a bit like S-expressions, but suitable for
   encoding in XML.  They are used for various configuration files,
   the installation instructions, and as part of the protocol between
   the frontend and the backend.

   An X-expression, or xexp for short, has a 'tag' and is either a
   'list' or 'text'.  Lists have children and text xexps have a
   string.  There are utility functions for treating texts as
   integers, but all types must be inferred from the context.

   Empty lists and empty texts are treated identically.  That is, a
   list xexp without children is also a text xexp with an empty
   string.  Those xexps are called "empty".

   The external representation of a xexp uses a subset of the subset
   of XML supported by the glib XML parser.  For a non-empty text
   node, the text is written with all the necessary escapes between
   its tag:

       <TAG>TEXT</TAG>

   For a non-empty list xexp, the format is
   
       <TAG>CHILD1 CHILD2 ...</TAG>

   with optional white space around the representations of the
   children.

   Empty xexps are written like this by xexp_write

       <TAG/>

   but the equivalent <tag></tag> form is also understood by
   xexp_read.

   Using lists and texts, some 'higher-level' constructs are defined:

   - A association list is a list that is meant to be used as a
     dictionary using xexp_aref et al.

   - An empty xexp is often used as a flag whose presence or absence
     in a association list is used to encode a boolean value.

   Each xexp has a 'rest' pointer that is used to form the list of
   childron of a list xexp.  Because of this way to form lists, a xexp
   can only be the child of at most one other xexp, and can not appear
   twice in a list.

   NULL is not a valid xexp, but NULL is used to designate the end of
   the list of children.

   A 'free standing' xexp is a xexp that is not a child of another
   xexp.  Free standing xexps have a 'rest' pointer of NULL.

   Memory management is very simplistic: X-expressions can not share
   structure or form cycles.  Every xexp is either 'free standing' or
   the child of exactly one other xexp.  In other words, as soon as
   you put a xexp into a list xexp, the list assumes ownership of that
   xexp.  (Yes, I can see already that I will add reference counting
   eventually, and then a tracing GC...)

   The following reference states the pre-conditions for some
   functions.  When these conditions are not fulfilled, the
   implementation will generally emit a warning and then do something
   safe.  For example, xexp_first is only defined for list xexps, but
   when you apply it to a text xexp, you simple get NULL (and a
   warning).  Thus, using xexps wrongly will not abort your program
   (but you might of course end up losing or corrupting user data
   anyway, so be careful nevertheless).


   GENERAL XEXPS

   - const char *xexp_tag (xexp *X)

   Returns the tag of X.  The returned pointer is valid as long as X
   is.

   - int xexp_is (xexp *X, const char *tag)

   Returns true when X has the tag TAG, false otherwise.

   - int xexp_is_empty (xexp *X)

   Return true when X is empty, false otherwise.

   - xexp *xexp_rest (xexp *X)

   Return the 'rest' pointer of X.  By using xexp_first and xexp_rest
   you can traverse the tree of xexps hanging off a root node.  The
   returned pointer is valid as long as X is.
   
   - xexpr *xexp_copy (xexp *X)

   Return a deep-copy of X.  You should eventually put the result into
   another xexp or free it with xexp_free.  The result is a free
   standing xexp.

   - xexpr *xexp_free (xexp *X)

   Frees X and all its children, recursively.  X must be a free
   standing xexp.


   LIST XEXPS

   - xexp *xexp_list_new (const char* TAG)

   Create a new free standing list xexp with the given TAG.  TAG is
   copied and need not remain valid after this call.  The new xexp has
   no children initially and is thus a empty xexp.

   - int xexp_is_list (xexp *X)

   Return true when X is a list xexp, false otherwise.

   - xexp *xexp_first (xexp *X)

   Return the first child of X.  X must be a list xexp.  The returned
   pointer is valid as long as X is.  When X is empty, NULL is
   returned.

   - int xexp_length (xexp *X)

   Returns the number of children of X.  X must be a list xexp.

   - void xexp_cons (xexp *X, xexp *Y)

   Prepend Y to the list of children of X.  X must be a list xexp.  Y
   must be a free standing xexp.

   - void xexp_append_1 (xexp *X, xexp *Y)

   Append Y to the end of the list of children of X.  X must be a list
   xexp.  Y must be a free standing xexp.

   - void xexp_reverse (xexp *X)

   Reverse the order of the children of X.  X must be a list xexp.
   
   - void xexp_del (xexp *X, xexp *Y)

   Deletes Y from the list of children of X and frees it.  X must be a
   list xexp.  Y must be a child of X.


   TEXT XEXPS

   - xexp *xexp_text_new (const char *TAG, const char *TEXT)
   - xexp *xexp_text_newn (const char *TAG, const char *TEXT, int len)

   Create a new free text xexp with the given TAG and TEXT.  TAG and
   TEXT are copied and need not remain valid after this call.  If TEXT
   is the empty string, a empty xexp is created.

   - int xexp_is_text (xexp *X)
   
   Return true when X is a text xexp, false otherwise.

   - const char *xexp_text (xexp *X)

   Return the text of X.  X must be a text xexp.  The returned pointer
   is valid as long as X is.  When X is empty, the empty string is
   returned, not NULL.

   - int xexp_text_as_int (xexp *X)

   Parse the text of X as a decimal integer.  X must be a text xexp.

   
   ASSOCIATION LISTS

   X must be a list xexp for the following functions.

   - xexp *xexp_aref (xexp *X, const char *TAG)

   Return the first xexp that has tag TAG from the children of X.
   Return NULL if there is no such xexp.

   - const char *xexp_aref_text (xexp *X, const char *TAG)

   Find the first xexp that has tag TAG from the children of X and
   call xexp_text on it.  Return NULL if none is found.
   
   - int xexp_aref_bool (xexp *X, const char *TAG)
   
   Return true if there is a xexp among the children of X with tag
   TAG, false otherwise.
   
   - int xexp_aref_int (xexp *X, const char *TAG, int DEF)

   Find the first xexp that has tag TAG from the children of X and
   call xexp_text_as_int on it.  Return DEF if none is found.

   - void xexp_aset (xexp *X, xexp *VAL)

   Modify the children of X so that VAL is included in it and no other
   child has the same tag as Y.  VAL must be free standing.

   - void xexp_aset_text (xexp *X, const char *TAG, const char *VAL)

   If VAL is NULL, remove all childrem of X that have tag TAG.
   Otherwise, call xexp_aset on X with the result of xexp_text_new
   (TAG, VAL).

   - void xexp_aset_bool (xexp *x, const char *tag, int val)

   If VAL is false, remove all childrem of X that have tag TAG.
   Otherwise, call xexp_aset on X with the result of xexp_list_new
   (TAG).

   - void xexp_adel (xexp *x, const char *tag);

   Remove all children of X that have tag TAG.


   READING AND WRITING

   - xexp *xexp_read (FILE *F, GError **ERROR)

   Read exactly one xexp from F and return it.  If an error is
   encountered, NULL is returned and ERROR set appropriately.

   - void xexp_write (FILE *F, xexp *X)

   Write X to F.  F can be checked with ferror afterwards and errno is
   intact.

   - xexp *xexp_read_file (const char *FILENAME)

   Read the first xexp from the file named FILENAME.  When an error is
   encountered, log it to stderr and return NULL.

   - int xexp_write_file (const char *FILENAME, xexp *X)

   Write X to the file named FILENAME.  When the file can not be
   written, the error is logged to stderr, the old version of it is
   left in place and false is returned.  Otherwise, true is returned.
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
int xexp_is_empty (xexp *x);
xexp *xexp_rest (xexp *x);
xexp *xexp_copy (xexp *x);
void xexp_free (xexp *x);

/* Lists
 */
xexp *xexp_list_new (const char *tag);
int xexp_is_list (xexp *x);
xexp *xexp_first (xexp *x);
int xexp_length (xexp *x);
void xexp_cons (xexp *x, xexp *y);
void xexp_append_1 (xexp *x, xexp *y);
void xexp_reverse (xexp *x);
void xexp_del (xexp *x, xexp *y);

/* Texts
 */
xexp *xexp_text_new (const char *tag, const char *text);
xexp *xexp_text_newn (const char *tag, const char *text, int len);
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

/* Reading and writing
 */
xexp *xexp_read (FILE *f, GError **error);
void xexp_write (FILE *f, xexp *x);

xexp *xexp_read_file (const char *filename);
int xexp_write_file (const char *filename, xexp *x);

#endif
