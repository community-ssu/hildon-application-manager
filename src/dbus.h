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

#ifndef HAM_DBUS_H
#define HAM_DBUS_H

/*
 Struct to store info about the battery status

  level values:
    -1: Invalid data
     1: under 25% of max charge
     2: between 25%-50% of max charge
     3: between 50%-75% of max charge
     4: over 75% of max charge

  charging values:
     FALSE: Charging off
      TRUE: Charging on
*/
struct battery_info {
  gint level;
  gboolean charging;
};


/* Return the device name.
 */
const char *device_name ();

void init_dbus_or_die (bool top_existing);

void send_reboot_message (void);

battery_info *check_battery_status (void);

#endif /* !HAM_DBUS_H */
