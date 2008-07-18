/*
 * This file is part of the hildon-application-manager.
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
#include "user_files.h"

#define _(x) gettext (x)

int  package_sort_key = SORT_BY_NAME;
int  package_sort_sign = 1;

bool clean_after_install = true;
bool assume_connection = false;
bool break_locks = true;
bool download_packages_to_mmc = true;
bool use_apt_algorithms = false;
bool red_pill_mode = false;
bool red_pill_show_deps = true;
bool red_pill_show_all = true;
bool red_pill_show_magic_sys = true;
bool red_pill_include_details_in_log = false;
bool red_pill_check_always = false;
bool red_pill_ignore_wrong_domains = true;

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
	  else if (sscanf (line, "package-sort-key %d", &val) == 1)
	    package_sort_key = val;
	  else if (sscanf (line, "package-sort-sign %d", &val) == 1)
	    package_sort_sign = val;
	  else if (sscanf (line, "break-locks %d", &val) == 1)
	    break_locks = val;
	  else if (sscanf (line, "download-packages-to-mmc %d", &val) == 1)
	    download_packages_to_mmc = val;
	  else if (sscanf (line, "use-apt-algorithms %d", &val) == 1)
	    use_apt_algorithms = val;
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
	  else if (sscanf (line, "red-pill-check-always %d", &val) == 1)
	    red_pill_check_always = val;
	  else if (sscanf (line, "red-pill-ignore-wrong-domains %d", &val) == 1)
	    red_pill_ignore_wrong_domains = val;
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
      fprintf (f, "package-sort-key %d\n", package_sort_key);
      fprintf (f, "package-sort-sign %d\n", package_sort_sign);
      fprintf (f, "break-locks %d\n", break_locks);
      fprintf (f, "download-packages-to-mmc %d\n", download_packages_to_mmc);
      fprintf (f, "use-apt-algorithms %d\n", use_apt_algorithms);
      fprintf (f, "red-pill-mode %d\n", red_pill_mode);
      fprintf (f, "red-pill-show-deps %d\n", red_pill_show_deps);
      fprintf (f, "red-pill-show-all %d\n", red_pill_show_all);
      fprintf (f, "red-pill-show-magic-sys %d\n", red_pill_show_magic_sys);
      fprintf (f, "red-pill-include-details-in-log %d\n", 
	       red_pill_include_details_in_log);
      fprintf (f, "red-pill-check-always %d\n", red_pill_check_always);
      fprintf (f, "red-pill-ignore-wrong-domains %d\n",
	       red_pill_ignore_wrong_domains);
      fprintf (f, "assume-connection %d\n", assume_connection);
      fclose (f);
    }
}

enum boolean_options {
  OPT_CLEAN_AFTER_INSTALL,
  OPT_ASSUME_CONNECTION,
  OPT_BREAK_LOCKS,
  OPT_SHOW_DEPS,
  OPT_SHOW_ALL,
  OPT_SHOW_MAGIC_SYS,
  OPT_INCLUDE_DETAILS_IN_LOG,
  OPT_DOWNLOAD_PACKAGES_TO_MMC,
  OPT_CHECK_ALWAYS,
  OPT_IGNORE_WRONG_DOMAINS,
  OPT_USE_APT_ALGORITHMS,
  NUM_BOOLEAN_OPTIONS
};

struct settings_closure {
  GtkWidget *update_combo;

  GtkWidget *boolean_btn[NUM_BOOLEAN_OPTIONS];
  bool *boolean_var[NUM_BOOLEAN_OPTIONS];
};

static void
make_boolean_option (settings_closure *c, 
		     GtkWidget *box, GtkSizeGroup *group,
		     const int id, const char *text, bool *var)
{
  GtkWidget *caption, *btn;

  btn = gtk_check_button_new ();
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (btn), *var);
  caption = hildon_caption_new (group, text, btn,
				NULL, HILDON_CAPTION_OPTIONAL);
  gtk_box_pack_start (GTK_BOX (box), caption, FALSE, FALSE, 0);

  if (id > NUM_BOOLEAN_OPTIONS)
    abort ();

  c->boolean_btn[id] = btn;
  c->boolean_var[id] = var;
}

static GtkWidget *
make_settings_tab (settings_closure *c)
{
  GtkWidget *scrolled_window, *vbox;
  GtkSizeGroup *group;

  group = GTK_SIZE_GROUP (gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL));

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_NEVER,
				  GTK_POLICY_AUTOMATIC);

  vbox = gtk_vbox_new (FALSE, 0);

  make_boolean_option (c, vbox, group, OPT_CLEAN_AFTER_INSTALL,
		       "Clean apt cache", &clean_after_install);
  make_boolean_option (c, vbox, group, OPT_ASSUME_CONNECTION,
		       "Assume net connection", &assume_connection);
  make_boolean_option (c, vbox, group, OPT_BREAK_LOCKS,
		       "Break locks", &break_locks);
  make_boolean_option (c, vbox, group, OPT_SHOW_DEPS,
		       "Show dependencies", &red_pill_show_deps);
  make_boolean_option (c, vbox, group, OPT_SHOW_ALL,
		       "Show all packages", &red_pill_show_all);
  make_boolean_option (c, vbox, group, OPT_SHOW_MAGIC_SYS,
		       "Show magic system package",
		       &red_pill_show_magic_sys);
  make_boolean_option (c, vbox, group, OPT_INCLUDE_DETAILS_IN_LOG,
		       "Include package details in log",
		       &red_pill_include_details_in_log);
  make_boolean_option (c, vbox, group, OPT_DOWNLOAD_PACKAGES_TO_MMC,
		       "Use MMC to download packages",
		       &download_packages_to_mmc);
  make_boolean_option (c, vbox, group, OPT_CHECK_ALWAYS,
		       "Always check for updates",
		       &red_pill_check_always);
  make_boolean_option (c, vbox, group, OPT_IGNORE_WRONG_DOMAINS,
		       "Ignore packages from wrong domains",
		       &red_pill_ignore_wrong_domains);
  make_boolean_option (c, vbox, group, OPT_USE_APT_ALGORITHMS,
		       "Use apt-get algorithms",
		       &use_apt_algorithms);
  g_object_unref (group);

  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_window),
					 vbox);
  return scrolled_window;
}

static void
settings_dialog_response (GtkDialog *dialog, gint response, gpointer clos)
{
  settings_closure *c = (settings_closure *)clos;
  bool needs_refresh = false;

  if (response == GTK_RESPONSE_OK)
    {
      for (int i = 0; i < NUM_BOOLEAN_OPTIONS; i++)
	{
	  gboolean current_value =
	    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (c->boolean_btn[i]));

	  if ((i == OPT_SHOW_ALL || i == OPT_SHOW_MAGIC_SYS) &&
	      *(c->boolean_var[i]) != current_value)
	    {
	      needs_refresh = true;
	    }

	  *(c->boolean_var[i]) = current_value;
	}

      save_settings ();

      if (needs_refresh)
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
      
      dialog = gtk_dialog_new_with_buttons ("Settings",
					    NULL,
					    GTK_DIALOG_MODAL,
					    "Ok",
					    GTK_RESPONSE_OK,
					    "Cancel",
					    GTK_RESPONSE_CANCEL,
					    NULL);
      push_dialog (dialog);
      gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
      
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG(dialog)->vbox),
			  make_settings_tab (c),
			  TRUE, TRUE, 5);
      gtk_widget_set_usize (dialog, -1, 300);

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

bool fullscreen_toolbar = true;
bool normal_toolbar = true;

void
load_state ()
{
  FILE *f = user_file_open_for_read (UFILE_HAM_STATE);

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

	  if (sscanf (line, "fullscreen-toolbar %d", &val) == 1)
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
  FILE *f = user_file_open_for_write (UFILE_HAM_STATE);

  if (f)
    {
      fprintf (f, "fullscreen-toolbar %d\n", fullscreen_toolbar);
      fprintf (f, "normal-toolbar %d\n", normal_toolbar);
      fclose (f);
    }
}
