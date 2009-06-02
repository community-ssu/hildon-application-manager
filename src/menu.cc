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
#include <hildon/hildon.h>

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
add_item (HildonAppMenu *menu, const gchar *label, void (*func)())
{
  GtkWidget *item = hildon_button_new (HILDON_SIZE_AUTO, 
                                       HILDON_BUTTON_ARRANGEMENT_VERTICAL);
  hildon_button_set_title (HILDON_BUTTON (item), label);

  if (func)
    g_signal_connect (item, "clicked", G_CALLBACK (func), NULL);

  hildon_app_menu_append (menu, GTK_BUTTON (item));

  return item;
}

static GtkWidget *settings_menu_item = NULL;
static GtkWidget *install_from_file_menu_item = NULL;
static GtkWidget *update_all_menu_item = NULL;
static GtkWidget *search_menu_item = NULL;
static GtkWidget *refresh_menu_item = NULL;

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

static void
call_install_from_file ()
{
  install_from_file_flow (NULL);
}

void
create_menu ()
{
  GtkWidget *restore_item;
  xexp *restore_backup = NULL;

  HildonAppMenu *main = HILDON_APP_MENU (hildon_app_menu_new ());

  /* set menu */
  hildon_program_set_common_app_menu (hildon_program_get_instance (), main);

  /* Sort */
  add_item (main,
            _("ai_me_view_sort"),
            show_sort_settings_dialog_flow);

  /* Search */
  search_menu_item =
    add_item (main,
              _("ai_me_search"),
              show_search_dialog_flow);

  /* Refresh */
  refresh_menu_item =
    add_item (main,
      	      _("ai_me_refresh"),
              refresh_package_cache_without_user_flow);

  /* Application catalogues */
  add_item (main,
            _("ai_me_tools_repository"),
	    show_catalogue_dialog_flow);

  /* Log */
  add_item (main,
	    _("ai_me_tools_log"),
            show_log_dialog_flow);

  /* Restore applications */
  restore_item = add_item (main,
			   _("ai_me_tools_restore"),
                           restore_packages_flow);

  /* Install from file */
  install_from_file_menu_item =
    add_item (main,
              _("ai_me_package_install_file"),
              call_install_from_file);

  /* Settings */
  settings_menu_item =
    add_item (main,
	      _("ai_me_settings"),
	      show_settings_dialog_flow);

  /* Update all */
  update_all_menu_item =
    add_item(main,
      	     _("ai_me_update_all"),
	     update_all_packages_flow);

  gtk_widget_show_all (GTK_WIDGET (main));

  /* Hide restore_packages menu item when there is no backup */
  restore_backup = user_file_read_xexp (UFILE_RESTORE_BACKUP);
  gtk_widget_set_sensitive (restore_item, restore_backup != NULL);

  if (restore_backup != NULL)
    xexp_free (restore_backup);

  set_settings_menu_visible (red_pill_mode);
  set_install_from_file_menu_visible (red_pill_mode);
}

void
enable_search (bool flag)
{
  if (search_menu_item)
    g_object_set (G_OBJECT (search_menu_item), "visible", flag, NULL);
}

void
enable_refresh (bool flag)
{
  if (refresh_menu_item)
    g_object_set (G_OBJECT (refresh_menu_item), "visible", flag, NULL);
}

void
enable_update_all (bool flag)
{
  if (update_all_menu_item)
    g_object_set (G_OBJECT (update_all_menu_item), "visible", flag, NULL);
}

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
GtkWidget *
create_package_menu (const char *op_label)
{
  GtkWidget *menu = hildon_gtk_menu_new ();
  GtkWidget *item = gtk_menu_item_new_with_mnemonic (_("ai_me_details"));
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  g_signal_connect(G_OBJECT (item), "activate",
                   (GCallback) show_current_details, NULL);

  return menu;
}
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */
