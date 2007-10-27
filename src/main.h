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

#ifndef MAIN_H
#define MAIN_H

#include <gtk/gtk.h>

#include "apt-worker-proto.h"

extern "C" {
  #include <libosso.h>
}

enum detail_kind {
  no_details = 0,
  install_details = 1,
  remove_details = 2
};

struct package_info {

  package_info ();
  ~package_info ();

  void ref ();
  void unref ();

  int ref_count;

  char *name;
  bool broken;
  char *installed_version;
  int installed_size;
  char *installed_section;
  char *installed_pretty_name;
  char *available_version;
  char *available_section;
  char *available_pretty_name;
  char *installed_short_description;
  GdkPixbuf *installed_icon;
  char *available_short_description;
  GdkPixbuf *available_icon;

  bool have_info;
  apt_proto_package_info info;
 
  detail_kind have_detail_kind;
  char *maintainer;
  char *description;
  char *summary;
  GList *summary_packages[sumtype_max];  // GList of strings.
  char *dependencies;

  GtkTreeModel *model;
  GtkTreeIter iter;

  const char *get_display_name (bool installed);
};

void get_intermediate_package_info (package_info *pi,
				    bool only_installable_info,
				    void (*func) (package_info *, void *,
						  bool),
				    void *,
				    int state);

void get_intermediate_package_list_info (GList *packages,
					 bool only_installable_info,
					 void (*cont) (void *data),
					 void *data,
					 int state);

struct section_info {

  section_info ();
  ~section_info ();

  void ref ();
  void unref ();

  int ref_count;

  char *symbolic_name;
  const char *name;

  GList *packages;
};

void get_package_list (int state);
void get_package_list_with_cont (int state,
				 void (*cont) (void *data), void *data);
void show_current_details ();
void do_current_operation ();

void show_check_for_updates_view ();

void install_named_package (int state, const char *package,
			    void (*cont) (int n_successful, void *data),
			    void *data);
void install_named_packages (int state, const char **packages,
			     int install_type, bool automatic,
			     const char *title, const char *desc,
			     void (*cont) (int n_successful, void *data),
			     void *data);

/* REFRESH_PACKAGE_CACHE will download and digest the indices for all
   enabled catalogues.  It will show error messages to the user and
   allow the catalogue configuration to be edited and the operation to
   be retried in case of errors.

   When CATALOGUES is non-NULL, it will replace the existing catalogue
   configuration before the operation begins.

   When ASK is true, the user will be asked whether to actually
   perform the operation.

   When CONTINUED is true, error messages will include "Continue" and
   "Stop" buttons instead of "Ok".

   CONT is called when the operation has finished.  KEEP_GOING
   indicates whether the user wants to continue or not in case an
   error occurred.  When CONTINUED is false, KEEP_GOING will be true
   when there was no fatal error and continuing might make sense.
*/
void refresh_package_cache (int state,
			    xexp *new_catalogues,
			    bool ask, bool continued,
			    void (*cont) (bool keep_going, void *data), 
			    void *data);

void refresh_package_cache_flow ();

void install_from_file_flow (const char *filename);
void restore_packages_flow ();
void update_system_flow ();

void save_backup_data ();

void sort_all_packages ();
void show_main_view ();
void show_parent_view ();

void search_packages (const char *pattern, bool in_descriptions);

const char *nicify_section_name (const char *name);

GtkWindow *get_main_window ();
GtkWidget *get_main_toolbar ();
GtkWidget *get_main_trail ();
GtkWidget *get_device_label ();

void set_fullscreen (bool);
void toggle_fullscreen ();

void set_toolbar_visibility (bool fullscreen, bool visibility);

#define AI_TOPIC(x) ("Utilities_ApplicationInstaller_" x)

void set_dialog_help (GtkWidget *dialog, const char *topic);
void show_help ();

void with_initialized_packages (void (*cont) (void *data), void *data);

osso_context_t *get_osso_context (void);

#endif /* !MAIN_H */
