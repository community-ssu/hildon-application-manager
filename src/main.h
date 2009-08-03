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

#ifndef MAIN_H
#define MAIN_H

#include <gtk/gtk.h>

#include "apt-worker-proto.h"

extern "C" {
  #include <libosso.h>
}

#define TREE_VIEW_ICON_SIZE 48

enum detail_kind {
  no_details = 0,
  install_details = 1,
  remove_details = 2
};

enum view_id {
  NO_VIEW = 0,
  MAIN_VIEW,
  INSTALL_APPLICATIONS_VIEW,
  UPGRADE_APPLICATIONS_VIEW,
  UNINSTALL_APPLICATIONS_VIEW,
  INSTALL_SECTION_VIEW,
  SEARCH_RESULTS_VIEW
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
  int64_t installed_size;
  char *installed_section;
  char *installed_pretty_name;
  char *available_version;
  char *available_section;
  char *available_pretty_name;
  char *installed_short_description;
  GdkPixbuf *installed_icon;
  char *available_short_description;
  GdkPixbuf *available_icon;
  int flags;

  bool have_info;
  apt_proto_package_info info;
  third_party_policy_status third_party_policy;
 
  detail_kind have_detail_kind;
  char *maintainer;
  char *description;
  char *repository;
  char *summary;
  GList *summary_packages[sumtype_max];  // GList of strings.
  char *dependencies;

  GtkTreeModel *model;
  GtkTreeIter iter;

  const char *get_display_name (bool installed);
  const char *get_display_version (bool installed);
};

view_id get_current_view_id ();

void get_package_info (package_info *pi,
		       bool only_installable_info,
		       void (*cont) (package_info *pi, void *data, bool changed),
		       void *data);

void get_package_infos (GList *packages,
			bool only_installable_info,
			void (*cont) (void *data),
			void *data);

struct section_info {

  section_info ();
  ~section_info ();

  void ref ();
  void unref ();

  int ref_count;

  int rank;
  const char *name;

  GList *packages;
};

#define SECTION_RANK_ALL    0
#define SECTION_RANK_NORMAL 1
#define SECTION_RANK_OTHER  2

void get_package_list ();
void get_package_list_with_cont (void (*cont) (void *data), void *data);

void show_current_details ();
void do_current_operation ();

void show_check_for_updates_view ();

void install_named_package (const char *package,
                            void (*cont) (int n_successful, void *data),
                            void *data);
void install_named_packages (const char **packages,
			     int install_type, bool automatic,
			     const char *title, const char *desc,
			     void (*cont) (int n_successful, void *data),
			     void *data);

/* REFRESH_PACKAGE_CACHE_WITHOUT_USER refreshes the package cache in a
   non-intrusive way.  It just shows a progress note with cancel
   button.  No error messages are shown and no interaction from the
   user is required to complete the operation.
*/
void refresh_package_cache_without_user (const char *title,
					 void (*cont) (bool keep_going,
						       void *data),
					 void *data);

void refresh_package_cache_without_user_flow ();

void maybe_refresh_package_cache_without_user ();

void add_temp_catalogues_and_refresh (xexp *tempcat,
                                      const char *title,
                                      void (*cont) (bool keep_going,
                                                    void *data),
                                      void *data);

void set_catalogues_and_refresh (xexp *catalogues,
				 const char *title,
				 void (*cont) (bool keep_going, void *data),
				 void *data);

void install_from_file_flow (const char *filename);
void restore_packages_flow ();
void update_all_packages_flow ();

/* FORCE_SHOW_CATALOGUE_ERRORS if the user ignored the catalogue errors, this
   function will reset the ignoring request and the dialogue will be shown
   again if the errors remain
*/
void force_show_catalogue_errors ();

void save_backup_data ();

void sort_all_packages (bool refresh_view);
void show_main_view ();
void show_parent_view ();

void search_packages (const char *pattern, bool in_descriptions);

const char *nicify_section_name (const char *name);

GtkWindow *get_main_window ();
GtkWidget *get_main_trail ();
GtkWidget *get_device_label ();

#define AI_TOPIC(x) ("Utilities_ApplicationInstaller_" x)

void set_dialog_help (GtkWidget *dialog, const char *topic);
void show_help ();

void with_initialized_packages (void (*cont) (void *data), void *data);

osso_context_t *get_osso_context (void);

#endif /* !MAIN_H */
