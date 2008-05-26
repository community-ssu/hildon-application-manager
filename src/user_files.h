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

#ifndef USER_FILES_H
#define USER_FILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "xexp.h"

#define HAM_STATE_DIR ".hildon-application-manager"
#define UFILE_RESTORE_BACKUP "packages.backup"
#define UFILE_HAM_STATE "state"
#define UFILE_SEEN_UPDATES "seen-updates"
#define UFILE_SEEN_NOTIFICATIONS "seen-notifications"
#define UFILE_AVAILABLE_NOTIFICATIONS "available-notifications"
#define UFILE_AVAILABLE_NOTIFICATIONS_TMP   UFILE_AVAILABLE_NOTIFICATIONS ".tmp"
#define UFILE_BOOT "boot"
#define UFILE_UPDATE_NOTIFIER "update-notifier"
#define UFILE_LAST_UPDATE "last-update"

gchar *user_file_get_state_dir_path ();
FILE *user_file_open_for_read (const gchar *name);
FILE *user_file_open_for_write (const gchar *name);
int user_file_remove (const gchar *name);

xexp *user_file_read_xexp (const gchar *name);
void user_file_write_xexp (const gchar *name, xexp *x);

#ifdef __cplusplus
}
#endif

#endif /* !USER_FILES_H */

