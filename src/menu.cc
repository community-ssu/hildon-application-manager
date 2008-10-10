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

static void
insensitive_press (GtkButton *button, gpointer data)
{
  irritate_user ((const gchar *)data);
}

static GtkWidget *
add_item (GtkMenu *menu, const gchar *label, const gchar *insens,
	  void (*func)())
{
  GtkWidget *item = gtk_menu_item_new_with_label (label);
  gtk_menu_append (menu, item);
  if (func)
    g_signal_connect (item, "activate", G_CALLBACK (func), NULL);

  if (insens)
    g_signal_connect (item, "insensitive_press",
		      G_CALLBACK (insensitive_press), (void *)insens);

  return item;
}

static GtkWidget *
add_item_with_shortcut (GtkMenu *menu, const gchar *label,
			const gchar *insens,
			void (*func)(),
			GtkAccelGroup *accel_group, guint key)
{
  GtkWidget *item = add_item (menu, label, insens, func);

  gtk_widget_add_accelerator (item, "activate",
			      accel_group,
			      key, GDK_CONTROL_MASK,
			      GTK_ACCEL_VISIBLE);

  return item;
}

static GtkWidget *
add_check (GtkMenu *menu, const gchar *label, void (*func)())
{
  GtkWidget *item = gtk_check_menu_item_new_with_label (label);
  gtk_menu_append (menu, item);
  if (func)
    g_signal_connect (item, "activate", G_CALLBACK (func), NULL);
  return item;
}

static GtkWidget *
add_sep (GtkMenu *menu)
{
  GtkWidget *item = gtk_separator_menu_item_new ();
  gtk_menu_append (menu, item);
  return item;
}

static GtkMenu *
add_menu (GtkMenu *menu, const gchar *label)
{
  GtkWidget *sub = gtk_menu_new ();
  GtkWidget *item = add_item (menu, label, NULL, NULL);
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

static const gchar *insensitive_operation_press_label;

static void
insensitive_operation_press (GtkButton *button, gpointer data)
{
  irritate_user (insensitive_operation_press_label);
}

void
set_operation_menu_label (const char *label, bool sensitive,
			  const gchar *insens)
{
  if (operation_menu_item)
    {
      gtk_label_set 
	(GTK_LABEL (gtk_bin_get_child (GTK_BIN (operation_menu_item))),
	 label);
      gtk_widget_set_sensitive (operation_menu_item, sensitive);

      insensitive_operation_press_label = insens;
    }
}

void
set_operation_menu_item_sensitiveness (bool sensitive)
{
  gtk_widget_set_sensitive (operation_menu_item, sensitive);
}

static void
fullscreen_activated (GtkWidget *item)
{
  bool active =
    gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
  set_fullscreen (active);
}

static GtkWidget *fullscreen_item;

void
set_fullscreen_menu_check (bool f)
{
  if (fullscreen_item)
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (fullscreen_item), f);
}

static void
normal_toolbar_activated (GtkWidget *item)
{
  bool active =
    gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
  set_toolbar_visibility (false, active);
}

static void
fullscreen_toolbar_activated (GtkWidget *item)
{
  bool active =
    gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item));
  set_toolbar_visibility (true, active);
}

static void
call_install_from_file ()
{
  install_from_file_flow (NULL);
}

void
create_menu (HildonWindow *window)
{
  GtkWidget *item;
  GtkWidget *restore_item;
  GtkAccelGroup *accel_group;
  xexp *restore_backup = NULL;

  accel_group = gtk_accel_group_new ();
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

  GtkMenu *main = GTK_MENU (gtk_menu_new ());
  hildon_window_set_menu (window, GTK_MENU (main));

  GtkMenu *packages = add_menu (main, _("ai_me_package"));
  GtkMenu *view = add_menu (main, _("ai_me_view"));
  GtkMenu *tools = add_menu (main, _("ai_me_tools"));

  operation_menu_item = add_item (packages, "", NULL, do_current_operation);
  g_signal_connect (operation_menu_item, "insensitive_press",
		    G_CALLBACK (insensitive_operation_press), NULL);

  add_item (packages,
	    _("ai_me_package_install_file"), NULL,
	    call_install_from_file);
  details_menu_item = add_item (packages,
				_("ai_me_package_details"),
				_("ai_ib_nothing_to_view"),
				show_current_details);

  add_item (view,
	    _("ai_me_view_sort"), NULL,
	    show_sort_settings_dialog_flow);
  add_sep (view);
  fullscreen_item = add_check (view, _("ai_me_view_fullscreen"), NULL);
  g_signal_connect (fullscreen_item, "activate",
		    G_CALLBACK (fullscreen_activated), NULL);

  GtkMenu *toolbar = add_menu (view, _("ai_me_view_show_toolbar"));

  item = add_check (toolbar, _("ai_me_view_show_toolbar_normal"), NULL);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
				  normal_toolbar);
  g_signal_connect (item, "activate",
		    G_CALLBACK (normal_toolbar_activated), NULL);

  item = add_check (toolbar,
		    _("ai_me_view_show_toolbar_fullscreen"), NULL);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
				  fullscreen_toolbar);
  g_signal_connect (item, "activate",
		    G_CALLBACK (fullscreen_toolbar_activated), NULL);


  settings_menu_item = 
    add_item (tools,
	      _("Settings"), NULL,
	      show_settings_dialog_flow);

  add_item (tools,
	    _("ai_me_tools_repository"), NULL,
	    show_catalogue_dialog_flow);
  search_menu_item =
    add_item (tools,
	      _("ai_me_tools_search"), _("ai_ib_unable_search"),
	      show_search_dialog_flow);
  restore_item = add_item (tools,
			   _("ai_me_tools_restore"), 
			   _("ai_ib_nothing_restore"),
			   restore_packages_flow);
  add_item (tools,
	    _("ai_me_tools_log"), NULL,
	    show_log_dialog_flow);
  add_item (tools,
	    _("ai_me_tools_help"), NULL,
	    show_help);

  add_item_with_shortcut (main,
			  _("ai_me_close"), NULL,
			  menu_close,
			  accel_group, 'q');

  /* Set sensitiveness for restore_packages menu item */
  restore_backup = user_file_read_xexp (UFILE_RESTORE_BACKUP);
  gtk_widget_set_sensitive (restore_item, (restore_backup != NULL));

  if (restore_backup != NULL)
    xexp_free (restore_backup);

  gtk_widget_show_all (GTK_WIDGET (main));
  set_settings_menu_visible (red_pill_mode);
}

GtkWidget *
create_package_menu (const char *op_label,
                     const char *insensitive_op_press_label)
{
  GtkWidget *menu = gtk_menu_new ();

  add_item (GTK_MENU (menu),
            op_label,
            insensitive_op_press_label,
            do_current_operation);
  add_item (GTK_MENU (menu),
	    _("ai_me_cs_details"),
            NULL,
	    show_current_details);

  return menu;
}
