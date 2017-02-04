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

#include "ham-notifier.h"

#include <libintl.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <curl/curl.h>

#include <clockd/libtime.h>

#include <gconf/gconf-client.h>

#include <xexp.h>
#include <user_files.h>

#define DEBUG
#include "util.h"
#include "update-notifier-conf.h"

#define _(x) dgettext ("hildon-application-manager", (x))

#define HAM_NOTIFIER_BUTTON_ICON_NAME     "app_install_new_updates"
#define HAM_NOTIFIER_BUTTON_ICON_SIZE     48

#define DEFAULT_PROVIDER                  "Nokia"

#define HAM_NOTIFIER_GET_PRIVATE(obj) ((HamNotifier*)obj)->priv

struct _HamNotifierPrivate
{
  GtkWidget *button;
  gpointer data;

  gchar *url;
};

static void ham_notifier_build_button (HamNotifier *self);
static void empty_ufile_notifications (const gchar *ufile);

static void ham_notifier_finalize (gpointer object)
{
  HamNotifierPrivate *priv;

  priv = HAM_NOTIFIER_GET_PRIVATE (object);

  g_free (priv->url);
}

static void
ham_notifier_init (HamNotifier *self)
{
  HamNotifierPrivate *priv;

  priv = HAM_NOTIFIER_GET_PRIVATE (self);

  priv->url = NULL;
  ham_notifier_build_button (self);
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

/* Copy AVAILABLE_NOTIFICATIONS_FILE into SEEN_NOTIFICATIONS_FILE */
static void
update_notifications (const gchar* ufile)
{
  xexp *avail_nots;

  avail_nots = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
  if (avail_nots != NULL)
    {
      user_file_write_xexp (ufile, avail_nots);
      xexp_free (avail_nots);
    }
}

static void
update_seen_notifications ()
{
  update_notifications (UFILE_SEEN_NOTIFICATIONS);
}

static void
ham_notifier_dialog_response_cb (GtkDialog *dialog,
                                 gint response, gpointer data)
{
  HamNotifier *self;

  self = HAM_NOTIFIER (data);

  if ((response != GTK_RESPONSE_YES && response == GTK_RESPONSE_NO)
      || (response == GTK_RESPONSE_YES && response != GTK_RESPONSE_NO))
    {
      update_seen_notifications ();
      empty_ufile_notifications (UFILE_TAPPED_NOTIFICATIONS);
      gtk_widget_destroy (GTK_WIDGET (dialog));
      self->response (self->priv->data, response, data);
    }
}

static gint
ham_notifier_dialog_delete_cb (GtkDialog *dialog,
                               GdkEventAny *event, gpointer data)
{
  return TRUE; /* do no destroy */
}

static void
ham_notifier_set_dialog_info (HamNotifier* self, GtkDialog *dlg, gchar *content)
{
  GtkWidget *label;

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), content);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

  gtk_container_add (GTK_CONTAINER (dlg->vbox), label);
}

static gchar *
build_dialog_content ()
{
  xexp *notifications;
  gchar *content;

  content = NULL;

  notifications = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
  if (notifications == NULL)
    goto exit;

  if (xexp_is_tag_and_not_empty (notifications, "info"))
    {
      const gchar *title;
      const gchar *desc;
      const gchar *uri;
      const gchar *provider;

      title = xexp_aref_text(notifications, "title");
      desc = xexp_aref_text(notifications, "text");
      uri = xexp_aref_text(notifications, "uri");
      provider = xexp_aref_text(notifications, "provider");
      provider = provider ? provider : DEFAULT_PROVIDER;

      if (title != NULL && desc != NULL && uri != NULL && provider != NULL)
        {
          gchar *by;

          by = g_strdup_printf (_("apma_fi_by_provider"), provider);

          content = g_strdup_printf ("<big>%s\n%s</big>\n\n"
                                     "<small>%s</small>\n\n"
                                     "<b>%s</b>",
                                     title, by, desc, uri);

          g_free (by);
        }
    }

 exit:
  return content;
}

static void
ham_notifier_button_clicked_cb (GtkButton *button, gpointer data)
{
  GtkWidget *dlg;
  HamNotifier *self;
  gchar *content;

  self = HAM_NOTIFIER (data);

  content = build_dialog_content ();

  if (content != NULL)
    {
      dlg = gtk_dialog_new_with_buttons
	(_("ai_sb_app_push_desc"), NULL,
	 GTK_DIALOG_MODAL,
	 _("ai_sb_app_push_no"), GTK_RESPONSE_NO,
	 _("ai_sb_app_push_link"), GTK_RESPONSE_YES,
	 NULL);

      ham_notifier_set_dialog_info (self, GTK_DIALOG (dlg), content);
      g_free (content);

      g_signal_connect (G_OBJECT (dlg), "response",
			G_CALLBACK (ham_notifier_dialog_response_cb),
			self);

      g_signal_connect (G_OBJECT (dlg), "delete-event",
                        G_CALLBACK (ham_notifier_dialog_delete_cb),
                        NULL);

      gtk_widget_show_all (dlg);
    }
}

static void
ham_notifier_button_set_icon (HildonButton *button)
{
  GdkPixbuf *pixbuf;

  pixbuf = icon_load (HAM_NOTIFIER_BUTTON_ICON_NAME,
		      HAM_NOTIFIER_BUTTON_ICON_SIZE);

  if (pixbuf != NULL)
    {
      GtkWidget *image;

      image = gtk_image_new_from_pixbuf (pixbuf);

      if (image != NULL)
        {
          hildon_button_set_image (button, image);
          hildon_button_set_image_position (HILDON_BUTTON (button),
                                            GTK_POS_LEFT);
        }

      g_object_unref (pixbuf);
    }
}

static void
ham_notifier_build_button (HamNotifier *self)
{
  HamNotifierPrivate *priv;

  priv = HAM_NOTIFIER_GET_PRIVATE (self);

  priv->button = hildon_button_new_with_text
    (HILDON_SIZE_FINGER_HEIGHT | HILDON_SIZE_AUTO_WIDTH,
     HILDON_BUTTON_ARRANGEMENT_VERTICAL,
     _("apma_menu_plugin_title_software_releases"), "");
  hildon_button_set_alignment (HILDON_BUTTON (priv->button), 0, 0, 1, 1);

  ham_notifier_button_set_icon (HILDON_BUTTON (priv->button));

  g_signal_connect (G_OBJECT (priv->button), "clicked",
		    G_CALLBACK (ham_notifier_button_clicked_cb), self);

  gtk_widget_show (GTK_WIDGET (priv->button));
}

static const gchar *
get_osso_product_hardware ()
{
  static char *product_hardware = NULL;
  FILE *f;

  if (product_hardware)
    return product_hardware;

  /* XXX - There is a library in maemo somewhere to do this, but it is
           not included in the maemo SDK, so we have to do it
           ourselves.  Ridiculous, I know.
  */

  product_hardware = "";
  f = fopen ("/proc/component_version", "r");
  if (f)
    {
      gchar *line = NULL;
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

  if (product_hardware[0])
    return product_hardware;

  f = fopen ("/proc/cpuinfo", "r");
  if (f)
    {
      gchar *line = NULL;
      char *str = NULL;
      size_t len = 0;
      ssize_t n;

      while ((n = getline (&line, &len, f)) != -1)
        {
          if (n > 0 && line[n-1] == '\n')
            line[n-1] = '\0';

          if (sscanf (line, "Hardware : %a[^\n]", &str) == 1)
            break;
        }

      if (str)
        {
          if (strcmp(str, "Nokia 770") == 0)
            product_hardware = "SU-18";
          else if (strcmp(str, "Nokia N800") == 0)
            product_hardware = "RX-34";
          else if (strcmp(str, "Nokia N810") == 0)
            product_hardware = "RX-44";
          else if (strcmp(str, "Nokia N810 WiMAX") == 0)
            product_hardware = "RX-48";
          else if (strcmp(str, "Nokia RX-51 board") == 0)
            product_hardware = "RX-51";
          else
            product_hardware = "";
          free(str);
        }
    }

  return product_hardware;
}

static gchar *
str_substitute (const gchar *text, const gchar *key, const gchar *value)
{
  gchar *begin, *cursor;
  GString *converted;

  if (text == NULL)
    return NULL;

  converted = g_string_new (text);
  begin = cursor = converted->str;

  while ((cursor = g_strstr_len (cursor, -1, key)))
    {
      g_string_erase (converted, (cursor - begin), strlen (key));
      g_string_insert (converted, (cursor - begin), value);
      cursor++;
    }

  return g_string_free (converted, FALSE);
}

static gchar *
uri_eval (const gchar *url)
{
  return str_substitute (url, URL_VARIABLE_HARDWARE,
                         get_osso_product_hardware ());
}

static gchar *
get_uri ()
{
  gchar *uri;
  xexp *conf;
  xexp *conf2;

  uri = NULL;
  conf = xexp_read_file (SYSTEM_SETTINGS_DEFAULTS_FILE);
  conf2 = xexp_read_file (SYSTEM_SETTINGS_FILE);

  if (conf != NULL && conf2 != NULL)
    {
      xexp_append (conf2, conf);
      conf = conf2;
    }
  else if (conf2 != NULL)
    {
      conf = conf2;
    }

  if (conf != NULL)
    {
      uri = uri_eval (xexp_aref_text (conf, "notifier-uri"));
      xexp_free (conf);
    }

  return uri;
}

static gboolean
download_notifications (gchar *proxy)
{
  gchar *uri;
  FILE *tmpfile;
  CURL *handle;
  gboolean ok;

  handle = NULL;
  ok = FALSE;

  uri = get_uri ();
  LOG ("notification uri = %s", uri);

  tmpfile = user_file_open_for_write (UFILE_AVAILABLE_NOTIFICATIONS_TMP);
  LOG ("tmpfile %s", tmpfile != NULL ? "ok" : "failed!!!");

  if (uri != NULL && tmpfile != NULL)
  {
    CURLcode ret;
    xexp *data;
    glong response;

    handle = curl_easy_init ();
    if (handle == NULL)
      goto exit;

    ret = curl_easy_setopt (handle, CURLOPT_WRITEDATA, tmpfile);
    ret |= curl_easy_setopt (handle, CURLOPT_URL, uri);

    if (proxy != NULL)
      ret |= curl_easy_setopt (handle, CURLOPT_PROXY, proxy);

    if (ret != CURLE_OK)
      goto exit;

    LOG ("performing the download");
    ret = curl_easy_perform (handle);
    ret |= curl_easy_getinfo (handle, CURLINFO_RESPONSE_CODE, &response);

    LOG ("ret = %d, response = %ld", ret, response);
    if (ret != CURLE_OK || response != 200)
      goto exit;

    fflush (tmpfile);
    fsync (fileno (tmpfile));
    fclose (tmpfile);
    tmpfile = NULL;

    data = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS_TMP);

    if (data != NULL
        && xexp_is_list (data)
        && xexp_is (data, "info"))
      {
        /* Copy data to the final file if validated */
        user_file_write_xexp (UFILE_AVAILABLE_NOTIFICATIONS, data);
        xexp_free (data);
        ok = TRUE;
      }
  }

 exit:
  if (handle != NULL)
    curl_easy_cleanup (handle);

  if (tmpfile != NULL)
    {
      fflush (tmpfile);
      fsync (fileno (tmpfile));
      fclose (tmpfile);
    }

  user_file_remove (UFILE_AVAILABLE_NOTIFICATIONS_TMP);

  g_free (uri);

  return ok;
}

static void
empty_ufile_notifications (const gchar *ufile)
{
  /* as we have new notifications, we no longer need the old seen ones;
   * the writing of UFILE_*_NOTIFICATIONS will trigger an inotify */
  xexp *empty_notifications;

  empty_notifications = xexp_list_new ("info");
  user_file_write_xexp (ufile, empty_notifications);
  xexp_free (empty_notifications);
}

void
ham_notifier_empty_seen_notifications ()
{
  xexp *avail_notifications;
  xexp *seen_notifications;
  xexp *tapped_notifications;

  seen_notifications = tapped_notifications = NULL;
  avail_notifications = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
  if (avail_notifications == NULL)
    goto exit;

  seen_notifications = user_file_read_xexp (UFILE_SEEN_NOTIFICATIONS);

  /* let's create an empty seen-notifications file either
     there's not seen-notification file right now,
     or if the seen-notifications and available-notifications are different */
  if (seen_notifications == NULL
      || (xexp_is_tag_and_not_empty (avail_notifications, "info")
          && (!xexp_is_tag_and_not_empty (seen_notifications, "info")
              || (xexp_is_tag_and_not_empty (seen_notifications, "info")
                  && !compare_xexp_text (avail_notifications,
                                         seen_notifications,
                                         "title")
                  && !compare_xexp_text (avail_notifications,
                                         seen_notifications,
                                         "text")
                  && !compare_xexp_text (avail_notifications,
                                         seen_notifications,
                                         "uri")))))
    {
      empty_ufile_notifications (UFILE_SEEN_NOTIFICATIONS);
    }

  tapped_notifications = user_file_read_xexp (UFILE_TAPPED_NOTIFICATIONS);

  /* let's create an empty tapped-notifications file either
     there's not tapped-notification file right now,
     or if the tapped-notifications and available-notifications are
     different */
  if (tapped_notifications == NULL
      || (xexp_is_tag_and_not_empty (avail_notifications, "info")
          && (!xexp_is_tag_and_not_empty (tapped_notifications, "info")
              || (xexp_is_tag_and_not_empty (tapped_notifications, "info")
                  && !compare_xexp_text (avail_notifications,
                                         tapped_notifications,
                                         "title")
                  && !compare_xexp_text (avail_notifications,
                                         tapped_notifications,
                                         "text")
                  && !compare_xexp_text (avail_notifications,
                                         tapped_notifications,
                                         "uri")))))
    {
      empty_ufile_notifications (UFILE_TAPPED_NOTIFICATIONS);
    }

exit:
  if (avail_notifications != NULL)
    xexp_free (avail_notifications);

  if (seen_notifications != NULL)
    xexp_free (seen_notifications);

  if (tapped_notifications != NULL)
    xexp_free (tapped_notifications);
}

static gpointer
ham_notifier_check_thread (gpointer data)
{
  static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
  gchar *proxy;

  proxy = (gchar *) data;

  if (g_static_mutex_trylock (&mutex))
    {
      download_notifications (proxy);
      g_static_mutex_unlock (&mutex);
    }

  return NULL;
}

gboolean
ham_notifier_check (gchar *proxy)
{
  GError *error;

  error = NULL;

  g_thread_create (ham_notifier_check_thread, proxy, FALSE, &error);

  if (error != NULL)
    {
      fprintf (stderr, "can't create thread: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  LOG ("Notification download thread init");
  return TRUE;
}

GtkWidget*
ham_notifier_get_button (HamNotifier *self)
{
  HamNotifierPrivate *priv;

  priv = HAM_NOTIFIER_GET_PRIVATE (self);
  return priv->button;
}

gchar*
ham_notifier_get_url (HamNotifier *self)
{
  HamNotifierPrivate *priv;

  priv = HAM_NOTIFIER_GET_PRIVATE (self);
  return g_strdup (priv->url);
}

static NotificationsStatus
notifications_status (HamNotifier *self)
{
  xexp *seen_nots;
  xexp *avail_nots;
  xexp *tapped_nots;
  NotificationsStatus status;

  seen_nots = tapped_nots = NULL;
  status = NOTIFICATIONS_NONE;

  avail_nots = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
  if (avail_nots == NULL)
    goto exit;

  if (xexp_is_tag_and_not_empty (avail_nots, "info"))
    {
      gboolean isnew = xexp_aref_text (avail_nots, "title") != NULL
        && xexp_aref_text (avail_nots, "text") != NULL
        && xexp_aref_text (avail_nots, "uri") != NULL;

      if (isnew == FALSE)
        goto exit;
    }

  tapped_nots = user_file_read_xexp (UFILE_TAPPED_NOTIFICATIONS);
  if (tapped_nots != NULL
      && xexp_is_tag_and_not_empty (tapped_nots, "info"))
    {
      gboolean istapped = compare_xexp_text (avail_nots, tapped_nots, "title")
        && compare_xexp_text (avail_nots, tapped_nots, "text")
        && compare_xexp_text (avail_nots, tapped_nots, "uri");

      if (istapped == TRUE)
        status = NOTIFICATIONS_TAPPED;
    }

  seen_nots = user_file_read_xexp (UFILE_SEEN_NOTIFICATIONS);
  if (seen_nots != NULL
      && xexp_is_tag_and_not_empty (seen_nots, "info"))
    {
      gboolean isseen = compare_xexp_text (avail_nots, seen_nots, "title")
        && compare_xexp_text (avail_nots, seen_nots, "text")
        && compare_xexp_text (avail_nots, seen_nots, "uri");

      status = (isseen == TRUE) ?
        NOTIFICATIONS_NONE :
        NOTIFICATIONS_NEW;
    }
  else
    {
      if (status == NOTIFICATIONS_NONE)
        status = NOTIFICATIONS_NEW;
    }

 exit:
  if (status != NOTIFICATIONS_NONE && self != NULL)
    {
      HamNotifierPrivate *priv;

      priv = HAM_NOTIFIER_GET_PRIVATE (self);
      g_free (priv->url);
      priv->url = g_strdup (xexp_aref_text (avail_nots, "uri"));
    }

  if (avail_nots != NULL)
    xexp_free (avail_nots);

  if (tapped_nots != NULL)
    xexp_free (tapped_nots);

  if (seen_nots != NULL)
    xexp_free (seen_nots);

  LOG ("there's%snew notifications %d", status == NOTIFICATIONS_NONE ?
       " NOT " : " ", status);

  return status;
}

void
ham_notifier_icon_tapped ()
{
  update_notifications (UFILE_TAPPED_NOTIFICATIONS);
}

static gchar *
build_button_content ()
{
  xexp *avail_nots;
  const gchar *title;
  const gchar *provider;
  gchar *content;

  content = NULL;

  avail_nots = user_file_read_xexp (UFILE_AVAILABLE_NOTIFICATIONS);
  if (avail_nots == NULL)
    goto exit;

  if (xexp_is_tag_and_not_empty (avail_nots, "info"))
    {
      title = xexp_aref_text(avail_nots, "title");
      provider = xexp_aref_text(avail_nots, "provider");

      if (title != NULL)
        {
          gchar *by;

          by = g_strdup_printf (_("apma_fi_by_provider"),
                                provider ? provider : DEFAULT_PROVIDER);
          content = g_strconcat (title, " ", by, NULL);
          g_free (by);
        }
    }

 exit:
  if (avail_nots != NULL)
    xexp_free (avail_nots);
  return content;
}

NotificationsStatus
ham_notifier_status (HamNotifier *self)
{
  NotificationsStatus status;

  status = notifications_status (self);

  if (status != NOTIFICATIONS_NONE
      && self != NULL)
    {
      gchar *value;

      if ((value = build_button_content (self)) != NULL)
        {
          HamNotifierPrivate *priv;

          priv = HAM_NOTIFIER_GET_PRIVATE (self);
          hildon_button_set_value (HILDON_BUTTON (priv->button), value);
          g_free (value);
        }
    }

  return status;
}

HamNotifier*
ham_notifier_new (gpointer data)
{
  HamNotifier *self;

  self = g_new (HamNotifier, 1);
  self->priv = g_new (HamNotifierPrivate, 1);
  self->priv->data = data;

  ham_notifier_init (self);

  return self;

}

void
ham_notifier_free (HamNotifier *self)
{
  ham_notifier_finalize (self);

  g_free (self->priv);
  g_free (self);
}
