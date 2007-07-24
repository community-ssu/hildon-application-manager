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

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <string.h>
#include <libintl.h>

#include "dbus.h"
#include "util.h"
#include "log.h"
#include "main.h"
#include "operations.h"

/* For getting and tracking the Bluetooth name
 */
#define BTNAME_SERVICE                  "org.bluez"
#define BTNAME_REQUEST_IF               "org.bluez.Adapter"
#define BTNAME_SIGNAL_IF                "org.bluez.Adapter"
#define BTNAME_REQUEST_PATH             "/org/bluez/hci0"
#define BTNAME_SIGNAL_PATH              "/org/bluez/hci0"

#define BTNAME_REQ_GET                  "GetName"
#define BTNAME_SIG_CHANGED              "NameChanged"

#define BTNAME_MATCH_RULE "type='signal',interface='" BTNAME_SIGNAL_IF \
                          "',member='" BTNAME_SIG_CHANGED "'"

static void
dbus_mime_open (DBusConnection *conn, DBusMessage *message)
{
  DBusError error;
  DBusMessage *reply;
  char *filename;

  dbus_error_init (&error);
  if (dbus_message_get_args (message, &error,
			     DBUS_TYPE_STRING, &filename,
			     DBUS_TYPE_INVALID))
    {
      fprintf (stderr, "MIME_OPEN %s\n", filename);
      
      present_main_window ();
      if (strcmp (filename, "magic:restore-packages") == 0)
	restore_packages_flow ();
      else if (strcmp (filename, "magic:update-system") == 0)
	update_system_flow ();
      else
	install_from_file_flow (filename);

      reply = dbus_message_new_method_return (message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);
    }
  else
    {
      reply = dbus_message_new_error (message,
				      DBUS_ERROR_INVALID_ARGS,
				      error.message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);
    }
}

struct dip_clos {
  Window xid;
  char *title;
  char *desc;
  const char **packages;
  DBusConnection *conn;
  DBusMessage *message;
};

static void dbus_install_packages (DBusConnection *conn, DBusMessage *message);
static void dip_with_initialized_packages (void *data);
static void dip_install_done (int n_successful, void *data);
static void dip_end (int result, void *data);

static void
dbus_install_packages (DBusConnection *conn, DBusMessage *message)
{
  DBusError error;
  
  dbus_int32_t xid;
  const char **packages;
  int n_packages;

  dbus_connection_ref (conn);
  dbus_message_ref (message);
  
  dip_clos *c = new dip_clos;
  c->conn = conn;
  c->message = message;
  c->packages = NULL;

  dbus_error_init (&error);
  if (dbus_message_get_args (message, &error,
			     DBUS_TYPE_INT32, &xid,
			     DBUS_TYPE_STRING, &c->title,
			     DBUS_TYPE_STRING, &c->desc,
			     DBUS_TYPE_ARRAY,
			     DBUS_TYPE_STRING, &packages, &n_packages,
			     DBUS_TYPE_INVALID))
    {
      c->xid = xid;
      c->packages = packages;
      
      with_initialized_packages (dip_with_initialized_packages, c);
    }
  else
    {
      DBusMessage *reply;
      reply = dbus_message_new_error (message,
				      DBUS_ERROR_INVALID_ARGS,
				      error.message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);
      dip_end (-1, c);
    }
}

static void
dip_with_initialized_packages (void *data)
{
  dip_clos *c = (dip_clos *)data;

  if (c->xid)
    {
      if (start_foreign_interaction_flow (c->xid))
	install_named_packages (APTSTATE_DEFAULT, c->packages,
				INSTALL_TYPE_MULTI, false,
				c->title, c->desc,
				dip_install_done, c);
      else
	dip_end (-1, c);
    }
  else
    {
      present_main_window ();
      if (start_interaction_flow ())
	install_named_packages (APTSTATE_DEFAULT, c->packages,
				INSTALL_TYPE_MULTI, false,
				c->title, c->desc,
				dip_install_done, c);
      else
	dip_end (-1, c);
    }
}

static void
dip_install_done (int n_successful, void *data)
{
  dip_clos *c = (dip_clos *)data;

  end_interaction_flow ();

  dip_end (n_successful, c);
}

static void
dip_end (int result, void *data)
{
  dip_clos *c = (dip_clos *)data;

  DBusMessage *reply;
  dbus_int32_t dbus_result = result;
	  
  reply = dbus_message_new_method_return (c->message);
  dbus_message_append_args (reply,
			    DBUS_TYPE_INT32, &dbus_result,
			    DBUS_TYPE_INVALID);
  
  dbus_connection_send (c->conn, reply, NULL);
  dbus_message_unref (reply);

  // So that we don't lose the reply when we exit below.
  dbus_connection_flush (c->conn);

  dbus_free_string_array ((char **)c->packages);
  dbus_message_unref (c->message);
  dbus_connection_unref (c->conn);
  delete c;

  maybe_exit ();
}

static DBusHandlerResult 
dbus_handler (DBusConnection *conn, DBusMessage *message, void *data)
{
  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "mime_open"))
    {
      dbus_mime_open (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "install_packages"))
    {
      dbus_install_packages (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "top_application"))
    {
      DBusMessage *reply;

      fprintf (stderr, "SHOW\n");

      present_main_window ();

      reply = dbus_message_new_method_return (message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);

      return DBUS_HANDLER_RESULT_HANDLED;
    }

  fprintf (stderr, "UNKNOWN METHOD CALL\n");

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static char *btname = NULL;

const char *
device_name ()
{
  if (btname != NULL)
    return btname;
  else
    return dgettext ("hildon-fm", "sfil_li_folder_root");
}

static void
set_bt_name_from_message (DBusMessage *message)
{
  DBusMessageIter iter;
  const char *name = NULL;
  GtkWidget *label = NULL;

  g_return_if_fail (message != NULL);

  if (!dbus_message_iter_init (message, &iter))
    {
      add_log ("message did not have argument\n");
      return;
    }
  dbus_message_iter_get_basic (&iter, &name);

  if (btname) 
    g_free (btname);

  btname = g_strdup (name);

  label = get_device_label ();

  if (label)
    gtk_label_set_text (GTK_LABEL (label), btname);
}

static void 
btname_received (DBusPendingCall *call, void *user_data)
{
  DBusMessage *message;
  DBusError error;

  g_assert (dbus_pending_call_get_completed (call));
  message = dbus_pending_call_steal_reply (call);
  if (message == NULL)
    {
      add_log ("no reply\n");
      return;
    }

  dbus_error_init (&error);

  if (dbus_set_error_from_message (&error, message))
    {
      add_log ("get btname: %s\n", error.message);
      dbus_error_free (&error);
    }
  else   
    set_bt_name_from_message (message);

  dbus_message_unref (message);
}

static DBusHandlerResult
handle_dbus_signal (DBusConnection *conn,
		    DBusMessage *msg,
		    gpointer data)
{
  if (dbus_message_is_signal(msg, BTNAME_SIGNAL_IF, BTNAME_SIG_CHANGED))
    set_bt_name_from_message(msg);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void
init_dbus_or_die (bool top_existing)
{
  DBusError error;
  DBusConnection *connection;
  DBusMessage *request;
  DBusPendingCall *call = NULL;

  /* Set ourself up on the session bus.
   */

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Can't get session dbus: %s", error.message);
      exit (1);
    }

  dbus_connection_setup_with_g_main (connection, NULL);

  if (!dbus_connection_add_filter (connection, dbus_handler, NULL, NULL))
    {
      fprintf (stderr, "Can't add dbus filter");
      exit (1);
    }

  dbus_error_init (&error);
  int result = dbus_bus_request_name (connection,
				      "com.nokia.hildon_application_manager",
				      DBUS_NAME_FLAG_DO_NOT_QUEUE,
				      &error);

  if (result < 0)
    {
      fprintf (stderr, "Can't request name on dbus: %s\n", error.message);
      exit (1);
    }

  if (result == DBUS_REQUEST_NAME_REPLY_EXISTS)
    {
      /* There is already an instance of us running.  Bring it to the
	 front if requested.
      */
      if (top_existing)
	{
	  request = dbus_message_new_method_call 
	    ("com.nokia.hildon_application_manager",
	     "/com/nokia/hildon_application_manager",
	     "com.nokia.hildon_application_manager",
	     "top_application");
	  
	  if (request)
	    dbus_connection_send_with_reply_and_block (connection, request,
						       INT_MAX, NULL);
	}

      exit (0);
    }

  if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
      /* Wierdness, but let's continue anyway.
       */
      fprintf (stderr, "Couldn't be the primary owner.\n");
    }

  /* Listen on the system bus for changes to the device name.
   */

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Can't get system dbus: %s", error.message);
      exit (1);
    }

  /* Let's query initial state.  These calls are async, so they do not
     consume too much startup time.
   */
  request = dbus_message_new_method_call (BTNAME_SERVICE,
					  BTNAME_REQUEST_PATH,
					  BTNAME_REQUEST_IF,
					  BTNAME_REQ_GET);
  if (request == NULL)
    {
      fprintf (stderr, "dbus_message_new_method_call failed\n");
      return;
    }

  dbus_message_set_auto_start (request, TRUE);

  if (dbus_connection_send_with_reply (connection, request, &call, -1))
    {
      dbus_pending_call_set_notify (call, btname_received, NULL, NULL);
      dbus_pending_call_unref (call);
    }

  dbus_message_unref (request);

  dbus_connection_setup_with_g_main (connection, NULL);
  dbus_bus_add_match (connection, BTNAME_MATCH_RULE, &error);
  if (dbus_error_is_set(&error))
    {
      fprintf (stderr, "dbus_bus_add_match failed: %s\n", error.message);
      dbus_error_free (&error);
    }

  if (!dbus_connection_add_filter(connection, handle_dbus_signal, NULL, NULL))
    fprintf (stderr, "dbus_connection_add_filter failed\n");
}