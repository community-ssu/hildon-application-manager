/*
 * This file is part of the hildon-application-manager.
 *
 * Parts of this file are derived from apt.  Apt is copyright 1997,
 * 1998, 1999 Jason Gunthorpe and others.
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

#include <string.h>
#include <stdio.h>
#include <libintl.h>
#include <locale.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <hildon/hildon-note.h>
#include <hildon/hildon-caption.h>

#include "menu.h"
#include "repo.h"
#include "settings.h"
#include "apt-worker-client.h"
#include "util.h"
#include "log.h"
#include "confutils.h"
#include "apt-utils.h"

#define _(x)       gettext (x)

static gboolean
repository_uri_is_valid (const gchar* uri)
{
  const gchar *delimiter = ":";
  gchar **tokens;
  gchar *tmp, *repo_uri;
  gboolean result = FALSE;

  if (uri == NULL || strlen (uri) == 0 ||
      all_whitespace (uri) || !g_utf8_validate (uri, -1, NULL))
    return FALSE;

  repo_uri = g_strdup (uri);
  g_strstrip (repo_uri);

  tokens = g_strsplit (repo_uri, delimiter, 2);

  /* Check APT method */
  if ((tokens != NULL) && (tokens[1] != NULL) &&
      (strlen (tokens[0]) > 0) && (strlen (tokens[1]) > 0))
    {
      /* Check also that the uri is not just "<aptm-method>://" */
      gchar *uri_prefix = g_strdup_printf ("%s://", tokens[0]);
      if (!tokens_equal (uri, uri_prefix))
        {
          tmp = g_strdup_printf ("%s%s", APT_METHOD_PATH, tokens[0]);
          result = g_file_test (tmp, G_FILE_TEST_EXISTS);
          g_free (tmp);
        }
      g_free (uri_prefix);
    }
  g_strfreev(tokens);

  /* At last, look for blanks in the middle of the uri */
  if (g_strrstr (uri, " ") != NULL)
    return FALSE;

  return result;
}

static GtkWidget *
add_entry (GtkWidget *box, GtkSizeGroup *group,
	   const char *label,
	   const char *text, const char *end,
	   bool autocap, bool readonly, bool mandatory)
{
  GtkWidget *caption, *entry;
  gint pos = 0;

  if (readonly)
    {
      GtkTextBuffer *buffer;

      entry = gtk_text_view_new ();

      buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (entry));
      gtk_text_view_set_editable (GTK_TEXT_VIEW (entry), false);

      if (text)
        gtk_text_buffer_set_text (buffer, text, end-text);

      gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (entry), FALSE);
      g_object_set (entry, "can-focus", FALSE, NULL);    
    }
  else
    {
      entry = gtk_entry_new ();

#ifdef MAEMO_CHANGES
      int mode;
      g_object_get (entry, "hildon-input-mode", &mode, NULL);
      if (autocap)
	mode |= int (HILDON_GTK_INPUT_MODE_AUTOCAP);
      else
	mode &= ~int (HILDON_GTK_INPUT_MODE_AUTOCAP);
      g_object_set (entry, "hildon-input-mode", mode, NULL);
#endif
      
      if (text)
	{
	  if (end == NULL)
	    end = text + strlen (text);
	  gtk_editable_insert_text (GTK_EDITABLE (entry),
				    text, end-text, &pos);
	}
    }

  caption = hildon_caption_new (group, label, entry,
				NULL, (mandatory
				       ? HILDON_CAPTION_MANDATORY
				       : HILDON_CAPTION_OPTIONAL));
  gtk_box_pack_start_defaults (GTK_BOX (box), caption);
  
  return entry;
}

static void
pill_response (GtkDialog *dialog, gint response, gpointer unused)
{
  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (red_pill_mode != (response == GTK_RESPONSE_YES))
    {
      red_pill_mode = (response == GTK_RESPONSE_YES);
      save_settings ();

      set_settings_menu_visible (red_pill_mode);
      update_backend_options ();
      if (red_pill_show_all || red_pill_show_magic_sys)
        get_package_list ();
    }
}

static void
ask_the_pill_question ()
{
  GtkWidget *dialog;

  dialog =
    hildon_note_new_confirmation_add_buttons (NULL, 
					      "Which pill?",
					      "Red", GTK_RESPONSE_YES,
					      "Blue", GTK_RESPONSE_NO,
					      NULL);
  push_dialog (dialog);
  g_signal_connect (dialog, "response",
		    G_CALLBACK (pill_response), NULL);
  gtk_widget_show_all (dialog);
}

struct get_catalogues_closure {
  void (*cont) (xexp *catalogues, void *data);
  void *data;
};

static void
get_catalogues_callback (int cmd,
			 apt_proto_decoder *dec,
			 void *callback_data)
{
  get_catalogues_closure *c = (get_catalogues_closure *)callback_data;
  xexp *x = NULL;

  if (dec
      && (x = dec->decode_xexp ())
      && xexp_is (x, "catalogues"))
    {
      c->cont (x, c->data);
    }
  else
    {
      if (dec)
	what_the_fock_p ();
      xexp_free (x);
      c->cont (NULL, c->data);
    }
  delete c;
}

void
get_catalogues (void (*cont) (xexp *catalogues, void *data),
		void *data)
{
  get_catalogues_closure *c = new get_catalogues_closure;
  c->cont = cont;
  c->data = data;
  apt_worker_get_catalogues (get_catalogues_callback, c);
}

struct rm_temp_catalogues_closure {
  void (*cont) (void *data);
  void *data;
};

static void rtc_reply (bool keep_going, void *data)
{
  rm_temp_catalogues_closure* rtc_clos = (rm_temp_catalogues_closure*) data;

  rtc_clos->cont (rtc_clos->data);
  
  delete rtc_clos;
}

static void
rm_temp_catalogues_callback (int cmd,
                             apt_proto_decoder *dec,
                             void *data)
{
  refresh_package_cache_without_user (NULL, rtc_reply, data);
}

void
rm_temp_catalogues (void (*cont) (void* data), void *data)
{
  rm_temp_catalogues_closure* rtc_clos = new rm_temp_catalogues_closure;
  rtc_clos->cont = cont;
  rtc_clos->data = data;
  
  apt_worker_rm_temp_catalogues (rm_temp_catalogues_callback, rtc_clos);
}

const char *
catalogue_name (xexp *x)
{
  const char *name = "";
  xexp *n = xexp_aref (x, "name");
  if (n == NULL)
    ;
  else if (xexp_is_text (n))
    name = xexp_text (n);
  else
    {
      char *current_locale = setlocale (LC_MESSAGES, "");
      xexp *t = (current_locale
		 ? xexp_aref (n, current_locale)
		 : NULL);
      if (t == NULL)
        t = xexp_aref (n, "default");
      if (t && xexp_is_text (t))
        name = xexp_text (t);
    }
  return name;
}

void
set_catalogue_name (xexp *x, const char *name)
{
  xexp *n = xexp_aref (x, "name");
  if (n == NULL || xexp_is_text (n))
    xexp_aset_text (x, "name", name);
  else
    {
      char *current_locale = setlocale (LC_MESSAGES, "");
      xexp_aset_text (n, current_locale, name);
    }
}

/* "Application Catalogues" interaction flow.
 */

struct scdf_clos {
  xexp *catalogues;
};

static void scdf_dialog_done (bool changed, void *date);
static void scdf_end (bool keep_going, void *data);

void
show_catalogue_dialog_flow ()
{
  if (start_interaction_flow ())
    {
      scdf_clos *c = new scdf_clos;

      c->catalogues = NULL;
      show_catalogue_dialog (NULL, false,
			     scdf_dialog_done, c);
    }
}

static void
scdf_dialog_done (bool changed, void *data)
{
  scdf_clos *c = (scdf_clos *)data;

  if (changed)
    set_catalogues_and_refresh (c->catalogues,
				NULL, scdf_end, c);
  else
    scdf_end (true, c);
}

static void
scdf_end (bool keep_going, void *data)
{
  scdf_clos *c = (scdf_clos *)data;

  xexp_free (c->catalogues);
  delete c;

  end_interaction_flow ();
}

struct catcache {
  catcache *next;
  struct cat_dialog_closure *cat_dialog;
  xexp *catalogue_xexp;
  bool enabled, readonly, foreign, refresh_failed;
  const char *name;
  char *detail;
};

struct cat_dialog_closure {
  catcache *caches;
  xexp *catalogues_xexp;
  bool dirty;
  bool show_only_errors;

  bool showing_catalogues;

  catcache *selected_cat;
  GtkTreeIter selected_iter;

  GtkTreeView *tree;
  GtkListStore *store;
  GtkWidget *new_button;
  GtkWidget *edit_button;
  GtkWidget *delete_button;

  void (*cont) (bool changed, void *data);
  void *data;
};

struct cat_edit_closure {
  cat_dialog_closure *cat_dialog;
  xexp *catalogue;
  bool isnew;
  bool readonly;

  GtkWidget *name_entry;
  GtkWidget *uri_entry;
  GtkWidget *dist_entry;
  GtkWidget *components_entry;
  GtkWidget *disabled_button;
};

static cat_dialog_closure *current_cat_dialog_clos = NULL;

static void reset_cat_list (cat_dialog_closure *c);
static void set_cat_list (cat_dialog_closure *c, GtkTreeIter *iter_to_select);

static gboolean
is_package_catalogue (xexp *catalogue)
{
  const gchar *file = xexp_aref_text (catalogue, "file");
  const gchar *id   = xexp_aref_text (catalogue, "id");

  return (file && id);
}

static void
cat_edit_response (GtkDialog *dialog, gint response, gpointer clos)
{
  bool should_ask_the_pill_question = false;

  cat_edit_closure *c = (cat_edit_closure *)clos;

  if (c->readonly && !c->cat_dialog) // it cames from an .install file
    ;
  else if (response == GTK_RESPONSE_OK && c->readonly)
    {
      bool disabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON 
                  (c->disabled_button));
      xexp_aset_bool (c->catalogue, "disabled", disabled);
      set_cat_list (c->cat_dialog, &c->cat_dialog->selected_iter);
      c->cat_dialog->dirty = true;
    }
  else if (response == GTK_RESPONSE_OK && !c->readonly)
    {
      const char *name = gtk_entry_get_text (GTK_ENTRY (c->name_entry));

      if (all_whitespace (name))
      {
        irritate_user (_("ai_ib_enter_name"));
        gtk_widget_grab_focus (c->name_entry);
        return;
      }

      char *uri = g_strstrip (g_strdup (gtk_entry_get_text
                                        (GTK_ENTRY (c->uri_entry))));

      /* validate repository location                                         */

      /* TODO we need a more general text, like "Invalid repository location" */
      /* TODO encode URI to scape special characters?                         */
      if (!repository_uri_is_valid (uri))
        {
          irritate_user (_("ai_ib_enter_web_address"));
          gtk_widget_grab_focus (c->uri_entry);
          g_free (uri);
          return;
        }

      char *dist = g_strstrip (g_strdup (gtk_entry_get_text
                                         (GTK_ENTRY (c->dist_entry))));
      char *comps = g_strstrip (g_strdup (gtk_entry_get_text
                                          (GTK_ENTRY (c->components_entry))));
      bool disabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
						    (c->disabled_button));

      if (all_whitespace (comps))
        {
          /* Ensure there's a trailing '/' at the end of dist */
          char *tmp_dist = dist;

          /* Append the '/' character when needed */
          if (all_whitespace (dist))
            dist = g_strdup ("/");
          else if (!g_str_has_suffix (dist, "/"))
            dist = g_strconcat (tmp_dist, "/", NULL);

          /* Free tmp_dist if new memory was allocated for dist */
          if (dist != tmp_dist)
            g_free (tmp_dist);
        }
      else if (!all_whitespace (dist))
        {
          /* Remove the trailing '/' at the end of dist, if present */
          char *suffix = NULL;
          if (g_str_has_suffix (dist, "/") && (suffix = g_strrstr (dist, "/")))
            *suffix = '\0';
        }

      if (all_whitespace (dist))
        dist = NULL;

      reset_cat_list (c->cat_dialog);
      if (c->isnew)
        xexp_append_1 (c->cat_dialog->catalogues_xexp, c->catalogue);
      set_catalogue_name (c->catalogue, name);
      xexp_aset_bool (c->catalogue, "disabled", disabled);
      xexp_aset_text (c->catalogue, "components", comps);
      xexp_aset_text (c->catalogue, "dist", dist);
      xexp_aset_text (c->catalogue, "uri", uri);
      set_cat_list (c->cat_dialog, &c->cat_dialog->selected_iter);
      c->cat_dialog->dirty = true;

      g_free (uri);
      g_free (dist);
      g_free (comps);
    }
  else if (c->isnew)
    {
      xexp_free (c->catalogue);
      if (!strcmp (gtk_entry_get_text (GTK_ENTRY (c->uri_entry)), "matrix"))
	should_ask_the_pill_question = true;
    }

  delete c;
 
  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (should_ask_the_pill_question)
    ask_the_pill_question ();
}

static void
show_cat_edit_dialog (cat_dialog_closure *cat_dialog, xexp *catalogue,
		      bool isnew, bool readonly)
{
  GtkWidget *dialog, *vbox, *caption, *scrolledw;
  GtkSizeGroup *group;

  if (!xexp_is_list (catalogue) || !xexp_is (catalogue, "catalogue"))
    {
      irritate_user (_("ai_ib_unable_edit"));
      return;
    }

  cat_edit_closure *c = new cat_edit_closure;

  c->isnew = isnew;
  c->readonly = readonly;
  c->cat_dialog = cat_dialog;
  c->catalogue = catalogue;
  
  const char *title;
  if (c->readonly)
    title = _("ai_ti_catalogue_details");
  else if (isnew)
    title = _("ai_ti_new_repository");
  else
    title = _("ai_ti_edit_repository");

  dialog = gtk_dialog_new_with_buttons (title, NULL,
            GTK_DIALOG_MODAL,
            _("ai_bd_new_repository_ok"),
            GTK_RESPONSE_OK,
            _("ai_bd_new_repository_cancel"),
            GTK_RESPONSE_CANCEL,
            NULL);

  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

  if (!c->readonly)
    vbox = GTK_DIALOG (dialog)->vbox;
  else
    {
      /* Use an scrollbar for the read-only version, and use a vbox
	 with a 3px padding to make it look like to the other one */
      vbox = gtk_vbox_new (FALSE, 3);

      scrolledw = gtk_scrolled_window_new (NULL, NULL);
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledw),
				      GTK_POLICY_AUTOMATIC,
				      GTK_POLICY_NEVER);
      gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolledw), vbox);
      gtk_box_pack_start_defaults (GTK_BOX (GTK_DIALOG (dialog)->vbox), scrolledw);
    }

  group = GTK_SIZE_GROUP (gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL));

  const char *current_name = catalogue_name (catalogue);
  c->name_entry = add_entry (vbox, group,
			     _("ai_fi_new_repository_name"),
			     current_name, NULL, true, c->readonly, true);

  c->uri_entry = add_entry (vbox, group,
			    _("ai_fi_new_repository_web_address"),
			    xexp_aref_text (catalogue, "uri"),
			    NULL, false, c->readonly, true);

  c->dist_entry = add_entry (vbox, group,
			     _("ai_fi_new_repository_distribution"),
			     xexp_aref_text (catalogue, "dist"),
			     NULL, false, c->readonly, true);

  c->components_entry = add_entry (vbox, group,
				   _("ai_fi_new_repository_component"),
				   xexp_aref_text (catalogue, "components"),
				   NULL, false, c->readonly, false);

  c->disabled_button = gtk_check_button_new ();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (c->disabled_button),
                                xexp_aref_bool (catalogue, "disabled"));
  caption = hildon_caption_new (group,
                                _("ai_fi_new_repository_disabled"),
                                c->disabled_button,
                                NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start_defaults (GTK_BOX (vbox), caption);

  /* XXX- when the dialog is shown from an .install file. This is a
     quickfix.  For this case we'll need a three state dialog:
     i) editable; ii) enable/disable; iii) read only
  */
  gtk_widget_set_sensitive (c->disabled_button, c->cat_dialog != NULL);

  
  gtk_widget_set_usize (dialog, 650, -1);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (cat_edit_response), c);
  gtk_widget_show_all (dialog);
  g_object_unref (group);
}

static catcache *
get_selected_catalogue (cat_dialog_closure *c)
{
  GtkTreeSelection *selection = gtk_tree_view_get_selection (c->tree);
  GtkTreeIter iter;
  GtkTreeModel *model;
  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      catcache *c;
      gtk_tree_model_get (model, &iter, 0, &c, -1);
      return c;
    }

  return NULL;
}

static void
cat_icon_func (GtkTreeViewColumn *column,
	       GtkCellRenderer *cell,
	       GtkTreeModel *model,
	       GtkTreeIter *iter,
	       gpointer data)
{
  static GdkPixbuf *ok_browser_pixbuf = NULL;
  static GdkPixbuf *fail_browser_pixbuf = NULL;
  GdkPixbuf *browser_pixbuf = NULL;

  cat_dialog_closure *cd = (cat_dialog_closure *)data;
  catcache *c = NULL;

  gtk_tree_model_get (model, iter, 0, &c, -1);

  /* Load icon for successfully refreshed catalogues */
  if (ok_browser_pixbuf == NULL)
    {
      GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();

      ok_browser_pixbuf =
	gtk_icon_theme_load_icon (icon_theme,
				  "qgn_list_browser",
				  26,
				  GtkIconLookupFlags (0),
				  NULL);
    }

  /* Load icon for catalogues which failed while refreshing */
  if (fail_browser_pixbuf == NULL)
    {
      GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();

      /* FIXME: use the real icon for failing catalogues
	 when available in the UI specs */
      fail_browser_pixbuf =
	gtk_icon_theme_load_icon (icon_theme,
				  "qgn_list_gene_invalid",
				  26,
				  GtkIconLookupFlags (0),
				  NULL);
    }

  /* Select icon to show when showing the application catalogue dialog */
  if (!cd->show_only_errors && c->refresh_failed)
    browser_pixbuf = fail_browser_pixbuf;
  else
    browser_pixbuf = ok_browser_pixbuf;

  g_object_set (cell,
		"pixbuf", (c && c->foreign)? NULL : browser_pixbuf,
		"sensitive", c && c->enabled,
		NULL);
}

static void
cat_text_func (GtkTreeViewColumn *column,
	       GtkCellRenderer *cell,
	       GtkTreeModel *model,
	       GtkTreeIter *iter,
	       gpointer data)
{
  cat_dialog_closure *cd = (cat_dialog_closure *)data;
  catcache *c = NULL;
  gchar *full_name = NULL;

  gtk_tree_model_get (model, iter, 0, &c, -1);

  /* set 'failed catalogue' suffix when needed */
  if (!cd->show_only_errors && c->refresh_failed)
    {
      full_name = g_strdup_printf("%s - %s",
				  c->name,
				  _("ai_ia_failed_catalogue"));
    }
  else
    full_name = g_strdup (c->name);

  /* set full text for element */
  if (c != NULL
      && c->detail
      && cd->selected_cat == c)
    {
      gchar *markup = NULL;

      markup = g_markup_printf_escaped ("%s\n<small>%s</small>",
					full_name, c->detail);
      g_object_set (cell, "markup", markup, NULL);

      g_free (markup);
    }
  else
    {
	g_object_set (cell, "text", c? full_name : NULL, NULL);
    }

  g_free (full_name);
}

static void
cat_row_activated (GtkTreeView *treeview,
		   GtkTreePath *path,
		   GtkTreeViewColumn *column,
		   gpointer data)
{
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeIter iter;

  if (gtk_tree_model_get_iter (model, &iter, path))
    {
      catcache *c;
      gtk_tree_model_get (model, &iter, 0, &c, -1);
      if (c == NULL)
        return;

      show_cat_edit_dialog (c->cat_dialog, c->catalogue_xexp,
			    false, c->readonly);
    }
}

static void
emit_row_changed (GtkTreeModel *model, GtkTreeIter *iter)
{
  GtkTreePath *path;

  path = gtk_tree_model_get_path (model, iter);
  g_signal_emit_by_name (model, "row-changed", path, iter);
  gtk_tree_path_free (path);
}

static void
cat_selection_changed (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  cat_dialog_closure *c = (cat_dialog_closure *)data;

  catcache *old_selected = c->selected_cat;
  catcache *new_selected;

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    gtk_tree_model_get (model, &iter, 0, &new_selected, -1);
  else
    new_selected = NULL;

  c->selected_cat = new_selected;
  if (old_selected)
    emit_row_changed (model, &c->selected_iter);
  c->selected_iter = iter;

  if (new_selected)
    {
      emit_row_changed (model, &iter);
      gtk_widget_set_sensitive (c->edit_button,
                                 !new_selected->foreign);
      if (!c->show_only_errors)
        gtk_widget_set_sensitive (c->delete_button,
                                  !new_selected->readonly);
    }
  else
    {
      gtk_widget_set_sensitive (c->edit_button, FALSE);
      if (!c->show_only_errors)
        gtk_widget_set_sensitive (c->delete_button, FALSE);
    }
}

static char *
render_catalogue_errors (xexp *cat)
{
  xexp *errors = xexp_aref (cat, "errors");
  if (errors == NULL || xexp_first (errors) == NULL)
    return NULL;

  GString *report = g_string_new ("");

  for (xexp *err = xexp_first (errors); err; err = xexp_rest (err))
    {
      g_string_append_printf (report, "%s\n %s",
			      xexp_aref_text (err, "uri"),
			      xexp_aref_text (err, "msg"));
      if (xexp_rest (err))
	g_string_append (report, "\n");
    }

  char *str = report->str;
  g_string_free (report, 0);
  return str;
}

static catcache *
make_catcache_from_xexp (cat_dialog_closure *c, xexp *x)
{
  catcache *cat = new catcache;
  cat->catalogue_xexp = x;
  cat->cat_dialog = c;
  cat->refresh_failed = false;
  if (xexp_is (x, "catalogue") && xexp_is_list (x))
    {
      xexp *errors = NULL;

      cat->enabled = !xexp_aref_bool (x, "disabled");
      if (is_package_catalogue (x))
        cat->readonly = true;
      else
        cat->readonly = false;
      cat->foreign = false;
      cat->name = catalogue_name (x);
      cat->detail = render_catalogue_errors (x);

      /* Check for errors during the last refresh */
      errors = xexp_aref (x, "errors");
      if (errors != NULL && xexp_first (errors) != NULL)
        cat->refresh_failed = true;
    }
  else if (xexp_is (x, "source") && xexp_is_text (x))
    {
      cat->enabled = true;
      cat->readonly = true;
      cat->foreign = true;
      cat->name = xexp_text (x);
      cat->detail = NULL;
    }
  else
    {
      delete cat;
      cat = NULL;
    }
  return cat;
}

static void
reset_cat_list (cat_dialog_closure *c)
{
  catcache *next;
  for (catcache *cat = c->caches; cat; cat = next)
    {
      next = cat->next;
      g_free (cat->detail);
      delete cat;
    }
  c->caches = NULL;
  c->selected_cat = NULL;
}

static bool
cat_has_errors (xexp *cat)
{
  xexp *errors = (xexp_is (cat, "catalogue")
		  ? xexp_aref (cat, "errors")
		  : NULL);

  return errors != NULL && xexp_first (errors) != NULL;
}

static int
cat_compare (xexp *cat1, xexp *cat2)
{
  int w1 = (xexp_is (cat1, "catalogue")
	    ? xexp_aref_int (cat1, "sort-weight", 0)
	    : -2000);
  int w2 = (xexp_is (cat2, "catalogue")
	    ? xexp_aref_int (cat2, "sort-weight", 0)
	    : -2000);

  if (cat_has_errors (cat1))
    w1 -= 1000;

  if (cat_has_errors (cat2))
    w2 -= 1000;

  if (w1 > w2)
    return -1;
  else if (w1 == w2)
    {
      const char *n1 = catalogue_name (cat1);
      const char *n2 = catalogue_name (cat2);
      
      return strcmp (n1, n2);
    }
  else
    return 1;
}

static void
set_cat_list (cat_dialog_closure *c, GtkTreeIter *iter_to_select)
{
  gint position = 0;
  catcache **catptr = &c->caches;
  GtkTreePath *path_to_select = NULL;

  if (c->catalogues_xexp == NULL)
    return;

  xexp_list_sort (c->catalogues_xexp, cat_compare);

  /* Retrieve path to select if needed (used inside the loop) */
  if (iter_to_select)
    {
      path_to_select = gtk_tree_model_get_path (GTK_TREE_MODEL (c->store),
						iter_to_select);
    }

  /* If it exists, clear previous list store */
  if (c->store)
    gtk_list_store_clear (c->store);

  for (xexp *catx = xexp_first (c->catalogues_xexp); catx;
       catx = xexp_rest (catx))
    {
      if (c->show_only_errors && !cat_has_errors (catx))
	continue;

      catcache *cat = make_catcache_from_xexp (c, catx);
      if (cat)
	{
	  *catptr = cat;
	  catptr = &cat->next;
	  GtkTreeIter iter;
	  gtk_list_store_insert_with_values (c->store, &iter,
					     position,
					     0, cat,
					     -1);
	  /* Select first item by default */
 	  if (position == 0)
	    {
	      c->selected_cat = cat;
	      c->selected_iter = iter;
	    }
	  else if (path_to_select)
	    {
	      /* Select specified item if available */

	      GtkTreePath *current_path =
		gtk_tree_model_get_path (GTK_TREE_MODEL (c->store),
					 &iter);

	      /* Compare current iter with iter_to_select */
	      if (current_path &&
		  !gtk_tree_path_compare (current_path, path_to_select))
		{
		  c->selected_cat = cat;
		  c->selected_iter = iter;
		}

	      gtk_tree_path_free (current_path);
	    }

	  position += 1;
	}
    }

  /* Set the focus in the right list element */
  GtkTreeSelection *tree_selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (c->tree));
  gtk_tree_selection_select_iter (tree_selection, &c->selected_iter);

  gtk_widget_grab_focus (GTK_WIDGET (c->tree));

  *catptr = NULL;

  gtk_tree_path_free (path_to_select);
}

static void
refresh_cat_list (cat_dialog_closure *c)
{
  reset_cat_list (c);
  set_cat_list (c, NULL);
}

static GtkWidget *
make_cat_list (cat_dialog_closure *c)
{
  GtkCellRenderer *renderer;
  GtkWidget *scroller;

  c->store = gtk_list_store_new (1, GTK_TYPE_POINTER);
  c->tree =
    GTK_TREE_VIEW (gtk_tree_view_new_with_model (GTK_TREE_MODEL (c->store)));

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (renderer, "yalign", 0.0, NULL);
  gtk_tree_view_insert_column_with_data_func (c->tree,
					      -1,
					      NULL,
					      renderer,
					      cat_icon_func,
					      c,
					      NULL);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "yalign", 0.0, NULL);
  gtk_tree_view_insert_column_with_data_func (c->tree,
					      -1,
					      NULL,
					      renderer,
					      cat_text_func,
					      c,
					      NULL);

  g_signal_connect (c->tree, "row-activated", 
		    G_CALLBACK (cat_row_activated), c);

  g_signal_connect
    (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (c->tree))),
     "changed",
     G_CALLBACK (cat_selection_changed), c);

  refresh_cat_list (c);

  scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scroller), GTK_WIDGET (c->tree));

  return scroller;
}

struct remove_cat_clos {
  cat_dialog_closure *cat_dialog;
  xexp *catalogue;
};

static void
remove_cat_cont (bool res, void *data)
{
  remove_cat_clos *c = (remove_cat_clos *)data;
  cat_dialog_closure *d = c->cat_dialog;
  
  if (res)
    {
      reset_cat_list (d);
      xexp_del (d->catalogues_xexp, c->catalogue);
      set_cat_list (d, NULL);
      d->dirty = true;
    }

  delete c;
}

#define REPO_RESPONSE_NEW    1
#define REPO_RESPONSE_EDIT   2
#define REPO_RESPONSE_REMOVE 3

static void
cat_response (GtkDialog *dialog, gint response, gpointer clos)
{
  cat_dialog_closure *c = (cat_dialog_closure *)clos;
  
  if (response == REPO_RESPONSE_NEW)
    {
      xexp *x = xexp_list_new ("catalogue");
      xexp_aset_text (x, "name", "");
      xexp_aset_text (x, "uri", "http://");
      xexp_aset_text (x, "components", "user");
      show_cat_edit_dialog (c, x, true, false);
      return;
    }

  if (response == REPO_RESPONSE_EDIT)
    {
      catcache *cat = get_selected_catalogue (c);
      if (cat == NULL)
        return;

      show_cat_edit_dialog (c, cat->catalogue_xexp, false, cat->readonly);
      return;
    }

  if (response == REPO_RESPONSE_REMOVE)
    {
      catcache *cat = get_selected_catalogue (c);
      if (cat == NULL)
	return;

      char *text = g_strdup_printf (_("ai_nc_remove_repository"), cat->name);
      remove_cat_clos *rc = new remove_cat_clos;
      rc->cat_dialog = c;
      rc->catalogue = cat->catalogue_xexp;
      ask_yes_no (text, remove_cat_cont, rc);
      g_free (text);
    }

  if (response == GTK_RESPONSE_CLOSE ||
      response == GTK_RESPONSE_DELETE_EVENT)
    {
      reset_cat_list (c);
      pop_dialog (GTK_WIDGET (dialog));
      gtk_widget_destroy (GTK_WIDGET (dialog));

      c->cont (c->dirty, c->data);
      current_cat_dialog_clos = NULL;
      hide_updating ();

      delete c;
    }
}

static void
insensitive_cat_delete_press (GtkButton *button, gpointer data)
{
  cat_dialog_closure *c = (cat_dialog_closure *)data;

  GtkTreeModel *model;
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (c->tree),
				       &model, &iter))
    irritate_user (_("ai_ni_unable_remove_repository"));
}

static void
insensitive_cat_edit_press (GtkButton *button, gpointer data)
{
  cat_dialog_closure *c = (cat_dialog_closure *)data;

  GtkTreeModel *model;
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (c->tree),
                                       &model, &iter))
    irritate_user (_("ai_ib_unable_edit"));
}

static void
scd_get_catalogues_reply (xexp *catalogues, void *data)
{
  cat_dialog_closure *c = (cat_dialog_closure *)data;
  scdf_clos *f_clos = NULL;

  if ((c == NULL) || (c != current_cat_dialog_clos) || c->showing_catalogues)
    return;

  f_clos = (scdf_clos *)c->data;
  c->catalogues_xexp = catalogues;
  f_clos->catalogues = catalogues;

  c->showing_catalogues = true;
  refresh_cat_list (c);

  gtk_widget_set_sensitive (c->new_button, TRUE);

  /* Prevent the 'Updating' banner from being shown */
  prevent_updating ();
}


static void
ecu_reply (bool ignore, void *data)
{
  g_return_if_fail (data != NULL);

  /* Show the 'Updating' banner */
  allow_updating ();
  show_updating ();

  get_catalogues (scd_get_catalogues_reply, data);
}

static void
ensure_cache_updated (cat_dialog_closure *c)
{
  if (!is_package_cache_updated ())
    {
      /* Force a refresh if package cache is not up-to-date */
      refresh_package_cache_without_user (NULL, ecu_reply, c);
    }
  else
    {
      /* Show the 'Updating' banner */
      allow_updating ();
      show_updating ();

      /* Retrieve the catalogues information */
      get_catalogues (scd_get_catalogues_reply, c);
    }
}

void
show_catalogue_dialog (xexp *catalogues,
		       bool show_only_errors,
		       void (*cont) (bool changed, void *data),
		       void *data)
{
  cat_dialog_closure *c = new cat_dialog_closure;
  GtkWidget *dialog = NULL;

  c->caches = NULL;
  c->catalogues_xexp = catalogues;
  c->dirty = false;
  c->show_only_errors = show_only_errors;
  c->showing_catalogues = false;
  c->selected_cat = NULL;
  c->cont = cont;
  c->data = data;

  current_cat_dialog_clos = c;

  dialog = gtk_dialog_new ();

  if (show_only_errors)
    gtk_window_set_title (GTK_WINDOW (dialog), _("ai_ti_failed_repositories"));
  else
    gtk_window_set_title (GTK_WINDOW (dialog), _("ai_ti_repository"));
  
  push_dialog (dialog);
  
  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

  if (!show_only_errors)
    c->new_button = 
      gtk_dialog_add_button (GTK_DIALOG (dialog), 
                             _("ai_bd_repository_new"), REPO_RESPONSE_NEW);

  c->edit_button = 
    gtk_dialog_add_button (GTK_DIALOG (dialog), 
			   _("ai_bd_repository_edit"), REPO_RESPONSE_EDIT);

  if (!show_only_errors)
    c->delete_button =
      gtk_dialog_add_button (GTK_DIALOG (dialog), 
			     _("ai_bd_repository_delete"), REPO_RESPONSE_REMOVE);

  respond_on_escape (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
  
  gtk_widget_set_sensitive (c->new_button, FALSE);
  gtk_widget_set_sensitive (c->edit_button, FALSE); 
  if (!show_only_errors)
    {
      gtk_widget_set_sensitive (c->delete_button, FALSE);
      g_signal_connect (c->delete_button, "insensitive_press",
			G_CALLBACK (insensitive_cat_delete_press), c);

      g_signal_connect (c->edit_button, "insensitive_press",
                        G_CALLBACK (insensitive_cat_edit_press), c);
    }

  gtk_box_pack_start_defaults (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			       make_cat_list (c));

  g_signal_connect (dialog, "response",
		    G_CALLBACK (cat_response), c);

  gtk_widget_set_usize (dialog, 600, 300);
  
  gtk_widget_show_all (dialog);

  /* Make sure the cache is up-to-date first */
  ensure_cache_updated (c);
}

/* Adding catalogues 
 */

struct add_catalogues_closure {
  xexp *catalogues;
  xexp *cur;
  xexp *rest;
  bool ask, update;

  bool catalogues_changed;

  void (*cont) (bool res, void *data);
  void *data;
};

static void add_catalogues_cont_1 (xexp *catalogues, void *data);
static void add_catalogues_cont_2 (add_catalogues_closure *c);
static void add_catalogues_cont_3_add (bool res, void *data);
static void add_catalogues_cont_3_enable (bool res, void *data);
static void add_catalogues_cont_3 (bool res, bool enable, void *data);
static void add_catalogues_cont_4 (bool keep_going, void *data);

static void
add_catalogues_cont_3_add (bool res, void *data)
{
  add_catalogues_cont_3 (res, false, data);
}

static void
add_catalogues_cont_3_enable (bool res, void *data)
{
  add_catalogues_cont_3 (res, true, data);
}

static void
add_catalogues_cont_3 (bool res, bool enable, void *data)
{
  add_catalogues_closure *c = (add_catalogues_closure *)data;

  if (res)
    {
      /* Add or enable it.
       */
      if (enable)
	{
	  g_assert (c->cur);
	  xexp_aset_bool (c->cur, "disabled", false);
	}
      else 
	{
	  if (c->cur)
	    xexp_del (c->catalogues, c->cur);
	  xexp_append_1 (c->catalogues, xexp_copy (c->rest));
	}

      c->catalogues_changed = true;

      /* Move to next
       */
      c->rest = xexp_rest (c->rest);
      add_catalogues_cont_2 (c);
    }
  else
    {
      /* User cancelled.  If this operation is for updating the
	 catalogues before installing a package, we abort now.
	 Otherwise, the user can still decide whether to add the
	 remaining catalogues.
       */
      if (c->update)
	{
	  xexp_free (c->catalogues);
	  c->cont (false, c->data);
	  delete c;
	}
      else
	{
	  c->rest = xexp_rest (c->rest);
	  add_catalogues_cont_2 (c);
	}
    }
}

static void
add_catalogues_details (void *data)
{
  add_catalogues_closure *c = (add_catalogues_closure *)data;
  show_cat_edit_dialog (NULL, c->rest, true, true);
}

static void
add_catalogues_cont_4 (bool keep_going, void *data)
{
  add_catalogues_closure *c = (add_catalogues_closure *)data;
  xexp_free (c->catalogues);
  c->cont (keep_going, c->data);
  delete c;
}

static void
add_catalogues_cont_2 (add_catalogues_closure *c)
{
  if (c->rest == NULL)
    {
      /* We want to refresh the cache every time for an 'update'
	 operation since we really want it to be uptodate now even if
	 we didn't make any changes to the catalogue configuration.
      */
      if (c->catalogues_changed || c->update)
	set_catalogues_and_refresh (c->catalogues,
				    (c->update
				     ? _("ai_nw_preparing_installation")
				     : NULL),
				    add_catalogues_cont_4, c);
      else
	add_catalogues_cont_4 (true, c);
    }
  else
    {
      void (*cont) (bool res, void *data);

      c->cur = find_catalogue (c->catalogues, c->rest);

      if (!c->update || c->cur == NULL)
	{
	  // New version should be added
	  cont = add_catalogues_cont_3_add;
	}
      else if (c->cur && xexp_aref_bool (c->cur, "disabled"))
	{
	  // Old version should be enabled
	  cont = add_catalogues_cont_3_enable;
	}
      else
	cont = NULL;

      if (cont)
	{
	  /* The catalogue is new or needs to be enabled.  If wanted,
	     ask the user whether to add it.
	   */

	  if (c->ask)
	    {
	      char *str;
	      const char *name = catalogue_name (c->rest);

	      if (cont == add_catalogues_cont_3_enable)
		str = g_strdup_printf ("%s\n%s",
				       _("ai_ia_add_catalogue_enable"),
				       name);
	      else if (c->update)
		str = g_strdup_printf (_("ai_ia_add_catalogue_text"),
				       name);
	      else
		str = g_strdup_printf (_("ai_ia_add_catalogue_text2"),
				       name);
	    
	      ask_yes_no_with_arbitrary_details (_("ai_ti_add_catalogue"),
						 str,
						 cont,
						 add_catalogues_details,
						 c);
	      g_free (str);
	    }
	  else
	    cont (true, c);
	}
      else
	{
	  /* Nothing to be done for this catalogue, move to the next.
	   */
	  c->rest = xexp_rest (c->rest);
	  add_catalogues_cont_2 (c);
	}
    }
}

static void
add_catalogues_cont_1 (xexp *catalogues, void *data)
{
  add_catalogues_closure *c = (add_catalogues_closure *)data;

  if (catalogues == NULL)
    {
      c->cont (false, c->data);
      delete c;
    }
  else
    {
      c->catalogues = catalogues;
      add_catalogues_cont_2 (c);
    }
}

void
add_catalogues (xexp *catalogues,
		bool ask, bool update,
		void (*cont) (bool res, void *data),
		void *data)
{
  add_catalogues_closure *c = new add_catalogues_closure;
  c->cur = NULL;
  c->rest = xexp_first (catalogues);
  c->ask = ask;
  c->update = update;
  c->catalogues_changed = false;
  c->cont = cont;
  c->data = data;

  get_catalogues (add_catalogues_cont_1, c);
}

GString *
render_catalogue_report (xexp *catalogue_report)
{
  if (catalogue_report == NULL)
    return g_string_new (_("ai_ni_operation_failed"));

  GString *report = g_string_new ("");

  for (xexp *cat = xexp_first (catalogue_report); cat; cat = xexp_rest (cat))
    {
      xexp *errors = xexp_aref (cat, "errors");
      if (errors == NULL || xexp_first (errors) == NULL)
	continue;

      g_string_append_printf (report, "%s:\n", catalogue_name (cat));
      for (xexp *err = xexp_first (errors); err; err = xexp_rest (err))
	g_string_append_printf (report, "  %s\n   %s\n",
				xexp_aref_text (err, "uri"),
				xexp_aref_text (err, "msg"));
    }

  return report;
}

xexp *
get_failed_catalogues (xexp *catalogue_report)
{
  xexp *failed_cat_list = xexp_list_new ("catalogues");

  if (catalogue_report == NULL)
    return failed_cat_list;

  for (xexp *cat = xexp_first (catalogue_report); cat; cat = xexp_rest (cat))
    {
      xexp *errors = xexp_aref (cat, "errors");
      if (errors == NULL || xexp_first (errors) == NULL)
	continue;

      xexp_append_1 (failed_cat_list, xexp_copy (cat));
    }

  return failed_cat_list;
}
