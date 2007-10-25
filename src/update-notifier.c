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

   - Localize
   - Make sure icon doesn't blink when screen is off
*/

#include <glib.h>
#include <gtk/gtk.h>
#include <stdarg.h>

#include <libhildondesktop/libhildondesktop.h>
#include <libhildondesktop/statusbar-item.h>

#include <gconf/gconf-client.h>

#include "update-notifier.h"
#include "pixbufblinkifier.h"
#include "xexp.h"

#define USE_BLINKIFIER 1

typedef struct _UpdateNotifier      UpdateNotifier;
typedef struct _UpdateNotifierClass UpdateNotifierClass;

#define UPDATE_NOTIFIER_TYPE            (update_notifier_get_type ())
#define UPDATE_NOTIFIER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), UPDATE_NOTIFIER_TYPE, UpdateNotifier))
#define UPDATE_NOTIFIER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  UPDATE_NOTIFIER_TYPE, UpdateNotifierClass))
#define IS_UPDATE_NOTIFIER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UPDATE_NOTIFIER_TYPE))
#define IS_UPDATE_NOTIFIER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  UPDATE_NOTIFIER_TYPE))
#define UPDATE_NOTIFIER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  UPDATE_NOTIFIER_TYPE, UpdateNotifierClass))

struct _UpdateNotifier
{
  StatusbarItem parent;

  GtkWidget *button;
  GtkWidget *blinkifier;
  GtkWidget *menu;

  guint timeout_id;

  GConfClient *gconf;
};

struct _UpdateNotifierClass
{
  StatusbarItemClass parent_class;
};

GType update_notifier_get_type(void);

HD_DEFINE_PLUGIN (UpdateNotifier, update_notifier, STATUSBAR_TYPE_ITEM);

static void set_icon_visibility (UpdateNotifier *upno, int state);

static void update_icon_visibility (UpdateNotifier *upno, GConfValue *value);
static void update_menu (UpdateNotifier *upno);

static void update_notifier_finalize (GObject *object);

static void
update_notifier_class_init (UpdateNotifierClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = update_notifier_finalize;
}


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

  update_menu (upno);

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

  fprintf (stderr, "INVOKING\n");
}

static void
gconf_state_changed (GConfClient *client, guint cnxn_id,
		     GConfEntry *entry, gpointer data)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (data);

  update_icon_visibility (upno, entry->value);
}

static void
update_notifier_init (UpdateNotifier *upno)
{
  GtkWidget *item;
  GdkPixbuf *icon_pixbuf;
  GtkIconTheme *icon_theme;

  upno->gconf = gconf_client_get_default();
  gconf_client_add_dir (upno->gconf,
			UPNO_GCONF_DIR,
			GCONF_CLIENT_PRELOAD_ONELEVEL,
			NULL);
  gconf_client_notify_add (upno->gconf,
			   UPNO_GCONF_STATE,
			   gconf_state_changed, upno,
			   NULL, NULL);

  upno->button = gtk_button_new ();

  icon_theme = gtk_icon_theme_get_default ();
  icon_pixbuf = gtk_icon_theme_load_icon (icon_theme,
					  "qgn_stat_new_updates",
					  40,
					  GTK_ICON_LOOKUP_NO_SVG,
					  NULL);
#if USE_BLINKIFIER
  upno->blinkifier = g_object_new (PIXBUF_BLINKIFIER_TYPE,
				   "pixbuf", icon_pixbuf,
				   "frame-time", 100,
				   "n-frames", 10,
				   NULL);
#else
  upno->blinkifier = gtk_image_new_from_pixbuf (icon_pixbuf);
#endif
  
  gtk_container_add (GTK_CONTAINER (upno->button), upno->blinkifier);
  gtk_container_add (GTK_CONTAINER (upno), upno->button);

  gtk_widget_show (upno->blinkifier);
  gtk_widget_show (upno->button);


  upno->menu = gtk_menu_new ();
  item = gtk_menu_item_new_with_label ("Foo");
  gtk_menu_append (upno->menu, item);
  gtk_widget_show (item);

  g_signal_connect (upno->button, "pressed",
		    G_CALLBACK (button_pressed), upno);
  
  update_menu (upno);
  update_icon_visibility (upno, gconf_client_get (upno->gconf,
						  UPNO_GCONF_STATE,
						  NULL));
}

static void
update_notifier_finalize (GObject *object)
{
  UpdateNotifier *upno = UPDATE_NOTIFIER (object);
    
  G_OBJECT_CLASS (g_type_class_peek_parent
		  (G_OBJECT_GET_CLASS(object)))->finalize(object);
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

  g_object_set (upno,
		"condition", (state == UPNO_ICON_STATIC
			      || state == UPNO_ICON_BLINKING),
		NULL);

#if USE_BLINKIFIER
  g_object_set (upno->blinkifier,
		"blinking", (state == UPNO_ICON_BLINKING),
		NULL);
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
  GtkWidget *item;
  va_list ap;
  char *label;

  va_start (ap, fmt);
  label = g_strdup_vprintf (fmt, ap);
  va_end (ap);

  item = gtk_menu_item_new_with_label (label);
  gtk_menu_append (GTK_MENU (menu), item);
  gtk_widget_show (item);
  gtk_widget_set_sensitive (item, FALSE);

  g_free (label);
}

static void
update_menu (UpdateNotifier *upno)
{
  xexp *updates;
  int n_os = 0, n_nokia = 0, n_other = 0;
  GtkWidget *item;

  /* XXX - only do this when file has actually changed.
   */

  updates = xexp_read_file ("/var/lib/hildon-application-manager/available-updates");

  if (updates)
    {
      xexp *x;

      xexp_write (stderr, updates);

      if ((x = xexp_aref (updates, "os-updates")))
	n_os = xexp_aref_int (x, "count", 0);
      
      if ((x = xexp_aref (updates, "nokia-updates")))
	n_nokia = xexp_aref_int (x, "count", 0);

      if ((x = xexp_aref (updates, "other-updates")))
	n_other = xexp_aref_int (x, "count", 0);
    }

  if (upno->menu)
    gtk_widget_destroy (upno->menu);

  upno->menu = gtk_menu_new ();

  add_readonly_item (upno->menu, "Available software updates:");
  add_readonly_item (upno->menu, "   Nokia (%d)", n_nokia);
  add_readonly_item (upno->menu, "   Other (%d)", n_other);
  add_readonly_item (upno->menu, "   OS (%d)", n_os);

  item = gtk_separator_menu_item_new ();
  gtk_menu_append (upno->menu, item);
  gtk_widget_show (item);
  
  item = gtk_menu_item_new_with_label ("Invoke Application Manager");
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
