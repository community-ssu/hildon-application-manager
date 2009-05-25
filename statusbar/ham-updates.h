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

#define HAM_UPDATES_TYPE (ham_updates_get_type ())
#define HAM_UPDATES(obj)					\
	(G_TYPE_CHECK_INSTANCE_CAST ((obj), HAM_UPDATES_TYPE, HamUpdates))
#define HAM_UPDATES_CLASS(klass)				\
	(G_TYPE_CHECK_CLASS_CAST ((klass), HAM_UPDATES_TYPE, HamUpdatesClass))
#define IS_HAM_UPDATES(obj)					\
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), HAM_UPDATES_TYPE))
#define IS_HAM_UPDATES_CLASS(klass)				\
	(G_TYPE_CHECK_CLASS_TYPE ((klass), HAM_UPDATES_TYPE))
#define HAM_UPDATES_GET_CLASS(obj)				\
	(G_TYPE_INSTANCE_GET_CLASS ((obj), HAM_UPDATES_TYPE, HamUpdatesClass))

typedef struct _HamUpdates HamUpdates;
typedef struct _HamUpdatesClass HamUpdatesClass;

struct _HamUpdates
{
  GObject parent;
};

struct _HamUpdatesClass
{
  GObjectClass parent_class;

  /* signals */
  void (*check_done) (HamUpdates *self, gboolean ok);
  void (*response)   (HamUpdates *self, gint response);
};

GType ham_updates_get_type (void);

gboolean ham_updates_check (HamUpdates *self, gchar *proxy);
gboolean ham_updates_set_alarm (HamUpdates *self, alarm_event_t *event);
GtkWidget *ham_updates_get_button (HamUpdates *self);
time_t ham_updates_get_interval (HamUpdates *self);
gboolean ham_updates_are_available (HamUpdates *self, osso_context_t *context);

G_END_DECLS

#endif 	    /* !HAM_UPDATES_H */
