/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2005, 2006, 2007, 2008 Nokia Corporation.  All Rights reserved.
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


#ifndef PACKAGE_INFO_CELL_RENDERER_H
#define PACKAGE_INFO_CELL_RENDERER_H
#include <glib-object.h>
#include <gtk/gtkcellrenderer.h>

G_BEGIN_DECLS

#define TYPE_PACKAGE_INFO_CELL_RENDERER             (package_info_cell_renderer_get_type ())
#define PACKAGE_INFO_CELL_RENDERER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_PACKAGE_INFO_CELL_RENDERER, PackageInfoCellRenderer))
#define PACKAGE_INFO_CELL_RENDERER_CLASS(vtable)    (G_TYPE_CHECK_CLASS_CAST ((vtable), TYPE_PACKAGE_INFO_CELL_RENDERER, PackageInfoCellRendererClass))
#define IS_PACKAGE_INFO_CELL_RENDERER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_PACKAGE_INFO_CELL_RENDERER))
#define IS_PACKAGE_INFO_CELL_RENDERER_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), TYPE_PACKAGE_INFO_CELL_RENDERER))
#define PACKAGE_INFO_CELL_RENDERER_GET_CLASS(inst)  (G_TYPE_INSTANCE_GET_CLASS ((inst), TYPE_PACKAGE_INFO_CELL_RENDERER, PackageInfoCellRendererClass))

typedef struct _PackageInfoCellRenderer PackageInfoCellRenderer;
typedef struct _PackageInfoCellRendererClass PackageInfoCellRendererClass;

struct _PackageInfoCellRenderer
{
  GtkCellRenderer parent;

};

struct _PackageInfoCellRendererClass
{
  GtkCellRendererClass parent_class;

};

GType package_info_cell_renderer_get_type (void);

GtkCellRenderer* package_info_cell_renderer_new (void);

G_END_DECLS

#endif
