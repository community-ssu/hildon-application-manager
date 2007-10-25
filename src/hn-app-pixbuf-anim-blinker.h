/*
 * This file is part of maemo-af-desktop
 *
 * Copyright (C) 2005 Nokia Corporation.
 *
 * Contact: Karoliina Salminen <karoliina.t.salminen@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/* 
 * Simple blinking pixbuf animation class. 
 *   Authored by Chris Lord <chris@o-hand.com> 
 *               Matthew Allum <mallum@o-hand.com>
 */

#ifndef HN_APP_PIXBUF_ANIM_BLINKER_H
#define HN_APP_PIXBUF_ANIM_BLINKER_H

#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

typedef struct _HNAppPixbufAnimBlinker HNAppPixbufAnimBlinker;
typedef struct _HNAppPixbufAnimBlinkerClass HNAppPixbufAnimBlinkerClass;

#define TYPE_HN_APP_PIXBUF_ANIM_BLINKER              (hn_app_pixbuf_anim_blinker_get_type ())
#define HN_APP_PIXBUF_ANIM_BLINKER(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), TYPE_HN_APP_PIXBUF_ANIM_BLINKER, HNAppPixbufAnimBlinker))
#define IS_HN_APP_PIXBUF_ANIM_BLINKER(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), TYPE_HN_APP_PIXBUF_ANIM_BLINKER))

#define HN_APP_PIXBUF_ANIM_BLINKER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_HN_APP_PIXBUF_ANIM_BLINKER, HNAppPixbufAnimBlinkerClass))
#define IS_HN_APP_PIXBUF_ANIM_BLINKER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_HN_APP_PIXBUF_ANIM_BLINKER))
#define HN_APP_PIXBUF_ANIM_BLINKER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_HN_APP_PIXBUF_ANIM_BLINKER, HNAppPixbufAnimBlinkerClass))

GType hn_app_pixbuf_anim_blinker_get_type (void) G_GNUC_CONST;
GType hn_app_pixbuf_anim_blinker_iter_get_type (void) G_GNUC_CONST;

GdkPixbufAnimation *hn_app_pixbuf_anim_blinker_new (GdkPixbuf *pixbuf, 
						    gint       period,
						    gint       length,
						    gint       frequency);

void
hn_app_pixbuf_anim_blinker_stop (HNAppPixbufAnimBlinker *anim);

void
hn_app_pixbuf_anim_blinker_restart (HNAppPixbufAnimBlinker *anim);

G_END_DECLS

#endif

