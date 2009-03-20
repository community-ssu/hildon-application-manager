/*
 * This file is part of hildon-application-manager.
 *
 * Original version is part of osso-backup.
 *
 * Copyright (C) 2007, 2008 Nokia Corporation.
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


#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <dbus/dbus.h>

#define RUN_TEST_BLURB                                                          \
        "\n"                                                                    \
        "IMPORTANT:\n"                                                          \
        "\n"                                                                    \
        "This test will kill applications currently open in your build\n"       \
        "environment, you should be aware of this before start this test.\n"    \
        "\n"                                                                    \
        "If you want to continue, you can run this test with -r or --run.\n"    \
        "\n"


static gboolean      run_tests = FALSE;
static GOptionEntry  entries[] = {
        { "run", 'r', 0, G_OPTION_ARG_NONE, &run_tests, "Actually start the test.", NULL },
        { NULL }
};

void
close_apps (void)
{
  DBusConnection *conn;
  DBusMessage    *msg;

  conn = dbus_bus_get (DBUS_BUS_SESSION, NULL);
  if (!conn)
    {
      g_warning ("Could not get session bus.");
      return;
    }

  /*
   * This signal will close all non shown applications...
   */
  msg = dbus_message_new_signal ("/com/nokia/osso_app_killer",
                                 "com.nokia.osso_app_killer",
                                 "exit");

  dbus_connection_send (conn, msg, NULL);
  dbus_connection_flush (conn);

  dbus_connection_unref (conn);
  g_debug("close_apps(): message sent");
}


int
main (int argc, char **argv)
{
    GOptionContext *context;

    context = g_option_context_new ("- test app killing hildon-desktop API");
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (!run_tests)
      {
        g_printerr (RUN_TEST_BLURB);
        return EXIT_SUCCESS;
      }

    g_debug("close_apps()");

    close_apps();

    return EXIT_SUCCESS;
}

