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

#include "ham-notifier-status-menu-item.h"

#include <unistd.h>
#include <errno.h>
#include <sys/inotify.h>

#include <user_files.h>

#define DEBUG

#include "util.h"
#include "ham-notifier.h"
#include "update-notifier-conf.h"

#define HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), HAM_NOTIFIER_STATUS_MENU_ITEM_TYPE, HamNotifierStatusMenuItemPrivate))

typedef struct _HamNotifierStatusMenuItemPrivate HamNotifierStatusMenuItemPrivate;
struct _HamNotifierStatusMenuItemPrivate
{
  /* inotify */
  gint inotify_fd;
  guint io_watch;
  gint wd;

  HamNotifier *notifier;
};

static void setup_ui (HamNotifierStatusMenuItem *self);
static gboolean setup_inotify (HamNotifierStatusMenuItem *self);
static void close_inotify (HamNotifierStatusMenuItem *self);

static void update_button_visibility (HamNotifierStatusMenuItem *self);

/* For opening an URL */
#define URL_SERVICE                     "com.nokia.osso_browser"
#define URL_REQUEST_PATH                "/com/nokia/osso_browser/request"
#define URL_REQUEST_IF                  "com.nokia.osso_browser"
#define URL_OPEN_MESSAGE                "open_new_window"

HD_DEFINE_PLUGIN_MODULE (HamNotifierStatusMenuItem,
			 ham_notifier_status_menu_item,
                         HD_TYPE_STATUS_MENU_ITEM);

static void
ham_notifier_status_menu_item_class_finalize
(HamNotifierStatusMenuItemClass *klass)
{
  /* noop */
}

static void
ham_notifier_status_menu_item_finalize (GObject *object)
{
  HamNotifierStatusMenuItem *self;
  HamNotifierStatusMenuItemPrivate *priv;

  self = HAM_NOTIFIER_STATUS_MENU_ITEM (object);
  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (self);

  close_inotify (self);

  if (priv->notifier != NULL)
    g_object_unref (priv->notifier);

  G_OBJECT_CLASS (ham_notifier_status_menu_item_parent_class)->finalize (object);
}

static void
ham_notifier_status_menu_item_class_init (HamNotifierStatusMenuItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = ham_notifier_status_menu_item_finalize;

  g_type_class_add_private (object_class,
                            sizeof (HamNotifierStatusMenuItemPrivate));
}

static void
ham_notifier_status_menu_item_init (HamNotifierStatusMenuItem *self)
{
  HamNotifierStatusMenuItemPrivate *priv;

  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (self);

  priv->inotify_fd = -1;
  priv->io_watch = 0;
  priv->wd = -1;

  priv->notifier = NULL;

  if (setup_inotify (self))
    {
      setup_ui (self);
    }
  else
    {
      close_inotify (self);
    }
}

#define BUF_LEN 4096

static gboolean
ham_notifier_status_menu_item_inotify_cb (GIOChannel *source,
					  GIOCondition condition,
					  gpointer data)
{
  HamNotifierStatusMenuItemPrivate *priv;
  gchar buf[BUF_LEN];
  gint i;
  gint len;

  /* Return if the object was already destroyed
     or the if inotify is not still ready */
  g_return_val_if_fail (IS_HAM_NOTIFIER_STATUS_MENU_ITEM (data), FALSE);

  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (data);
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

      if (is_file_modified (event, priv->wd, UFILE_AVAILABLE_NOTIFICATIONS))
        {
          ham_notifier_empty_seen_notifications ();
        }
      else if (is_file_modified (event, priv->wd, UFILE_SEEN_NOTIFICATIONS))
        {
          update_button_visibility (HAM_NOTIFIER_STATUS_MENU_ITEM (data));
        }

      i += sizeof (struct inotify_event) + event->len;
    }

  return TRUE;

error_cancel:
  priv->io_watch = 0;
  close_inotify (HAM_NOTIFIER_STATUS_MENU_ITEM (data));
  return FALSE;
}

static gint
add_watch_for_path (HamNotifierStatusMenuItem *self, const gchar *path)
{
  HamNotifierStatusMenuItemPrivate *priv;
  gint watch;

  g_return_val_if_fail (path != NULL, -1);

  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (self);

  watch = inotify_add_watch (priv->inotify_fd, path,
                             IN_CLOSE_WRITE | IN_MOVED_TO);

  if (watch < 0)
    {
      g_warning ("Failed to create watch for local file %s : %s\n",
                 path, g_strerror (errno));
      return -1;
    }

  return watch;
}

static gboolean
setup_inotify (HamNotifierStatusMenuItem *self)
{
  HamNotifierStatusMenuItemPrivate *priv;
  gint fd;
  GIOChannel *io_channel;

  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (self);

  g_return_val_if_fail (priv->inotify_fd == -1, FALSE);

  if ((fd = inotify_init ()) < 0)
    {
      g_warning ("Failed to initialize inotify: %s", g_strerror (errno));
      return FALSE;
    }

  priv->inotify_fd = fd;

  io_channel = g_io_channel_unix_new (fd);
  priv->io_watch = g_io_add_watch (io_channel, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                   ham_notifier_status_menu_item_inotify_cb,
                                   self);
  g_io_channel_unref (io_channel);

  {
    gchar *path;

    path = user_file_get_state_dir_path ();
    priv->wd = add_watch_for_path (self, path);
    g_free (path);

    if (priv->wd == -1)
      return FALSE;
  }

  return TRUE;
}

static void
close_inotify (HamNotifierStatusMenuItem *self)
{
  HamNotifierStatusMenuItemPrivate *priv;

  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (self);

  if (priv->io_watch > 0)
    {
      g_source_remove (priv->io_watch);
      priv->io_watch = 0;
    }

  if (priv->inotify_fd > 0)
    {
      if (priv->wd != -1)
	{
	  inotify_rm_watch (priv->inotify_fd, priv->wd);
	  priv->wd = -1;
	}
      close (priv->inotify_fd);
      priv->inotify_fd = -1;
    }
}

/*
  dbus-send --print-reply --dest=com.nokia.osso_browser
  /com/nokia/osso_browser/request
  com.nokia.osso_browser.open_new_window
  string:'http://example.com'
*/
static gboolean
open_url (HamNotifierStatusMenuItem *self, gchar *url)
{
  DBusConnection *conn;
  DBusMessage *msg;
  gboolean result;

  if (url == NULL)
    return FALSE;

  conn = hd_status_plugin_item_get_dbus_connection
    (HD_STATUS_PLUGIN_ITEM (self), DBUS_BUS_SESSION, NULL);

  if (conn == NULL)
    return FALSE;

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
  dbus_message_unref (msg);
  dbus_connection_unref (conn);

  return result;

}

static void
ham_notifier_status_menu_item_response_cb (HamNotifierStatusMenuItem *self,
					   gint response, gpointer data)
{
  gtk_widget_hide (GTK_WIDGET (self));

  if (response == GTK_RESPONSE_YES)
    {
      gchar *url;

      url = ham_notifier_get_url (HAM_NOTIFIER (data));
      open_url (self, url);
      g_free (url);
    }
}

static void
build_status_menu_button (HamNotifierStatusMenuItem *self)
{
  HamNotifierStatusMenuItemPrivate *priv;

  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (self);

  priv->notifier = g_object_new (HAM_NOTIFIER_TYPE, NULL);

  g_signal_connect_swapped
    (priv->notifier, "response",
     G_CALLBACK (ham_notifier_status_menu_item_response_cb), self);

  gtk_container_add (GTK_CONTAINER (self),
		     ham_notifier_get_button (priv->notifier));
}

static void
setup_ui (HamNotifierStatusMenuItem *self)
{
  build_status_menu_button (self);
  update_button_visibility (self);
}

static void
update_button_visibility (HamNotifierStatusMenuItem *self)
{
  HamNotifierStatusMenuItemPrivate *priv;
  gboolean visible;

  priv = HAM_NOTIFIER_STATUS_MENU_ITEM_GET_PRIVATE (self);

  g_object_get (G_OBJECT (self), "visible", &visible, NULL);

  if (ham_notifier_are_available (priv->notifier) == TRUE)
    {
      if (visible == FALSE)
	gtk_widget_show (GTK_WIDGET (self));
    }
  else
    {
      if (visible == TRUE)
	gtk_widget_hide (GTK_WIDGET (self));
    }
}
