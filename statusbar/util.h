/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007, 2008 Nokia Corporation.  All Rights reserved.
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

#ifndef UTIL_H
#define UTIL_H

#include <gtk/gtk.h>

#include <time.h>

#include <sys/inotify.h>

/* HAM name known by the window manager */
#define HAM_APPNAME              "Application manager"

/* logging mechanism */
#ifdef DEBUG
#define LOG(...) my_log (__FILE__, __PRETTY_FUNCTION__, __VA_ARGS__);
#else
#define LOG(...)
#endif

void my_log (const gchar *file, const gchar *function, const gchar *fmt, ...);

/* TRUE if the application is running in the scratchbox environment */
gboolean running_in_scratchbox ();

/* Save the LAST_UPDATE timstamp from disk */
void save_last_update_time (time_t t);

/* Load the LAST_UPDATE timstamp from disk */
time_t load_last_update_time ();

/* TRUE if there is a window called "Application Manager" */
gboolean ham_is_running ();

/* Return the current http proxy in a form suitable for the
   "http_proxy" environment variable, or NULL if no proxy has
   currently been configured.  You must free the return value with
   g_free.

   The current proxy is taken either from gconf.
*/
gchar *get_gconf_http_proxy ();

/* verify if the event modifies the specified file in the specified
   watcher
*/
gboolean is_file_modified (struct inotify_event *event, gint watch,
			   const gchar *filename);

/* finds and loads an icon from the theme */
GdkPixbuf *icon_load (const gchar *name, gint size);

#endif 	    /* !UTIL_H */
