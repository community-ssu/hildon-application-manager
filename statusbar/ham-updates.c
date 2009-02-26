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

#include "ham-updates.h"

#include <libintl.h>
#include <sys/wait.h>
#include <string.h>

#include <clockd/libtime.h>

#include <gconf/gconf-client.h>

#include <xexp.h>
#include <user_files.h>

/* #define DEBUG */
#include "util.h"
#include "update-notifier-conf.h"

#define _(x) dgettext ("hildon-application-manager", (x))

#define HAM_UPDATES_BUTTON_ICON_NAME     "general_application_manager"
#define HAM_UPDATES_BUTTON_ICON_SIZE     64

#define HAM_UPDATES_OS         _("ai_sb_update_os")
#define HAM_UPDATES_NOKIA      _("ai_sb_update_nokia_%d")
#define HAM_UPDATES_THIRDPARTY _("ai_sb_update_thirdparty_%d")

#define HAM_UPDATES_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HAM_UPDATES_TYPE, HamUpdatesPrivate))

typedef struct _Updates Updates;
struct _Updates {
  GSList *os;
  GSList *certified;
  GSList *other;
  gint total;
};

typedef struct _HamUpdatesPrivate HamUpdatesPrivate;
struct _HamUpdatesPrivate
{
  GtkWidget *button;

  /* apt-worker spawn */
  guint child_id;
};

enum
  {
    CHECK_DONE,
    RESPONSE,
    LAST_SIGNAL
  };

static guint ham_updates_signals[LAST_SIGNAL];

static void ham_updates_build_button (HamUpdates *self);

static Updates *updates_fetch ();
static void updates_free (Updates* updates);

G_DEFINE_TYPE (HamUpdates, ham_updates, G_TYPE_OBJECT)

static void ham_updates_finalize (GObject *object)
{
  HamUpdatesPrivate *priv;

  priv = HAM_UPDATES_GET_PRIVATE (object);

  if (priv->child_id > 0)
    g_source_remove (priv->child_id);

  G_OBJECT_CLASS (ham_updates_parent_class)->finalize (object);
}

static void
ham_updates_class_init (HamUpdatesClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = ham_updates_finalize;

  ham_updates_signals[CHECK_DONE] =
    g_signal_new ("check-done",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (HamUpdatesClass, check_done),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOOLEAN,
		  G_TYPE_NONE,
		  1, G_TYPE_BOOLEAN);

  ham_updates_signals[RESPONSE] =
    g_signal_new ("response",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		   G_STRUCT_OFFSET (HamUpdatesClass, response),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1, G_TYPE_INT);

  g_type_class_add_private (klass, sizeof (HamUpdatesPrivate));
}

static void
ham_updates_init (HamUpdates *self)
{
  HamUpdatesPrivate *priv;

  priv = HAM_UPDATES_GET_PRIVATE (self);

  priv->child_id = 0;
  ham_updates_build_button (self);
}

static void
update_seen_updates_file (void)
{
  xexp *available_updates;

  available_updates = xexp_read_file (AVAILABLE_UPDATES_FILE);

  if (available_updates  != NULL)
    {
      user_file_write_xexp (UFILE_SEEN_UPDATES, available_updates);
      xexp_free (available_updates);
    }
}

static void
ham_updates_dialog_response_cb (GtkDialog *dialog,
				gint response, gpointer data)
{
  if ((response != GTK_RESPONSE_YES && response == GTK_RESPONSE_NO)
      || (response == GTK_RESPONSE_YES && response != GTK_RESPONSE_NO))
    {
      if (response == GTK_RESPONSE_NO)
        update_seen_updates_file ();

      gtk_widget_destroy (GTK_WIDGET (dialog));
      g_signal_emit (data, ham_updates_signals[RESPONSE], 0, response);
    }
}

static gint
ham_updates_dialog_delete_cb (GtkDialog *dialog,
                              GdkEventAny *event, gpointer data)
{
  return TRUE; /* do no destroy */
}

static void
build_category_string (GString *str, const gchar *title, GSList *list)
{
  GSList *l;
  gint c;
  gchar *summary, *pkglist;

  summary = g_strdup_printf (title, g_slist_length (list));

  c = 0;
  pkglist = NULL;
  for (l = list; l != NULL && c < 3; l = l->next)
    {
      gchar *tmp;

      tmp = g_strdup_printf ("%s%s%s",
                             (pkglist != NULL) ? pkglist : "",
                             (c++ > 0) ? ", " : "",
                             (gchar *) l->data);
      if (tmp != NULL)
        {
          g_free (pkglist);
          pkglist = tmp;
        }
    }

  if (summary != NULL && pkglist != NULL)
    g_string_append_printf (str, "%s<big>%s</big>\n<small>%s</small>",
                            (str->len > 0) ? "\n\n" : "",
                            summary, pkglist);

  g_free (summary);
  g_free (pkglist);
}

static gchar*
build_dialog_content ()
{
  Updates *updates;
  gchar* retval;

  updates = updates_fetch ();
  retval = NULL;

  if (updates == NULL)
    return NULL;

  if (updates->total > 0)
    {
      GString *str;

      str = g_string_new (NULL);

      if (g_slist_length (updates->os) > 0)
        build_category_string (str, HAM_UPDATES_OS, updates->os);

      if (g_slist_length (updates->certified) > 0)
        build_category_string (str, HAM_UPDATES_NOKIA, updates->certified);

      if (g_slist_length (updates->other) > 0)
        build_category_string (str, HAM_UPDATES_THIRDPARTY, updates->other);

      retval = g_string_free (str, FALSE);
    }

  updates_free (updates);
  return retval;
}

static void
ham_updates_set_dialog_info (HamUpdates* self, GtkDialog *dlg, gchar *content)
{
  GtkWidget *label;

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), content);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

  gtk_container_add (GTK_CONTAINER (dlg->vbox), label);
}

static void
ham_updates_button_clicked_cb (GtkButton *button, gpointer data)
{
  GtkWidget *dlg;
  HamUpdates *self;
  gchar *content;

  g_return_if_fail (IS_HAM_UPDATES (data));

  self = HAM_UPDATES (data);

  content = build_dialog_content ();

  if (content != NULL)
    {
      dlg = gtk_dialog_new_with_buttons
	(_("ai_sb_update_description"), NULL,
	 GTK_DIALOG_MODAL,
	 _("ai_sb_update_am"), GTK_RESPONSE_YES,
	 _("ai_sb_app_push_no"), GTK_RESPONSE_NO,
	 NULL);

      ham_updates_set_dialog_info (self, GTK_DIALOG (dlg), content);
      g_free (content);

      g_signal_connect (G_OBJECT (dlg), "response",
			G_CALLBACK (ham_updates_dialog_response_cb),
			self);
      g_signal_connect (G_OBJECT (dlg), "delete-event",
                        G_CALLBACK (ham_updates_dialog_delete_cb),
                        NULL);

      gtk_widget_show_all (dlg);
    }
}

static void
ham_updates_button_set_icon (HildonButton *button)
{
  GdkPixbuf *pixbuf;

  pixbuf = icon_load (HAM_UPDATES_BUTTON_ICON_NAME,
		      HAM_UPDATES_BUTTON_ICON_SIZE);

  if (pixbuf != NULL)
    {
      GtkWidget *image;

      image = gtk_image_new_from_pixbuf (pixbuf);

      if (image != NULL)
	hildon_button_set_image (button, image);

      g_object_unref (pixbuf);
    }
}

static void
ham_updates_build_button (HamUpdates *self)
{
  HamUpdatesPrivate *priv;

  priv = HAM_UPDATES_GET_PRIVATE (self);

  priv->button = hildon_button_new_with_text
    (HILDON_SIZE_FULLSCREEN_WIDTH | HILDON_SIZE_FINGER_HEIGHT,
     HILDON_BUTTON_ARRANGEMENT_VERTICAL, _("ai_sb_update_description"), "");

  ham_updates_button_set_icon (HILDON_BUTTON (priv->button));

  g_signal_connect (G_OBJECT (priv->button), "clicked",
		    G_CALLBACK (ham_updates_button_clicked_cb), self);

  gtk_widget_show (GTK_WIDGET (priv->button));
}

static void
ham_updates_check_done_cb (GPid pid, gint status, gpointer data)
{
  HamUpdatesPrivate *priv;
  gboolean ok;

  g_return_if_fail (IS_HAM_UPDATES (data));

  priv = HAM_UPDATES_GET_PRIVATE (data);

  priv->child_id = 0;

  ok = (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 0);

  if (ok == TRUE)
    save_last_update_time (time_get_time ());

  g_signal_emit (data, ham_updates_signals[CHECK_DONE], 0, ok);
}

gboolean
ham_updates_check (HamUpdates *self, gchar *proxy)
{
  gchar *gainroot_cmd;
  GPid pid;
  GError *error;
  gboolean retval;

  retval = FALSE;

  /* Choose the right gainroot command */
  gainroot_cmd = NULL;

  if (running_in_scratchbox ())
    gainroot_cmd = g_strdup ("/usr/bin/fakeroot");
  else
    gainroot_cmd = g_strdup ("/usr/bin/sudo");

  /* Build command to be spawned */
  gchar *argv[] = {
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
      retval = FALSE;
    }
  else
    {
      HamUpdatesPrivate *priv;

      priv = HAM_UPDATES_GET_PRIVATE (self);
      priv->child_id = g_child_watch_add (pid, ham_updates_check_done_cb, self);
      retval = TRUE;
    }

  g_free (gainroot_cmd);

  return retval;
}

time_t
ham_updates_get_interval (HamUpdates *self)
{
  GConfClient *gconf;
  time_t interval;

  gconf = gconf_client_get_default ();

  if (gconf == NULL)
    return (time_t) UPNO_DEFAULT_CHECK_INTERVAL;

  interval = (time_t) gconf_client_get_int (gconf, UPNO_GCONF_CHECK_INTERVAL,
					    NULL);

  if (interval <= 0)
    {
      interval = (time_t) UPNO_DEFAULT_CHECK_INTERVAL;
      gconf_client_set_int (gconf, UPNO_GCONF_CHECK_INTERVAL, (gint) interval,
			    NULL);
    }

  g_object_unref (gconf);

  return interval;
}

gboolean
ham_updates_set_alarm (HamUpdates *self, alarm_event_t *event)
{
  alarm_action_t *action;
  time_t interval;

  g_return_val_if_fail (event != NULL, FALSE);

  /* If the trigger time is missed (due to the device being off or
     system time being adjusted beyond the trigger point) the alarm
     should be run anyway. */
  event->flags |= ALARM_EVENT_RUN_DELAYED;

  /* Run only when internet connection is available. */
  /* conic is needed */
  if (!running_in_scratchbox ())
    event->flags |= ALARM_EVENT_CONNECTED;

  /* If the system time is moved backwards, the alarm should be
     rescheduled. */
  event->flags |= ALARM_EVENT_BACK_RESCHEDULE;

  interval = ham_updates_get_interval (self);
  event->alarm_time = ALARM_RECURRING_SECONDS (time_get_time () + interval);

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

  return TRUE;
}

GtkWidget*
ham_updates_get_button (HamUpdates *self)
{
  HamUpdatesPrivate *priv;

  priv = HAM_UPDATES_GET_PRIVATE (self);
  return priv->button;
}

static gchar*
build_category_title (const gchar *title, GSList *list)
{
  gint count;

  count = g_slist_length (list);
  if (count > 0)
    {
      gchar *summary, *tmp;

      tmp = g_strdup_printf (title, count);
      if (tmp != NULL)
        {
          summary = g_strdup_printf ("%s...", tmp);
          g_free (tmp);
          return summary;
        }
    }

  return NULL;
}

static gchar*
build_button_content (Updates *updates)
{
  gchar *retval;

  retval = NULL;
  if ((retval = build_category_title (HAM_UPDATES_OS, updates->os)) == NULL)
    {
      if ((retval = build_category_title (HAM_UPDATES_NOKIA,
                                          updates->certified)) == NULL)
	{
	  retval = build_category_title (HAM_UPDATES_THIRDPARTY,
                                         updates->other);
	}
    }

  return retval;
}

static gboolean
ham_is_showing_check_for_updates_view (osso_context_t *context)
{
  if (ham_is_running ())
    {
      osso_return_t result;
      osso_rpc_t reply;

      result = osso_rpc_run (context,
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

gboolean
ham_updates_are_available (HamUpdates *self, osso_context_t *context)
{
  HamUpdatesPrivate *priv;
  Updates *updates;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  priv = HAM_UPDATES_GET_PRIVATE (self);

  updates = updates_fetch ();

  if (updates == NULL)
    return FALSE;

  if (updates->total > 0
      && !ham_is_showing_check_for_updates_view (context))
    {
      gchar *value;

      if ((value = build_button_content (updates)) != NULL)
	{
	  hildon_button_set_value (HILDON_BUTTON (priv->button), value);
	  g_free (value);
          updates_free (updates);

	  return TRUE;
	}
    }

  updates_free (updates);

  return FALSE;
}

static Updates *
updates_fetch ()
{
  xexp *available_updates;
  xexp *seen_updates;
  Updates *retval;

  retval = g_new0 (Updates, 1);

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
          retval->total++;

        if (xexp_is (x, "os"))
	  {
	    retval->os = g_slist_append (retval->os, g_strdup (xexp_text (x)));
	  }
        else if (xexp_is (x, "certified"))
	  {
	    retval->certified = g_slist_append (retval->certified,
						g_strdup (xexp_text (x)));
	  }
        else
	  {
	    retval->other = g_slist_append (retval->other,
					    g_strdup (xexp_text (x)));
	  }
      }

      xexp_free (available_updates);

      if (seen_updates != NULL)
        xexp_free (seen_updates);
    }

  if (retval != NULL && retval->total > 0)
    LOG ("new pkgs = %d, os = %d, cert = %d, other = %d", retval->total,
	 g_slist_length (retval->os),
	 g_slist_length (retval->certified),
	 g_slist_length (retval->other));

 exit:
  return retval;
}

static void
updates_list_free (GSList *list)
{
  GSList *l;

  for (l = list; l != NULL; l = l->next)
    g_free (l->data);

  g_slist_free (list);
}

static void
updates_free (Updates *updates)
{
  g_return_if_fail (updates != NULL);

  updates_list_free (updates->os);
  updates_list_free (updates->certified);
  updates_list_free (updates->other);

  g_free (updates);
}
