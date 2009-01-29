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

#define HAM_NOTIFIER_TYPE (ham_notifier_get_type ())
#define HAM_NOTIFIER(obj)					\
	(G_TYPE_CHECK_INSTANCE_CAST ((obj), HAM_NOTIFIER_TYPE, HamNotifier))
#define HAM_NOTIFIER_CLASS(klass)				\
	(G_TYPE_CHECK_CLASS_CAST ((klass), HAM_NOTIFIER_TYPE, HamNotifierClass))
#define IS_HAM_NOTIFIER(obj)					\
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), HAM_NOTIFIER_TYPE))
#define IS_HAM_NOTIFIER_CLASS(klass)				\
	(G_TYPE_CHECK_CLASS_TYPE ((klass), HAM_NOTIFIER_TYPE))
#define HAM_NOTIFIER_GET_CLASS(obj)				\
	(G_TYPE_INSTANCE_GET_CLASS ((obj), HAM_NOTIFIER_TYPE, HamNotifierClass))

typedef struct _HamNotifier HamNotifier;
typedef struct _HamNotifierClass HamNotifierClass;

struct _HamNotifier
{
  GObject parent;
};

struct _HamNotifierClass
{
  GObjectClass parent_class;

  /* signals */
  void (*response)   (HamNotifier *self, gint response);
};

GType ham_notifier_get_type (void);

gboolean ham_notifier_check (gchar *proxy);
GtkWidget *ham_notifier_get_button (HamNotifier *self);
gchar *ham_notifier_get_url (HamNotifier *self);
gboolean ham_notifier_are_available (HamNotifier *self);

void ham_notifier_empty_seen_notifications ();

G_END_DECLS

#endif 	    /* !HAM_NOTIFIER_H */
