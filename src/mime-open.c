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

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <hildon/hildon-window.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

GtkWidget *window, *grab_widget;
DBusConnection *connection;

void
install_reply (DBusPendingCall *pending_reply, void *data)
{
  DBusMessage *reply = dbus_pending_call_steal_reply (pending_reply);
  if (reply)
    {
      DBusError error;
      dbus_int32_t result;

      dbus_error_init (&error);
      if (dbus_set_error_from_message (&error, reply))
	fprintf (stderr, "ERROR: %s\n", error.message);
      else if (dbus_message_get_args (reply, NULL,
				      DBUS_TYPE_INT32, &result,
				      DBUS_TYPE_INVALID))
	{
	  fprintf (stderr, "done: %d\n", result);
	  if (result < 0)
	    hildon_banner_show_information (GTK_WIDGET (NULL),
					    NULL, "AM is busy");
	}
      else
	fprintf (stderr, "done: malformed reply\n");
      dbus_message_unref (reply);
    }

  gtk_grab_remove (grab_widget);
  gtk_widget_destroy (grab_widget);
}

void
install (GtkWidget *parent, const char *arg)
{
  DBusMessage     *msg;
  gchar           *service = "com.nokia.hildon_application_manager";
  gchar           *object_path = "/com/nokia/hildon_application_manager";
  gchar           *interface = "com.nokia.hildon_application_manager";
  dbus_int32_t     xid = GDK_WINDOW_XID (parent->window);
  DBusPendingCall *pending_return = NULL;
  const char      *title = "Special offer";
  const char      *desc = "Trust me, you want these packages";
  const char      *package_array[] = { arg, "karl", "erik" };
  const char     **packages = package_array;

  msg = dbus_message_new_method_call (service, object_path,
				      interface, "install_packages");
  if (msg)
    {
      dbus_message_append_args (msg,
				DBUS_TYPE_INT32, &xid,
				DBUS_TYPE_STRING, &title,
				DBUS_TYPE_STRING, &desc,
				DBUS_TYPE_ARRAY,
				DBUS_TYPE_STRING, &packages, 3,
				DBUS_TYPE_INVALID);

      if (dbus_connection_send_with_reply (connection, msg,
					   &pending_return,
					   INT_MAX))
	{
	  grab_widget = gtk_invisible_new ();
	  gtk_widget_show (grab_widget);
	  gtk_grab_add (grab_widget);
	  dbus_pending_call_set_notify (pending_return, install_reply,
					NULL, NULL);
	}

      dbus_message_unref (msg);
    }
}

void
install_clicked (GtkWidget *button, gpointer data)
{
  GtkEntry *entry = GTK_ENTRY (data);

  install (window, gtk_entry_get_text (entry));
}

void
show ()
{
  DBusMessage     *msg;
  gchar           *service = "com.nokia.hildon_application_manager";
  gchar           *object_path = "/com/nokia/hildon_application_manager";
  gchar           *interface = "com.nokia.hildon_application_manager";

  msg = dbus_message_new_method_call (service, object_path,
				      interface, "top_application");
  if (msg)
    {
      dbus_connection_send (connection, msg, NULL);
      dbus_message_unref (msg);
    }
}

void
show_clicked (GtkWidget *button, gpointer data)
{
  show ();
}

static gboolean
window_delete_event (GtkWidget* widget, GdkEvent *ev, gpointer data)
{
  gtk_main_quit ();
  return TRUE;
}

int
main (int argc, char **argv)
{
  GtkWidget *vbox, *entry, *button, *show_button;
  DBusError error;

  gtk_init (&argc, &argv);

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "dbus: %s\n", error.message);
      exit (1);
    }

  dbus_connection_setup_with_g_main (connection, NULL);

  window = hildon_window_new ();
  gtk_window_set_title (GTK_WINDOW (window), "AM remote control");
  g_set_application_name ("");
  g_signal_connect (window, "delete-event",
		    G_CALLBACK (window_delete_event), NULL);

  entry = gtk_entry_new ();
  if (argc > 0)
    gtk_entry_set_text (GTK_ENTRY (entry), argv[1]);

  button = gtk_button_new_with_label ("install");
  g_signal_connect (button, "clicked",
		    G_CALLBACK (install_clicked), entry);

  show_button = gtk_button_new_with_label ("show");
  g_signal_connect (show_button, "clicked",
		    G_CALLBACK (show_clicked), entry);

  vbox = gtk_vbox_new (5, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), entry, 10, FALSE, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), button, 10, FALSE, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), show_button, 10, FALSE, FALSE);

  gtk_container_add (GTK_CONTAINER (window), vbox);

  gtk_widget_show_all (window);

  gtk_main ();

  exit (0);
}
