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

#define _GNU_SOURCE

#include "ham-updates-status-menu-item.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <hildon/hildon.h>
#include <libosso.h>
#include <clockd/libtime.h>
#include <libalarm.h>
#include <conic.h>

#include <xexp.h>
#include <user_files.h>

#define DEBUG

#include "util.h"
#include "ham-updates.h"
#include "ham-notifier.h"
#include "update-notifier-conf.h"

/* appname for OSSO and alarmd */
#define APPNAME                  "ham_updates_status_menu_item"

/* inotify paths */
#define  INOTIFY_DIR             "/var/lib/hildon-application-manager"

#define STATUSBAR_ICON_NAME      "app_install_new_updates"
#define STATUSBAR_ICON_SIZE      16

#define BLINK_INTERVAL           500 /* milliseconds */

#define HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), HAM_UPDATES_STATUS_MENU_ITEM_TYPE, HamUpdatesStatusMenuItemPrivate))

typedef enum _State State;
enum _State
  {
    ICON_STATE_INVISIBLE,
    ICON_STATE_STATIC,
    ICON_STATE_BLINKING
  };

typedef enum _ConState ConState;
enum _ConState
  {
    CONN_ONLINE,
    CONN_OFFLINE
  };

/* inotify watchers id */
enum
  {
    HOME, VAR, MAXWATCH
  };

typedef struct _HamUpdatesStatusMenuItemPrivate HamUpdatesStatusMenuItemPrivate;
struct _HamUpdatesStatusMenuItemPrivate
{
  /* ui */
  GdkPixbuf *no_icon;
  GdkPixbuf *icon;

  /* environment */
  osso_context_t *osso;
  GConfClient *gconf;

  /* state */
  State state;

  /* alarm id */
  cookie_t alarm_cookie;

  /* inotify */
  gint inotify_fd;
  guint io_watch;
  gint wd[2];

  /* libconic */
  ConIcConnection *conic;
  ConState constate;

  /* blinker timeout */
  guint blinker_id;

  /* updates object */
  HamUpdates *updates;
};

/* setup prototypes */
static gboolean setup_dbus (HamUpdatesStatusMenuItem *self);
static void setup_gconf (HamUpdatesStatusMenuItem *self);
static gboolean setup_alarm (HamUpdatesStatusMenuItem *upno);
static gboolean setup_alarm_now (gpointer data);
static gboolean setup_inotify (HamUpdatesStatusMenuItem *self);
static void close_inotify (HamUpdatesStatusMenuItem *self);
static void setup_ui (HamUpdatesStatusMenuItem *self);

/* teardown prototypes */
static void delete_all_alarms (void);

/* state handling prototypes */
static void load_state (HamUpdatesStatusMenuItem *self);
static void save_state (HamUpdatesStatusMenuItem *self);
static void update_state (HamUpdatesStatusMenuItem *self);
static void set_state (HamUpdatesStatusMenuItem *self, State state);
static State get_state (HamUpdatesStatusMenuItem *self);

/* connection state prototypes */
static void setup_connection_state (HamUpdatesStatusMenuItem *self);

static void ham_updates_status_menu_display_event_cb
(osso_display_state_t state, gpointer data);

/* icon blinking */
static void blink_icon_off (HamUpdatesStatusMenuItem *self);
static void blink_icon_on (HamUpdatesStatusMenuItem *self);

HD_DEFINE_PLUGIN_MODULE (HamUpdatesStatusMenuItem, ham_updates_status_menu_item,
                         HD_TYPE_STATUS_MENU_ITEM);

static void
ham_updates_status_menu_item_class_finalize
(HamUpdatesStatusMenuItemClass *klass)
{
  /* noop */
}

static void
ham_updates_status_menu_item_finalize (GObject *object)
{
  HamUpdatesStatusMenuItem *self;
  HamUpdatesStatusMenuItemPrivate *priv;

  self = HAM_UPDATES_STATUS_MENU_ITEM (object);
  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  blink_icon_off (self);

  if (priv->icon != NULL)
    g_object_unref (priv->icon);

  if (priv->no_icon != NULL)
    g_object_unref (priv->no_icon);

  delete_all_alarms ();

  close_inotify (self);

  if (priv->updates != NULL)
    g_object_unref (priv->updates);

  if (priv->conic != NULL)
    g_object_unref (priv->conic);

  if (priv->gconf != NULL)
    g_object_unref (priv->gconf);

  if (priv->osso != NULL)
    osso_deinitialize (priv->osso);

  G_OBJECT_CLASS (ham_updates_status_menu_item_parent_class)->finalize (object);
}

static void
ham_updates_status_menu_item_class_init (HamUpdatesStatusMenuItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = ham_updates_status_menu_item_finalize;

  g_type_class_add_private (object_class,
                            sizeof (HamUpdatesStatusMenuItemPrivate));
}

static void
ham_updates_status_menu_item_init (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  priv->osso = NULL;

  priv->state = ICON_STATE_INVISIBLE;

  priv->alarm_cookie = 0;

  priv->inotify_fd = -1;
  priv->io_watch = 0;
  priv->wd[HOME] = priv->wd[VAR] = -1;

  priv->conic = NULL;
  priv->constate = CONN_OFFLINE;

  priv->icon = priv->no_icon = NULL;

  priv->updates = NULL;

  priv->blinker_id = 0;

  if (setup_dbus (self))
    {
      setup_gconf (self);
      setup_ui (self);

      setup_connection_state (self);
      osso_hw_set_display_event_cb (priv->osso,
                                    ham_updates_status_menu_display_event_cb,
                                    self);

      if (setup_inotify (self))
        {
          /* We only setup the alarm after a one minute pause since the alarm
             daemon is not yet running when the plugins are loaded after boot.
             It is arguably a bug in the alarm framework that the daemon needs
             to be running to access and modify the alarm queue.
          */
          g_timeout_add_seconds (60, setup_alarm_now, self);
        }
      else
        {
          close_inotify (self);
        }
    }
}

static gchar*
get_http_proxy (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  gchar *proxy;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if ((proxy = getenv ("http_proxy")) != NULL)
    return g_strdup (proxy);

  proxy = NULL;

  if (priv->conic != NULL)
    {
      const gchar* host;
      gint port;

      host = con_ic_connection_get_proxy_host (priv->conic,
                                               CON_IC_PROXY_PROTOCOL_HTTP);
      port = con_ic_connection_get_proxy_port (priv->conic,
                                               CON_IC_PROXY_PROTOCOL_HTTP);

      if (host != NULL)
        proxy = g_strdup_printf ("http://%s:%d", host, port);
    }
  else if (priv->gconf != NULL)
    {
      proxy = get_gconf_http_proxy ();
    }

  return proxy;
}

static void
ham_updates_status_menu_item_check_done_cb (HamUpdatesStatusMenuItem *self,
                                            gboolean ok, gpointer data)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (ok)
    {
      LOG ("Check for updates done");
      update_state (self);
    }
  else
    {
      /* Ask the Application Manager to perform the update, but don't
	 start it if it isn't running already.
       */
      if (ham_is_running ())
        {
          LOG ("Calling HAM RPC async");
          osso_rpc_async_run (priv->osso,
                              HILDON_APP_MGR_SERVICE,
                              HILDON_APP_MGR_OBJECT_PATH,
                              HILDON_APP_MGR_INTERFACE,
                              HILDON_APP_MGR_OP_CHECK_UPDATES,
                              NULL,
                              NULL,
                              DBUS_TYPE_INVALID);
        }
    }
}

static void
ham_updates_status_menu_display_event_cb (osso_display_state_t state,
                                          gpointer data)
{
  g_return_if_fail (IS_HAM_UPDATES_STATUS_MENU_ITEM (data));

  if (get_state (HAM_UPDATES_STATUS_MENU_ITEM (data)) == ICON_STATE_BLINKING)
    {
      if (state == OSSO_DISPLAY_OFF)
        blink_icon_off (HAM_UPDATES_STATUS_MENU_ITEM (data));
      else
        blink_icon_on (HAM_UPDATES_STATUS_MENU_ITEM (data));
    }
}

static void
check_for_updates (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (priv->constate == CONN_ONLINE)
    {
      gchar *proxy;

      proxy = get_http_proxy (self);
      ham_updates_check (priv->updates, proxy);
      g_free (proxy);
    }
}

static void
check_for_notifications (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (priv->constate == CONN_ONLINE)
    {
      gchar *proxy;

      proxy = get_http_proxy (self);
      ham_notifier_check (proxy);
      g_free (proxy);
    }
}

static gint
ham_updates_status_menu_item_rpc_cb (const gchar* interface,
                                     const gchar* method,
                                     GArray* arguments,
                                     gpointer data,
                                     osso_rpc_t* retval)
{
  HamUpdatesStatusMenuItem *self;

  g_return_val_if_fail (IS_HAM_UPDATES_STATUS_MENU_ITEM (data), OSSO_ERROR);
  g_return_val_if_fail (interface != NULL && method != NULL, OSSO_ERROR);

  LOG ("RPC Message: %s:%s", interface, method);

  self = HAM_UPDATES_STATUS_MENU_ITEM (data);

  if (strcmp (interface, UPDATE_NOTIFIER_INTERFACE) != 0)
    return OSSO_ERROR;

  if (strcmp (method, UPDATE_NOTIFIER_OP_CHECK_UPDATES) == 0)
    {
      /* Search for new available updates */
      check_for_updates (self);

      /* Search for new notifications */
      check_for_notifications (self);
    }
  else if (strcmp (method, UPDATE_NOTIFIER_OP_CHECK_STATE) == 0)
    {
      /* Update states of the satusbar item */
      update_state (self);
    }
  else
    return OSSO_ERROR;

  return OSSO_OK;
}

static gboolean
setup_dbus (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  osso_return_t result;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  g_return_val_if_fail (priv->osso == NULL, FALSE);

  priv->osso = osso_initialize (APPNAME, PACKAGE_VERSION, TRUE, NULL);

  if (priv->osso == NULL)
    return FALSE;

  result = osso_rpc_set_cb_f (priv->osso,
                              UPDATE_NOTIFIER_SERVICE,
                              UPDATE_NOTIFIER_OBJECT_PATH,
                              UPDATE_NOTIFIER_INTERFACE,
                              ham_updates_status_menu_item_rpc_cb, self);

  return (result == OSSO_OK);
}

static void
ham_updates_status_menu_item_interval_changed_cb (GConfClient *client,
                                                  guint cnxn_id,
                                                  GConfEntry *entry,
                                                  gpointer data)
{
  g_return_if_fail (IS_HAM_UPDATES_STATUS_MENU_ITEM (data));

  LOG ("Interval value changed");
  delete_all_alarms ();
  HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (data)->alarm_cookie = 0;
  setup_alarm (HAM_UPDATES_STATUS_MENU_ITEM (data));
}

static void
setup_gconf (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  g_return_if_fail (priv->gconf == NULL);

  priv->gconf = gconf_client_get_default ();

  gconf_client_add_dir (priv->gconf,
                        UPNO_GCONF_DIR, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  gconf_client_notify_add (priv->gconf, UPNO_GCONF_CHECK_INTERVAL,
                           ham_updates_status_menu_item_interval_changed_cb,
                           self, NULL, NULL);
}

#define BUF_LEN 4096

static gboolean
ham_updates_status_menu_item_inotify_cb (GIOChannel *source,
                                         GIOCondition condition,
                                         gpointer data)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  gchar buf[BUF_LEN];
  gint i;
  gint len;

  /* Return if the object was already destroyed
     or the if inotify is not still ready */
  g_return_val_if_fail (IS_HAM_UPDATES_STATUS_MENU_ITEM (data), FALSE);

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (data);
  g_return_val_if_fail (priv->inotify_fd != -1, FALSE);

  while (TRUE)
    {
      while ((len = read (priv->inotify_fd, buf, BUF_LEN)) < 0
             && errno == EINTR);
      if (len > 0)
        break;
      else if (len < 0)
        {
          g_warning ("Error reading inotify event: %s", g_strerror (errno));
          goto error_cancel;
        }

      g_assert (len == 0);
      g_warning ("Buffer size %u too small", BUF_LEN);
      goto error_cancel;
    }

  i = 0;
  while (i < len)
    {
      struct inotify_event *event;

      event = (struct inotify_event *) &buf[i];

      LOG ("inotify: %s", event->name);

      if (is_file_modified (event, priv->wd[VAR], AVAILABLE_UPDATES_FILE_NAME)
          || is_file_modified (event, priv->wd[HOME], UFILE_SEEN_UPDATES)
          || is_file_modified (event, priv->wd[HOME], UFILE_SEEN_NOTIFICATIONS))
        {
          update_state (HAM_UPDATES_STATUS_MENU_ITEM (data));
        }

      i += sizeof (struct inotify_event) + event->len;
    }

  return TRUE;

error_cancel:
  priv->io_watch = 0;
  close_inotify (HAM_UPDATES_STATUS_MENU_ITEM (data));
  return FALSE;
}

static gint
add_watch_for_path (HamUpdatesStatusMenuItem *self, const gchar *path)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  gint watch;

  g_return_val_if_fail (path != NULL, -1);

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  watch = inotify_add_watch (priv->inotify_fd, path,
                             IN_CLOSE_WRITE | IN_MOVED_TO);

  if (watch < 0)
    {
      g_warning ("Failed to create watch for local file %s : %s\n",
                 path, strerror (errno));
      return -1;
    }

  return watch;
}

static gboolean
setup_inotify (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  gint fd;
  GIOChannel *io_channel;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  g_return_val_if_fail (priv->inotify_fd == -1, FALSE);

  if ((fd = inotify_init ()) < 0)
    {
      g_warning ("Failed to initialize inotify: %s", g_strerror (errno));
      return FALSE;
    }

  priv->inotify_fd = fd;

  io_channel = g_io_channel_unix_new (fd);
  priv->io_watch = g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                   ham_updates_status_menu_item_inotify_cb,
                                   self);
  g_io_channel_unref (io_channel);

  {
    gchar *path;

    path = user_file_get_state_dir_path ();
    priv->wd[HOME] = add_watch_for_path (self, path);
    g_free (path);

    priv->wd[VAR] = add_watch_for_path (self, INOTIFY_DIR);

    if (priv->wd[HOME] == -1 || priv->wd[VAR] == -1)
      return FALSE;
  }

  return TRUE;
}

static void
close_inotify (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (priv->io_watch > 0)
    {
      g_source_remove (priv->io_watch);
      priv->io_watch = 0;
    }

  if (priv->inotify_fd > 0)
    {
      gint i;

      for (i = 0; i < MAXWATCH; i++)
        {
          if (priv->wd[i] != -1)
            {
              inotify_rm_watch (priv->inotify_fd, priv->wd[i]);
              priv->wd[i] = -1;
            }
        }
      close (priv->inotify_fd);
      priv->inotify_fd = -1;
    }
}

static void
delete_all_alarms (void)
{
  int i;
  cookie_t *cookies;

  if ((cookies = alarmd_event_query (0, 0, 0, 0, APPNAME)))
    {
      for (i = 0; cookies[i]; ++i)
        {
          LOG ("deleting event %d", cookies[i]);
          alarmd_event_del (cookies[i]);
        }
      free(cookies);
    }
}

static cookie_t
get_last_alarm (void)
{
  cookie_t *cookies;
  cookie_t retval;

  retval = 0;
  if ((cookies = alarmd_event_query (0, 0, 0, 0, APPNAME)))
    {
      if (cookies[1] != 0)
        {
          LOG ("Several alarm events found! Killing them all!");
          delete_all_alarms ();
        }
      else
        {
          retval = cookies[0];
        }
      free (cookies);
    }

  return retval;
 }

static gboolean
setup_alarm (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  alarm_event_t *event;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (priv->alarm_cookie != 0)
    {
      LOG ("a previous alarm had been set");
      return TRUE;
    }

  priv->alarm_cookie = get_last_alarm ();
  LOG ("The stored alarm id is %d", priv->alarm_cookie);

  /* We reset the alarm when we don't have a cookie for the old alarm
     (which probably means there is no old alarm), when we can't find
     the old alarm although we have a cookie (which shouldn't happen
     unless someone manually mucks with the alarm queue), or if the
     interval has changed.

     Otherwise we leave the old alarm in place, but we update its
     parameters without touching the timing.
  */
  if (priv->alarm_cookie > 0)
    {
      alarm_event_t *old_event;

      old_event = alarmd_event_get (priv->alarm_cookie);

      if (old_event != NULL)
        {
          if (alarm_event_is_recurring (old_event))
            {
              LOG ("There's already a recurring event. No new event needed");
              return TRUE;
            }
         else
            {
              LOG ("A not recurring event found! Killing them all!");
              delete_all_alarms ();
            }
        }
    }

  LOG ("Creating a new event");

  /* Setup new alarm based on old alarm. */
  event = alarm_event_create ();

  ham_updates_set_alarm (priv->updates, event);

  alarm_event_set_alarm_appid (event, APPNAME);

  if (alarm_event_is_sane (event) != -1)
    {
      priv->alarm_cookie = alarmd_event_add (event);
      alarm_event_delete (event);
    }
  else
    LOG ("alarm event is not correct!");

  LOG ("The new alarm id is %d", priv->alarm_cookie);

  return priv->alarm_cookie > 0;
}

static void
run_service_now (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  time_t now;
  time_t last_update;
  time_t interval;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  now = time_get_time ();
  last_update = load_last_update_time ();
  interval = ham_updates_get_interval (priv->updates);

  LOG ("now = %d, last update = %d, interval = %d", now, last_update, interval);
  if (now - last_update > interval)
    {
      LOG ("we haven't checked for updates since long time ago");
      /* Search for new avalable updates */
      check_for_updates (self);
      check_for_notifications (self);
    }
}

static gboolean
setup_alarm_now (gpointer data)
{
  HamUpdatesStatusMenuItem *self;

  g_return_val_if_fail (IS_HAM_UPDATES_STATUS_MENU_ITEM (data), FALSE);

  self = HAM_UPDATES_STATUS_MENU_ITEM (data);

  run_service_now (self);

  if (setup_alarm (self))
    return FALSE;

  /* Try again in one minute. */
  return TRUE;
}

static void
ham_execute (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  LOG ("Starts Application Manager and opens 'Check for update'");

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  osso_rpc_async_run (priv->osso,
                      HILDON_APP_MGR_SERVICE,
                      HILDON_APP_MGR_OBJECT_PATH,
                      HILDON_APP_MGR_INTERFACE,
                      HILDON_APP_MGR_OP_SHOW_CHECK_FOR_UPDATES,
                      NULL,
                      NULL,
                      DBUS_TYPE_INVALID);
}

static void
ham_updates_status_menu_item_response_cb (HamUpdatesStatusMenuItem *self,
                                          gint response, gpointer data)
{
  if (response == GTK_RESPONSE_YES)
    {
      ham_execute (self);
      set_state (self, ICON_STATE_INVISIBLE);
    }
  else
    {
      set_state (self, ICON_STATE_STATIC);
    }
}

static void
build_status_menu_button (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);
  priv->updates = g_object_new (HAM_UPDATES_TYPE, NULL);

  g_signal_connect_swapped
    (priv->updates, "check-done",
     G_CALLBACK (ham_updates_status_menu_item_check_done_cb), self);
  g_signal_connect_swapped
    (priv->updates, "response",
     G_CALLBACK (ham_updates_status_menu_item_response_cb), self);

  gtk_container_add (GTK_CONTAINER (self),
                     ham_updates_get_button (priv->updates));
}

static void
build_status_area_icon (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  priv->icon = icon_load (STATUSBAR_ICON_NAME, STATUSBAR_ICON_SIZE);
}

static void
setup_ui (HamUpdatesStatusMenuItem *self)
{
  build_status_menu_button (self);
  build_status_area_icon (self);
  load_state (self);
  update_state (self);
}

static void
load_state (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  xexp *state = NULL;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  state = user_file_read_xexp (UFILE_UPDATE_NOTIFIER);

  if (state != NULL)
    {
      set_state (self,
                 xexp_aref_int (state, "icon-state", ICON_STATE_INVISIBLE));
      xexp_free (state);
    }

  LOG ("state loaded");
}

static void
save_state (HamUpdatesStatusMenuItem *self)
{
  xexp *x_state = NULL;

  x_state = xexp_list_new ("state");
  xexp_aset_int (x_state, "icon-state", (gint) get_state (self));
  user_file_write_xexp (UFILE_UPDATE_NOTIFIER, x_state);
  xexp_free (x_state);

  LOG ("state saved");
}

#include "transparent-icon.c"

static gboolean
blink_icon_cb (gpointer data)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  static gboolean visible = TRUE;

  g_return_val_if_fail (HD_IS_STATUS_PLUGIN_ITEM (data), FALSE);

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (data);

  g_return_val_if_fail (priv->icon != NULL && priv->no_icon != NULL, FALSE);

  if (visible)
    hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (data),
                                                priv->no_icon);
  else
    hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (data),
                                                priv->icon);

  visible = !visible;

  return TRUE;
}

static void
blink_icon_on (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  GError *error;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  /* we're already blinking */
  if (priv->blinker_id != 0)
    return;

  error = NULL;

  if (priv->no_icon == NULL)
    priv->no_icon = gdk_pixbuf_new_from_inline (-1, transparent_icon_inline,
                                                FALSE, &error);
  if (error != NULL)
    {
      fprintf (stderr, "error loading transparent inline pixbuf: %s",
               error->message);
      g_error_free (error);
    }
  else
    {
      priv->blinker_id = g_timeout_add (BLINK_INTERVAL, blink_icon_cb, self);
    }
}

static void
blink_icon_off (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (priv->no_icon != NULL)
    {
      g_object_unref (priv->no_icon);
      priv->no_icon = NULL;
    }

  if (priv->blinker_id > 0)
    {
      g_source_remove (priv->blinker_id);
      priv->blinker_id = 0;
    }
}

static void
update_icon_state (HamUpdatesStatusMenuItem *self)
{
  State state;

  state = get_state (self);

  if (state == ICON_STATE_INVISIBLE)
    {
      LOG ("turning off the icon");
      blink_icon_off (self);
      hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (self),
                                                  NULL);
      return;
    }
  else if (state == ICON_STATE_STATIC)
    {
      HamUpdatesStatusMenuItemPrivate *priv;

      LOG ("turning on the icon");
      blink_icon_off (self);
      priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

      hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (self),
                                                  priv->icon);
    }
  else if (state == ICON_STATE_BLINKING)
    {
      LOG ("turning blinking the icon");
      blink_icon_on (self);
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
set_state (HamUpdatesStatusMenuItem* self, State state)
{
  State oldstate;

  g_return_if_fail (state >= ICON_STATE_INVISIBLE &&
                    state <= ICON_STATE_BLINKING);

  oldstate = get_state (self);

  /* let's avoid the obvious */
  if (state == oldstate)
    return;

  /* this rule seems to be applied ever: */
  /* we can only go to blinking if we're invisible */
/*   if (oldstate != ICON_STATE_INVISIBLE */
/*       && state == ICON_STATE_BLINKING) */
/*     return; */

  {
    HamUpdatesStatusMenuItemPrivate *priv;

    priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);
    LOG ("Changing icon state from %d to %d", priv->state, state);
    priv->state = state;
    save_state (self);
    update_icon_state (self);
  }
}

static State
get_state (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  return priv->state;
}

static void
update_state (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;
  gboolean updates_avail;
  gboolean visible;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  LOG ("updating the state");

  updates_avail = ham_updates_are_available (priv->updates, priv->osso);

  g_object_get (G_OBJECT (self), "visible", &visible, NULL);

  /* shall we show the updates button? */
  if (updates_avail == TRUE)
    {
      if (visible == FALSE)
        gtk_widget_show (GTK_WIDGET (self));
    }
  else
    {
      if (visible == TRUE)
        gtk_widget_hide (GTK_WIDGET (self));
    }

  /* shall we blink the status area icon? */
  if (updates_avail == TRUE || ham_notifier_are_available (NULL) == TRUE)
    {
      set_state (self, ICON_STATE_BLINKING);
    }
  else
    {
      set_state (self, ICON_STATE_INVISIBLE);
    }
}

static void
ham_updates_status_menu_item_connection_cb (ConIcConnection *connection,
                                            ConIcConnectionEvent *event,
                                            gpointer data)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  g_return_if_fail (IS_HAM_UPDATES_STATUS_MENU_ITEM (data));
  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (data);

  LOG ("got a connect notification");

  if (con_ic_connection_event_get_status (event) == CON_IC_STATUS_CONNECTED)
    {
      const gchar *bearer;

      bearer = con_ic_event_get_bearer_type (CON_IC_EVENT (event));

      /* XXX - only the WLAN_INFRA and the WLAN_ADHOC bearers are
               considered cheap.  There should be a general platform
               feature that tells us whether we need to be careful with
               network access or not.  */
      priv->constate = (strcmp (bearer, "WLAN_ADHOC") == 0
                       || strcmp (bearer, "WLAN_INFRA") == 0);
    }

  LOG ("we're %s", priv->constate == CONN_OFFLINE ? "offline" : "online");
}

static void
setup_connection_state (HamUpdatesStatusMenuItem *self)
{
  HamUpdatesStatusMenuItemPrivate *priv;

  priv = HAM_UPDATES_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (running_in_scratchbox ())
    {
      /* if we're in scratchbox will assume that we have an inet
         connection */
      priv->constate = CONN_ONLINE;
      LOG ("we're online");
    }
  else
    {
      priv->conic = con_ic_connection_new ();

      g_signal_connect (G_OBJECT (priv->conic), "connection-event",
                        G_CALLBACK (ham_updates_status_menu_item_connection_cb),
                        self);
      g_object_set (G_OBJECT (priv->conic),
                    "automatic-connection-events", TRUE, NULL);
    }
}