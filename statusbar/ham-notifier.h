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
 * Foundation, Inc., 51 Franklin St, Fitnessfth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef   	HAM_NOTIFIER_H
# define   	HAM_NOTIFIER_H

#include <hildon/hildon.h>
#include <libalarm.h>
#include <libosso.h>

G_BEGIN_DECLS

#define HAM_NOTIFIER(obj)					\
  ((HamNotifier *) obj)

typedef enum
{
  NOTIFICATIONS_NEW,    /* icon blinking  */
  NOTIFICATIONS_TAPPED, /* icon static    */
  NOTIFICATIONS_NONE    /* icon invisible */
} NotificationsStatus;

typedef struct _HamNotifier HamNotifier;
typedef struct _HamNotifierPrivate HamNotifierPrivate;

struct _HamNotifier
{
  HamNotifierPrivate *priv;

  /* callbacks */
  void (*response)   (gpointer self, gint response, gpointer data);
};

gboolean ham_notifier_check (gchar *proxy);
GtkWidget *ham_notifier_get_button (HamNotifier *self);
gchar *ham_notifier_get_url (HamNotifier *self);
NotificationsStatus ham_notifier_status (HamNotifier *self);

void ham_notifier_empty_seen_notifications ();
void ham_notifier_icon_tapped ();

HamNotifier *ham_notifier_new (gpointer data);
void ham_notifier_free (HamNotifier *self);

G_END_DECLS

#endif 	    /* !HAM_NOTIFIER_H */
