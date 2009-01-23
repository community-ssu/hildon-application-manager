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

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <libintl.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <hildon/hildon.h>
#include <libhildonwm/hd-wm.h>
#include <libosso.h>
#include <clockd/libtime.h>
#include <libalarm.h>
#include <conic.h>

#include <xexp.h>
#include <user_files.h>

#include "update-notifier.h"
#include "update-notifier-conf.h"

#define DEBUG
#ifdef DEBUG
#define LOG(...) my_log (__PRETTY_FUNCTION__, __VA_ARGS__);
#else
#define LOG(...)
#endif

/* appname for OSSO and alarmd */
#define APPNAME                  "hildon_update_notifier"

/* HAM name known by the window manager */
#define HAM_APPNAME              "Application manager"

/* gconf keys */
#define HTTP_PROXY_GCONF_DIR     "/system/http_proxy"

/* inotify paths */
#define  VARLIB_INOTIFY_DIR      "/var/lib/hildon-application-manager"

#define STATUSBAR_HAM_ICON_SIZE  16

#define _(x) dgettext ("hildon-application-manager", (x))

#define UPDATE_NOTIFIER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UPDATE_NOTIFIER_TYPE, UpdateNotifierPrivate))

typedef enum _State State;
enum _State {
  UPNO_STATE_INVISIBLE,
  UPNO_STATE_STATIC,
  UPNO_STATE_BLINKING
};

typedef enum _ConState ConState;
enum _ConState {
  UPNO_CON_ONLINE,
  UPNO_CON_OFFLINE
};

/* inotify watchers id */
enum {
  HOME,
  VARLIB,
  MAXWATCH
};

typedef struct _UpdateNotifierPrivate UpdateNotifierPrivate;
struct _UpdateNotifierPrivate
{
  /* ui */
  GdkPixbuf *icon;
  GtkWidget *button;

  /* environment */
  osso_context_t *osso;
  GConfClient *gconf;

  /* state */
  State state;
  cookie_t alarm_cookie;

  /* inotify */
  gint inotify_fd;
  guint io_watch;
  gint wd[2];

  /* libconic */
  ConIcConnection *conic;
  ConState constate;

  /* apt-worker spawn */
  guint child_id;
};

/* setup prototypes */
static gboolean setup_dbus (UpdateNotifier *self);
static void setup_gconf (UpdateNotifier *self);
static gboolean setup_alarm (UpdateNotifier *upno);
static gboolean setup_alarm_now (gpointer data);
static gboolean setup_inotify (UpdateNotifier *self);
static void close_inotify (UpdateNotifier *self);
static void setup_ui (UpdateNotifier *self);

/* state handling prototypes */
static void load_state (UpdateNotifier *self);
static void save_state (UpdateNotifier *self);
static void update_state (UpdateNotifier *self);
static void set_state (UpdateNotifier *self, State state);
static State get_state (UpdateNotifier *self);

/* connection state prototypes */
static void setup_connection_state (UpdateNotifier *self);

/* misc prototypes */
static void my_log (const gchar *function, const gchar *fmt, ...);

/* ham querying */
static gboolean ham_is_running ();
static gboolean ham_is_showing_check_for_updates_view (UpdateNotifier *self);

HD_DEFINE_PLUGIN_MODULE (UpdateNotifier, update_notifier,
                         HD_TYPE_STATUS_MENU_ITEM);

static void
update_notifier_class_finalize (UpdateNotifierClass *klass)
{
  /* noop */
}

static void
update_notifier_finalize (GObject *object)
{
  UpdateNotifier *self;
  UpdateNotifierPrivate *priv;

  self = UPDATE_NOTIFIER (object);
  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  if (priv->icon != NULL)
    g_object_unref (priv->icon);

  if (priv->child_id > 0)
    g_source_remove (priv->child_id);

  alarmd_event_del (priv->alarm_cookie);

  close_inotify (self);

  if (priv->conic != NULL)
    g_object_unref (priv->conic);

  if (priv->gconf != NULL)
    g_object_unref (priv->gconf);

  if (priv->osso != NULL)
    osso_deinitialize (priv->osso);

  G_OBJECT_CLASS (update_notifier_parent_class)->finalize (object);
}

static void
update_notifier_class_init (UpdateNotifierClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = update_notifier_finalize;

  g_type_class_add_private (object_class, sizeof (UpdateNotifierPrivate));
}

static void
update_notifier_init (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  priv->osso = NULL;

  priv->state = UPNO_STATE_INVISIBLE;
  priv->alarm_cookie = 0;

  priv->inotify_fd = -1;
  priv->io_watch = 0;
  priv->wd[HOME] = priv->wd[VARLIB] = -1;

  priv->conic = NULL;
  priv->constate = UPNO_CON_OFFLINE;

  if (setup_dbus (self))
    {
      LOG ("dbus setup");

      setup_gconf (self);
      setup_ui (self);
      setup_connection_state (self);

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

static gboolean
running_in_scratchbox ()
{
  return access("/targets/links/scratchbox.config", F_OK) == 0;
}

static gchar*
get_http_proxy (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  gchar *proxy;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

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
  else if (gconf_client_get_bool (priv->gconf,
                                  HTTP_PROXY_GCONF_DIR "/use_http_proxy", NULL))
    {
      gchar *user;
      gchar *password;
      gchar *host;
      gint port;

      user = NULL;
      password = NULL;

      if (gconf_client_get_bool (priv->gconf,
                                 HTTP_PROXY_GCONF_DIR "/use_authentication",
				 NULL))
	{
	  user = gconf_client_get_string
            (priv->gconf, HTTP_PROXY_GCONF_DIR "/authentication_user", NULL);
	  password = gconf_client_get_string
	    (priv->gconf,
             HTTP_PROXY_GCONF_DIR "/authentication_password", NULL);
	}

      host = gconf_client_get_string (priv->gconf,
                                      HTTP_PROXY_GCONF_DIR "/host", NULL);
      port = gconf_client_get_int (priv->gconf,
                                   HTTP_PROXY_GCONF_DIR "/port", NULL);

      if (user != NULL)
	{
	  // XXX - encoding of '@', ':' in user and password?

	  if (password != NULL)
	    proxy = g_strdup_printf ("http://%s:%s@%s:%d",
				     user, password, host, port);
	  else
	    proxy = g_strdup_printf ("http://%s@%s:%d", user, host, port);
	}
      else
	proxy = g_strdup_printf ("http://%s:%d", host, port);

      g_free (user);
      g_free (password);
      g_free (host);

      /* XXX - there is also ignore_hosts, which we ignore for now,
	       since transcribing it to no_proxy is hard... mandatory,
	       non-transparent proxies are evil anyway.
      */
    }

  return proxy;
}

static void
save_last_update_time (time_t t)
{
  gchar *text;
  xexp *x;

  text = g_strdup_printf ("%d", t);
  x = xexp_text_new ("time", text);
  g_free (text);
  user_file_write_xexp (UFILE_LAST_UPDATE, x);
  xexp_free (x);
}

static void
check_for_updates_done (GPid pid, gint status, gpointer data)
{
  UpdateNotifier *self;
  UpdateNotifierPrivate *priv;

  g_return_if_fail (IS_UPDATE_NOTIFIER (data));

  self = UPDATE_NOTIFIER (data);
  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  priv->child_id = 0;

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 0)
    {
      LOG ("Check for updates done");
      save_last_update_time (time_get_time ());
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
check_for_updates (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  gchar *gainroot_cmd;
  gchar *proxy;
  GPid pid;
  GError *error;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  if (priv->constate == UPNO_CON_OFFLINE)
    return;

  error = NULL;

  LOG ("calling apt-worker checking for updates");

  /* Choose the right gainroot command */
  gainroot_cmd = NULL;
  if (running_in_scratchbox ())
    gainroot_cmd = g_strdup ("/usr/bin/fakeroot");
  else
    gainroot_cmd = g_strdup ("/usr/bin/sudo");

  proxy = get_http_proxy (self);
  LOG ("Proxy = %s", proxy);

  /* Build command to be spawned */
  char *argv[] = {
    gainroot_cmd,
    "/usr/libexec/apt-worker",
    "check-for-updates",
    proxy,
    NULL
  };

  if (!g_spawn_async_with_pipes (NULL,
				 argv,
				 NULL,
				 G_SPAWN_DO_NOT_REAP_CHILD,
				 NULL,
				 NULL,
				 &pid,
				 NULL,
				 NULL,
				 NULL,
				 &error))
    {
      fprintf (stderr, "can't run %s: %s\n", argv[0], error->message);
      g_error_free (error);
    }
  else
    {
      priv->child_id = g_child_watch_add (pid, check_for_updates_done, self);
    }

  g_free (gainroot_cmd);
  g_free (proxy);
}

static gint
update_notifier_rpc_cb (const gchar* interface, const gchar* method,
                        GArray* arguments, gpointer data,
                        osso_rpc_t* retval)
{
  UpdateNotifier *self;

  g_return_val_if_fail (IS_UPDATE_NOTIFIER (data), OSSO_ERROR);
  g_return_val_if_fail (interface != NULL && method != NULL, OSSO_ERROR);

  LOG ("RPC Message: %s:%s", interface, method);

  self = UPDATE_NOTIFIER (data);

  if (strcmp (interface, UPDATE_NOTIFIER_INTERFACE) != 0)
    return OSSO_ERROR;

  if (strcmp (method, UPDATE_NOTIFIER_OP_CHECK_UPDATES) == 0)
    {
      /* Search for new avalable updates */
      check_for_updates (self);
      /* check_for_notifications (self); */
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
setup_dbus (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  osso_return_t result;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  g_return_val_if_fail (priv->osso == NULL, FALSE);

  priv->osso = osso_initialize (APPNAME, PACKAGE_VERSION, TRUE, NULL);

  if (priv->osso == NULL)
    return FALSE;

  result = osso_rpc_set_cb_f (priv->osso,
                              UPDATE_NOTIFIER_SERVICE,
                              UPDATE_NOTIFIER_OBJECT_PATH,
                              UPDATE_NOTIFIER_INTERFACE,
                              update_notifier_rpc_cb, self);

  return (result == OSSO_OK);
}

static void
update_notifier_interval_changed_cb (GConfClient *client, guint cnxn_id,
                                     GConfEntry *entry, gpointer data)
{
  g_return_if_fail (IS_UPDATE_NOTIFIER (data));

  LOG ("Interval value changed");
  setup_alarm (UPDATE_NOTIFIER (data));
}

static void
setup_gconf (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  g_return_if_fail (priv->gconf == NULL);

  priv->gconf = gconf_client_get_default ();

  gconf_client_add_dir (priv->gconf,
                        UPNO_GCONF_DIR, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  gconf_client_notify_add (priv->gconf, UPNO_GCONF_CHECK_INTERVAL,
                           update_notifier_interval_changed_cb, self,
                           NULL, NULL);
}

static gboolean
xexp_is_tag_and_not_empty (xexp *x, const char *tag)
{
  return (x != NULL && xexp_is (x, tag) && !xexp_is_empty (x));
}

static gboolean
compare_xexp_text (xexp *x_a, xexp *x_b, const char *tag)
{
  const char *text_a = NULL;
  const char *text_b = NULL;

  if ((x_a == NULL) || (x_b == NULL))
    return ((x_a == NULL) && (x_b == NULL));

  text_a = xexp_aref_text(x_a, tag);
  text_b = xexp_aref_text(x_b, tag);

  if ((text_a == NULL) || (text_b == NULL))
    return ((text_a == NULL) && (text_b == NULL));
  else
    return (strcmp (text_a, text_b) == 0);
}

static void
update_seen_notifications ()
{
  xexp *avail_notifications;
  xexp *seen_notifications;

 avail_notifications = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
 seen_notifications = user_file_read_xexp (UFILE_SEEN_NOTIFICATIONS);

 if (avail_notifications && seen_notifications &&
     xexp_is_tag_and_not_empty (avail_notifications, "info") &&
     (!xexp_is_tag_and_not_empty (seen_notifications, "info") ||
      (xexp_is_tag_and_not_empty (seen_notifications, "info") &&
       !compare_xexp_text (avail_notifications, seen_notifications, "title") &&
       !compare_xexp_text (avail_notifications, seen_notifications, "text") &&
       !compare_xexp_text (avail_notifications, seen_notifications, "uri"))))
   {
     /* as we have new notifications, we no longer need the old seen ones;
      * the writing of UFILE_SEEN_NOTIFICATIONS will trigger an inotify */
     xexp* empty_seen_notifications;

     empty_seen_notifications = xexp_list_new ("info");
     user_file_write_xexp (UFILE_SEEN_NOTIFICATIONS, empty_seen_notifications);
     xexp_free (empty_seen_notifications);
   }

 if (avail_notifications)
   xexp_free (avail_notifications);

 if (seen_notifications)
   xexp_free (seen_notifications);
}

static gboolean
is_file_modified_event (struct inotify_event *event,
                        int watch, const char *name)
{
  return (event->wd == watch
          && (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
          && event->len > 0
          && strcmp (event->name, name) == 0);
}

#define BUF_LEN 4096

static gboolean
update_notifier_inotify_cb (GIOChannel *source, GIOCondition condition,
                            gpointer data)
{
  UpdateNotifierPrivate *priv;
  gchar buf[BUF_LEN];
  gint i;
  gint len;

  /* Return if the object was already destroyed
     or the if inotify is not still ready */
  g_return_val_if_fail (IS_UPDATE_NOTIFIER (data), FALSE);

  priv = UPDATE_NOTIFIER_GET_PRIVATE (data);
  g_return_val_if_fail (priv->inotify_fd != -1, FALSE);

  LOG ("inotify callback");

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

      if (is_file_modified_event (event, priv->wd[VARLIB],
                                  AVAILABLE_UPDATES_FILE_NAME) ||
          is_file_modified_event (event, priv->wd[HOME],
                                  UFILE_SEEN_UPDATES) ||
          is_file_modified_event (event, priv->wd[HOME],
                                  UFILE_SEEN_NOTIFICATIONS))
        {
          update_state (UPDATE_NOTIFIER (data));
        }
      else if (is_file_modified_event (event, priv->wd[HOME],
                                       UFILE_AVAILABLE_NOTIFICATIONS))
        {
          update_seen_notifications ();
          update_state (UPDATE_NOTIFIER (data));
        }

      i += sizeof (struct inotify_event) + event->len;
    }

  return TRUE;

error_cancel:
  priv->io_watch = 0;
  close_inotify (UPDATE_NOTIFIER (data));
  return FALSE;
}

static gint
add_watch_for_path (UpdateNotifier *self, const gchar *path)
{
  UpdateNotifierPrivate *priv;
  gint watch;

  g_return_val_if_fail (path != NULL, -1);

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

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
setup_inotify (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  gint fd;
  GIOChannel *io_channel;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  g_return_val_if_fail (priv->inotify_fd == -1, FALSE);

  if ((fd = inotify_init ()) < 0)
    {
      g_warning ("Failed to initialize inotify: %s", g_strerror (errno));
      return FALSE;
    }

  priv->inotify_fd = fd;

  io_channel = g_io_channel_unix_new (fd);
  priv->io_watch = g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                   update_notifier_inotify_cb, self);
  g_io_channel_unref (io_channel);

  {
    gchar *path;

    path = user_file_get_state_dir_path ();
    priv->wd[HOME] = add_watch_for_path (self, path);
    g_free (path);

    priv->wd[VARLIB] = add_watch_for_path (self, VARLIB_INOTIFY_DIR);

    if (priv->wd[HOME] == -1 || priv->wd[VARLIB] == -1)
      return FALSE;
  }

  return TRUE;
}

static void
close_inotify (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  if (priv->io_watch > 0)
    g_source_remove (priv->io_watch);
  priv->io_watch = 0;

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
    }
  priv->inotify_fd = -1;
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

static time_t
get_interval (UpdateNotifier* self)
{
  UpdateNotifierPrivate *priv;
  time_t interval;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  interval = (time_t) gconf_client_get_int (priv->gconf,
                                            UPNO_GCONF_CHECK_INTERVAL,
                                            NULL);

  if (interval <= 0)
    {
      /* Use default value and set it from now on */
      interval = UPNO_DEFAULT_CHECK_INTERVAL;
      gconf_client_set_int (priv->gconf,
                            UPNO_GCONF_CHECK_INTERVAL,
                            (gint) interval,
                            NULL);
    }

  interval = (time_t) 60 * 1;  /* FIXME: remove this! */

  LOG ("The interval is %d", interval);
  return interval;
}

static gboolean
setup_alarm (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  alarm_event_t *event;
  alarm_action_t *action;
  time_t interval;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  g_return_val_if_fail (priv->alarm_cookie == 0, FALSE);

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
  alarm_event_set_alarm_appid (event, APPNAME);

  /* If the trigger time is missed (due to the device being off or
     system time being adjusted beyond the trigger point) the alarm
     should be run anyway. */
  event->flags |= ALARM_EVENT_RUN_DELAYED;

  /* Run only when internet connection is available. */
  /* conic is needed */
  /* FIXME: event->flags |= ALARM_EVENT_CONNECTED; */

  /* If the system time is moved backwards, the alarm should be
     rescheduled. */
  event->flags |= ALARM_EVENT_BACK_RESCHEDULE;

  interval = get_interval (self);
  event->alarm_time = ALARM_RECURRING_SECONDS (time (NULL) + interval);

  /* set the recurrence */
  event->recur_count = -1; /* infinite recorrence */
  event->recur_secs = ALARM_RECURRING_SECONDS (interval);

  /* create the action */
  action = alarm_event_add_actions (event, 1);

  action->flags |= ALARM_ACTION_WHEN_TRIGGERED;
  action->flags |= ALARM_ACTION_TYPE_DBUS;
  action->flags |= ALARM_ACTION_DBUS_USE_ACTIVATION;

  alarm_action_set_dbus_service (action, UPDATE_NOTIFIER_SERVICE);
  alarm_action_set_dbus_path (action, UPDATE_NOTIFIER_OBJECT_PATH);
  alarm_action_set_dbus_interface (action, UPDATE_NOTIFIER_INTERFACE);
  alarm_action_set_dbus_name (action, UPDATE_NOTIFIER_OP_CHECK_UPDATES);

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

static gboolean
setup_alarm_now (gpointer data)
{
  g_return_val_if_fail (IS_UPDATE_NOTIFIER (data), FALSE);

  if (setup_alarm (UPDATE_NOTIFIER (data)))
    return FALSE;

  /* Try again in one minute. */
  return TRUE;
}

static void
build_button (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  gchar *title;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  title = _("ai_sb_update_description");

  priv->button = hildon_button_new_with_text (HILDON_SIZE_FULLSCREEN_WIDTH |
                                              HILDON_SIZE_FINGER_HEIGHT,
                                              HILDON_BUTTON_ARRANGEMENT_VERTICAL,
                                              title, "");

  /* set icon */
  {
    GdkPixbuf *pixbuf;

     pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                        "general_application_manager",
                                        STATUSBAR_HAM_ICON_SIZE,
                                        GTK_ICON_LOOKUP_NO_SVG, NULL);

    if (pixbuf != NULL)
      {
        GtkWidget *image;

        image = gtk_image_new_from_pixbuf (pixbuf);

        if (image != NULL)
          hildon_button_set_image (HILDON_BUTTON (priv->button), image);

        g_object_unref (pixbuf);
      }
  }

  gtk_widget_show (GTK_WIDGET (priv->button));
}

static void
build_status_area_icon (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  priv->icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                         "qgn_stat_new_updates",
                                         40, GTK_ICON_LOOKUP_NO_SVG, NULL);

/*   hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (self), */
/*                                               priv->icon); */
}

static void
setup_ui (UpdateNotifier *self)
{
  build_button (self);
  build_status_area_icon (self);
  load_state (self);
  update_state (self);
}

static void
load_state (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  xexp *state = NULL;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  state = user_file_read_xexp (UFILE_UPDATE_NOTIFIER);

  if (state != NULL)
    {
      set_state (self,
                 xexp_aref_int (state, "icon-state", UPNO_STATE_INVISIBLE));
      /* priv->alarm_cookie =
         (cookie_t) xexp_aref_int (state, "alarm-cookie", 0); */
      xexp_free (state);
    }

  LOG ("state loaded");
}

static void
save_state (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  xexp *x_state = NULL;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  x_state = xexp_list_new ("state");

  xexp_aset_int (x_state, "icon-state", (gint) priv->state);
  /* xexp_aset_int (x_state, "alarm-cookie", priv->alarm_cookie); */

  user_file_write_xexp (UFILE_UPDATE_NOTIFIER, x_state);

  xexp_free (x_state);

  LOG ("state saved");
}

static void
update_widget_state (UpdateNotifier *self)
{
  State state;

  state = get_state (self);

  if (state == UPNO_STATE_INVISIBLE)
    {
      hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (self),
                                                  NULL);
      gtk_widget_hide (GTK_WIDGET (self));

      return;
    }
  else /* this is common to blinking and static */
    {
      UpdateNotifierPrivate *priv;

      priv = UPDATE_NOTIFIER_GET_PRIVATE (self);
      hd_status_plugin_item_set_status_area_icon (HD_STATUS_PLUGIN_ITEM (self),
                                                  priv->icon);
      gtk_widget_show (GTK_WIDGET (self));
    }

  switch ((gint) state)
    {
    case UPNO_STATE_STATIC:
      /* @todo */
      break;
    case UPNO_STATE_BLINKING:
      /* @todo */
      break;
    default:
      g_assert_not_reached ();
    }
}

static void
set_state (UpdateNotifier* self, State state)
{
  UpdateNotifierPrivate *priv;

  g_return_if_fail (state >= UPNO_STATE_INVISIBLE &&
                    state <= UPNO_STATE_BLINKING);

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  /* let's avoid the obvious */
  if (state == priv->state)
    return;

  /* this rule seems to be applied ever: */
  /* we can only go to blinking if we're invisible */
  if (state == UPNO_STATE_BLINKING && priv->state != UPNO_STATE_INVISIBLE)
    return;

  {
    LOG ("Changing icon state from %d to %d", priv->state, state);
    priv->state = state;
    save_state (self);
    update_widget_state (self);
  }
}

static State
get_state (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  return priv->state;
}

/* contains and transports the number of available updates types */
typedef struct {
  gint os;
  gint certified;
  gint other;
  gint new;
} UpdatesCount;

/* calculates the number of available updates types */
static UpdatesCount *
get_updates_count ()
{
  xexp *available_updates;
  xexp *seen_updates;
  UpdatesCount *retval;

  retval = g_new0 (UpdatesCount, 1);

  available_updates = xexp_read_file (AVAILABLE_UPDATES_FILE);

  if (available_updates == NULL)
    goto exit;

  seen_updates = user_file_read_xexp (UFILE_SEEN_UPDATES);

  if (seen_updates == NULL)
    seen_updates = xexp_list_new ("updates");

  /* preconditions ok */
  {
    xexp *x;
    xexp *y;

    y = NULL;

    for (x = xexp_first (available_updates); x != NULL; x = xexp_rest (x))
      {
        if (!xexp_is_text (x))
          continue;

        if ((seen_updates != NULL) && xexp_is_list (seen_updates))
          {
            const gchar *pkg;

            pkg = xexp_text (x);

            for (y = xexp_first (seen_updates); y != NULL; y = xexp_rest (y))
              if (xexp_is_text (y) && strcmp (pkg, xexp_text (y)) == 0)
                break;
          }

        if (y == NULL)
          retval->new++;

        if (xexp_is (x, "os"))
          retval->os++;
        else if (xexp_is (x, "certified"))
          retval->certified++;
        else
          retval->other++;
      }

      xexp_free (available_updates);

      if (seen_updates != NULL)
        xexp_free (seen_updates);
    }

 exit:
  LOG ("new pkgs = %d, new os = %d, new cert = %d, other = %d",
       retval->new, retval->os, retval->certified, retval->other);

  return retval;
}

static gchar*
maybe_add_dots (gchar *str)
{
  gchar *tmp;

  tmp = NULL;
  if (str)
    {
      tmp = g_strconcat (str, "...", NULL);
      g_free (str);
    }

  return tmp;
}

static gchar*
build_status_menu_button_value (UpdatesCount *uc)
{
  gchar *retval;

  g_return_val_if_fail (uc != NULL && uc->new != 0, NULL);

  retval = NULL;

  if (uc->os > 0)
    retval = g_strdup_printf (_("ai_sb_update_os_%d"), uc->os);

  if (uc->certified > 0)
    {
      retval = maybe_add_dots (retval);
      if (!retval)
        retval = g_strdup_printf (_("ai_sb_update_nokia_%d"), uc->certified);
    }

  if (uc->other > 0)
    {
      retval = maybe_add_dots (retval);
      if (!retval)
        retval = g_strdup_printf (_("ai_sb_update_thirdparty_%d"), uc->other);
    }

  LOG ("update string = %s", retval);
  return retval;
}

static gboolean
update_status_menu_button_value (UpdateNotifier *self)
{
  UpdatesCount *uc;
  gboolean retval;

  retval = FALSE;

  if ((uc = get_updates_count ()) == NULL)
    return FALSE;

  if (uc->new > 0 && !ham_is_showing_check_for_updates_view (self))
    {
      gchar *value;

      if ((value = build_status_menu_button_value (uc)) != NULL)
        {
          UpdateNotifierPrivate *priv;
          priv = UPDATE_NOTIFIER_GET_PRIVATE (self);
          hildon_button_set_value (HILDON_BUTTON (priv->button), value);
          retval = TRUE;
          g_free (value);
        }
    }

  g_free (uc);
  return retval;
}

static void
update_state (UpdateNotifier *self)
{
  LOG ("updating the state");

  if (update_status_menu_button_value (self)
      /* @todo || update_notifications_button_value (self) */)
    set_state (self, UPNO_STATE_BLINKING);
  else
    set_state (self, UPNO_STATE_INVISIBLE);
}

static void
update_notifier_connection_cb (ConIcConnection *connection,
                               ConIcConnectionEvent *event,
                               gpointer data)
{
  UpdateNotifierPrivate *priv;

  g_return_if_fail (IS_UPDATE_NOTIFIER (data));
  priv = UPDATE_NOTIFIER_GET_PRIVATE (data);

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

  LOG ("we're %s", priv->constate == UPNO_CON_OFFLINE ? "offline" : "online");
}

static void
setup_connection_state (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  if (running_in_scratchbox ())
    {
      /* if we're in scratchbox will assume that we have an inet
         connection */
      priv->constate = UPNO_CON_ONLINE;
      LOG ("we're online");
    }
  else
    {
      priv->conic = con_ic_connection_new ();

      g_signal_connect (G_OBJECT (priv->conic), "connection-event",
                        G_CALLBACK (update_notifier_connection_cb), self);
      g_object_set (G_OBJECT (priv->conic),
                    "automatic-connection-events", TRUE, NULL);
    }
}

static void
my_log (const gchar *function, const gchar *fmt, ...)
{
  va_list args;
  gchar *tmp;

  va_start (args, fmt);
  tmp = g_strdup_vprintf (fmt, args);
  g_printerr ("update-notifier (%s): %s\n", function, tmp);
  g_free (tmp);
  va_end (args);
}

static gboolean
ham_is_running ()
{
  HDWM *hdwm;
  HDWMEntryInfo *info;
  GList *apps;
  GList *l;
  gchar *appname;

  hdwm = hd_wm_get_singleton ();
  hd_wm_update_client_list (hdwm);

  apps = hd_wm_get_applications (hdwm);
  for (l = apps; l; l = l->next)
    {
      info = (HDWMEntryInfo *) l->data;

      appname = hd_wm_entry_info_get_app_name (info);

      if (appname && (strcmp (HAM_APPNAME, appname) == 0))
        return TRUE;
    }

  return FALSE;
}

static gboolean
ham_is_showing_check_for_updates_view (UpdateNotifier *self)
{
  if (ham_is_running ())
    {
      UpdateNotifierPrivate *priv;
      osso_return_t result;
      osso_rpc_t reply;

      priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

      LOG ("asking if ham is showing the \"check for updates\" view");
      result = osso_rpc_run (priv->osso,
			     HILDON_APP_MGR_SERVICE,
			     HILDON_APP_MGR_OBJECT_PATH,
			     HILDON_APP_MGR_INTERFACE,
			     HILDON_APP_MGR_OP_SHOWING_CHECK_FOR_UPDATES,
			     &reply,
			     DBUS_TYPE_INVALID);

      if (result == OSSO_OK && reply.type == DBUS_TYPE_BOOLEAN)
        return reply.value.b;
    }

  return FALSE;
}
