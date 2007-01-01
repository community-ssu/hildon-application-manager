/*
 * This file is part of osso-application-installer
 *
 * Copyright (C) 2005, 2006 Nokia Corporation.  All Right reserved.
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
  char *available_version;
  char *available_section;
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

  char *filename;
};

void get_intermediate_package_info (package_info *pi,
				    bool only_installable_info,
				    void (*func) (package_info *, void *,
						  bool),
				    void *);

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

void get_package_list ();
void get_package_list_with_cont (void (*cont) (void *data), void *data);
void show_current_details ();
void do_current_operation ();
void install_named_package (const char *package);
void refresh_package_cache (bool ask);
void refresh_package_cache_with_cont (bool ask,
				      void (*cont) (bool res, void *data), 
				      void *data);
void install_from_file ();
void sort_all_packages ();
void show_main_view ();
void show_parent_view ();

void search_packages (const char *pattern, bool in_descriptions);

const char *nicify_section_name (const char *name);

GtkWindow *get_main_window ();
GtkWidget *get_main_trail ();
GtkWidget *get_device_label ();

void set_fullscreen (bool);
void toggle_fullscreen ();
void present_main_window ();

void set_toolbar_visibility (bool fullscreen, bool visibility);

#define AI_TOPIC(x) ("Utilities_ApplicationInstaller_" x)

void set_dialog_help (GtkWidget *dialog, const char *topic);
void show_help ();

#endif /* !MAIN_H */
