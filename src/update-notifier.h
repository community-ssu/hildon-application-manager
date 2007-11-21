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

#ifndef UPDATE_NOTIFIER_H
#define UPDATE_NOTIFIER_H

#include <glib.h>
#include <gtk/gtk.h>
#include <dbus/dbus.h>
#include <gconf/gconf-client.h>
#include <libhildondesktop/statusbar-item.h>

#define UPDATE_NOTIFIER_TYPE            (update_notifier_get_type ())
#define UPDATE_NOTIFIER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), UPDATE_NOTIFIER_TYPE, UpdateNotifier))
#define UPDATE_NOTIFIER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  UPDATE_NOTIFIER_TYPE, UpdateNotifierClass))
#define IS_UPDATE_NOTIFIER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), UPDATE_NOTIFIER_TYPE))
#define IS_UPDATE_NOTIFIER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  UPDATE_NOTIFIER_TYPE))
#define UPDATE_NOTIFIER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  UPDATE_NOTIFIER_TYPE, UpdateNotifierClass))

typedef struct _UpdateNotifier      UpdateNotifier;
typedef struct _UpdateNotifierClass UpdateNotifierClass;

struct _UpdateNotifier
{
  StatusbarItem parent;
};

struct _UpdateNotifierClass
{
  StatusbarItemClass parent_class;
};

GType update_notifier_get_type(void);

#endif /* !UPDATE_NOTIFIER_H */
