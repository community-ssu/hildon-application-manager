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

#include <stdlib.h>
#include <libintl.h>

#include <gtk/gtk.h>

#include "menu.h"
#include "util.h"
#include "main.h"
#include "log.h"
#include "settings.h"
#include "repo.h"
#include "search.h"
#include "apt-worker-client.h"
#include "user_files.h"

#define _(x) gettext (x)

static GtkWidget *
add_item (GtkMenu *menu, const gchar *label, void (*func)())
{
  GtkWidget *item = gtk_menu_item_new_with_label (label);
  gtk_menu_append (menu, item);
  if (func)
    g_signal_connect (item, "activate", G_CALLBACK (func), NULL);

  return item;
}

static GtkMenu *
add_menu (GtkMenu *menu, const gchar *label)
{
  GtkWidget *sub = gtk_menu_new ();
  GtkWidget *item = add_item (menu, label, NULL);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), sub);
  return GTK_MENU (sub);
}

void
menu_close ()
{
  hide_main_window ();
  maybe_exit ();
}

static GtkWidget *settings_menu_item = NULL;
static GtkWidget *details_menu_item = NULL;
static GtkWidget *search_menu_item = NULL;
static GtkWidget *operation_menu_item = NULL;
static GtkWidget *install_from_file_menu_item = NULL;

void
set_settings_menu_visible (bool flag)
{
  if (settings_menu_item)
    {
      if (flag)
	gtk_widget_show (settings_menu_item);
      else
	gtk_widget_hide (settings_menu_item);
    }
}

void
set_install_from_file_menu_visible (bool flag)
{
  if (install_from_file_menu_item)
    {
      if (flag)
        gtk_widget_show (install_from_file_menu_item);
      else
        gtk_widget_hide (install_from_file_menu_item);
    }
}

void
set_details_menu_sensitive (bool flag)
{
  if (details_menu_item)
    gtk_widget_set_sensitive (details_menu_item, flag);
}

void
set_search_menu_sensitive (bool flag)
{
  if (search_menu_item)
    gtk_widget_set_sensitive (search_menu_item, flag);
}

void
set_operation_menu_label (const char *label, bool sensitive)
{
  if (operation_menu_item)
    {
      gtk_label_set
	(GTK_LABEL (gtk_bin_get_child (GTK_BIN (operation_menu_item))),
	 label);
      gtk_widget_set_sensitive (operation_menu_item, sensitive);
    }
}

void
set_operation_menu_item_sensitiveness (bool sensitive)
{
  gtk_widget_set_sensitive (operation_menu_item, sensitive);
}

static void
call_install_from_file ()
{
  install_from_file_flow (NULL);
}

void
create_menu (HildonWindow *window)
{
  GtkWidget *restore_item;
  GtkAccelGroup *accel_group;
  xexp *restore_backup = NULL;

  accel_group = gtk_accel_group_new ();
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

  GtkMenu *main = GTK_MENU (gtk_menu_new ());
  hildon_window_set_menu (window, GTK_MENU (main));

  GtkMenu *packages = add_menu (main, _("ai_me_package"));
  GtkMenu *tools = add_menu (main, _("ai_me_tools"));

  add_item (main,
            _("ai_me_view_sort"),
            show_sort_settings_dialog_flow);

  operation_menu_item = add_item (packages, "", do_current_operation);

  install_from_file_menu_item =
    add_item (packages,
              _("ai_me_package_install_file"),
              call_install_from_file);

  details_menu_item = add_item (packages,
				_("ai_me_package_details"),
				show_current_details);

  settings_menu_item =
    add_item (tools,
	      _("Settings"),
	      show_settings_dialog_flow);

  add_item (tools,
	    _("ai_me_tools_repository"),
	    show_catalogue_dialog_flow);
  search_menu_item =
    add_item (tools,
	      _("ai_me_tools_search"),
	      show_search_dialog_flow);
  restore_item = add_item (tools,
			   _("ai_me_tools_restore"),
			   restore_packages_flow);
  add_item (tools,
	    _("ai_me_tools_log"),
	    show_log_dialog_flow);

  /* Set sensitiveness for restore_packages menu item */
  restore_backup = user_file_read_xexp (UFILE_RESTORE_BACKUP);
  gtk_widget_set_sensitive (restore_item, (restore_backup != NULL));

  if (restore_backup != NULL)
    xexp_free (restore_backup);

  gtk_widget_show_all (GTK_WIDGET (main));
  set_settings_menu_visible (red_pill_mode);
  set_install_from_file_menu_visible (red_pill_mode);
}

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
GtkWidget *
create_package_menu (const char *op_label)
{
  GtkWidget *menu = gtk_menu_new ();

  add_item (GTK_MENU (menu),
            op_label,
            do_current_operation);
  add_item (GTK_MENU (menu),
	    _("ai_me_cs_details"),
	    show_current_details);

  return menu;
}
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */
