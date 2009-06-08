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
#include <hildon/hildon.h>

#include "settings.h"
#include "util.h"
#include "log.h"
#include "apt-worker-client.h"
#include "menu.h"
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
bool red_pill_show_all = false;
bool red_pill_show_magic_sys = false;
bool red_pill_include_details_in_log = false;
bool red_pill_check_always = false;
bool red_pill_ignore_wrong_domains = true;
bool red_pill_ignore_thirdparty_policy = false;
bool red_pill_permanent = false;

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
	  else if (sscanf (line, "red-pill-ignore-thirdparty-policy %d", &val) == 1)
	    red_pill_ignore_thirdparty_policy = val;
	  else if (sscanf (line, "red-pill-permanent %d", &val) == 1)
	    red_pill_permanent = val;
	  else
	    add_log ("Unrecognized configuration line: '%s'\n", line);
	}
      free (line);
      fclose (f);
    }

  show_sort_order();

  if (!red_pill_permanent)
    red_pill_mode = false;

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
      fprintf (f, "red-pill-ignore-thirdparty-policy %d\n",
	       red_pill_ignore_thirdparty_policy);
      fprintf (f, "red-pill-permanent %d\n", red_pill_permanent);
      fprintf (f, "assume-connection %d\n", assume_connection);
      fflush (f);
      fsync (fileno (f));
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
  OPT_IGNORE_THIRDPARTY_POLICY,
  OPT_USE_APT_ALGORITHMS,
#if 0
  OPT_PERMANENT,
#endif
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
  GtkWidget *btn;

  btn = hildon_check_button_new(HILDON_SIZE_FINGER_HEIGHT);
  gtk_button_set_label(GTK_BUTTON(btn), text);
  hildon_check_button_set_active(HILDON_CHECK_BUTTON(btn), *var);
  gtk_box_pack_start (GTK_BOX (box), btn, FALSE, FALSE, 0);

  if (id >= NUM_BOOLEAN_OPTIONS)
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

  scrolled_window = hildon_pannable_area_new ();

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
  make_boolean_option (c, vbox, group, OPT_IGNORE_THIRDPARTY_POLICY,
		       "Ignore the third party packages policy for SSU",
		       &red_pill_ignore_thirdparty_policy);
  make_boolean_option (c, vbox, group, OPT_USE_APT_ALGORITHMS,
		       "Use apt-get algorithms",
		       &use_apt_algorithms);
#if 0
  make_boolean_option (c, vbox, group, OPT_PERMANENT,
 		       "Red pill is permanent",
 		       &red_pill_permanent);
#endif
  g_object_unref (group);

  hildon_pannable_area_add_with_viewport (HILDON_PANNABLE_AREA (scrolled_window),
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
	    hildon_check_button_get_active (HILDON_CHECK_BUTTON (c->boolean_btn[i]));

	  if ((i == OPT_SHOW_ALL || i == OPT_SHOW_MAGIC_SYS) &&
	      *(c->boolean_var[i]) != current_value)
	    {
	      needs_refresh = true;
	    }

	  *(c->boolean_var[i]) = current_value;
	}

      save_settings ();
      update_backend_options ();

      if (needs_refresh)
	get_package_list ();
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

      dialog = gtk_dialog_new_with_buttons (_("ai_me_settings"),
					    NULL,
					    GTK_DIALOG_MODAL,
					    GTK_STOCK_CANCEL,
					    GTK_RESPONSE_CANCEL,
					    dgettext ("hildon-libs",
						      "wdgt_bd_save"),
					    GTK_RESPONSE_OK,
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
	GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "sort-key"));
      if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dialog), "sort-order"))
	  == GTK_SORT_ASCENDING)
	package_sort_sign = 1;
      else
	package_sort_sign = -1;

      show_sort_order();

      save_settings ();
      sort_all_packages (true);
    }

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  end_interaction_flow ();
}

static GtkTreeModel *
make_sort_model()
{
  GtkTreeIter itr;
  GtkListStore *ls = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
  char *str = NULL;

  gtk_list_store_append(ls, &itr);
  str = g_strdup_printf("%s/%s", _("ai_va_sort_name"), 
                        g_dgettext("hildon-libs", "ckdg_va_sort_ascending"));
  gtk_list_store_set(ls, &itr, 0, str, 1, SORT_BY_NAME, 2, GTK_SORT_ASCENDING, -1);
  g_free(str);

  gtk_list_store_append(ls, &itr);
  str = g_strdup_printf("%s/%s", _("ai_va_sort_name"), 
                        g_dgettext("hildon-libs", "ckdg_va_sort_descending"));
  gtk_list_store_set(ls, &itr, 0, str, 1, SORT_BY_NAME, 2, GTK_SORT_DESCENDING, -1);
  g_free(str);

  gtk_list_store_append(ls, &itr);
  str = g_strdup_printf("%s/%s", _("ai_va_sort_version"), 
                        g_dgettext("hildon-libs", "ckdg_va_sort_ascending"));
  gtk_list_store_set(ls, &itr, 0, str, 1, SORT_BY_VERSION, 2, GTK_SORT_ASCENDING, -1);
  g_free(str);

  gtk_list_store_append(ls, &itr);
  str = g_strdup_printf("%s/%s", _("ai_va_sort_version"), 
                        g_dgettext("hildon-libs", "ckdg_va_sort_descending"));
  gtk_list_store_set(ls, &itr, 0, str, 1, SORT_BY_VERSION, 2, GTK_SORT_DESCENDING, -1);
  g_free(str);

  gtk_list_store_append(ls, &itr);
  str = g_strdup_printf("%s/%s", _("ai_va_sort_size"), 
                        g_dgettext("hildon-libs", "ckdg_va_sort_ascending"));
  gtk_list_store_set(ls, &itr, 0, str, 1, SORT_BY_SIZE, 2, GTK_SORT_ASCENDING, -1);
  g_free(str);

  gtk_list_store_append(ls, &itr);
  str = g_strdup_printf("%s/%s", _("ai_va_sort_size"), 
                        g_dgettext("hildon-libs", "ckdg_va_sort_descending"));
  gtk_list_store_set(ls, &itr, 0, str, 1, SORT_BY_SIZE, 2, GTK_SORT_DESCENDING, -1);
  g_free(str);

  return GTK_TREE_MODEL(ls);
}

static void
sort_dialog_row_activate(GtkTreeView *tree, 
                         GtkTreePath *tp, 
                         GtkTreeViewColumn *column, 
                         GObject *dialog)
{
  GtkTreeIter itr;
  GtkTreeModel *tm = gtk_tree_view_get_model(tree);

  if (gtk_tree_model_get_iter(tm, &itr, tp))
    {
      char *sort_order_str;
      int sort_by, order;

      gtk_tree_model_get(tm, &itr, 0, &sort_order_str, 1, &sort_by, 2, &order, -1);
      g_object_set_data(dialog, "sort-key", GINT_TO_POINTER(sort_by));
      g_object_set_data(dialog, "sort-order", GINT_TO_POINTER(order));

      gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
}

void
show_sort_settings_dialog_flow ()
{
  if (start_interaction_flow ())
    {
      GtkTreeViewColumn *column;
      GtkCellRenderer *cr;
      GtkWidget *tree, *pan;
      GtkWidget *dialog;

      dialog = gtk_dialog_new_with_buttons(_("ai_me_view_sort"), NULL, GTK_DIALOG_MODAL, NULL);

      tree = gtk_tree_view_new_with_model(make_sort_model());
      column = gtk_tree_view_column_new();
      cr = GTK_CELL_RENDERER(g_object_new(GTK_TYPE_CELL_RENDERER_TEXT, 
                       "alignment", PANGO_ALIGN_CENTER, "xalign", 0.5, NULL));
      gtk_tree_view_column_pack_start(column, cr, TRUE);
      gtk_tree_view_column_set_attributes(column, cr, "text", 0, NULL);
      gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

      pan = hildon_pannable_area_new();
      gtk_container_add(GTK_CONTAINER(pan), tree);
      gtk_widget_set_size_request(pan, -1, 350);

      gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), pan);         

      g_signal_connect(G_OBJECT(tree),
                       "row-activated",
                       (GCallback)sort_dialog_row_activate, 
                       dialog);

      push_dialog (dialog);

      g_signal_connect (dialog, "response",
			G_CALLBACK (sort_settings_dialog_response),
			NULL);
      gtk_widget_show_all (dialog);
    }
}

const char *
backend_options ()
{
  static char options[6];

  char *ptr = options;
  if (break_locks)
    *ptr++ = 'B';
  if (red_pill_mode && !red_pill_ignore_wrong_domains)
    *ptr++ = 'D';
  if (download_packages_to_mmc)
    *ptr++ = 'M';
  if (red_pill_mode && red_pill_ignore_thirdparty_policy)
    *ptr++ = 'T';
  *ptr++ = '\0';

  return options;
}

static void
set_options_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  return;
}

void
update_backend_options ()
{
  apt_worker_set_options (backend_options (), set_options_reply, NULL);
}
