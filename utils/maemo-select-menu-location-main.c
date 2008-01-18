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

/*
 * This utility lets the user move the menu entry for an application
 * to a new location.  This task is present to the use as choosing a
 * folder where to put the menu entry, but in reality the entry must
 * already be in the menu (most likely in "Extras") and this utility
 * will just move it around.
 *
 * The caller of this utility can specify a default location and the
 * menu entry is silently moved into that location before showing the
 * dialog.
 *
 * The default location is specified via the untranslated name of the
 * folder, such as "tana_fi_tools".  If a non-existing name is used,
 * that folder is created.  In that case, it is probably a good idea
 * to use a plain English name like "Education".
 *
 * It uses lauch_dialog_move_to, make_new_folder, and move_application
 * from lib_task_navigator for the heavy lifting.
 *
 * The "Application installer packaging guide" explains how to use
 * this utility in packages.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <libintl.h>
#include <libhildondesktop/libhildonmenu.h>
#include <hildon/hildon-caption.h>

#define _ai_(a) dgettext ("hildon-application-manager", a)
#define _dt_(a) dgettext ("maemo-af-desktop", a)
#define _fm_(a) dgettext ("hildon-fm", a)

#define RESPONSE_CHANGE_FOLDER 1

#if 0
#define DEBUG
#endif

static void move_to_location (GtkTreeModel *model, const gchar *desktop_id,
			      const gchar *location);

#ifdef DEBUG
static void
dump_menu_1 (GtkTreeModel *model, GtkTreeIter *parent, int level)
{
  GtkTreeIter iter;
  gboolean valid;
  int i;

  valid = gtk_tree_model_iter_children (model, &iter, parent);
  i = 0;
  while (valid)
    {
      gchar *name, *text_domain;
      gtk_tree_model_get (model, &iter, 
			  TREE_MODEL_NAME, &name,
			  TREE_MODEL_TEXT_DOMAIN, &text_domain,
			  -1);
      printf ("%.*s%s (%s)\n", level, "               ", name, _dt_(name));
      g_free (name);
      g_free (text_domain);

      dump_menu_1 (model, &iter, level+1);

      valid = gtk_tree_model_iter_next (model, &iter);
      i++;
    }
}

static void
dump_menu (GtkTreeModel *model)
{
  dump_menu_1 (model, NULL, 0);
}
#endif

/* GtkTreeStore acrobatics
 */

static void
move_item (GtkTreeStore *store, GtkTreeIter *item, GtkTreeIter *parent)
{
  GtkTreeModel *model = GTK_TREE_MODEL (store);
  GtkTreeIter dest;
  gint n = gtk_tree_model_get_n_columns (model), i;
  GValue *values = g_new0 (GValue, n);
  
  if (gtk_tree_store_is_ancestor (store, parent, item))
    return;

  for (i = 0; i < n; i++)
    gtk_tree_model_get_value (model, item, i, values+i);
  
  gtk_tree_store_append (store, &dest, parent);

  for (i = 0; i < n; i++)
    {
      gtk_tree_store_set_value (store, &dest, i, values+i);
      g_value_unset (values+i);
    }

  gtk_tree_store_remove (store, item);
  g_free (values);
}

static void
make_folder (GtkTreeStore *store, GtkTreeIter *folder, const gchar *name)
{
  gtk_tree_store_append (store, folder, NULL);

  gtk_tree_store_set 
    (store, folder,
     TREE_MODEL_NAME, name,
     TREE_MODEL_ICON, NULL, //item_icon,
     TREE_MODEL_EMBLEM_EXPANDER_OPEN, NULL, //composited_emblem_open, 
     TREE_MODEL_EMBLEM_EXPANDER_CLOSED, NULL, //composited_emblem_closed, 
     TREE_MODEL_EXEC, "",
     TREE_MODEL_SERVICE, "",
     TREE_MODEL_DESKTOP_ID, "",
     -1);
}

/* The functions find_window_1 and find_window walk the X11 Window
   hierachy, looking for a window with a given WM_NAME.  They are used
   by find_application_installer_window to find the Application
   Manager window, which in turn is used by dialog_realized to make
   the dialogs transient for it.
 */

static Window
find_window_1 (Window win, const char *name, int level, int max_level)
{
  char *win_name;
  
  if (XFetchName (gdk_display, win, &win_name))
    {
      if (!strcmp (win_name, name))
	{
	  XFree (win_name);
	  return win;
	}
      XFree (win_name);
    }
  
  if (level < max_level)
    {
      Window root, parent, *children;
      unsigned int n_children;
      
      if (XQueryTree (gdk_display, win, &root, &parent,
		      &children, &n_children))
	{
	  int i;
	  Window w;

	  for (i = 0; i < n_children; i++)
	    {
	      w = find_window_1 (children[i], name, level+1, max_level);
	      if (w)
		{
		  XFree (children);
		  return w;
		}
	    }
	  XFree (children);
	}
    }
  
  return 0;
}

static Window
find_window (const char *name, int max_level)
{
  return find_window_1 (GDK_ROOT_WINDOW (), name, 0, max_level);
}

static Window
find_application_manager_window ()
{
  return find_window ("hildon-application-manager", 2);
}

static GtkWidget *parent_window = NULL;
static GSList *dialog_stack = NULL;

static void
dialog_realized (GtkWidget *dialog, gpointer data)
{
  GdkWindow *win = dialog->window;
  if (parent_window != NULL)
    {
      GdkWindow *parent_win = parent_window->window;

      XSetTransientForHint (GDK_WINDOW_XDISPLAY (win),
			    GDK_WINDOW_XID (win),
			    GDK_WINDOW_XID (parent_win));
    }
  else
    {
      /* This happens only for the main dialog  */
      Window ai_win = find_application_manager_window ();

      if (ai_win)
	XSetTransientForHint (GDK_WINDOW_XDISPLAY (win), GDK_WINDOW_XID (win),
			      ai_win);
    }
}

static void
dialog_exposed (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  GdkWindow *win  = widget->window;
  GtkWidget *child = NULL;

  /* Raise this window to the top of the X stack if it's the topmost one */
  if (dialog_stack == NULL || dialog_stack->data == widget)
    gdk_window_set_keep_above (win, TRUE);

  /* Progate expose event to children */
  child = gtk_bin_get_child (GTK_BIN (widget));
  if (child)
    {
      gtk_container_propagate_expose (GTK_CONTAINER (widget),
				      child,
				      event);
    }
}

void
push_dialog (GtkWidget *dialog)
{
  gtk_window_set_modal (GTK_WINDOW (dialog), parent_window == NULL);

  if (dialog_stack)
    {
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
				    GTK_WINDOW (dialog_stack->data));
    }

  g_signal_connect (dialog, "realize",
		    G_CALLBACK (dialog_realized), NULL);

  g_signal_connect (dialog, "expose-event",
		    G_CALLBACK (dialog_exposed), NULL);

  dialog_stack = g_slist_prepend (dialog_stack, dialog);
}

void
pop_dialog (GtkWidget *dialog)
{
  g_assert (dialog_stack && dialog_stack->data == dialog);

  {
    GSList *old = dialog_stack;
    dialog_stack = dialog_stack->next;
    g_slist_free_1 (old);
  }

  if (dialog_stack == NULL || dialog_stack->next == NULL)
    parent_window = NULL;
}

static gboolean
find_string (GtkTreeIter *result,
	     int column, const char *wanted,
	     GtkTreeModel *model, GtkTreeIter *parent, gboolean recurse)
{
  GtkTreeIter iter;
  gboolean valid;
  int i;

  valid = gtk_tree_model_iter_children (model, &iter, parent);
  i = 0;
  while (valid)
    {
      gboolean found;

      gchar *string;
      gtk_tree_model_get (model, &iter, 
			  column, &string,
			  -1);
      found = (strcmp (string, wanted) == 0);
      g_free (string);

      if (found)
	{
	  *result = iter;
	  return TRUE;
	}

      if (recurse && find_string (result, column, wanted, model, &iter, TRUE))
	return TRUE;

      valid = gtk_tree_model_iter_next (model, &iter);
      i++;
    }

  gtk_tree_model_get_iter_first (model,result);  
 
  return FALSE;
}

/* find_desktop_id and find_name are two useful specializations of
   find_string.
 */

static gboolean
find_desktop_id (GtkTreeIter *result,
		 const char *wanted,
		 GtkTreeModel *model, GtkTreeIter *parent)
{
  return find_string (result, TREE_MODEL_DESKTOP_ID, wanted, model, parent, TRUE);
}

static gboolean
find_name (GtkTreeIter *result,
           const char *wanted,
           GtkTreeModel *model,
           GtkTreeIter *parent,
           gboolean recurse)
{
  return find_string (result, TREE_MODEL_NAME, wanted, model, parent, recurse);
}

/** "Change Folder" dialog
 */

gboolean
is_folder (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  gchar *desktop_id;
  gboolean result;

  gtk_tree_model_get (model, iter,
		      TREE_MODEL_DESKTOP_ID, &desktop_id,
		      -1);
  result = (desktop_id == NULL || desktop_id[0] == '\0');

  g_free (desktop_id);
  return result;
}

static void
name_func (GtkTreeViewColumn *column,
	   GtkCellRenderer *cell,
	   GtkTreeModel *model,
	   GtkTreeIter *iter,
	   gpointer data)
{
  gchar *name;
  gtk_tree_model_get (model, iter,
		      TREE_MODEL_NAME, &name,
		      -1);
  g_object_set (cell, "text", _dt_(name), NULL);
  g_free (name);
}

GtkWidget *
make_folder_tree_view (GtkTreeModel *model)
{
  GtkTreeViewColumn *column;
  GtkCellRenderer   *renderer;
  GtkTreeModel *folder_model;
  GtkWidget *treeview;

  folder_model = gtk_tree_model_filter_new (model, NULL);

  gtk_tree_model_filter_set_visible_func 
    (GTK_TREE_MODEL_FILTER (folder_model),
     is_folder, NULL, NULL);

  treeview = gtk_tree_view_new_with_model (folder_model);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_append_column (GTK_TREE_VIEW(treeview), column);
	
  /* Icon */
  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_add_attribute (column, renderer, "pixbuf",
				      TREE_MODEL_ICON);
  gtk_tree_view_column_add_attribute (column, renderer,
				      "pixbuf-expander-closed",
				      TREE_MODEL_EMBLEM_EXPANDER_CLOSED);
  gtk_tree_view_column_add_attribute (column, renderer,
				      "pixbuf-expander-open",
				      TREE_MODEL_EMBLEM_EXPANDER_OPEN);

  /* Name */
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (column, renderer,
					   name_func, treeview, NULL);
  g_object_set (renderer, "yalign", 0.5, NULL);

  gtk_tree_view_expand_all (GTK_TREE_VIEW(treeview));

  return treeview;
}

gchar *
run_move_to_dialog (GtkWindow *parent, gchar *title, GtkTreeModel *model)
{
  GtkWidget *box, *scroller, *treeview, *dialog;
  GtkTreeSelection *selection;
  GtkTreeModel *sel_model;
  GtkTreeIter sel_iter;
  gchar *dest = NULL;

  dialog = gtk_dialog_new_with_buttons (title,
					parent,
					(GTK_DIALOG_MODAL
					 | GTK_DIALOG_DESTROY_WITH_PARENT
					 | GTK_DIALOG_NO_SEPARATOR),
					_fm_("ckdg_bd_change_folder_ok"),
					GTK_RESPONSE_OK,
					NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  box = gtk_hbox_new (TRUE, 12);
  treeview = make_folder_tree_view (model);

  scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroller),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scroller, 240, 200);


  gtk_container_add (GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), box);
  gtk_box_pack_start (GTK_BOX (box), scroller, FALSE, TRUE, 0);
  gtk_container_add (GTK_CONTAINER(scroller), treeview);

  parent_window = GTK_WIDGET (parent);
  push_dialog (dialog);

  gtk_widget_show_all (dialog);
  gtk_dialog_run (GTK_DIALOG (dialog));

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));
  if (gtk_tree_selection_get_selected (selection, &sel_model, &sel_iter))
    gtk_tree_model_get (sel_model, &sel_iter,
			TREE_MODEL_NAME, &dest,
			-1);

  gtk_widget_destroy (dialog);
  pop_dialog (dialog);

  return dest;
}

/* The state of the "Select location" dialog is kept in a sml_closure
   instance.

   DESKTOP_ID identifies the entry we are moving around.  We search
   for it after every change.

   LOCATION_ICON and LOCATION_LABEL are used by
   update_select_location_dialog to update the dialog when the entry
   has been moved around.
*/

typedef struct {
  GtkTreeModel *model;
  const gchar *desktop_id;
  GtkWidget *dialog;
  GtkWidget *location_icon;
  GtkWidget *location_label;
} sml_closure;

/* update_select_location_dialog changes the widgets in the dialog to
   reflect the current location of the entry identified by DESKTOP_ID.
*/

static void
update_select_location_dialog (sml_closure *c)
{
  GtkTreeIter entry_iter, parent_iter;
  gchar *name = NULL;
  GdkPixbuf *icon = NULL;

  /* XXX - When the entry is directly below the root (and thus has no
           parent with a name or icon that we could display), we just
           display nothing for now.
  */

  if (find_desktop_id (&entry_iter, c->desktop_id, c->model, NULL)
      && gtk_tree_model_iter_parent (c->model, &parent_iter, &entry_iter))
    gtk_tree_model_get (c->model, &parent_iter,
			TREE_MODEL_NAME, &name,
			TREE_MODEL_ICON, &icon,
			-1);

  gtk_label_set_text (GTK_LABEL (c->location_label), _dt_(name));
  gtk_image_set_from_pixbuf (GTK_IMAGE (c->location_icon), icon);
  g_free (name);
}

/* Let the user move DESKTOP_ID to some other folder.
 */
static void
change_folder (sml_closure *c)
{
  GtkTreeIter entry_iter;

  if (find_desktop_id (&entry_iter, c->desktop_id, c->model, NULL))
    {
      gchar *dest;
      dest = run_move_to_dialog (GTK_WINDOW (c->dialog),
				 _fm_("ckdg_ti_change_folder"),
				 c->model);
      if (dest)
	move_to_location (c->model, c->desktop_id, dest);
      g_free (dest);
      update_select_location_dialog (c);
    }
}

static void
change_folder_pressed (GtkWidget *widget, GdkEventButton *event,
		       gpointer data)
{
  sml_closure *c = (sml_closure *)data;
  change_folder (c);
}

/* selection_location_response handles clicks on the dialog response
   buttons.  Clicking on "Ok" also quits the current main loop.
*/
static void
selection_location_response (GtkDialog *dialog, int response, gpointer data)
{
  sml_closure *c = (sml_closure *)data;

  if (response == RESPONSE_CHANGE_FOLDER)
    change_folder (c);
  else
    {
      set_menu_contents (c->model);
      g_free (c);

      gtk_widget_destroy (GTK_WIDGET (dialog));
      pop_dialog (GTK_WIDGET (dialog));

      gtk_main_quit ();
    }
}

/* run_select_location_dialog creates and runs the "Select location"
   dialog for the entry identified by DESKTOP_ID.  When it returns,
   everything has been done and the utility will exit.
*/

static void
run_select_location_dialog (GtkTreeModel *model,
			    const gchar *desktop_id)
{
  sml_closure *c;
  GtkWidget *vbox;
  GtkSizeGroup *group;

  c = g_new (sml_closure, 1);
  c->model = model;
  c->desktop_id = desktop_id;

  c->dialog = gtk_dialog_new_with_buttons 
    (_ai_("ai_ti_select_location"),
     NULL,
     GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
     _ai_("ai_bd_select_location_ok"), GTK_RESPONSE_OK,
     _ai_("ai_bd_select_location_change_folder"), RESPONSE_CHANGE_FOLDER,
     NULL);

  vbox = GTK_DIALOG (c->dialog)->vbox;

  gtk_box_pack_start (GTK_BOX (vbox),
		      gtk_label_new (_ai_("ai_ia_select_location")),
		      FALSE, FALSE, 0);

  group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  {
    GtkWidget *label, *caption;
    GtkTreeIter entry_iter;
    
    gchar *name = NULL;
    if (find_desktop_id (&entry_iter, c->desktop_id, c->model, NULL))
      gtk_tree_model_get (model, &entry_iter,
			  TREE_MODEL_NAME, &name,
			  -1);
    else
      {
	/* XXX - this is an odd place to exit...
	 */
	fprintf (stderr, "maemo-select-menu-location: "
		 "%s not found in menu.\n", desktop_id);
	exit (1);
      }
    label = gtk_label_new (_dt_(name));
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
    g_free (name);

    caption = hildon_caption_new (group,
				  _ai_("ai_fi_select_location_application"), 
				  label,
				  NULL, HILDON_CAPTION_OPTIONAL);

    gtk_box_pack_start (GTK_BOX (vbox), caption, FALSE, FALSE, 0);
  }

  {
    /* This emulates the looks of a GtkFileChooserButton.
    */

    GtkWidget *label, *image, *eventbox, *hbox, *caption;
    
    label = gtk_label_new (NULL);
    gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

    image = gtk_image_new_from_pixbuf (NULL);

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 10);

    eventbox = gtk_event_box_new ();
    gtk_container_add (GTK_CONTAINER (eventbox), hbox);

    caption = hildon_caption_new (group,
				  _ai_("ai_fi_select_location_location"), 
				  eventbox,
				  NULL, HILDON_CAPTION_OPTIONAL);

    gtk_box_pack_start (GTK_BOX (vbox), caption, FALSE, FALSE, 0);

    g_signal_connect (eventbox, "button-press-event",
		      G_CALLBACK (change_folder_pressed), c);

    c->location_icon = image;
    c->location_label = label;
  }

  g_signal_connect (c->dialog, "response",
		    G_CALLBACK (selection_location_response), c);

  update_select_location_dialog (c);

  parent_window = NULL;
  push_dialog (c->dialog);

  gtk_widget_show_all (c->dialog);
  gtk_main ();
}

/* move_to_location performs the silent move of the entry identified
   by DESKTOP_ID to the default location named LOCATION.
*/

static void
move_to_location (GtkTreeModel *model, const gchar *desktop_id,
		  const gchar *location)
{
  GtkTreeIter entry_iter, folder_iter;
  GtkTreePath *path = NULL;
  
  if (!find_desktop_id (&entry_iter, desktop_id, model, NULL))
    return;
  
  if (!find_name (&folder_iter, location, model, NULL, TRUE))
    {
#if 0
      /* XXX - Making new folders doesn't seem to work so well right
	       now.
       */
      make_folder (GTK_TREE_STORE (model), &folder_iter, location);
#endif	 

      if (!find_name (&folder_iter, location, model, NULL, TRUE))
	{
	  fprintf (stderr, "maemo-select-menu-location: "
		   "could not create '%s' folder\n", location);
	  return;
	}
    }

  if (path)
    gtk_tree_path_free( path );

  move_item (GTK_TREE_STORE (model), &entry_iter, &folder_iter);
}

/* remove the location from the menu */
static int
remove_location (GtkTreeModel *model, const char *location)
{
  GtkTreeIter folder_iter;
  GtkTreeIter root_iter;
  
  if (!gtk_tree_model_get_iter_root (model, &root_iter))
    {
      fprintf (stderr, "Menu is empty\n");
      return 1;
    }

  if (find_name (&folder_iter, location, model, &root_iter, FALSE))
    {
      if (gtk_tree_model_iter_has_child (model, &folder_iter))
        {
          fprintf (stderr, "Folder %s is not empty\n", location);
          return 1;
        }
      gtk_tree_store_remove (GTK_TREE_STORE (model), &folder_iter);
      set_menu_contents (model);
      return 0;
    }

  fprintf (stderr, "No top folder named %s\n", location);
  return 1;
}

int
main (int argc, char **argv)
{
  GtkTreeModel *model;
  char *desktop_id, *default_location;

  gtk_init (&argc, &argv);

  if (argc == 1)
    {
      fprintf (stderr,
	       "Usage: maemo-select-menu-location "
	       "<app>.desktop [default location]\n"
           "       maemo-select-menu-location "
           "--remove <location>\n");
      exit (1);
    }
  
  model = get_menu_contents ();
  g_object_ref (model);

  if (argc >=3 && strcmp (argv[1], "--remove") == 0)
    return remove_location (model, argv[2]);

  desktop_id = argv[1];
  default_location = (argc > 2)? argv[2] : NULL;

  if (default_location)
    move_to_location (model, desktop_id, default_location);

#ifdef DEBUG
  dump_menu (model);
#endif

  run_select_location_dialog (model, desktop_id);

#ifdef DEBUG
  dump_menu (model);
#endif

  return 0;
}
