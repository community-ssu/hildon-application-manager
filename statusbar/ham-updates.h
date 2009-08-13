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

#ifndef   	HAM_UPDATES_H
# define   	HAM_UPDATES_H

#include <hildon/hildon.h>
#include <libalarm.h>
#include <libosso.h>

G_BEGIN_DECLS

#define HAM_UPDATES(obj)					\
  ((HamUpdates *) obj)

typedef enum
{
  UPDATES_NEW,    /* icon blinking  */
  UPDATES_TAPPED, /* icon static    */
  UPDATES_NONE    /* icon invisible */
} UpdatesStatus;

typedef struct _HamUpdates HamUpdates;
typedef struct _HamUpdatesPrivate HamUpdatesPrivate;

struct _HamUpdates
{
  HamUpdatesPrivate *priv;

  /* callbacks */
  void (*check_done) (gpointer self, gboolean ok, gpointer data);
  void (*response)   (gpointer self, gint response, gpointer data);
};

gboolean ham_updates_check (HamUpdates *self, gchar *proxy);
gboolean ham_updates_set_alarm (HamUpdates *self, alarm_event_t *event);
GtkWidget *ham_updates_get_button (HamUpdates *self);
time_t ham_updates_get_interval (HamUpdates *self);
UpdatesStatus ham_updates_status (HamUpdates *self, osso_context_t *context);
void ham_updates_icon_tapped ();

HamUpdates *ham_updates_new (gpointer data);
void ham_updates_free (HamUpdates *self);

G_END_DECLS

#endif 	    /* !HAM_UPDATES_H */
