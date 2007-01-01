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

#ifndef SETTINGS_H
#define SETTINGS_H

void load_settings ();
void save_settings ();

// User serviceable settings
//
extern int    update_interval_index;
extern int    package_sort_key;
extern int    package_sort_sign;

// Non-user serviceable settings, please ask your local geek.
//
extern bool clean_after_install;
extern bool assume_connection;
extern bool break_locks;
extern bool red_pill_mode;
extern bool red_pill_show_deps;
extern bool red_pill_show_all;
extern bool red_pill_show_magic_sys;

#define UPDATE_INTERVAL_SESSION 0
#define UPDATE_INTERVAL_WEEK    1
#define UPDATE_INTERVAL_MONTH   2
#define UPDATE_INTERVAL_NEVER   3

#define SORT_BY_NAME    0
#define SORT_BY_VERSION 1
#define SORT_BY_SIZE    2

// Persistent state
//
extern int  last_update;    // not a time_t until 2036
extern bool fullscreen_toolbar;
extern bool normal_toolbar;
extern int  force_ui_version;

extern int  ui_version;

void show_settings_dialog ();
void show_sort_settings_dialog ();

#endif /* !SETTINGS_H */
