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

#include <libosso.h>
#include <libhildondesktop/libhildondesktop.h>
#include <libhildonwm/hd-wm.h>

#include <alarm_event.h>

#include "update-notifier.h"
#include "update-notifier-conf.h"

#include "hn-app-pixbuf-anim-blinker.h"
#include "xexp.h"

#define _(x) dgettext ("hildon-application-manager", (x))

#define USE_BLINKIFIER 0

#define HTTP_PROXY_GCONF_DIR      "/system/http_proxy"

#define UPDATE_NOTIFIER_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), UPDATE_NOTIFIER_TYPE, UpdateNotifierPrivate))

typedef struct _UpdateNotifierPrivate UpdateNotifierPrivate;
struct _UpdateNotifierPrivate
{
  GtkWidget *button;
  GtkWidget *blinkifier;
  GtkWidget *menu;
  GtkWidget *open_ham_item;
  GdkPixbuf *static_pic;

  gint button_toggled_handler_id;
  gint menu_selection_done_handler_id;
  gint open_ham_item_activated_handler_id;

  guint blinking_timeout_id;
  guint alarm_init_timeout_id;

  osso_context_t *osso_ctxt;
  GConfClient *gconf;
  guint *gconf_notifications;
  GIOChannel *inotify_channel;
  gboolean inotify_ready;
  int home_watch;
  int varlibham_watch;

  int icon_state;
};

HD_DEFINE_PLUGIN (UpdateNotifier, update_notifier, STATUSBAR_TYPE_ITEM);

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

static void update_icon_visibility (UpdateNotifier *upno, GConfValue *value);
static void update_state (UpdateNotifier *upno);

static void show_check_for_updates_view (UpdateNotifier *upno);
static gboolean showing_check_for_updates_view (UpdateNotifier *upno);
static void check_for_updates (UpdateNotifier *upno);

static void cleanup_gconf (UpdateNotifier *upno);
static void cleanup_inotify (UpdateNotifier *upno);
static void cleanup_dbus (UpdateNotifier *upno);
static void cleanup_alarm (UpdateNotifier *upno);

/* Initialization/destruction functions */

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

      setup_inotify (upno);

      update_icon_visibility (upno, gconf_client_get (priv->gconf,
						      UPNO_GCONF_STATE,
						      NULL));
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

  /* Clean up stuff */
  cleanup_alarm (upno);
  cleanup_inotify (upno);
  cleanup_gconf (upno);
  cleanup_dbus (upno);

  /* Unregister signal handlers */
  g_signal_handler_disconnect (priv->button,
			       priv->button_toggled_handler_id);

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
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  
  GtkRequisition req;
  
  gtk_widget_size_request (GTK_WIDGET (menu->toplevel), &req);

  gdk_window_get_origin (priv->button->window, x, y);
  *x += (priv->button->allocation.x
	 + priv->button->allocation.width
	 - req.width);
  *y += (priv->button->allocation.y
	 + priv->button->allocation.height);

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

  if (priv->icon_state == UPNO_ICON_BLINKING)
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
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);

  priv->gconf = gconf_client_get_default();
  gconf_client_add_dir (priv->gconf,
			UPNO_GCONF_DIR,
			GCONF_CLIENT_PRELOAD_ONELEVEL,
			NULL);

  /* Add gconf notifications and store connection IDs */
  priv->gconf_notifications = g_new0 (guint, 3);
  priv->gconf_notifications[0] =
    gconf_client_notify_add (priv->gconf,
			     UPNO_GCONF_STATE,
			     gconf_state_changed, upno,
			     NULL, NULL);
  priv->gconf_notifications[1] =
    gconf_client_notify_add (priv->gconf,
			     UPNO_GCONF_CHECK_INTERVAL,
			     gconf_interval_changed, upno,
			     NULL, NULL);

  /* Finish the list of connection IDs */
  priv->gconf_notifications[2] = -1;
}

static void
set_condition_carefully (UpdateNotifier *upno, gboolean condition)
{
  /* Setting the 'condition' of the plugin will cause the overflow row
     of the status bar to be closed, regardless of whether the
     condition has actually changed or not.  Thus, we are careful here
     not to call g_object_set when the condition has not changed.
  */

  gboolean old_condition;
  g_object_get (upno, "condition", &old_condition, NULL);
  if (old_condition != condition)
    g_object_set (upno, "condition", condition, NULL);
}

static void
update_icon_visibility (UpdateNotifier *upno, GConfValue *value)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  int state = UPNO_ICON_INVISIBLE;

  if (value && value->type == GCONF_VALUE_INT)
    state = gconf_value_get_int (value);

  priv->icon_state = state;

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
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
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

  /* Show the icon (blinking) if there are new updates and if the
     'check for udpates' view is not being shown in HAM */
  if ((n_new > 0) && !showing_check_for_updates_view (upno))
    {
      if (priv->icon_state == UPNO_ICON_INVISIBLE)
	set_icon_visibility (upno, UPNO_ICON_BLINKING);
    }
  else
    set_icon_visibility (upno, UPNO_ICON_INVISIBLE);

  if (priv->menu)
    {
      g_signal_handler_disconnect(priv->menu,
				  priv->menu_selection_done_handler_id);
      g_signal_handler_disconnect (priv->open_ham_item,
				   priv->open_ham_item_activated_handler_id);
      gtk_widget_destroy (priv->menu);
    }

  priv->menu = gtk_menu_new ();

  if (n_certified + n_other + n_os > 0)
    {
      add_readonly_item (priv->menu, _("ai_sb_update_description"));

      if (n_certified > 0)
	add_readonly_item (priv->menu, _("ai_sb_update_nokia_%d"),
			   n_certified);
      if (n_other > 0)
	add_readonly_item (priv->menu, _("ai_sb_update_thirdparty_%d"),
			   n_other);
      if (n_os > 0)
	add_readonly_item (priv->menu, _("ai_sb_update_os"));

      item = gtk_separator_menu_item_new ();
      gtk_menu_append (priv->menu, item);
      gtk_widget_show (item);
    }

  item = gtk_menu_item_new_with_label (_("ai_sb_update_am"));
  gtk_menu_append (priv->menu, item);
  gtk_widget_show (item);

  priv->menu_selection_done_handler_id =
    g_signal_connect (priv->menu, "selection-done",
			   G_CALLBACK(menu_hidden), upno);

  priv->open_ham_item = item;
  priv->open_ham_item_activated_handler_id =
    g_signal_connect (item, "activate",
		      G_CALLBACK (open_ham_menu_item_activated), upno);
}

static void
set_icon_visibility (UpdateNotifier *upno, int state)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  gconf_client_set_int (priv->gconf, 
			UPNO_GCONF_STATE,
			state,
			NULL);
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

  result = osso_rpc_set_cb_f_with_free (priv->osso_ctxt,
					UPDATE_NOTIFIER_SERVICE,
					UPDATE_NOTIFIER_OBJECT_PATH,
					UPDATE_NOTIFIER_INTERFACE,
					osso_rpc_handler,
					upno,
					osso_rpc_free_val);

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

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 0)
    {
      gconf_client_set_int (priv->gconf,
			    UPNO_GCONF_LAST_UPDATE, time (NULL),
			    NULL);
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

	  if ((priv->home_watch != -1 &&
	       is_file_modified_event (event,
				       priv->home_watch,
				       SEEN_UPDATES_FILE))
	      || (priv->varlibham_watch != -1 &&
		  is_file_modified_event (event,
					  priv->varlibham_watch,
					  "available-updates")))
	    update_state (upno);
	    
	  i += sizeof (struct inotify_event) + event->len;
	}

      return TRUE;
    }
}

static void
setup_inotify (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
  int ifd;

  ifd = inotify_init ();
  if (ifd >= 0)
    {
      priv->inotify_channel = g_io_channel_unix_new (ifd);
      g_io_add_watch (priv->inotify_channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
		      handle_inotify, upno);

      
      priv->home_watch =
	inotify_add_watch (ifd,
			   getenv ("HOME"),
			   IN_CLOSE_WRITE | IN_MOVED_TO);

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
  time_t last = (time_t)G_MAXINT32;
  cookie_t *cookies = NULL;
 
  cookies = alarm_event_query (first, last, 0, 0);

  if (cookies == NULL)
    return;

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
  g_free (cookies);
}

static gboolean
setup_alarm (UpdateNotifier *upno)
{
  UpdateNotifierPrivate *priv = UPDATE_NOTIFIER_GET_PRIVATE (upno);
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

  interval = gconf_client_get_int (priv->gconf,
				   UPNO_GCONF_CHECK_INTERVAL,
				   NULL);
  if (interval <= 0)
    interval = UPNO_DEFAULT_CHECK_INTERVAL;

  alarm_cookie = gconf_client_get_int (priv->gconf,
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
    alarm_event_del (alarm_cookie);

  /* Search for more alarms to delete (if available) */
  search_and_delete_all_alarms ();

  alarm_cookie = alarm_event_add (&new_alarm);

  gconf_client_set_int (priv->gconf,
			UPNO_GCONF_ALARM_COOKIE,
			alarm_cookie,
			NULL);

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

  alarm_cookie = gconf_client_get_int (priv->gconf,
				       UPNO_GCONF_ALARM_COOKIE,
				       NULL);
  alarm_event_del (alarm_cookie);
}
