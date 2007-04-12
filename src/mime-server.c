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

static char* type_to_name (int message_type)
{
    switch (message_type)
    {
      case DBUS_MESSAGE_TYPE_SIGNAL:
	return "signal";
      case DBUS_MESSAGE_TYPE_METHOD_CALL:
	return "method call";
      case DBUS_MESSAGE_TYPE_METHOD_RETURN:
	return "method return";
      case DBUS_MESSAGE_TYPE_ERROR:
	return "error";
      default:
	return "(unknown message type)";
    }
}

DBusHandlerResult 
handler (DBusConnection *conn, DBusMessage *message, void *data)
{
  const char *sender;
  int message_type;
    
  message_type = dbus_message_get_type (message);
  sender = dbus_message_get_sender (message); 

  if (dbus_message_is_method_call (message, "com.nokia.foo", "mime_open"))
    {
      printf ("mime_open!\n");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  else
    {
      printf ("???\n");
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
}

int
main (int argc, char **argv)
{
  DBusError error;
  DBusConnection *connection;

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "dbus: %s\n", error.message);
      exit (1);
    }

  dbus_connection_setup_with_g_main (connection, NULL);

  if (!dbus_connection_add_filter (connection, handler, NULL, NULL))
    {
      fprintf (stderr, "can't add filter\n");
      exit (1);
    }

  sleep (1);

  if (dbus_bus_request_name (connection, "com.nokia.foo", 0, &error) < 0)
    {
      fprintf (stderr, "request_name: %s\n", error.message);
      exit (1);
    }

  {
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);
  }
}
