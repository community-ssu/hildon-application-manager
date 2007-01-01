#ifndef HILDON_BREAD_CRUMB_TRAIL_H
#define HILDON_BREAD_CRUMB_TRAIL_H

#include <gtk/gtk.h>

/* First version.  It is not done as a widget class but rather as a
   set of functions.  Also, it uses global variables for state, so
   there can be only one.
*/

typedef const gchar *hildon_bread_crumb_trail_get_label (GList *node);
typedef void hildon_bread_crumb_trail_clicked (GList *node);

GtkWidget *hildon_bread_crumb_trail_new
             (hildon_bread_crumb_trail_get_label *get_label,
	      hildon_bread_crumb_trail_clicked *clicked);

/* ITEMS is a list of whatever you like.  GET_LABEL will be called on
   them to get a label for display.
 */
void hildon_bread_crumb_trail_set_path (GtkWidget *trail, GList *items);

#endif /* !HILDON_BREAD_CRUMB_TRAIL_H */
