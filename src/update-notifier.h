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

#ifndef UPDATE_NOTIFIER_H
#define UPDATE_NOTIFIER_H

#define UPNO_GCONF_DIR     "/apps/hildon/update-notifier"
#define UPNO_GCONF_STATE   UPNO_GCONF_DIR "/state"

#define SEEN_UPDATES_FILE  ".hildon-application-manager-seen-updates"

enum {
  UPNO_ICON_INVISIBLE,
  UPNO_ICON_STATIC,
  UPNO_ICON_BLINKING
};

#endif /* !UPDATE_NOTIFIER_H */
