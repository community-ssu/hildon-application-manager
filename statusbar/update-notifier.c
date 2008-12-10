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
#include <libintl.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <hildon/hildon.h>
#include <libosso.h>
#include <clockd/libtime.h>
#include <libalarm.h>

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

#define _(x) dgettext ("hildon-application-manager", (x))

#define UPDATE_NOTIFIER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), UPDATE_NOTIFIER_TYPE, UpdateNotifierPrivate))

typedef enum _State State;
enum _State {
  UPNO_ICON_INVISIBLE,
  UPNO_ICON_STATIC,
  UPNO_ICON_BLINKING
};

typedef struct _UpdateNotifierPrivate UpdateNotifierPrivate;
struct _UpdateNotifierPrivate
{
  /* ui */
  GtkWidget *icon;
  GtkWidget *button;

  /* environment */
  osso_context_t *osso;
  GConfClient *gconf;

  /* state */
  State icon_state;
  cookie_t alarm_cookie;
};

/* setup prototypes */
static gboolean setup_dbus (UpdateNotifier *self);
static void setup_gconf (UpdateNotifier *self);

/* state handling prototypes */
static void load_state (UpdateNotifier *self);
static void save_state (UpdateNotifier *self);
static void set_state (UpdateNotifier *self, State state);
static State get_state (UpdateNotifier *self);

/* misc prototypes */
static void my_log (const gchar *function, const gchar *fmt, ...);

HD_DEFINE_PLUGIN_MODULE (UpdateNotifier, update_notifier,
                         HD_TYPE_STATUS_MENU_ITEM);

static void
update_notifier_class_finalize (UpdateNotifierClass *klass)
{
}

static void
update_notifier_finalize (GObject *object)
{
  UpdateNotifier *self;
  UpdateNotifierPrivate *priv;

  self = UPDATE_NOTIFIER (object);
  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  if (priv->osso != NULL)
    osso_deinitialize (priv->osso);

  if (priv->gconf != NULL)
      g_object_unref (priv->gconf);


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
  priv->icon_state = UPNO_ICON_INVISIBLE;
  priv->alarm_cookie = 0;

  if (setup_dbus (self))
    {
      LOG ("dbus setup");

      setup_gconf (self);
      load_state (self);
/*       setup_inotify (self); */

/*       create_widgets (self); */
    }
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
      /* check_for_updates (self); */
    }
  else if (strcmp (method, UPDATE_NOTIFIER_OP_CHECK_STATE) == 0)
    {
      /* Update states of the satusbar item */
      /* update_state (self); */
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

  priv->osso = osso_initialize ("hildon_update_notifier",
                                PACKAGE_VERSION, TRUE, NULL);

  if (!priv->osso)
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

  /* setup_alarm (UPDATE_NOTIFIER (data)); */
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
                           hildon_update_notifier_interval_changed_cb, self,
                           NULL, NULL);
}

static void
load_state (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  xexp *x_state = NULL;
  gint old_cookie = 0;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  old_cookie = gconf_client_get_int (priv->gconf,
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

  x_state = user_file_read_xexp (UFILE_UPDATE_NOTIFIER);

  if (x_state != NULL)
    {
      priv->icon_state = (State) xexp_aref_int (x_state, "icon-state",
                                                UPNO_ICON_INVISIBLE);
      priv->alarm_cookie = (cookie_t) xexp_aref_int (x_state, "alarm-cookie",
                                                     old_cookie);
      xexp_free (x_state);
    }
}

static void
save_state (UpdateNotifier *self)
{
  UpdateNotifierPrivate *priv;
  xexp *x_state = NULL;
  gint old_cookie = 0;

  priv = UPDATE_NOTIFIER_GET_PRIVATE (self);

  x_state = xexp_list_new ("state");

  xexp_aset_int (x_state, "icon-state", (gint) priv->icon_state);
  xexp_aset_int (x_state, "alarm-cookie", priv->alarm_cookie);

  user_file_write_xexp (UFILE_UPDATE_NOTIFIER, x_state);

  xexp_free (x_state);
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
