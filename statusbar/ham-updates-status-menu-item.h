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

#ifndef HAM_UPDATES_STATUS_MENU_ITEM_H
#define HAM_UPDATES_STATUS_MENU_ITEM_H

#include <libhildondesktop/libhildondesktop.h>

G_BEGIN_DECLS

#define HAM_UPDATES_STATUS_MENU_ITEM_TYPE            (update_notifier_get_type ())
#define HAM_UPDATES_STATUS_MENU_ITEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), HAM_UPDATES_STATUS_MENU_ITEM_TYPE, HamUpdatesStatusMenuItem))
#define HAM_UPDATES_STATUS_MENU_ITEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  HAM_UPDATES_STATUS_MENU_ITEM_TYPE, HamUpdatesStatusMenuItemClass))
#define IS_HAM_UPDATES_STATUS_MENU_ITEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HAM_UPDATES_STATUS_MENU_ITEM_TYPE))
#define IS_HAM_UPDATES_STATUS_MENU_ITEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  HAM_UPDATES_STATUS_MENU_ITEM_TYPE))
#define HAM_UPDATES_STATUS_MENU_ITEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  HAM_UPDATES_STATUS_MENU_ITEM_TYPE, HamUpdatesStatusMenuItemClass))

typedef struct _HamUpdatesStatusMenuItem      HamUpdatesStatusMenuItem;
typedef struct _HamUpdatesStatusMenuItemClass HamUpdatesStatusMenuItemClass;

struct _HamUpdatesStatusMenuItem
{
  HDStatusMenuItem parent;
};

struct _HamUpdatesStatusMenuItemClass
{
  HDStatusMenuItemClass parent_class;
};

GType ham_updates_status_menu_item_get_type(void);

G_END_DECLS

#endif /* !HAM_UPDATES_STATUS_MENU_ITEM_H */
