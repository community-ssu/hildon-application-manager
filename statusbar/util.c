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

#include "util.h"

#include <xexp.h>
#include <user_files.h>

#include <string.h>
#include <unistd.h>

#include <dbus/dbus.h>
#include <gconf/gconf-client.h>

#include "update-notifier-conf.h"


gboolean
running_in_scratchbox ()
{
  return access ("/targets/links/scratchbox.config", F_OK) == 0;
}

void
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

time_t
load_last_update_time ()
{
  int t = 0;
  xexp *x = user_file_read_xexp (UFILE_LAST_UPDATE);
  if (x && xexp_is_text (x) && xexp_is (x, "time"))
    t = xexp_text_as_int (x);
  xexp_free (x);
  return (time_t) t;
}

void
my_log (const gchar* file, const gchar *function, const gchar *fmt, ...)
{
  va_list args;
  gchar *tmp;

  va_start (args, fmt);
  tmp = g_strdup_vprintf (fmt, args);
  g_printerr ("%s (%s): %s\n", file, function, tmp);
  g_free (tmp);
  va_end (args);
}

gboolean
ham_is_running ()
{
  DBusConnection *connection;
  DBusError error;
  gboolean exists;

  LOG ("");

  connection = NULL;
  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
  if (dbus_error_is_set (&error))
    {
      dbus_error_free (&error);
      exists = FALSE;
      goto exit;
    }

  dbus_error_init (&error);
  exists = dbus_bus_name_has_owner (connection, HILDON_APP_MGR_SERVICE, &error);
  if (dbus_error_is_set (&error))
    dbus_error_free (&error);

 exit:
  if (connection != NULL)
    dbus_connection_unref (connection);
  return exists;
}

/* FIXME: this mechanism to obtain the http proxy seems to be
 * deprecated in Fremantle */
gchar *
get_gconf_http_proxy ()
{
  char *proxy;
  char *proxy_mode = NULL;

  GConfClient *conf = gconf_client_get_default ();

  /* We clear the cache here in order to force a fresh fetch of the
     values.  Otherwise, there is a race condition with the
     iap_callback: the OSSO_IAP_CONNECTED message might come before
     the GConf cache has picked up the new proxy settings.

     At least, that's the theory.
  */
  gconf_client_clear_cache (conf);

  proxy_mode = gconf_client_get_string (conf, "/system/proxy/mode", NULL);
  if (strcmp (proxy_mode, "none")
      && gconf_client_get_bool (conf, "/system/http_proxy/use_http_proxy",
                                NULL))
    {
      char *user = NULL;
      char *password = NULL;
      char *host = NULL;
      gint port;

      if (gconf_client_get_bool (conf, "/system/http_proxy/use_authentication",
				 NULL))
	{
	  user = gconf_client_get_string
	    (conf, "/system/http_proxy/authentication_user", NULL);
	  password = gconf_client_get_string
	    (conf, "/system/http_proxy/authentication_password", NULL);
	}

      host = gconf_client_get_string (conf, "/system/http_proxy/host", NULL);
      port = gconf_client_get_int (conf, "/system/http_proxy/port", NULL);

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
    }
  else
    proxy = NULL;

  /* XXX - there is also ignore_hosts, which we ignore for now, since
           transcribing it to no_proxy is hard... mandatory,
           non-transparent proxies are evil anyway.
   */

  g_free (proxy_mode);
  g_object_unref (conf);

  return proxy;
}

gboolean
is_file_modified (struct inotify_event *event, int watch, const char *filename)
{
  return (event->wd == watch
          && (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
          && event->len > 0
          && strcmp (event->name, filename) == 0);
}

GdkPixbuf *
icon_load (const gchar *name, gint size)
{
  GtkIconTheme *icon_theme;
  GdkPixbuf *pixbuf;
  GError *error;

  if (name == NULL)
    return NULL;

  pixbuf = NULL;
  error = NULL;

  icon_theme = gtk_icon_theme_get_default ();

  if (size < 1)
  {
    gint idx;
    /* size was smaller than one => use the largest natural size available */
    gint *icon_sizes = gtk_icon_theme_get_icon_sizes (icon_theme, name);
    for (idx = 0; icon_sizes[idx] != 0 && icon_sizes[idx] != -1; idx++)
      size = icon_sizes[idx];
    g_free (icon_sizes);
  }

  pixbuf =  gtk_icon_theme_load_icon (icon_theme, name, size,
                                      GTK_ICON_LOOKUP_NO_SVG, &error);

  if (error != NULL)
  {
    fprintf (stderr, "error loading pixbuf '%s': %s", name, error->message);
    g_error_free (error);
  }

  return pixbuf;
}
