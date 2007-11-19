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

/* XXX

   - Make sure icon doesn't blink when screen is off
   - Plug all the leaks
*/

#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <libhildondesktop/libhildondesktop.h>

#include <alarm_event.h>

#include "update-notifier.h"
#include "hn-app-pixbuf-anim-blinker.h"
#include "xexp.h"

#define _(x) dgettext ("hildon-application-manager", (x))

#define USE_BLINKIFIER 1

#define UPNO_GCONF_DIR            "/apps/hildon/update-notifier"
#define UPNO_GCONF_STATE          UPNO_GCONF_DIR "/state"
#define UPNO_GCONF_ALARM_COOKIE   UPNO_GCONF_DIR "/alarm_cookie"
#define UPNO_GCONF_CZECH_INTERVAL UPNO_GCONF_DIR "/check_interval"

#define HTTP_PROXY_GCONF_DIR      "/system/http_proxy"

#define AVAILABLE_UPDATES_FILE "/var/lib/hildon-application-manager/available-updates"

#define UPDATE_NOTIFIER_SERVICE "com.nokia.hildon_update_notifier"
#define UPDATE_NOTIFIER_OBJECT_PATH "/com/nokia/hildon_update_notifier"
#define UPDATE_NOTIFIER_INTERFACE "com.nokia.hildon_update_notifier"

#define UPDATE_NOTIFIER_OP_CHECK_UPDATES "check_for_updates"
#define UPDATE_NOTIFIER_OP_CHECK_STATE "check_state"

#define HILDON_APP_MGR_SERVICE "com.nokia.hildon_application_manager"
#define HILDON_APP_MGR_OBJECT_PATH "/com/nokia/hildon_application_manager"
#define HILDON_APP_MGR_INTERFACE "com.nokia.hildon_application_manager"

enum {
  UPNO_ICON_INVISIBLE,
  UPNO_ICON_STATIC,
  UPNO_ICON_BLINKING
};

HD_DEFINE_PLUGIN (UpdateNotifier, update_notifier, STATUSBAR_TYPE_ITEM);

/* Initialization/destruction functions */
static void update_notifier_class_init (UpdateNotifierClass *klass);
static void update_notifier_init (UpdateNotifier *upno);
static void update_notifier_finalize (GObject *object);

/* Private functions */
static void button_pressed (GtkWidget *button, gpointer data);
static void menu_activated (GtkWidget *menu, gpointer data);

static void setup_gconf (UpdateNotifier *upno);

static void set_icon_visibility (UpdateNotifier *upno, int state);
static void setup_dbus (UpdateNotifier *upno);

static gchar *get_http_proxy ();
static void setup_inotify (UpdateNotifier *upno);
static void setup_alarm (UpdateNotifier *upno);

static void update_icon_visibility (UpdateNotifier *upno, GConfValue *value);
static void update_state (UpdateNotifier *upno);

static void show_check_for_updates_view (UpdateNotifier *upno);
static void check_for_updates (UpdateNotifier *upno);

static void cleanup_gconf (UpdateNotifier *upno);
static void cleanup_dbus (UpdateNotifier *upno);
static void cleanup_inotify (UpdateNotifier *upno);
static void cleanup_alarm (UpdateNotifier *upno);

/* Initialization/destruction functions */

static void
update_notifier_class_init (UpdateNotifierClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = update_notifier_finalize;
}

static void
update_notifier_init (UpdateNotifier *upno)
{
  GdkPixbuf *icon_pixbuf;
  GtkIconTheme *icon_theme;

  setup_gconf (upno);

  upno->button = gtk_button_new ();

  icon_theme = gtk_icon_theme_get_default ();
  icon_pixbuf = gtk_icon_theme_load_icon (icon_theme,
					  "qgn_stat_new_updates",
					  40,
					  GTK_ICON_LOOKUP_NO_SVG,
					  NULL);
#if USE_BLINKIFIER
  upno->static_pic = icon_pixbuf;
  upno->blinkifier = gtk_image_new_from_animation (hn_app_pixbuf_anim_blinker_new(icon_pixbuf, 1000, -1, 100));
#else
  upno->blinkifier = gtk_image_new_from_pixbuf (icon_pixbuf);
#endif
  
  gtk_container_add (GTK_CONTAINER (upno->button), upno->blinkifier);
  gtk_container_add (GTK_CONTAINER (upno), upno->button);

  gtk_widget_show (upno->blinkifier);
  gtk_widget_show (upno->button);

  g_signal_connect (upno->button, "pressed",
		    G_CALLBACK (button_pressed), upno);

  setup_dbus (upno);
  setup_inotify (upno);
  setup_alarm (upno);

  update_icon_visibility (upno, gconf_client_get (upno->gconf,
						  UPNO_GCONF_STATE,
						  NULL));
  update_state (upno);
}

static void
update_notifier_finalize (GObject *object)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (object);

  if (upno->menu)
    gtk_widget_destroy (upno->menu);

  if (upno->static_pic)
    g_object_unref (upno->static_pic);

  cleanup_alarm (upno);
  cleanup_inotify (upno);
  cleanup_dbus (upno);
  cleanup_gconf (upno);

  G_OBJECT_CLASS (g_type_class_peek_parent
		  (G_OBJECT_GET_CLASS(object)))->finalize(object);
}

/* Private functions */

static void
menu_position_func (GtkMenu   *menu, 
		    gint      *x, 
		    gint      *y,
		    gboolean  *push_in, 
		    gpointer   data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  
  GtkRequisition req;
  
  gtk_widget_size_request (GTK_WIDGET (menu->toplevel), &req);

  gdk_window_get_origin (upno->button->window, x, y);
  *x += (upno->button->allocation.x
	 + upno->button->allocation.width
	 - req.width);
  *y += (upno->button->allocation.y
	 + upno->button->allocation.height);

  *push_in = FALSE;
}

static void
button_pressed (GtkWidget *button, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  set_icon_visibility (upno, UPNO_ICON_STATIC);

  gtk_menu_popup (GTK_MENU (upno->menu),
		  NULL, NULL,
		  menu_position_func, upno,
		  1,
		  gtk_get_current_event_time ());
}

static void
menu_activated (GtkWidget *menu, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  show_check_for_updates_view (upno);
}

static void
gconf_state_changed (GConfClient *client, guint cnxn_id,
		     GConfEntry *entry, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  update_icon_visibility (upno, entry->value);
}

static void
gconf_interval_changed (GConfClient *client, guint cnxn_id,
			GConfEntry *entry, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  setup_alarm (upno);
}

static void
setup_gconf (UpdateNotifier *upno)
{
  upno->gconf = gconf_client_get_default();
  gconf_client_add_dir (upno->gconf,
			UPNO_GCONF_DIR,
			GCONF_CLIENT_PRELOAD_ONELEVEL,
			NULL);

  /* Add gconf notifications and store connection IDs */
  upno->gconf_notifications = g_new0 (guint, 3);
  upno->gconf_notifications[0] =
    gconf_client_notify_add (upno->gconf,
			     UPNO_GCONF_STATE,
			     gconf_state_changed, upno,
			     NULL, NULL);
  upno->gconf_notifications[1] =
    gconf_client_notify_add (upno->gconf,
			     UPNO_GCONF_CZECH_INTERVAL,
			     gconf_interval_changed, upno,
			     NULL, NULL);

  /* Finish the list of connection IDs */
  upno->gconf_notifications[2] = -1;
}

#if !USE_BLINKIFIER
static gboolean
blink_icon (gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  
  if (GTK_WIDGET_VISIBLE (upno->blinkifier))
    gtk_widget_hide (upno->blinkifier);
  else
    gtk_widget_show (upno->blinkifier);

  return TRUE;
}
#endif

static void
update_icon_visibility (UpdateNotifier *upno, GConfValue *value)
{
  int state = UPNO_ICON_INVISIBLE;

  if (value && value->type == GCONF_VALUE_INT)
    state = gconf_value_get_int (value);

  upno->icon_state = state;

  g_object_set (upno,
		"condition", (state == UPNO_ICON_STATIC
			      || state == UPNO_ICON_BLINKING),
		NULL);

#if USE_BLINKIFIER
  if (state == UPNO_ICON_BLINKING)
    g_object_set(upno->blinkifier, "pixbuf-animation", hn_app_pixbuf_anim_blinker_new(upno->static_pic, 1000, -1, 100), NULL);
  else
    g_object_set(upno->blinkifier, "pixbuf", upno->static_pic, NULL);
#else
  if (state == UPNO_ICON_BLINKING)
    {
      if (upno->timeout_id == 0)
	upno->timeout_id = g_timeout_add (500, blink_icon, upno);
    }
  else
    {
      gtk_widget_show (upno->blinkifier);
      if (upno->timeout_id > 0)
	{
	  g_source_remove (upno->timeout_id);
	  upno->timeout_id = 0;
	}
    }
#endif
}

static void
add_readonly_item (GtkWidget *menu, const char *fmt, ...)
{
  GtkWidget *label, *item;
  va_list ap;
  gchar *text;

  va_start (ap, fmt);
  text = g_strdup_vprintf (fmt, ap);
  va_end (ap);

  item = gtk_menu_item_new ();
  label = gtk_label_new (text);
  gtk_misc_set_alignment (GTK_MISC(label), 0.0, 0.5);
  gtk_container_add (GTK_CONTAINER (item), label);
  gtk_menu_append (GTK_MENU (menu), item);
  gtk_widget_show (item);
  gtk_widget_show (label);
  gtk_widget_set_sensitive (item, FALSE);
  label->state = GTK_STATE_NORMAL;  /* Hurray for weak encapsulation. */

  g_free (text);
}

static void
update_state (UpdateNotifier *upno)
{
  xexp *available_updates, *seen_updates;
  int n_os = 0, n_certified = 0, n_other = 0;
  int n_new = 0;

  GtkWidget *item;
  
  available_updates = xexp_read_file (AVAILABLE_UPDATES_FILE);
  
  gchar *name = g_strdup_printf ("%s/%s", getenv ("HOME"),
				 SEEN_UPDATES_FILE);
  seen_updates = xexp_read_file (name);
  g_free (name);

  if (seen_updates == NULL)
    seen_updates = xexp_list_new ("updates");

  if (available_updates)
    {
      xexp *x, *y;

      for (x = xexp_first (available_updates); x; x = xexp_rest (x))
	{
	  if (xexp_is_text (x))
	    {
	      const char *pkg = xexp_text (x);
	      
	      for (y = xexp_first (seen_updates); y; y = xexp_rest (y))
		if (xexp_is_text (y)
		    && strcmp (pkg, xexp_text (y)) == 0)
		  break;
	      
	      if (y == NULL)
		n_new++;
	      
	      if (xexp_is (x, "os"))
		n_os++;
	      else if (xexp_is (x, "certified"))
		n_certified++;
	      else
		n_other++;
	    }
	}
    }

  xexp_free (available_updates);
  xexp_free (seen_updates);

  if (n_new > 0)
    {
      if (upno->icon_state == UPNO_ICON_INVISIBLE)
	set_icon_visibility (upno, UPNO_ICON_BLINKING);
    }
  else
    set_icon_visibility (upno, UPNO_ICON_INVISIBLE);
    
  if (upno->menu)
    gtk_widget_destroy (upno->menu);

  upno->menu = gtk_menu_new ();

  if (n_certified + n_other + n_os > 0)
    {
      add_readonly_item (upno->menu, _("ai_sb_update_description"));

      if (n_certified > 0)
	add_readonly_item (upno->menu, _("ai_sb_update_nokia_%d"),
			   n_certified);
      if (n_other > 0)
	add_readonly_item (upno->menu, _("ai_sb_update_thirdparty_%d"),
			   n_other);
      if (n_os > 0)
	add_readonly_item (upno->menu, _("ai_sb_update_os"));

      item = gtk_separator_menu_item_new ();
      gtk_menu_append (upno->menu, item);
      gtk_widget_show (item);
    }

  item = gtk_menu_item_new_with_label (_("ai_sb_update_am"));
  gtk_menu_append (upno->menu, item);
  gtk_widget_show (item);
  g_signal_connect (item, "activate",
		    G_CALLBACK (menu_activated), upno);
}

static void
set_icon_visibility (UpdateNotifier *upno, int state)
{
  gconf_client_set_int (upno->gconf, 
			UPNO_GCONF_STATE,
			state,
			NULL);
}

static DBusHandlerResult 
dbus_filter (DBusConnection *conn, DBusMessage *message, void *data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  DBusMessage *reply = NULL;
  gboolean message_handled = TRUE;

  if (dbus_message_is_method_call (message,
 				   UPDATE_NOTIFIER_SERVICE,
				   UPDATE_NOTIFIER_OP_CHECK_UPDATES))
    {
      /* Search for new available updates */
      check_for_updates (upno);
    }
  else if (dbus_message_is_method_call (message,
					UPDATE_NOTIFIER_SERVICE,
					UPDATE_NOTIFIER_OP_CHECK_STATE))
    {
      /* Update states of the statusbar item */
      update_state (upno);
    }
  else
    {
      /* Not handled */
      message_handled = FALSE;
    }

  /* Send proper DBus reply message if handled */
  if (message_handled)
    {
      reply = dbus_message_new_method_return (message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);

      return DBUS_HANDLER_RESULT_HANDLED;
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
setup_dbus (UpdateNotifier *upno)
{
  upno->dbus = dbus_bus_get (DBUS_BUS_SESSION, NULL);

  if (upno->dbus)
    {
      dbus_connection_add_filter (upno->dbus, dbus_filter, upno, NULL);
      dbus_bus_request_name (upno->dbus,
			     UPDATE_NOTIFIER_SERVICE,
			     DBUS_NAME_FLAG_DO_NOT_QUEUE,
			     NULL);
    }
}

static void
show_check_for_updates_view (UpdateNotifier *upno)
{
  DBusMessage     *msg;

  if (upno->dbus)
    {
      msg = dbus_message_new_method_call (HILDON_APP_MGR_SERVICE,
					  HILDON_APP_MGR_OBJECT_PATH,
					  HILDON_APP_MGR_INTERFACE,
					  "show_check_for_updates_view");
      if (msg)
	{
	  dbus_connection_send (upno->dbus, msg, NULL);
	  dbus_message_unref (msg);
	}
    }
}

static void
check_for_updates_done (GPid pid, int status, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 0)
    {
      update_state (upno);
    }
  else
    {
      /* Ask the Application Manager to perform the update, but don't
	 start it if it isn't running already.
       */

      DBusMessage     *msg;

      if (upno->dbus)
	{
	  msg = dbus_message_new_method_call (HILDON_APP_MGR_SERVICE,
					      HILDON_APP_MGR_OBJECT_PATH,
					      HILDON_APP_MGR_INTERFACE,
					      UPDATE_NOTIFIER_OP_CHECK_UPDATES);
	  if (msg)
	    {
	      dbus_message_set_auto_start (msg, FALSE);
	      dbus_connection_send (upno->dbus, msg, NULL);
	      dbus_message_unref (msg);
	    }
	}
    }
}

static void
check_for_updates (UpdateNotifier *upno)
{
  GError *error = NULL;
  GPid child_pid;
  gchar *gainroot_cmd = NULL;
  gchar *proxy = get_http_proxy ();
  struct stat info;

  /* Choose the right gainroot command */
  if (!stat ("/targets/links/scratchbox.config", &info))
    gainroot_cmd = g_strdup ("/usr/bin/fakeroot");
  else
    gainroot_cmd = g_strdup ("/usr/bin/sudo");

  /* Build command to be spawned */
  const char *argv[] =
    { gainroot_cmd,
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
				 &child_pid,
				 NULL,
				 NULL,
				 NULL,
				 &error))
    {
      fprintf (stderr, "can't run %s: %s\n", argv[0], error->message);
      g_error_free (error);
    }
  else
    g_child_watch_add (child_pid, check_for_updates_done, upno);

  g_free (gainroot_cmd);
  g_free (proxy);
}

static gchar *
get_http_proxy ()
{
  GConfClient *conf = NULL;
  gchar *proxy = NULL;

  if ((proxy = getenv ("http_proxy")) != NULL)
    return g_strdup (proxy);

  conf = gconf_client_get_default ();

  if (gconf_client_get_bool (conf, HTTP_PROXY_GCONF_DIR "/use_http_proxy",
			     NULL))
    {
      gchar *user = NULL;
      gchar *password = NULL;
      gchar *host = NULL;
      gint port;

      if (gconf_client_get_bool (conf, HTTP_PROXY_GCONF_DIR "/use_authentication",
				 NULL))
	{
	  user = gconf_client_get_string
	    (conf, HTTP_PROXY_GCONF_DIR "/authentication_user", NULL);
	  password = gconf_client_get_string
	    (conf, HTTP_PROXY_GCONF_DIR "/authentication_password", NULL);
	}

      host = gconf_client_get_string (conf, HTTP_PROXY_GCONF_DIR "/host", NULL);
      port = gconf_client_get_int (conf, HTTP_PROXY_GCONF_DIR "/port", NULL);

      if (user)
	{
	  // XXX - encoding of '@', ':' in user and password?

	  if (password)
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

  g_object_unref (conf);

  return proxy;
}

#define BUF_LEN 4096

static gboolean
is_file_modified_event (struct inotify_event *event,
			int watch, const char *name)
{
  return (event->wd == watch
	  && (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
	  && event->len > 0
	  && !strcmp (event->name, name));
}

static gboolean
handle_inotify (GIOChannel *channel, GIOCondition cond, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  
  char buf[BUF_LEN];
  int ifd = g_io_channel_unix_get_fd (channel);
  int len, i;

  while (1)
    {
      len = read (ifd, buf, BUF_LEN);
      if (len < 0) 
	{
	  if (errno == EINTR)
	    continue;
	  else
	    return FALSE;
	}
      
      i = 0;
      while (i < len)
	{
	  struct inotify_event *event;

	  event = (struct inotify_event *) &buf[i];

	  if (is_file_modified_event (event,
				      upno->home_watch,
				      SEEN_UPDATES_FILE)
	      || is_file_modified_event (event,
					 upno->varlibham_watch,
					 "available-updates"))
	    update_state (upno);
	    
	  i += sizeof (struct inotify_event) + event->len;
	}

      return TRUE;
    }
}

static void
setup_inotify (UpdateNotifier *upno)
{
  int ifd;

  ifd = inotify_init ();
  if (ifd >= 0)
    {
      upno->inotify_channel = g_io_channel_unix_new (ifd);
      g_io_add_watch (upno->inotify_channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
		      handle_inotify, upno);

      
      upno->home_watch =
	inotify_add_watch (ifd,
			   getenv ("HOME"),
			   IN_CLOSE_WRITE | IN_MOVED_TO);

      upno->varlibham_watch =
	inotify_add_watch (ifd,
			   "/var/lib/hildon-application-manager",
			   IN_CLOSE_WRITE | IN_MOVED_TO);
    }
}

static void
setup_alarm (UpdateNotifier *upno)
{
  alarm_event_t new_alarm;
  alarm_event_t *old_alarm = NULL;
  cookie_t alarm_cookie;
  int interval;

  /* We reset the alarm when we don't have a cookie for the old alarm
     (which probably means there is no old alarm), when we can't find
     the old alarm although we have a cookie (which shouldn't happen
     unless someone manually mucks with the alarm queue), or if the
     interval has changed.

     Otherwise we leave the old alarm in place, but we update its
     parameters without touching the timing.
  */

  interval = gconf_client_get_int (upno->gconf,
				   UPNO_GCONF_CZECH_INTERVAL,
				   NULL);
  if (interval <= 0)
    interval = 24 * 60;

  alarm_cookie = gconf_client_get_int (upno->gconf,
				       UPNO_GCONF_ALARM_COOKIE,
				       NULL);

  if (alarm_cookie > 0)
    old_alarm = alarm_event_get (alarm_cookie);

  /* Setup new alarm based on old alarm.
   */

  memset (&new_alarm, 0, sizeof(alarm_event_t));

  if (old_alarm == NULL || old_alarm->recurrence != interval)
    {
      /* Reset timing parameters.
       */

      time_t now = time (NULL);

      new_alarm.alarm_time = now + 60 * interval;
      new_alarm.recurrence = interval;
    }
  else
    {
      /* Copy timing parameters.
       */

      new_alarm.alarm_time = old_alarm->alarm_time;
      new_alarm.recurrence = old_alarm->recurrence;
    }
  alarm_event_free (old_alarm);

  /* Setup the rest.
   */

  new_alarm.recurrence_count = -1;
  new_alarm.snooze = 0;

  new_alarm.dbus_service = UPDATE_NOTIFIER_SERVICE;
  new_alarm.dbus_path = UPDATE_NOTIFIER_OBJECT_PATH;
  new_alarm.dbus_interface = UPDATE_NOTIFIER_INTERFACE;
  new_alarm.dbus_name = UPDATE_NOTIFIER_OP_CHECK_UPDATES;

  new_alarm.flags = (ALARM_EVENT_NO_DIALOG
		     | ALARM_EVENT_CONNECTED
		     | ALARM_EVENT_RUN_DELAYED);

  /* Replace old event with new one.  If we fail to delete the old
     alarm, we still add the new one, just to be safe.
   */
  if (alarm_cookie > 0)
    {
      int i = 0;
      time_t first = time (NULL);
      time_t last = (time_t)G_MAXINT32;

      /* Delete old alarm */
      alarm_event_del (alarm_cookie);

      /* Search for more alarms to delete (if available) */
      cookie_t *cookies = alarm_event_query (first, last, 0, 0);
      for (i = 0; cookies[i] != 0; i++)
	{
	  alarm_event_t *alarm = alarm_event_get (cookies[i]);
 	  if (alarm->dbus_interface != NULL &&
	      !strcmp (alarm->dbus_interface, UPDATE_NOTIFIER_INTERFACE))
	    {
	      alarm_event_del (cookies[i]);
	    }
 	  alarm_event_free (alarm);
	}
    }

  alarm_cookie = alarm_event_add (&new_alarm);

  gconf_client_set_int (upno->gconf,
			UPNO_GCONF_ALARM_COOKIE,
			alarm_cookie,
			NULL);
}

static void
cleanup_gconf (UpdateNotifier *upno)
{
  int i = 0;
  gconf_client_remove_dir (upno->gconf,
			   UPNO_GCONF_DIR,
			   NULL);

  for (i = 0; upno->gconf_notifications[i] != G_MAXUINT32; i++)
    gconf_client_notify_remove (upno->gconf,
				upno->gconf_notifications[i]);
  g_free (upno->gconf_notifications);
  g_object_unref(upno->gconf);
}

static void
cleanup_dbus (UpdateNotifier *upno)
{
  upno->dbus = dbus_bus_get (DBUS_BUS_SESSION, NULL);

  if (upno->dbus != NULL)
    {
      dbus_connection_remove_filter (upno->dbus, dbus_filter, NULL);
      dbus_connection_close (upno->dbus);
      dbus_connection_unref (upno->dbus);
    }
}

static void
cleanup_inotify (UpdateNotifier *upno)
{
  if (upno->inotify_channel != NULL)
    {
      int ifd = g_io_channel_unix_get_fd (upno->inotify_channel);

      inotify_rm_watch (ifd, upno->home_watch);
      inotify_rm_watch (ifd, upno->varlibham_watch);

      g_io_channel_shutdown (upno->inotify_channel, TRUE, NULL);
      g_io_channel_unref (upno->inotify_channel);
    }
}

static void
cleanup_alarm (UpdateNotifier *upno)
{
  cookie_t alarm_cookie;
  alarm_cookie = gconf_client_get_int (upno->gconf,
				       UPNO_GCONF_ALARM_COOKIE,
				       NULL);
  alarm_event_del (alarm_cookie);
}
