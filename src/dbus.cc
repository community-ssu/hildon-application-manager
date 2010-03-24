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

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <string.h>
#include <libintl.h>
#include <libhal.h>

#include "apt-worker-client.h"
#include "dbus.h"
#include "util.h"
#include "log.h"
#include "main.h"
#include "operations.h"

#include <mce/dbus-names.h>
#include <mce/mode-names.h>

/* For getting and tracking the Bluetooth name
 */
#define BT_SERVICE                      "org.bluez"
#define BTMAN_REQUEST_IF                "org.bluez.Manager"
#define BTNAME_REQUEST_IF               "org.bluez.Adapter"
#define BTNAME_SIGNAL_IF                "org.bluez.Adapter"
#define BTMAN_REQUEST_PATH              "/"
#define BTNAME_ADAPTER_PATH             "/org/bluez/hci0"

#define BTMAN_REQ_ADAPTER               "DefaultAdapter"
#define BTNAME_REQ_GET                  "GetProperties"
#define BTNAME_SIG_CHANGED              "PropertyChanged"

#define BTNAME_MATCH_RULE "type='signal',interface='" BTNAME_SIGNAL_IF \
                          "',member='" BTNAME_SIG_CHANGED "'"

static void
install_package (DBusConnection *conn, DBusMessage *message, bool trusted)
{
  DBusError error;
  DBusMessage *reply;
  char *filename;

  dbus_error_init (&error);
  if (dbus_message_get_args (message, &error,
			     DBUS_TYPE_STRING, &filename,
			     DBUS_TYPE_INVALID))
    {
      present_main_window ();
      if (strcmp (filename, "magic:restore-packages") == 0)
	restore_packages_flow ();
      else
	install_from_file_flow (filename, trusted);

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

      maybe_init_packages_list ();
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
        install_named_packages (c->packages, INSTALL_TYPE_MULTI, false,
                                c->title, c->desc, dip_install_done, c);
      else
	dip_end (-1, c);
    }
  else
    {
      present_main_window ();
      if (start_interaction_flow ())
        install_named_packages (c->packages, INSTALL_TYPE_MULTI, false,
                                c->title, c->desc, dip_install_done, c);
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

struct dif_clos {
  Window xid;
  char *filename;
  DBusConnection *conn;
  DBusMessage *message;
};

static void dbus_install_file (DBusConnection *conn, DBusMessage *message);
static void dif_with_initialized_packages (void *data);
static void dif_install_done (bool success, void *data);
static void dif_end (int result, void *data);

static void
dbus_install_file (DBusConnection *conn, DBusMessage *message)
{
  DBusError error;

  dbus_int32_t xid;

  dbus_connection_ref (conn);
  dbus_message_ref (message);

  dif_clos *c = new dif_clos;
  c->conn = conn;
  c->message = message;
  c->filename = NULL;

  dbus_error_init (&error);
  if (dbus_message_get_args (message, &error,
			     DBUS_TYPE_INT32, &xid,
			     DBUS_TYPE_STRING, &c->filename,
			     DBUS_TYPE_INVALID))
    {
      c->xid = xid;

      maybe_init_packages_list ();
      with_initialized_packages (dif_with_initialized_packages, c);
    }
  else
    {
      DBusMessage *reply;
      reply = dbus_message_new_error (message,
				      DBUS_ERROR_INVALID_ARGS,
				      error.message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);
      dif_end (-1, c);
    }
}

static void
dif_with_initialized_packages (void *data)
{
  dif_clos *c = (dif_clos *)data;

  if (c->xid)
    {
      if (start_foreign_interaction_flow (c->xid))
	install_file (c->filename, false, dif_install_done, c);
      else
	dif_end (-1, c);
    }
  else
    {
      present_main_window ();
      if (start_interaction_flow ())
	install_file (c->filename, false, dif_install_done, c);
      else
	dif_end (-1, c);
    }
}

static void
dif_install_done (bool success, void *data)
{
  dif_clos *c = (dif_clos *)data;

  end_interaction_flow ();

  dif_end (success? 1 : 0, c);
}

static void
dif_end (int result, void *data)
{
  dif_clos *c = (dif_clos *)data;

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

  dbus_message_unref (c->message);
  dbus_connection_unref (c->conn);
  delete c;

  maybe_exit ();
}

static void
dbus_top_application (DBusConnection *conn, DBusMessage *message)
{
  DBusMessage *reply;

  present_main_window ();
  maybe_init_packages_list ();

  reply = dbus_message_new_method_return (message);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}

static void
dbus_show_check_for_updates_view (DBusConnection *conn, DBusMessage *message)
{
  DBusMessage *reply;

  present_main_window ();
  maybe_init_packages_list ();

  if (is_idle ())
    show_check_for_updates_view ();

  reply = dbus_message_new_method_return (message);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}

static void
dbus_showing_check_for_updates_view (DBusConnection *conn, DBusMessage *message)
{
  DBusMessage *reply;
  gboolean showing_view = FALSE;

  /* Check if 'check for updates' view is being shown */
  if (get_current_view_id () == UPGRADE_APPLICATIONS_VIEW)
    showing_view = TRUE;

  /* Build reply message with the required boolean value */
  reply = dbus_message_new_method_return (message);
  dbus_message_append_args (reply,
                            DBUS_TYPE_BOOLEAN , &showing_view,
                            DBUS_TYPE_INVALID);

  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}

static void icfu_end (bool ignored, void *data);

static void
idle_check_for_updates (void *unused)
{
  refresh_package_cache_without_user (NULL, icfu_end, NULL);
}

static void
icfu_end (bool ignored, void *data)
{
  end_interaction_flow ();
}

static void
dbus_check_for_updates (DBusConnection *conn, DBusMessage *message)
{
  DBusMessage *reply;

  maybe_init_packages_list ();
  start_interaction_flow_when_idle (idle_check_for_updates, NULL);

  reply = dbus_message_new_method_return (message);
  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);
}

static void dsp_with_initialized_packages (void *data);

static void
dbus_search_packages (DBusConnection *conn, DBusMessage *message)
{
  DBusError error;
  const gchar *arg_pattern;
  gchar *pattern;
  dbus_int32_t dbus_result = 1;

  dbus_connection_ref (conn);
  dbus_message_ref (message);

  dbus_error_init (&error);
  if (dbus_message_get_args (message, &error,
			     DBUS_TYPE_STRING, &arg_pattern,
			     DBUS_TYPE_INVALID))
    {
      pattern = g_strdup (arg_pattern);

      if (pattern != NULL)
        {
          maybe_init_packages_list ();
          with_initialized_packages (dsp_with_initialized_packages, pattern);
        }
    }
  else
    {
      DBusMessage *reply;
      reply = dbus_message_new_error (message,
				      DBUS_ERROR_INVALID_ARGS,
				      error.message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);
      dbus_result = -1;
    }

  DBusMessage * reply = dbus_message_new_method_return (message);
  dbus_message_append_args (reply,
                            DBUS_TYPE_INT32, &dbus_result,
                            DBUS_TYPE_INVALID);

  dbus_connection_send (conn, reply, NULL);
  dbus_message_unref (reply);

  // So that we don't lose the reply when we exit below.
  dbus_connection_flush (conn);

  dbus_message_unref (message);
  dbus_connection_unref (conn);
}

static void
dsp_with_initialized_packages (void *data)
{
  gchar *pattern = (gchar *) data;

  present_main_window ();
  if (is_idle ())
    {
      show_install_applications_view ();
      search_packages (pattern, true);
    }
  g_free (pattern);
}

static DBusHandlerResult
dbus_handler (DBusConnection *conn, DBusMessage *message, void *data)
{
  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "mime_open"))
    {
      install_package (conn, message, false);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "install_trusted_package"))
    {
      install_package (conn, message, true);
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
				   "install_file"))
    {
      dbus_install_file (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "top_application"))
    {
      dbus_top_application (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "show_check_for_updates_view"))
    {
      dbus_show_check_for_updates_view (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "showing_check_for_updates_view"))
    {
      dbus_showing_check_for_updates_view (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "check_for_updates"))
    {
      dbus_check_for_updates (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
                                   "com.nokia.hildon_application_manager",
                                   "search_packages"))
    {
      dbus_search_packages (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static char *btname = NULL;

const char *
device_name ()
{
  if (btname != NULL)
    return btname;
  else
    {
      const char *name = getenv ("OSSO_PRODUCT_NAME");
      if (name)
        return name;

      return "";
    }
}

/* Utility function to extract property values from BlueZ4 reply */
static gchar *
string_property_from_message (DBusMessage *message, const char *property_name)
{
  DBusMessageIter itr, ar_itr, dic_itr, var_itr;
  char *ret = NULL, *dic_entry_name = NULL;
  int the_type = 0;

  if (!dbus_message_iter_init (message, &itr))
    {
      add_log ("message did not have argument\n");
      return ret;
    }

  for (; ((the_type = dbus_message_iter_get_arg_type (&itr))
          != DBUS_TYPE_INVALID); dbus_message_iter_next (&itr))
    if (DBUS_TYPE_ARRAY == the_type)
      {
        dbus_message_iter_recurse(&itr, &ar_itr);
        for (; ((the_type = dbus_message_iter_get_arg_type (&ar_itr))
                != DBUS_TYPE_INVALID); dbus_message_iter_next (&ar_itr))
          if (DBUS_TYPE_DICT_ENTRY == the_type)
            {
              dbus_message_iter_recurse(&ar_itr, &dic_itr);
              if ((dbus_message_iter_get_arg_type (&dic_itr))
                  == DBUS_TYPE_STRING)
                {
                  /* checking for property name ... */
                  dbus_message_iter_get_basic(&dic_itr, &dic_entry_name);
                  if (dic_entry_name && !strcmp(dic_entry_name, property_name))
                    {
                      dbus_message_iter_next (&dic_itr);
                      if ((dbus_message_iter_get_arg_type (&dic_itr))
                          == DBUS_TYPE_VARIANT)
                        {
                          dbus_message_iter_recurse (&dic_itr, &var_itr);
                          if ((dbus_message_iter_get_arg_type (&var_itr))
                              == DBUS_TYPE_STRING) {
                            /* getting property value */
                            dbus_message_iter_get_basic (&var_itr, &ret);
                            if (ret)
                              ret = g_strdup (ret);
                            break;
                          }
                        }
                    }
                }
            }
      }
  return ret;
}

static gchar *
string_value_from_signal (DBusMessage *message)
{
  DBusMessageIter iter, value_iter;
  gchar *name, *value;

  dbus_message_iter_init (message, &iter);
  dbus_message_iter_get_basic (&iter, &name);

  if (strncmp (name, "Name", 4))
    return NULL; /* the name didn't change... */

  dbus_message_iter_next (&iter);
  dbus_message_iter_recurse (&iter, &value_iter);
  dbus_message_iter_get_basic (&value_iter, &value);

  if (value)
    value = g_strdup (value);

  return value;
}

static void
set_bt_name_from_message (DBusMessage *message,
                          bool from_property)
{
  g_return_if_fail (message != NULL);

  if (btname)
    g_free (btname);

  if (from_property)
    btname = string_property_from_message (message, "Name");
  else
    btname = string_value_from_signal (message);

  if (!btname) /* btname can be NULL when the Name property didn't change */
    return;

  // Update an unused label with device's name
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
      add_log ("get btname: no reply\n");
      return;
    }

  dbus_error_init (&error);

  if (dbus_set_error_from_message (&error, message))
    {
      add_log ("get btname: %s\n", error.message);
      dbus_error_free (&error);
    }
  else
    set_bt_name_from_message (message, true);

  dbus_message_unref (message);
}

static DBusHandlerResult
handle_dbus_signal (DBusConnection *conn,
		    DBusMessage *msg,
		    gpointer data)
{
  if (dbus_message_is_signal(msg, BTNAME_SIGNAL_IF, BTNAME_SIG_CHANGED))
    set_bt_name_from_message (msg, false);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
btadapter_received (DBusPendingCall *call, void *user_data)
{
  DBusMessage *message, *request;
  DBusMessageIter itr;
  DBusConnection *connection;
  gchar *str, *adapter;
  DBusError error;

  g_assert (dbus_pending_call_get_completed (call));
  message = dbus_pending_call_steal_reply (call);
  if (message == NULL)
    {
      add_log ("get btadapter: no reply\n");
      return;
    }

  dbus_error_init (&error);

  if (dbus_set_error_from_message (&error, message))
    {
      add_log ("get btadapter: %s\n", error.message);
      dbus_error_free (&error);
    }
  else
    {
      dbus_message_iter_init (message, &itr);
      dbus_message_iter_get_basic (&itr, &str);

      if (str)
        adapter = g_strdup (str);
      else /* fallback ... */
        adapter = g_strdup (BTNAME_ADAPTER_PATH);

      request = dbus_message_new_method_call (BT_SERVICE,
                                              adapter,
                                              BTNAME_REQUEST_IF,
                                              BTNAME_REQ_GET);
      if (request == NULL)
        {
          fprintf (stderr, "dbus_message_new_method_call failed\n");
          return;
        }

      connection = (DBusConnection *) user_data;

      if (dbus_connection_send_with_reply (connection, request, &call, -1))
        {
          dbus_pending_call_set_notify (call, btname_received, NULL, NULL);
          dbus_pending_call_unref (call);
        }

      dbus_message_unref (request);
    }
  dbus_message_unref (message);
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
  /* Warning: connection variable reused for system bus */
  connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
  if (connection == NULL)
    {
      fprintf (stderr, "Can't get system dbus: %s", error.message);
      exit (1);
    }

  /* Let's query initial state.  These calls are async, so they do not
     consume too much startup time.
   */
  request = dbus_message_new_method_call (BT_SERVICE,
                                          BTMAN_REQUEST_PATH,
                                          BTMAN_REQUEST_IF,
                                          BTMAN_REQ_ADAPTER);
  if (request == NULL)
    {
      fprintf (stderr, "dbus_message_new_method_call failed\n");
      return;
    }

  dbus_message_set_auto_start (request, TRUE);

  if (dbus_connection_send_with_reply (connection, request, &call, -1))
    {
      dbus_pending_call_set_notify (call, btadapter_received,
                                    connection, NULL);
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

bool
enough_battery_p (void)
{
  LibHalContext *hal;

  int i;
  char **devs;
  int n_devs;

  hal = libhal_ctx_new ();
  libhal_ctx_set_dbus_connection (hal, dbus_bus_get (DBUS_BUS_SYSTEM, NULL));
  devs = libhal_find_device_by_capability (hal, "battery", &n_devs, NULL);

  if (devs)
    {
      for (i = 0; i < n_devs; i++)
	{
	  DBusError error;

	  dbus_error_init (&error);
	  dbus_bool_t charging = libhal_device_get_property_bool
	    (hal, devs[i], "battery.rechargeable.is_charging", &error);

	  if (dbus_error_is_set (&error))
	    dbus_error_free (&error);
	  else
	    {
	      if (charging)
		break;
	    }

	  dbus_error_init (&error);
	  dbus_int32_t percentage = libhal_device_get_property_int
	    (hal, devs[i], "battery.charge_level.percentage", &error);

	  if (dbus_error_is_set (&error))
	    {
	      dbus_error_free (&error);
	      break;
	    }
	  else
	    {
	      if (percentage > 50)
		break;
	    }
	}
    }

  libhal_ctx_shutdown (hal, NULL);

  return devs == NULL || i < n_devs;
}
