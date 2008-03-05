/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007 Nokia Corporation.  All Rights reserved.
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

#ifndef UPDATE_NOTIFIER_CONF_H
#define UPDATE_NOTIFIER_CONF_H

#define UPNO_GCONF_DIR            "/apps/hildon/update-notifier"
#define UPNO_GCONF_STATE          UPNO_GCONF_DIR "/state"
#define UPNO_GCONF_ALARM_COOKIE   UPNO_GCONF_DIR "/alarm_cookie"
#define UPNO_GCONF_CHECK_INTERVAL UPNO_GCONF_DIR "/check_interval"
#define UPNO_GCONF_LAST_UPDATE    UPNO_GCONF_DIR "/last_update"
#define UPNO_GCONF_URI            UPNO_GCONF_DIR "/uri"

#define UPNO_DEFAULT_CHECK_INTERVAL (24*60)

#define UPDATE_NOTIFIER_SERVICE "com.nokia.hildon_update_notifier"
#define UPDATE_NOTIFIER_OBJECT_PATH "/com/nokia/hildon_update_notifier"
#define UPDATE_NOTIFIER_INTERFACE "com.nokia.hildon_update_notifier"

#define UPDATE_NOTIFIER_OP_CHECK_UPDATES "check_for_updates"
#define UPDATE_NOTIFIER_OP_CHECK_STATE "check_state"

#define HILDON_APP_MGR_SERVICE "com.nokia.hildon_application_manager"
#define HILDON_APP_MGR_OBJECT_PATH "/com/nokia/hildon_application_manager"
#define HILDON_APP_MGR_INTERFACE "com.nokia.hildon_application_manager"
#define HILDON_APP_MGR_OP_CHECK_UPDATES "check_for_updates"
#define HILDON_APP_MGR_OP_SHOW_CHECK_FOR_UPDATES "show_check_for_updates_view"
#define HILDON_APP_MGR_OP_SHOWING_CHECK_FOR_UPDATES "showing_check_for_updates_view"

#define AVAILABLE_UPDATES_FILE "/var/lib/hildon-application-manager/available-updates"
#define AVAILABLE_NOTIFICATIONS_MENU_WIDTH 500
#define AVAILABLE_NOTIFICATIONS_MENU_LEFT_PADDING 80 + 9
#define AVAILABLE_NOTIFICATIONS_MENU_TOP_PADDING 9

#define URL_VARIABLE_PREFIX     "$\{"
#define URL_VARIABLE_SUFFIX     "}"
#define URL_VARIABLE_HARDWARE  URL_VARIABLE_PREFIX "HARDWARE" URL_VARIABLE_SUFFIX

#endif /* !UPDATE_NOTIFIER_CONF_H */
