/*
 * This file is part of the hildon-application-manager.
 *
 * Parts of this file are derived from apt.  Apt is copyright 1997,
 * 1998, 1999 Jason Gunthorpe and others.
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

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <hildon-mime.h>

void
mime_open (DBusConnection *con, const char *file)
{
  DBusMessage     *msg, *reply;
  DBusError        error;
  DBusMessageIter  iter;
  gchar           *service = "com.nokia.hildon_application_manager";
  gchar           *object_path = "/com/nokia/hildon_application_manager";
  gchar           *interface = "com.nokia.hildon_application_manager";
  

  msg = dbus_message_new_method_call (service, object_path,
				      interface, "mime_open");
  if (msg)
    {
      // dbus_message_set_no_reply (msg, TRUE);
		
      dbus_message_iter_init_append (msg, &iter);
      dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &file);

#if 1
      dbus_connection_send (con, msg, NULL);
#else
      dbus_error_init (&error);
      reply = dbus_connection_send_with_reply_and_block (con, msg, 2000,
							 &error);
      if (dbus_error_is_set (&error))
	fprintf (stderr, "mime_open: %s\n", error.message);
      else
	fprintf (stderr, "done\n");
#endif

      dbus_message_unref (msg);
    }
}

int
main (int argc, char **argv)
{
  DBusError error;
  DBusConnection *connection;

  if (argc > 1)
    {
      dbus_error_init (&error);
      connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
      if (connection == NULL)
	{
	  fprintf (stderr, "dbus: %s\n", error.message);
	  exit (1);
	}

      dbus_connection_setup_with_g_main (connection, NULL);

      mime_open (connection, argv[1]);

      {
	GMainLoop *loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (loop);
      }

    }

  exit (0);
}
