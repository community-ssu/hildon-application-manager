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
#include <libhildonwm/hd-wm.h>
#include <gconf/gconf-client.h>

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

gboolean
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

/* FIXME: this mechanism to obtain the http proxy seems to be
 * deprecated in Fremantle */
gchar *
get_gconf_http_proxy ()
{
  char *proxy;

  GConfClient *conf = gconf_client_get_default ();

  /* We clear the cache here in order to force a fresh fetch of the
     values.  Otherwise, there is a race condition with the
     iap_callback: the OSSO_IAP_CONNECTED message might come before
     the GConf cache has picked up the new proxy settings.

     At least, that's the theory.
  */
  gconf_client_clear_cache (conf);

  if (gconf_client_get_bool (conf, "/system/http_proxy/use_http_proxy",
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

  g_object_unref (conf);

  return proxy;
}
