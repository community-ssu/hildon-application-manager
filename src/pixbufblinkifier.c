/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007, 2008 Nokia Corporation.  All Rights reserved.
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

#include "pixbufblinkifier.h"

enum
{
  PBB_PROPERTY_BLINKING = 1,
  PBB_PROPERTY_PIXBUF,
  PBB_PROPERTY_FRAME_TIME,
  PBB_PROPERTY_N_FRAMES
};

typedef struct
{
  GdkPixbuf *pb;
  GdkPixbuf *pb_priv;
  guint cx;
  guint cy;
  guint blink_timeout;
  guint frame_time_ms;
  guint n_frames;
  guint counter;
} PBBPrivate;

#define PBB_GET_PRIV(x) (G_TYPE_INSTANCE_GET_PRIVATE((x), PIXBUF_BLINKIFIER_TYPE, PBBPrivate))

static void
instance_init(GTypeInstance *inst, gpointer klass)
{
  PBBPrivate *priv = PBB_GET_PRIV(inst);

  priv->pb = NULL;
  priv->cx = 0;
  priv->cy = 0;
  priv->blink_timeout = 0;
  priv->frame_time_ms = 100;
  priv->n_frames = 10;
  priv->counter = 0;

  GTK_WIDGET_SET_FLAGS(inst, GTK_NO_WINDOW);

  gtk_widget_add_events(GTK_WIDGET(inst), GDK_EXPOSURE_MASK);
}

static void
instance_finalize(GObject *obj)
{
  PBBPrivate *priv = PBB_GET_PRIV(obj);

  if (priv->blink_timeout && priv->pb) {
    g_source_remove(priv->blink_timeout);
    priv->blink_timeout = 0;
  }

  if (priv->pb) {
    g_object_unref(priv->pb);
    priv->pb = NULL;
  }
  if (priv->pb_priv) {
    g_object_unref(priv->pb_priv);
    priv->pb_priv = NULL;
  }
}

static gboolean
draw_scene(GtkWidget *widget, PBBPrivate *priv)
{
  gint x_offset, y_offset;
  gint counter = priv->n_frames - 1 - (priv->counter << 1);
  GdkGC *gc = NULL;

  if (!(widget->window && priv->pb && priv->pb_priv)) return FALSE;
  if ((gc = gdk_gc_new(widget->window)) == NULL) return FALSE;

  gdk_pixbuf_fill(priv->pb_priv, 0x00000000);

  gdk_pixbuf_composite(priv->pb, priv->pb_priv, 0, 0, priv->cx, priv->cy, 0, 0, 1, 1, GDK_INTERP_NEAREST,
    ((int)(255.0 * ((double)(ABS(counter)))/((double)(priv->n_frames - 1)))));

  x_offset = ((widget->allocation.width - priv->cx) >> 1) + widget->allocation.x;
  y_offset = ((widget->allocation.height - priv->cy) >> 1) + widget->allocation.y;

  gdk_window_clear_area(widget->window, x_offset, y_offset, priv->cx, priv->cy);
  gdk_draw_pixbuf(widget->window, gc, priv->pb_priv, 0, 0, x_offset, y_offset, priv->cx, priv->cy, GDK_RGB_DITHER_NONE, 0, 0);
  g_object_unref(gc);

  return TRUE;
}

static gboolean
blink_timeout(GtkWidget *widget)
{
  PBBPrivate *priv = PBB_GET_PRIV(widget);

  draw_scene(widget, priv);

  (priv->counter)++;
  priv->counter %= priv->n_frames;

  return TRUE;
}

static gboolean
expose_event(GtkWidget *widget, GdkEventExpose *event)
{
  return draw_scene(widget, PBB_GET_PRIV(widget));
}

static void
pbb_set_blinking(GObject *obj, gboolean is_blinking)
{
  PBBPrivate *priv = PBB_GET_PRIV(obj);

  if (is_blinking == (priv->blink_timeout != 0)) return;

  if (is_blinking) {
    if (!(priv->pb))
      priv->blink_timeout = 1;
    else {
      priv->counter = 0;
      priv->blink_timeout = g_timeout_add(priv->frame_time_ms, (GSourceFunc)blink_timeout, obj);
      gtk_widget_queue_draw(GTK_WIDGET(obj));
    }
  } else {
    g_source_remove(priv->blink_timeout);
    priv->blink_timeout = 0;
    priv->counter = 0;
    gtk_widget_queue_draw(GTK_WIDGET(obj));
  }

  g_object_notify(obj, "blinking");
}

static void
pbb_set_pixbuf(GObject *obj, GdkPixbuf *pb)
{
  guint new_cx = 0, new_cy = 0;
  PBBPrivate *priv = PBB_GET_PRIV(obj);

  if (pb == priv->pb) return;

  if (priv->blink_timeout) {
    if (priv->pb)
      g_source_remove(priv->blink_timeout);
    priv->blink_timeout = 1;
  }

  if (priv->pb) {
    g_object_unref(priv->pb);
    g_object_unref(priv->pb_priv);
  }

  if (pb) {
    g_object_ref(pb);
    priv->pb_priv = gdk_pixbuf_copy(pb);
    new_cx = gdk_pixbuf_get_width(pb);
    new_cy = gdk_pixbuf_get_height(pb);
  }

  if (!(new_cx == priv->cx && new_cy == priv->cy))
    gtk_widget_queue_resize(GTK_WIDGET(obj));

  if (priv->blink_timeout) {
    priv->counter = 0;
    priv->blink_timeout = g_timeout_add(priv->frame_time_ms, (GSourceFunc)blink_timeout, obj);
  }

  priv->pb = pb;
  priv->cx = new_cx;
  priv->cy = new_cy;

  g_object_notify(obj, "pixbuf");
}

static void
pbb_set_frame_time(GObject *obj, guint frame_time_ms)
{
  PBBPrivate *priv = PBB_GET_PRIV(obj);

  priv->frame_time_ms = frame_time_ms;

  if (priv->blink_timeout) {
    if (priv->pb) {
      g_source_remove(priv->blink_timeout);
      priv->counter = 0;
      g_timeout_add(priv->frame_time_ms, (GSourceFunc)blink_timeout, obj);
    }
  }

  g_object_notify(obj, "frame-time");
}

static void
pbb_set_n_frames(GObject *obj, guint n_frames)
{
  PBBPrivate *priv = PBB_GET_PRIV(obj);

  priv->n_frames = n_frames;

  if (priv->blink_timeout) {
    if (priv->pb) {
      g_source_remove(priv->blink_timeout);
      priv->counter = 0;
      g_timeout_add(priv->frame_time_ms, (GSourceFunc)blink_timeout, obj);
    }
  }
  g_object_notify(obj, "n-frames");
}

static void
set_property(GObject *obj, guint prop_id, const GValue *val, GParamSpec *pspec)
{
  if (PBB_PROPERTY_BLINKING == prop_id)
    pbb_set_blinking(obj, g_value_get_boolean(val));
  else
  if (PBB_PROPERTY_PIXBUF == prop_id)
    pbb_set_pixbuf(obj, g_value_get_object(val));
  else
  if (PBB_PROPERTY_FRAME_TIME == prop_id)
    pbb_set_frame_time(obj, g_value_get_uint(val));
  else
  if (PBB_PROPERTY_N_FRAMES == prop_id)
    pbb_set_n_frames(obj, g_value_get_uint(val));
}

static void
get_property(GObject *obj, guint prop_id, GValue *val, GParamSpec *pspec)
{
  PBBPrivate *priv = PBB_GET_PRIV(obj);

  if (PBB_PROPERTY_BLINKING == prop_id)
    g_value_set_boolean(val, (0 == priv->blink_timeout));
  else
  if (PBB_PROPERTY_PIXBUF == prop_id)
    g_value_set_object(val, priv->pb);
  else
  if (PBB_PROPERTY_FRAME_TIME == prop_id)
    g_value_set_uint(val, priv->frame_time_ms);
  else
  if (PBB_PROPERTY_N_FRAMES == prop_id)
    g_value_set_uint(val, priv->n_frames);
}

static void
size_request(GtkWidget *widget, GtkRequisition *rq)
{
  PBBPrivate *priv = PBB_GET_PRIV(widget);

  if (GTK_WIDGET_CLASS(g_type_class_peek(g_type_parent(PIXBUF_BLINKIFIER_TYPE)))->size_request)
    GTK_WIDGET_CLASS(g_type_class_peek(g_type_parent(PIXBUF_BLINKIFIER_TYPE)))->size_request(widget, rq);

  rq->width  += priv->cx;
  rq->height += priv->cy;
}

static void
class_init(gpointer klass, gpointer klass_data)
{
  G_OBJECT_CLASS(klass)->finalize = instance_finalize;
  G_OBJECT_CLASS(klass)->set_property = set_property;
  G_OBJECT_CLASS(klass)->get_property = get_property;

  GTK_WIDGET_CLASS(klass)->size_request = size_request;
  GTK_WIDGET_CLASS(klass)->expose_event = expose_event;

  g_object_class_install_property(G_OBJECT_CLASS(klass), PBB_PROPERTY_BLINKING,
    g_param_spec_boolean("blinking", "Blinking", "To Blink Or Not To Blink", FALSE, G_PARAM_READABLE | G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), PBB_PROPERTY_PIXBUF,
    g_param_spec_object("pixbuf", "Pixbuf", "Pic To Blink", GDK_TYPE_PIXBUF, G_PARAM_READABLE | G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), PBB_PROPERTY_FRAME_TIME,
    g_param_spec_uint("frame-time", "Frame Time", "How Long To Show A Frame", 10, 5000, 100, G_PARAM_READABLE | G_PARAM_WRITABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), PBB_PROPERTY_N_FRAMES,
    g_param_spec_uint("n-frames", "Number Of Frames", "How Many Frames", 1, 100, 10, G_PARAM_READABLE | G_PARAM_WRITABLE));

  g_type_class_add_private(klass, sizeof(PBBPrivate));
}

GType
pixbuf_blinkifier_get_type()
{
  static GType the_type = 0;

  if (!the_type) {
    static GTypeInfo info =
    {
      .class_size     = sizeof(PixbufBlinkifierClass),
      .base_init      = NULL,
      .base_finalize  = NULL,
      .class_init     = class_init,
      .class_finalize = NULL,
      .class_data     = NULL,
      .instance_size  = sizeof(PixbufBlinkifier),
      .n_preallocs    = 0,
      .instance_init  = instance_init,
      .value_table    = NULL
    };

  the_type = g_type_register_static(GTK_TYPE_WIDGET, "PixbufBlinkifier", &info, 0);
  }

  return the_type;
}
