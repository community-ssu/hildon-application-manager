/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007 Nokia Corporation.  All Rights reserved.
 *
 * Contact: Gabriel Schulhof <gabriel.schulhof@nokia.com>
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

#ifndef _PIXBUF_BLINKIFIER_H_
#define _PIXBUF_BLINKIFIER_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _PixbufBlinkifier      PixbufBlinkifier;
typedef struct _PixbufBlinkifierClass PixbufBlinkifierClass;

struct _PixbufBlinkifier
{
  GtkWidget parent_instance;
};

struct _PixbufBlinkifierClass
{
  GtkWidgetClass parent_class;
};

GType pixbuf_blinkifier_get_type();

#define PIXBUF_BLINKIFIER_TYPE pixbuf_blinkifier_get_type()

G_END_DECLS

#endif /* !_PIXBUF_BLINKIFIER_H_ */
