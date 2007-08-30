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

#include <errno.h>
#include <string.h>
#include <libintl.h>

#include <gtk/gtk.h>
#include <hildon/hildon-sort-dialog.h>
#include <hildon/hildon-caption.h>

#include "settings.h"
#include "util.h"
#include "log.h"
#include "apt-worker-client.h"

#define _(x) gettext (x)

int  update_interval_index = UPDATE_INTERVAL_WEEK;
int  package_sort_key = SORT_BY_NAME;
int  package_sort_sign = 1;

bool clean_after_install = true;
bool assume_connection = false;
bool break_locks = true;
bool red_pill_mode = false;
bool red_pill_show_deps = true;
bool red_pill_show_all = true;
bool red_pill_show_magic_sys = true;
bool red_pill_include_details_in_log = false;

#define SETTINGS_FILE ".osso/hildon-application-manager"

static FILE *
open_user_file (const char *file, const char *mode)
{
  gchar *name = g_strdup_printf ("%s/%s", getenv ("HOME"), file);
  FILE *f = fopen (name, mode);
  if (f == NULL && errno != ENOENT)
    add_log ("%s: %s\n", name, strerror (errno));
  g_free (name);
  return f;
}

void
load_settings ()
{
  /* XXX - we should probably use XML for this.
   */

  FILE *f = open_user_file (SETTINGS_FILE, "r");
  if (f)
    {
      char *line = NULL;
      size_t len = 0;
      ssize_t n;
      while ((n = getline (&line, &len, f)) != -1)
	{
	  int val;

	  if (n > 0 && line[n-1] == '\n')
	    line[n-1] = '\0';

	  if (sscanf (line, "clean-after-install %d", &val) == 1)
	    clean_after_install = val;
	  else if (sscanf (line, "update-interval-index %d", &val) == 1)
	    update_interval_index = val;
	  else if (sscanf (line, "package-sort-key %d", &val) == 1)
	    package_sort_key = val;
	  else if (sscanf (line, "package-sort-sign %d", &val) == 1)
	    package_sort_sign = val;
	  else if (sscanf (line, "break-locks %d", &val) == 1)
	    break_locks = val;
	  else if (sscanf (line, "red-pill-mode %d", &val) == 1)
	    red_pill_mode = val;
	  else if (sscanf (line, "red-pill-show-deps %d", &val) == 1)
	    red_pill_show_deps = val;
	  else if (sscanf (line, "red-pill-show-all %d", &val) == 1)
	    red_pill_show_all = val;
	  else if (sscanf (line, "red-pill-show-magic-sys %d", &val) == 1)
	    red_pill_show_magic_sys = val;
	  else if (sscanf (line, "red-pill-include-details-in-log %d", &val)
		   == 1)
	    red_pill_include_details_in_log = val;
	  else if (sscanf (line, "assume-connection %d", &val) == 1)
	    assume_connection = val;
	  else
	    add_log ("Unrecognized configuration line: '%s'\n", line);
	}
      free (line);
      fclose (f);
    }
  
  /* XML - only kidding.
   */
}

void
save_settings ()
{
  FILE *f = open_user_file (SETTINGS_FILE, "w");
  if (f)
    {
      fprintf (f, "clean-after-install %d\n", clean_after_install);
      fprintf (f, "update-interval-index %d\n", update_interval_index);
      fprintf (f, "package-sort-key %d\n", package_sort_key);
      fprintf (f, "package-sort-sign %d\n", package_sort_sign);
      fprintf (f, "break-locks %d\n", break_locks);
      fprintf (f, "red-pill-mode %d\n", red_pill_mode);
      fprintf (f, "red-pill-show-deps %d\n", red_pill_show_deps);
      fprintf (f, "red-pill-show-all %d\n", red_pill_show_all);
      fprintf (f, "red-pill-show-magic-sys %d\n", red_pill_show_magic_sys);
      fprintf (f, "red-pill-include-details-in-log %d\n", 
	       red_pill_include_details_in_log);
      fprintf (f, "assume-connection %d\n", assume_connection);
      fclose (f);
    }
}

#define MAX_BOOLEAN_OPTIONS 10

struct settings_closure {
  GtkWidget *update_combo;

  int n_booleans;
  GtkWidget *boolean_btn[MAX_BOOLEAN_OPTIONS];
  bool *boolean_var[MAX_BOOLEAN_OPTIONS];

  settings_closure () { n_booleans = 0; }
};

static void
make_boolean_option (settings_closure *c, 
		     GtkWidget *box, GtkSizeGroup *group,
		     const char *text, bool *var)
{
  GtkWidget *caption, *btn;

  btn = gtk_check_button_new ();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), *var);
  caption = hildon_caption_new (group, text, btn,
				NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start (GTK_BOX (box), caption, FALSE, FALSE, 0);

  int i = c->n_booleans++;
  if (c->n_booleans > MAX_BOOLEAN_OPTIONS)
    abort ();

  c->boolean_btn[i] = btn;
  c->boolean_var[i] = var;
}

static GtkWidget *
make_settings_tab (settings_closure *c)
{
  GtkWidget *tab, *combo;
  GtkSizeGroup *group;

  group = GTK_SIZE_GROUP (gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL));

  combo = gtk_combo_box_new_text ();
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo),
			     _("ai_va_settings_once_session"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo),
			     _("ai_va_settings_once_week"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo),
			     _("ai_va_settings_once_month"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (combo),
			     _("ai_va_settings_only_manually"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), update_interval_index);

  c->update_combo = combo;

  tab = hildon_caption_new (group, _("ai_fi_settings_update_list"),
			    combo,
			    NULL, HILDON_CAPTION_OPTIONAL);
  if (red_pill_mode)
    {
      GtkWidget *caption = tab;

      tab = gtk_vbox_new (FALSE, 0);
      gtk_box_pack_start (GTK_BOX (tab), caption, FALSE, FALSE, 0);

      make_boolean_option (c, tab, group,
			   "Clean apt cache", &clean_after_install);
      make_boolean_option (c, tab, group,
			   "Assume net connection", &assume_connection);
      make_boolean_option (c, tab, group,
			   "Break locks", &break_locks);
      make_boolean_option (c, tab, group,
			   "Show dependencies", &red_pill_show_deps);
      make_boolean_option (c, tab, group,
			   "Show all packages", &red_pill_show_all);
      make_boolean_option (c, tab, group,
			   "Show magic system package",
			   &red_pill_show_magic_sys);
      make_boolean_option (c, tab, group,
			   "Include package details in log",
			   &red_pill_include_details_in_log);
    }

  g_object_unref (group);

  return tab;
}

static void
settings_dialog_response (GtkDialog *dialog, gint response, gpointer clos)
{
  settings_closure *c = (settings_closure *)clos;

  if (response == GTK_RESPONSE_OK)
    {
      update_interval_index =
	gtk_combo_box_get_active (GTK_COMBO_BOX (c->update_combo));

      for (int i = 0; i < c->n_booleans; i++)
	*(c->boolean_var[i]) =
	  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (c->boolean_btn[i]));

      save_settings ();
      if (red_pill_mode)
	get_package_list (APTSTATE_DEFAULT);
    }

  delete c;

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));  

  end_interaction_flow ();
}

void
show_settings_dialog_flow ()
{
  if (start_interaction_flow ())
    {
      GtkWidget *dialog;
      settings_closure *c = new settings_closure;
      
      dialog = gtk_dialog_new_with_buttons (_("ai_ti_settings"),
					    NULL,
					    GTK_DIALOG_MODAL,
					    _("ai_bd_settings_ok"),
					    GTK_RESPONSE_OK,
					    _("ai_bd_settings_cancel"),
					    GTK_RESPONSE_CANCEL,
					    NULL);
      push_dialog (dialog);
      gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
      set_dialog_help (dialog, AI_TOPIC ("settings"));
      
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG(dialog)->vbox),
			  make_settings_tab (c),
			  FALSE, FALSE, 20);
      
      g_signal_connect (dialog, "response",
			G_CALLBACK (settings_dialog_response),
			c);
      gtk_widget_show_all (dialog);
    }
}

static void
sort_settings_dialog_response (GtkDialog *dialog, gint response, gpointer clos)
{
  if (response == GTK_RESPONSE_OK)
    {
      package_sort_key =
	hildon_sort_dialog_get_sort_key (HILDON_SORT_DIALOG (dialog));
      if (hildon_sort_dialog_get_sort_order (HILDON_SORT_DIALOG (dialog))
	  == GTK_SORT_ASCENDING)
	package_sort_sign = 1;
      else
	package_sort_sign = -1;
      save_settings ();
      sort_all_packages ();
    }

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  end_interaction_flow ();
}

void
show_sort_settings_dialog_flow ()
{
  if (start_interaction_flow ())
    {
      GtkWidget *dialog;

      dialog = hildon_sort_dialog_new (NULL);
      push_dialog (dialog);

      hildon_sort_dialog_add_sort_key (HILDON_SORT_DIALOG (dialog),
				       _("ai_va_sort_name"));
      hildon_sort_dialog_add_sort_key (HILDON_SORT_DIALOG (dialog),
				       _("ai_va_sort_version"));
      hildon_sort_dialog_add_sort_key (HILDON_SORT_DIALOG (dialog),
				       _("ai_va_sort_size"));
      
      hildon_sort_dialog_set_sort_key (HILDON_SORT_DIALOG (dialog),
				       package_sort_key);
      hildon_sort_dialog_set_sort_order (HILDON_SORT_DIALOG (dialog),
					 (package_sort_sign > 0
					  ? GTK_SORT_ASCENDING
					  : GTK_SORT_DESCENDING));
      
      g_signal_connect (dialog, "response",
			G_CALLBACK (sort_settings_dialog_response),
			NULL);
      gtk_widget_show_all (dialog);
    }
}

/* Persistent state
 */

int  last_update = 0;
bool fullscreen_toolbar = true;
bool normal_toolbar = true;

#define STATE_FILE ".hildon-application-manager-state"

void
load_state ()
{
  FILE *f = open_user_file (STATE_FILE, "r");
  if (f)
    {
      char *line = NULL;
      size_t len = 0;
      ssize_t n;
      while ((n = getline (&line, &len, f)) != -1)
	{
	  int val;

	  if (n > 0 && line[n-1] == '\n')
	    line[n-1] = '\0';

	  if (sscanf (line, "last-update %d", &val) == 1)
	    last_update = val;
	  else if (sscanf (line, "fullscreen-toolbar %d", &val) == 1)
	    fullscreen_toolbar = val;
	  else if (sscanf (line, "normal-toolbar %d", &val) == 1)
	    normal_toolbar = val;
	  else
	    add_log ("Unrecognized state line: '%s'\n", line);
	}
      free (line);
      fclose (f);
    }
}

void
save_state ()
{
  FILE *f = open_user_file (STATE_FILE, "w");
  if (f)
    {
      fprintf (f, "last-update %d\n", last_update);
      fprintf (f, "fullscreen-toolbar %d\n", fullscreen_toolbar);
      fprintf (f, "normal-toolbar %d\n", normal_toolbar);
      fclose (f);
    }
}
