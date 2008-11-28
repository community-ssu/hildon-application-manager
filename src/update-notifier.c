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

/* XXX

   - Plug all the leaks
*/

#define _GNU_SOURCE

#include <stdarg.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <curl/curl.h>

#include <libosso.h>
#include <libhildonwm/hd-wm.h>

#include <libalarm.h>

#include "update-notifier.h"
#include "update-notifier-conf.h"

#include "hn-app-pixbuf-anim-blinker.h"
#include "xexp.h"
#include "user_files.h"

#define _(x) dgettext ("hildon-application-manager", (x))

#define USE_BLINKIFIER 0

#define HTTP_PROXY_GCONF_DIR      "/system/http_proxy"
#define HAM_APPID                 "hildon-application-manager-client"

/* For opening an URL */
#define URL_SERVICE                     "com.nokia.osso_browser"
#define URL_REQUEST_PATH                "/com/nokia/osso_browser/request"
#define URL_REQUEST_IF                  "com.nokia.osso_browser"
#define URL_OPEN_MESSAGE                "open_new_window"

#define UPDATE_NOTIFIER_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), UPDATE_NOTIFIER_TYPE, UpdateNotifierPrivate))

/* The persistent state of the update notifier.  It is stored in
   UFILE_UPDATE_NOTIFIER.
*/
typedef struct {
  int icon_state;
  cookie_t alarm_cookie;
} upno_state;

typedef struct _UpdateNotifierPrivate UpdateNotifierPrivate;
struct _UpdateNotifierPrivate
{
  GtkWidget *button;
  GtkWidget *blinkifier;
  GtkWidget *menu;
  GtkWidget *open_ham_item;
  GtkWidget *show_notification_item;
  GtkWidget *reject_notification_item;
  GdkPixbuf *static_pic;

  gint button_toggled_handler_id;
  gint menu_selection_done_handler_id;
  gint open_ham_item_activated_handler_id;
  gint show_notification_item_activated_handler_id;
  gint reject_notification_item_activated_handler_id;

  guint blinking_timeout_id;
  guint alarm_init_timeout_id;

  osso_context_t *osso_ctxt;
  GConfClient *gconf;
  guint *gconf_notifications;
  GIOChannel *inotify_channel;
  gboolean inotify_ready;
  int home_watch;
  int varlibham_watch;
  guint inotify_io_watch_id;
  guint child_watch_id;

  upno_state state;
  
  GMutex* notifications_thread_mutex;
};

HD_DEFINE_PLUGIN_MODULE (UpdateNotifier, update_notifier, HD_TYPE_STATUS_MENU_ITEM);

enum {
  UPNO_ICON_INVISIBLE,
  UPNO_ICON_STATIC,
  UPNO_ICON_BLINKING
};

/* Initialization/destruction functions */
static void update_notifier_class_init (UpdateNotifierClass *klass);
static void update_notifier_init (UpdateNotifier *upno);
static void update_notifier_finalize (GObject *object);

/* Private functions */
static void button_toggled (GtkWidget *button, gpointer data);
static void menu_hidden (GtkMenuShell *menu, gpointer user_data);
static void open_ham_menu_item_activated (GtkWidget *menu, gpointer data);
static void display_event_cb (osso_display_state_t state, gpointer data);

static void setup_gconf (UpdateNotifier *upno);

static void set_icon_visibility (UpdateNotifier *upno, int state);
static gboolean setup_dbus (UpdateNotifier *upno);

static gchar *get_http_proxy ();
static void setup_inotify (UpdateNotifier *upno);
static gboolean setup_alarm (UpdateNotifier *upno);

static void update_icon_visibility (UpdateNotifier *upno);
static void update_state (UpdateNotifier *upno);

static void show_check_for_updates_view (UpdateNotifier *upno);
static gboolean showing_check_for_updates_view (UpdateNotifier *upno);
static void check_for_updates (UpdateNotifier *upno);
static void check_for_notifications (UpdateNotifier *upno);

static void cleanup_gconf (UpdateNotifier *upno);
static void cleanup_inotify (UpdateNotifier *upno);
static void cleanup_dbus (UpdateNotifier *upno);
static void cleanup_alarm (UpdateNotifier *upno);

static char *url_eval (const char *url);
static gint str_find_pos (const gchar *haystack, const gchar *needle);
static gchar *str_substitute (const gchar *url, const gchar *code, const gchar *value);
static const char *get_osso_product_hardware ();

static void load_state (UpdateNotifier *upno);
static void save_state (UpdateNotifier *upno);

static void save_last_update_time (time_t t);

/* Initialization/destruction functions */

static void
update_notifier_class_finalize (UpdateNotifierClass *klass)
{
}

static void
update_notifier_class_init (UpdateNotifierClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = update_notifier_finalize;

  g_type_class_add_private (object_class, sizeof (UpdateNotifierPrivate));
}

static gboolean
setup_alarm_now (gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  if (setup_alarm (upno))
    {
      priv->alarm_init_timeout_id = 0;
      return FALSE;
    }

  /* Try again in one minute.
   */
  return TRUE;
}

static void
update_notifier_init (UpdateNotifier *upno)
{
  GdkPixbuf *icon_pixbuf;
  GtkIconTheme *icon_theme;
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  priv->osso_ctxt = NULL;

  /* Setup dbus */
  if (setup_dbus (upno))
    {
      priv->notifications_thread_mutex =g_mutex_new ();
      
      osso_hw_set_display_event_cb (priv->osso_ctxt, display_event_cb, upno);

      setup_gconf (upno);

      priv->button = gtk_toggle_button_new ();
      
      icon_theme = gtk_icon_theme_get_default ();
      icon_pixbuf = gtk_icon_theme_load_icon (icon_theme,
					      "qgn_stat_new_updates",
					      40,
					      GTK_ICON_LOOKUP_NO_SVG,
					      NULL);
#if USE_BLINKIFIER
      priv->static_pic = icon_pixbuf;
      priv->blinkifier = gtk_image_new_from_animation (hn_app_pixbuf_anim_blinker_new(icon_pixbuf, 1000, -1, 100));
#else
      priv->blinkifier = gtk_image_new_from_pixbuf (icon_pixbuf);
#endif
  
      gtk_container_add (GTK_CONTAINER (priv->button), priv->blinkifier);
      gtk_container_add (GTK_CONTAINER (upno), priv->button);

      gtk_widget_show (priv->blinkifier);
      gtk_widget_show (priv->button);

      priv->button_toggled_handler_id =
	g_signal_connect (priv->button, "toggled",
			  G_CALLBACK (button_toggled), upno);

      load_state (upno);

      setup_inotify (upno);

      update_icon_visibility (upno);
      update_state (upno);

      /* We only setup the alarm after a one minute pause since the alarm
	 daemon is not yet running when the plugins are loaded after boot.
	 It is arguably a bug in the alarm framework that the daemon needs
	 to be running to access and modify the alarm queue.
      */
      priv->alarm_init_timeout_id =
	g_timeout_add (60*1000, setup_alarm_now, upno);
    }
}

static void
update_notifier_finalize (GObject *object)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (object);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  /* Destroy local widgets */
  if (priv->menu)
    gtk_widget_destroy (priv->menu);

  if (priv->static_pic)
    g_object_unref (priv->static_pic);

  if (priv->blinking_timeout_id > 0)
    g_source_remove (priv->blinking_timeout_id);

  if (priv->alarm_init_timeout_id > 0)
    g_source_remove (priv->alarm_init_timeout_id);

  if (priv->child_watch_id > 0)
    g_source_remove (priv->child_watch_id);
  
  /* Clean up stuff */
  cleanup_alarm (upno);
  cleanup_inotify (upno);
  cleanup_gconf (upno);
  cleanup_dbus (upno);

  if (priv->notifications_thread_mutex)
    g_mutex_free (priv->notifications_thread_mutex);
  
#if 0
  /* XXX - this doesn't work for some reason, priv->button seems to be
           corrupted.  Keeping it alive with a refcount didn't help...
  */
  gtk_signal_handler_disconnect (priv->button,
				 priv->button_toggled_handler_id);
#endif

  G_OBJECT_CLASS (g_type_class_peek_parent
		  (G_OBJECT_GET_CLASS(object)))->finalize(object);
}

/* Private functions */

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
menu_position_func (GtkMenu   *menu, 
		    gint      *x, 
		    gint      *y,
		    gboolean  *push_in, 
		    gpointer   data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  
  GtkRequisition req;
  
  gtk_widget_size_request (GTK_WIDGET (menu->toplevel), &req);

  gdk_window_get_origin (priv->button->window, x, y);
  *x += (priv->button->allocation.x
	 + priv->button->allocation.width
	 - req.width);
  *x = MAX (*x, AVAILABLE_NOTIFICATIONS_MENU_LEFT_PADDING);
  *y += (priv->button->allocation.y
	 + priv->button->allocation.height
	 + AVAILABLE_NOTIFICATIONS_MENU_TOP_PADDING);

  *push_in = FALSE;
}

static void
button_toggled (GtkWidget *button, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  set_icon_visibility (upno, UPNO_ICON_STATIC);

  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (button)))
    return;

  gtk_menu_popup (GTK_MENU (priv->menu),
		  NULL, NULL,
		  menu_position_func, upno,
		  1,
		  gtk_get_current_event_time ());
}

static void
menu_hidden (GtkMenuShell *menu, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->button), FALSE);
}

static void
open_ham_menu_item_activated (GtkWidget *menu, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  show_check_for_updates_view (upno);
}

/* This function copies AVAILABLE_NOTIFICATIONS_FILE to SEEN_NOTIFICATIONS_FILE */
static void
update_seen_notifications (UpdateNotifier *upno)
{
  xexp *available_nots;

  available_nots = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
  if (available_nots != NULL)
    {
      user_file_write_xexp (UFILE_SEEN_NOTIFICATIONS, available_nots);
      xexp_free (available_nots);

      /* Force to inmediatly set its INVISIBLE state */
      set_icon_visibility (upno, UPNO_ICON_INVISIBLE);
    }
}

static gboolean
dbus_open_url (char* url)
{
  /*
   dbus-send --print-reply --dest=com.nokia.osso_browser /com/nokia/osso_browser/request com.nokia.osso_browser.open_new_window string:'http://example.com'
   */

  if (!url)
    return FALSE;

  DBusConnection *conn;
  DBusMessage *msg;
  gboolean result = FALSE;

  conn = dbus_bus_get (DBUS_BUS_SESSION, NULL);
  if (!conn)
    {
      return result;
    }
  
  msg = dbus_message_new_method_call (URL_SERVICE,
                                      URL_REQUEST_PATH,
                                      URL_REQUEST_IF,
                                      URL_OPEN_MESSAGE);
  dbus_message_set_no_reply(msg, TRUE);
  dbus_message_append_args (msg,
                            DBUS_TYPE_STRING, &url,
                            DBUS_TYPE_INVALID);

  result = dbus_connection_send (conn, msg, NULL);
  dbus_connection_flush (conn);
  dbus_message_unref(msg);

  return result;
}

struct show_notification_menu_item_activated_data
{
  gchar *notification_url;
  UpdateNotifier *upno;
};

static void
show_notification_menu_item_activated (GtkWidget *menu, gpointer data)
{
  struct show_notification_menu_item_activated_data *c = 
    (struct show_notification_menu_item_activated_data*)data;
  UpdateNotifier *upno = UPDATE_NOTIFIER (c->upno);
  
  if (dbus_open_url (c->notification_url))
    update_seen_notifications (upno);
  
  update_state(upno);

  g_free (c->notification_url);
  g_free (c);
}

static void
reject_notification_menu_item_activated (GtkWidget *menu, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  
  update_seen_notifications (upno);
  update_state(upno);
}

#if !USE_BLINKIFIER
static gboolean
blink_icon (gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  if (GTK_WIDGET_VISIBLE (priv->blinkifier))
    gtk_widget_hide (priv->blinkifier);
  else
    gtk_widget_show (priv->blinkifier);

  return TRUE;
}
#endif

static void
display_event_cb (osso_display_state_t state, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  if (priv->state.icon_state == UPNO_ICON_BLINKING)
    {
#if USE_BLINKIFIER
      if (state == OSSO_DISPLAY_OFF)
	{
	  g_object_set(priv->blinkifier, "pixbuf", priv->static_pic, NULL);
	}
      else
	{
	  g_object_set(priv->blinkifier, "pixbuf-animation",
		       hn_app_pixbuf_anim_blinker_new(priv->static_pic, 1000, -1, 100),
		       NULL);
	}
#else
      if (state == OSSO_DISPLAY_OFF)
	{
	  if (priv->blinking_timeout_id > 0)
	    {
	      g_source_remove (priv->blinking_timeout_id);
	      priv->blinking_timeout_id = 0;
	    }
	}
      else
	{
	  if (priv->blinking_timeout_id == 0)
	    priv->blinking_timeout_id = g_timeout_add (500, blink_icon, upno);
	}
#endif
    }
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
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  priv->gconf = gconf_client_get_default();
  gconf_client_add_dir (priv->gconf,
			UPNO_GCONF_DIR,
			GCONF_CLIENT_PRELOAD_ONELEVEL,
			NULL);

  /* Add gconf notifications and store connection IDs */
  priv->gconf_notifications = g_new0 (guint, 2);
  priv->gconf_notifications[0] =
    gconf_client_notify_add (priv->gconf,
			     UPNO_GCONF_CHECK_INTERVAL,
			     gconf_interval_changed, upno,
			     NULL, NULL);

  /* Finish the list of connection IDs */
  priv->gconf_notifications[1] = -1;
}

static void
set_condition_carefully (UpdateNotifier *upno, gboolean condition)
{
#if 0 /* this is not valid in the new libhildondesktop-1 */
  /* Setting the 'condition' of the plugin will cause the overflow row
     of the status bar to be closed, regardless of whether the
     condition has actually changed or not.  Thus, we are careful here
     not to call g_object_set when the condition has not changed.
  */

  gboolean old_condition;
  g_object_get (upno, "condition", &old_condition, NULL);
  if (old_condition != condition)
    g_object_set (upno, "condition", condition, NULL);
#endif
}

static void
update_icon_visibility (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  int state = priv->state.icon_state;

  set_condition_carefully (upno, (state == UPNO_ICON_STATIC
				  || state == UPNO_ICON_BLINKING));

#if USE_BLINKIFIER
  if (state == UPNO_ICON_BLINKING)
    g_object_set(priv->blinkifier, "pixbuf-animation", hn_app_pixbuf_anim_blinker_new(priv->static_pic, 1000, -1, 100), NULL);
  else
    g_object_set(priv->blinkifier, "pixbuf", priv->static_pic, NULL);
#else
  if (state == UPNO_ICON_BLINKING)
    {
      if (priv->blinking_timeout_id == 0)
	priv->blinking_timeout_id = g_timeout_add (500, blink_icon, upno);
    }
  else
    {
      gtk_widget_show (priv->blinkifier);
      if (priv->blinking_timeout_id > 0)
	{
	  g_source_remove (priv->blinking_timeout_id);
	  priv->blinking_timeout_id = 0;
	}
    }
#endif
}

static void
menu_add_readonly_item (GtkWidget *menu, gboolean is_markup,
			const char *fmt, ...)
{
  GtkWidget *label, *item;
  va_list ap;
  gchar *text;
  GtkRequisition req;

  va_start (ap, fmt);
  text = g_strdup_vprintf (fmt, ap);
  va_end (ap);

  if (is_markup)
    {
      item = gtk_menu_item_new ();
      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL(label), text);
    }
  else
    {
      item = gtk_menu_item_new ();
      label = gtk_label_new (text);
    }
  gtk_label_set_line_wrap (GTK_LABEL(label), TRUE);
  gtk_label_set_justify (GTK_LABEL(label), GTK_JUSTIFY_FILL);
  gtk_misc_set_alignment (GTK_MISC(label), 0.0, 0.5);
  gtk_widget_size_request (label, &req);
  if (req.width > AVAILABLE_NOTIFICATIONS_MENU_WIDTH)
    gtk_widget_set_size_request (label, AVAILABLE_NOTIFICATIONS_MENU_WIDTH, -1);
  
  gtk_container_add (GTK_CONTAINER (item), label);
  gtk_menu_append (GTK_MENU (menu), item);
  gtk_widget_show (item);
  gtk_widget_show (label);
  gtk_widget_set_sensitive (item, FALSE);
  label->state = GTK_STATE_NORMAL;  /* Hurray for weak encapsulation. */

  g_free (text);
}

static void
menu_add_separator (GtkWidget *menu)
{
  GtkWidget *item = gtk_separator_menu_item_new ();
  gtk_menu_append (menu, item);
  gtk_widget_show (item);
}

static void
safe_signal_disconnect (gpointer instance, gulong handler_id)
{
  if ((instance != NULL) &&
      g_signal_handler_is_connected (instance, handler_id))
    {
      g_signal_handler_disconnect (instance, handler_id);
    }
}

void
cleanup_menu (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  
  if (priv->menu)
    {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->button), FALSE);
      
      safe_signal_disconnect (priv->menu,
                              priv->menu_selection_done_handler_id);
      safe_signal_disconnect (priv->open_ham_item,
                              priv->open_ham_item_activated_handler_id);
      safe_signal_disconnect (priv->show_notification_item,
                              priv->show_notification_item_activated_handler_id);
      safe_signal_disconnect (priv->reject_notification_item,
                              priv->reject_notification_item_activated_handler_id);
      gtk_widget_destroy (priv->menu);

      priv->menu = NULL;
    }
  priv->open_ham_item = NULL;
  priv->show_notification_item = NULL;
  priv->reject_notification_item = NULL;
}

gboolean
create_new_updates_menu (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  xexp *available_updates = NULL, *seen_updates = NULL;
  int n_os = 0, n_certified = 0, n_other = 0, n_new = 0;
  GtkWidget *item = NULL;

  available_updates = xexp_read_file (AVAILABLE_UPDATES_FILE);
  seen_updates = user_file_read_xexp (UFILE_SEEN_UPDATES);

  if (seen_updates == NULL)
    seen_updates = xexp_list_new ("updates");

  if (available_updates != NULL)
    {
      xexp *x = NULL, *y = NULL;

      for (x = xexp_first (available_updates); x; x = xexp_rest (x))
        {
          if (xexp_is_text (x))
            {
	      if ((seen_updates != NULL) && xexp_is_list (seen_updates))
		{
		  const char *pkg = xexp_text (x);

		  for (y = xexp_first (seen_updates); y; y = xexp_rest (y))
		    if (xexp_is_text (y)
			&& strcmp (pkg, xexp_text (y)) == 0)
		      break;
		}

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

      xexp_free (available_updates);

      if (seen_updates != NULL)
	xexp_free (seen_updates);
    }

  if (n_new > 0 && !showing_check_for_updates_view (upno))
    {
      cleanup_menu (upno);

      priv->menu = gtk_menu_new ();

      priv->menu_selection_done_handler_id =
        g_signal_connect (priv->menu, "selection-done",
                          G_CALLBACK(menu_hidden), upno);
      
      menu_add_readonly_item (priv->menu, FALSE, _("ai_sb_update_description"));

      if (n_certified > 0)
        menu_add_readonly_item (priv->menu, FALSE, _("ai_sb_update_nokia_%d"),
                                n_certified);
      if (n_other > 0)
        menu_add_readonly_item (priv->menu, FALSE, _("ai_sb_update_thirdparty_%d"),
                                n_other);
      if (n_os > 0)
        menu_add_readonly_item (priv->menu, FALSE, _("ai_sb_update_os"));

      menu_add_separator (priv->menu);

      item = gtk_menu_item_new_with_label (_("ai_sb_update_am"));
      gtk_menu_shell_append ((GtkMenuShell *)priv->menu, item);
      gtk_widget_show (item);

      priv->open_ham_item = item;
      priv->open_ham_item_activated_handler_id =
        g_signal_connect (item, "activate",
                          G_CALLBACK (open_ham_menu_item_activated), upno);

      if (priv->state.icon_state == UPNO_ICON_INVISIBLE)
        set_icon_visibility (upno, UPNO_ICON_BLINKING);

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

gboolean
create_new_notifications_menu (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  xexp *seen_notifications = NULL;
  xexp *available_nots = NULL;
  const gchar *available_title = NULL;
  const gchar *available_text = NULL;
  const gchar *available_uri = NULL;
  gboolean new_notifications = FALSE;
  gboolean result = FALSE;
  GtkWidget *item = NULL;

  available_nots = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
  seen_notifications = user_file_read_xexp (UFILE_SEEN_NOTIFICATIONS);

  if (available_nots && xexp_is_tag_and_not_empty (available_nots, "info"))
      {
        available_title = xexp_aref_text(available_nots, "title");
        available_text = xexp_aref_text(available_nots, "text");
        available_uri = xexp_aref_text(available_nots, "uri");
      }

  new_notifications = (available_title!=NULL) && (available_uri!=NULL) && (available_text!=NULL);  

  if (new_notifications && seen_notifications &&
      xexp_is_tag_and_not_empty (seen_notifications, "info"))
    {
      new_notifications = new_notifications
        && !compare_xexp_text (available_nots, seen_notifications, "title")
        && !compare_xexp_text (available_nots, seen_notifications, "text")
        && !compare_xexp_text (available_nots, seen_notifications, "uri");
    }

  if (seen_notifications)
    xexp_free (seen_notifications);

  /* Create the menu */
  if (new_notifications)
    {
      gchar* formatted_title = NULL;
      gchar* formatted_text = NULL;

      cleanup_menu (upno);

      priv->menu = gtk_menu_new ();

      priv->menu_selection_done_handler_id =
        g_signal_connect (priv->menu, "selection-done",
                          G_CALLBACK(menu_hidden), upno);

      menu_add_readonly_item (priv->menu, FALSE, _("ai_sb_app_push_desc"));

      formatted_title = g_markup_printf_escaped ("<b>%s</b>",
                                                 available_title);
      menu_add_readonly_item (priv->menu, TRUE, formatted_title);
      g_free (formatted_title);

      formatted_text = g_markup_printf_escaped ("<small>%s</small>",
                                                available_text);
      menu_add_readonly_item (priv->menu, TRUE, formatted_text);
      g_free (formatted_text);

      menu_add_separator (priv->menu);

      item = gtk_menu_item_new_with_label (_("ai_sb_app_push_no"));
      gtk_menu_shell_append ((GtkMenuShell *)priv->menu, item);
      gtk_widget_show (item);

      priv->reject_notification_item = item;
      priv->reject_notification_item_activated_handler_id =
        g_signal_connect (item, "activate",
                          G_CALLBACK (reject_notification_menu_item_activated),
                          upno);

      menu_add_separator (priv->menu);

      item = gtk_menu_item_new_with_label (_("ai_sb_app_push_link"));
      gtk_menu_shell_append ((GtkMenuShell *)priv->menu, item);
      gtk_widget_show (item);

      struct show_notification_menu_item_activated_data *c = 
        g_new0 (struct show_notification_menu_item_activated_data, 1);
      c->upno = upno;
      c->notification_url = g_strdup (available_uri);
      priv->show_notification_item = item;
      priv->show_notification_item_activated_handler_id =
        g_signal_connect (item, "activate",
                          G_CALLBACK (show_notification_menu_item_activated),
                          c);
      
      if (priv->state.icon_state == UPNO_ICON_INVISIBLE)
        set_icon_visibility (upno, UPNO_ICON_BLINKING);
      
      result = TRUE;
    }
  else
    {
      result = FALSE;
    }

  if (available_nots)
    xexp_free (available_nots);

  return result;
}

static void
update_state (UpdateNotifier *upno)
{
  g_return_if_fail (upno != NULL);

  if (!create_new_updates_menu (upno) && !create_new_notifications_menu (upno))
    {
      cleanup_menu (upno);
      set_icon_visibility (upno, UPNO_ICON_INVISIBLE);
    }
}

static void
set_icon_visibility (UpdateNotifier *upno, int state)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  int old_state = priv->state.icon_state;

  if (state != old_state && state != UPNO_ICON_STATIC)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(priv->button), FALSE);

  priv->state.icon_state = state;
  save_state (upno);

  update_icon_visibility (upno);
}

static gint
osso_rpc_handler(const gchar* interface, const gchar* method,
                 GArray* arguments, gpointer data, osso_rpc_t* retval)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  if (!strcmp (interface, UPDATE_NOTIFIER_INTERFACE))
    {
      if (!strcasecmp(method, UPDATE_NOTIFIER_OP_CHECK_UPDATES))
	{
	  /* Search for new available updates */
	  check_for_updates (upno);
	  /* Search for notifications */
	  check_for_notifications (upno);
	}
      else if (!strcasecmp(method, UPDATE_NOTIFIER_OP_CHECK_STATE))
	{
	  /* Update states of the statusbar item */
	  update_state (upno);
	}
    }

  return OSSO_OK;
}

static gboolean
setup_dbus (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  osso_return_t result;

  priv->osso_ctxt = osso_initialize ("hildon_update_notifier",
				     PACKAGE_VERSION, TRUE, NULL);
  if (!priv->osso_ctxt)
    return FALSE;

  result = osso_rpc_set_cb_f (priv->osso_ctxt,
			      UPDATE_NOTIFIER_SERVICE,
			      UPDATE_NOTIFIER_OBJECT_PATH,
			      UPDATE_NOTIFIER_INTERFACE,
			      osso_rpc_handler,
			      upno);

  return (result == OSSO_OK);
}

static gboolean
ham_is_running ()
{
  HDWM *hdwm;
  HDWMEntryInfo *info = NULL;
  GList *apps_list = NULL;
  GList *l = NULL;
  gchar *ham_appname = NULL;
  gchar *current_appname = NULL;
  gboolean app_found = FALSE;

  hdwm = hd_wm_get_singleton ();
  hd_wm_update_client_list (hdwm);

  ham_appname = g_strdup ("Application manager");
  apps_list = hd_wm_get_applications (hdwm);
  for (l = apps_list; l; l = l->next)
    {
      info = (HDWMEntryInfo *) l->data;

      if (current_appname != NULL)
	g_free (current_appname);

      current_appname = g_strdup (hd_wm_entry_info_get_app_name (info));

      if (current_appname && !strcmp (ham_appname, current_appname))
	{
	  app_found = TRUE;
	  break;
	}
    }

  /* Free resources */
  g_free (ham_appname);
  g_free (current_appname);

  return app_found;
}

static void
show_check_for_updates_view (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  osso_rpc_async_run (priv->osso_ctxt,
		      HILDON_APP_MGR_SERVICE,
		      HILDON_APP_MGR_OBJECT_PATH,
		      HILDON_APP_MGR_INTERFACE,
		      HILDON_APP_MGR_OP_SHOW_CHECK_FOR_UPDATES,
		      NULL,
		      NULL,
		      DBUS_TYPE_INVALID);
}

static gboolean
showing_check_for_updates_view (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  osso_return_t result;
  osso_rpc_t reply;

  if (ham_is_running ())
    {
      result = osso_rpc_run (priv->osso_ctxt,
			     HILDON_APP_MGR_SERVICE,
			     HILDON_APP_MGR_OBJECT_PATH,
			     HILDON_APP_MGR_INTERFACE,
			     HILDON_APP_MGR_OP_SHOWING_CHECK_FOR_UPDATES,
			     &reply,
			     DBUS_TYPE_INVALID);

      /* Return boolean value from reply */
      if (result == OSSO_OK && reply.type == DBUS_TYPE_BOOLEAN)
	return reply.value.b;
    }

  return FALSE;
}

static void
check_for_updates_done (GPid pid, int status, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  priv->child_watch_id = 0;

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 0)
    {
      save_last_update_time (time (NULL));
      update_state (upno);
    }
  else
    {
      /* Ask the Application Manager to perform the update, but don't
	 start it if it isn't running already.
       */
      if (ham_is_running ())
	{
	  osso_rpc_async_run (priv->osso_ctxt,
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

static gboolean
expensive_connection (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  /* XXX - only the WLAN_INFRA and the WLAN_ADHOC bearers are
           considered cheap.  There should be a general platform
           feature that tells us whether we need to be careful with
           network access or not.  Also, peeking into GConf is not the
           best thing, but the ConIc API doesn't seem to have an easy
           way to query the currently active connection.
  */

  gboolean cheap;

  char *last_used_type =
    gconf_client_get_string (priv->gconf,
			     "/system/osso/connectivity/IAP/last_used_type",
			     NULL);

  cheap = (last_used_type != NULL
	   && (strcmp (last_used_type, "WLAN_ADHOC") == 0
	       || strcmp (last_used_type, "WLAN_INFRA") == 0));

  g_free (last_used_type);
  return !cheap;
}

static void
check_for_updates (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  GError *error = NULL;
  GPid child_pid;
  gchar *gainroot_cmd = NULL;
  gchar *proxy = get_http_proxy ();
  struct stat info;

  if (expensive_connection (upno))
    return;

  /* Choose the right gainroot command */
  if (!stat ("/targets/links/scratchbox.config", &info))
    gainroot_cmd = g_strdup ("/usr/bin/fakeroot");
  else
    gainroot_cmd = g_strdup ("/usr/bin/sudo");

  /* Build command to be spawned */
  char *argv[] =
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
    priv->child_watch_id = 
      g_child_watch_add (child_pid, check_for_updates_done, upno);

  g_free (gainroot_cmd);
  g_free (proxy);
}

static char *
get_notifier_uri (void)
{
  char *uri = NULL;
  xexp *notifier_conf = xexp_read_file (UPNO_NOTIFIER_CONF);

  if (notifier_conf != NULL)
    {
      const char *tmp_uri = xexp_aref_text (notifier_conf, "uri");

      if (tmp_uri != NULL)
	uri = g_strdup (tmp_uri);

      xexp_free (notifier_conf);
    }

  return uri;
}

static gpointer
check_for_notifications_thread (gpointer userdata)
{
  UpdateNotifierPrivate *priv = NULL;
  FILE *tmp_file = NULL;
  gchar *proxy = NULL;
  gchar *notifier_uri = NULL;
  gchar *uri = NULL;

  g_return_val_if_fail (userdata != NULL, NULL);

  /* Return if no notifer URI was found */
  notifier_uri = get_notifier_uri ();
  if (notifier_uri == NULL)
    return NULL;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (userdata);

  /* only one thread is allowed to download stuff at any given moment */
  if (!g_mutex_trylock (priv->notifications_thread_mutex))
    return NULL;

  uri = url_eval (notifier_uri);
  tmp_file  = user_file_open_for_write (UFILE_AVAILABLE_NOTIFICATIONS_TMP);

  if (uri != NULL && tmp_file != NULL)
    {
      CURL *handle = curl_easy_init ();
      xexp *tmp_data = NULL;
      int success;
      long response;

      proxy = get_http_proxy ();

      /* curl_easy_setopt (handle, CURLOPT_WRITEFUNCTION, NULL); */
      curl_easy_setopt (handle, CURLOPT_WRITEDATA, tmp_file);
      curl_easy_setopt (handle, CURLOPT_URL, uri);
      if (proxy != NULL)
	curl_easy_setopt (handle, CURLOPT_PROXY, proxy);

      success = 0;
      if (curl_easy_perform (handle) == 0
	  && curl_easy_getinfo (handle, CURLINFO_RESPONSE_CODE, &response) == 0)
	success = (response == 200);

      curl_easy_cleanup (handle);

      fflush (tmp_file);
      fsync (fileno (tmp_file));
      fclose (tmp_file);

      /* Validate data */
      if (success)
	tmp_data = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS_TMP);
      else
	tmp_data = NULL;

      if (tmp_data != NULL 
	  && xexp_is_list (tmp_data)
	  && xexp_is (tmp_data, "info"))
        {
          /* Copy data to the final file if validated */
          user_file_write_xexp (UFILE_AVAILABLE_NOTIFICATIONS, tmp_data);

	  /* Delete temp file only on success.  It is useful for
	     debugging. */
	  user_file_remove (UFILE_AVAILABLE_NOTIFICATIONS_TMP);
        }
    }
  else if (tmp_file != NULL)
    {
      fclose (tmp_file);
      user_file_remove (UFILE_AVAILABLE_NOTIFICATIONS_TMP);
    }

  g_mutex_unlock (priv->notifications_thread_mutex);
  if (notifier_uri != NULL)
    g_free (notifier_uri);
  if (uri != NULL)
    g_free (uri);
  if (proxy != NULL)
    g_free (proxy);
  return NULL;
}

static void
check_for_notifications (UpdateNotifier *upno)
{
  g_return_if_fail (upno != NULL);
  g_thread_create ((GThreadFunc)check_for_notifications_thread, upno, FALSE, NULL);
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
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  char buf[BUF_LEN];
  int ifd = g_io_channel_unix_get_fd (channel);
  int len, i;

  /* Return if the object was already destroyed
     or the if inotify is not still ready */
  if (!priv || !priv->inotify_ready)
    return FALSE;

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

          if (priv->varlibham_watch != -1
              && is_file_modified_event (event, priv->varlibham_watch, AVAILABLE_UPDATES_FILE_NAME))
            {
              update_state (upno);
            }
          else if (priv->home_watch != -1)
            {
              if (is_file_modified_event (event, priv->home_watch, UFILE_AVAILABLE_NOTIFICATIONS))
                {
                  xexp *available_nots = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
                  xexp *seen_notifications = user_file_read_xexp (UFILE_SEEN_NOTIFICATIONS);

                  if (available_nots && seen_notifications
		      && xexp_is_tag_and_not_empty (available_nots, "info")
		      && (!xexp_is_tag_and_not_empty (seen_notifications, "info")
			  || (xexp_is_tag_and_not_empty (seen_notifications, "info")
			      && !compare_xexp_text (available_nots, seen_notifications, "title")
			      && !compare_xexp_text (available_nots, seen_notifications, "text")
			      && !compare_xexp_text (available_nots, seen_notifications, "uri"))))
		    {
		      /* as we have new notifications, we no longer need the old seen ones;
		       * the writing of UFILE_SEEN_NOTIFICATIONS will trigger an inotify */
		      xexp* empty_seen_notifications = xexp_list_new ("info");
		      user_file_write_xexp (UFILE_SEEN_NOTIFICATIONS, empty_seen_notifications);
		      xexp_free (empty_seen_notifications);
		    }

		  if (available_nots)
		    xexp_free (available_nots);

		  if (seen_notifications)
		    xexp_free (seen_notifications);
                }
              else if (is_file_modified_event (event, priv->home_watch, UFILE_SEEN_UPDATES)
                  || is_file_modified_event (event, priv->home_watch, UFILE_SEEN_NOTIFICATIONS))
                {
                  update_state (upno);
                }
            }

          i += sizeof (struct inotify_event) + event->len;
        }

      return TRUE;
    }
}

static void
setup_inotify (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  gchar *state_dir;
  int ifd;

  ifd = inotify_init ();
  if (ifd >= 0)
    {
      priv->inotify_channel = g_io_channel_unix_new (ifd);
      priv->inotify_io_watch_id =
	g_io_add_watch (priv->inotify_channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
			handle_inotify, upno);

      state_dir = user_file_get_state_dir_path ();
      priv->home_watch =
        inotify_add_watch (ifd,
                           state_dir,
                           IN_CLOSE_WRITE | IN_MOVED_TO);
      g_free (state_dir);

      priv->varlibham_watch =
	inotify_add_watch (ifd,
			   "/var/lib/hildon-application-manager",
			   IN_CLOSE_WRITE | IN_MOVED_TO);

      priv->inotify_ready = TRUE;
    }
}

static void
search_and_delete_all_alarms (void)
{
  int i = 0;
  time_t first = time (NULL);
  time_t last = (time_t) G_MAXINT32;
  cookie_t *cookies = NULL;
 
  cookies = alarmd_event_query (first, last, 0, 0, HAM_APPID);

  if (cookies == NULL)
    return;

  for (i = 0; cookies[i] != 0; i++)
    {
      alarm_event_t *event = alarmd_event_get (cookies[i]);
      alarm_action_t *action = alarm_event_get_action (event, 0);
      if ((action != NULL) &&
          (!strcmp (alarm_action_get_dbus_service (action),
                    UPDATE_NOTIFIER_INTERFACE)))
        {
          alarmd_event_del (cookies[i]);
        }
      alarm_event_delete (event);
    }
  g_free (cookies);
}

static gboolean
setup_alarm (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  alarm_event_t *new_event;
  alarm_action_t *new_action;
  alarm_event_t *old_event = NULL;
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

  interval = gconf_client_get_int (priv->gconf,
				   UPNO_GCONF_CHECK_INTERVAL,
				   NULL);
  if (interval <= 0)
    {
      /* Use default value and set it from now on */
      interval = UPNO_DEFAULT_CHECK_INTERVAL;
      gconf_client_set_int (priv->gconf,
                            UPNO_GCONF_CHECK_INTERVAL,
                            interval,
                            NULL);
    }

  alarm_cookie = priv->state.alarm_cookie;

  if (alarm_cookie > 0)
    old_event = alarmd_event_get (alarm_cookie);

  /* Setup new alarm based on old alarm.
   */

  new_event = alarm_event_create ();

  alarm_event_set_alarm_appid (new_event, HAM_APPID);
  alarm_event_set_title (new_event, "H-A-M Update Notifier");
  alarm_event_set_message (new_event, NULL);

  new_event->flags |= ALARM_EVENT_CONNECTED;
  new_event->flags |= ALARM_EVENT_RUN_DELAYED;

  new_event->recur_count = -1;
  new_event->snooze_secs = 0;

  if (old_event == NULL || old_event->recur_secs != interval)
    {
      /* Reset timing parameters.
       */

      time_t now = time (NULL);

      new_event->alarm_time = now + ALARM_RECURRING_MINUTES (interval);
      new_event->recur_secs = interval;
    }
  else
    {
      /* Copy timing parameters.
       */

      new_event->alarm_time = old_event->alarm_time;
      new_event->recur_secs = old_event->recur_secs;
    }

  alarm_event_delete (old_event);

  /* Setup the rest.
   */

  new_action = alarm_event_add_actions (new_event, 1);
  new_action->flags = ALARM_ACTION_TYPE_DBUS | ALARM_ACTION_WHEN_TRIGGERED;
  alarm_action_set_dbus_service (new_action, UPDATE_NOTIFIER_SERVICE);
  alarm_action_set_dbus_path (new_action, UPDATE_NOTIFIER_OBJECT_PATH);
  alarm_action_set_dbus_interface (new_action, UPDATE_NOTIFIER_INTERFACE);
  alarm_action_set_dbus_name (new_action, UPDATE_NOTIFIER_OP_CHECK_UPDATES);


  /* Replace old event with new one.  If we fail to delete the old
     alarm, we still add the new one, just to be safe.
   */
  if (alarm_cookie > 0)
    alarmd_event_del (alarm_cookie);

  /* Search for more alarms to delete (if available) */
  search_and_delete_all_alarms ();

  alarm_cookie = alarmd_event_add (new_event);

  priv->state.alarm_cookie = alarm_cookie;
  save_state (upno);

  return alarm_cookie > 0;
}

static void
cleanup_gconf (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  int i = 0;

  gconf_client_remove_dir (priv->gconf,
			   UPNO_GCONF_DIR,
			   NULL);

  for (i = 0; priv->gconf_notifications[i] != G_MAXUINT32; i++)
    gconf_client_notify_remove (priv->gconf,
				priv->gconf_notifications[i]);
  g_free (priv->gconf_notifications);
  g_object_unref(priv->gconf);
}

static void
cleanup_dbus (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  osso_deinitialize(priv->osso_ctxt);
}

static void
cleanup_inotify (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  if (priv->inotify_channel != NULL)
    {
      int ifd = g_io_channel_unix_get_fd (priv->inotify_channel);

      inotify_rm_watch (ifd, priv->home_watch);
      inotify_rm_watch (ifd, priv->varlibham_watch);
      priv->home_watch = -1;
      priv->varlibham_watch = -1;

      if (priv->inotify_io_watch_id > 0)
	g_source_remove (priv->inotify_io_watch_id);

      g_io_channel_shutdown (priv->inotify_channel, TRUE, NULL);
      g_io_channel_unref (priv->inotify_channel);
      priv->inotify_channel = NULL;
      priv->inotify_ready = FALSE;
    }
}

static void
cleanup_alarm (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  cookie_t alarm_cookie;

  alarm_cookie = priv->state.alarm_cookie;
  alarmd_event_del (alarm_cookie);
}

/* Returns the position of needle in haystack, or -1 if it can't be found.
 */
static gint
str_find_pos (const gchar *haystack, const gchar *needle)
{
  gint i, j;
  gint pos = -1;
  gint needle_len = strlen (needle);
  gint end = strlen (haystack) - needle_len;
  gboolean found = FALSE;

  for (i = 0; !found && (i <= end); i++)
    {
      found = TRUE;
      for (j = 0; j < needle_len; j++)
        {
          found = found && (haystack[i+j] == needle[j]);
          if (!found)
            break;
        }

      if (found)
        pos = i;
    }
  return pos;
}

/* Substitutes every appearance of code in url with value.
 */
static gchar *
str_substitute (const gchar *url, const gchar *code, const gchar *value)
{
  gchar *tmp = NULL;
  GString *string = NULL;
  gint pos = -1;
  gint max_iterations = -1;

  if (url == NULL)
    return NULL;

  max_iterations = strlen (url);
  string = g_string_new (url);

  pos = str_find_pos (string->str, code);

  while (pos >= 0 && max_iterations >= 0)
    {
      g_string_erase (string, pos, strlen (code));
      g_string_insert (string, pos, value);
      pos = str_find_pos (string->str, code);
      max_iterations--;
    }

  tmp = g_strdup (string->str);
  g_string_free (string, TRUE);
  return tmp;
}

/* Substitutes the know variables in url with the appropiate value.
 * Right now, it only recognizes URL_VARIABLE_HARDWARE
 */
static char *
url_eval (const char *url)
{
  char * result;
  
  result = str_substitute (url, URL_VARIABLE_HARDWARE, get_osso_product_hardware());
  
  return result;
}

static const char *
get_osso_product_hardware ()
{
  static char *product_hardware = NULL;

  if (product_hardware)
    return product_hardware;

  /* XXX - There is a library in maemo somewhere to do this, but it is
           not included in the maemo SDK, so we have to do it
           ourselves.  Ridiculous, I know.
  */

  product_hardware = "";
  FILE *f = fopen ("/proc/component_version", "r");
  if (f)
    {
      char *line = NULL;
      size_t len = 0;
      ssize_t n;

      while ((n = getline (&line, &len, f)) != -1)
	{
	  if (n > 0 && line[n-1] == '\n')
	    line[n-1] = '\0';

	  if (sscanf (line, "product %as", &product_hardware) == 1)
	    break;
	}

      free (line);
      fclose (f);
    }

  return product_hardware;
}

/* These keys used to be used to store some state of the
   update-notifier, but that was of course a bad idea.  We rescue the
   alarm cookie and then delete the lot.
 */
#define UPNO_GCONF_OLD_STATE          UPNO_GCONF_DIR "/state"
#define UPNO_GCONF_OLD_ALARM_COOKIE   UPNO_GCONF_DIR "/alarm_cookie"
#define UPNO_GCONF_OLD_LAST_UPDATE    UPNO_GCONF_DIR "/last_update"
#define UPNO_GCONF_OLD_URI            UPNO_GCONF_DIR "/uri"

static void
load_state (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  xexp *x_state = user_file_read_xexp (UFILE_UPDATE_NOTIFIER);

  int old_cookie = gconf_client_get_int (priv->gconf,
					 UPNO_GCONF_OLD_ALARM_COOKIE,
					 NULL);

  gconf_client_unset (priv->gconf,
		      UPNO_GCONF_OLD_ALARM_COOKIE,
		      NULL);

  gconf_client_unset (priv->gconf,
		      UPNO_GCONF_OLD_STATE,
		      NULL);

  gconf_client_unset (priv->gconf,
		      UPNO_GCONF_OLD_LAST_UPDATE,
		      NULL);

  gconf_client_unset (priv->gconf,
		      UPNO_GCONF_OLD_URI,
		      NULL);

  if (x_state)
    {
      priv->state.icon_state = xexp_aref_int (x_state, "icon-state",
					      UPNO_ICON_INVISIBLE);
      priv->state.alarm_cookie = xexp_aref_int (x_state, "alarm-cookie",
						old_cookie);
      xexp_free (x_state);
    }
}

static void
save_state (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  xexp *x_state = xexp_list_new ("state");
  xexp_aset_int (x_state, "icon-state", priv->state.icon_state);
  xexp_aset_int (x_state, "alarm-cookie", priv->state.alarm_cookie);
  user_file_write_xexp (UFILE_UPDATE_NOTIFIER, x_state);
  xexp_free (x_state);
}

static void
save_last_update_time (time_t t)
{
  char *text = g_strdup_printf ("%d", t);
  xexp *x = xexp_text_new ("time", text);
  g_free (text);
  user_file_write_xexp (UFILE_LAST_UPDATE, x);
  xexp_free (x);
}
