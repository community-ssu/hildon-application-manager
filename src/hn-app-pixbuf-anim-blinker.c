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

#define GDK_PIXBUF_ENABLE_BACKEND

#include <glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gdk-pixbuf-animation.h>
#include "hn-app-pixbuf-anim-blinker.h"

#define FIXED_PERIOD 100

struct _HNAppPixbufAnimBlinker
{
        GdkPixbufAnimation parent_instance;

	gboolean stopped;
	gint period;
	gint length;
	gint frequency;
	
	GdkPixbuf *pixbuf;
	GdkPixbuf *blended;
};

struct _HNAppPixbufAnimBlinkerClass
{
        GdkPixbufAnimationClass parent_class;
};


typedef struct _HNAppPixbufAnimBlinkerIter HNAppPixbufAnimBlinkerIter;
typedef struct _HNAppPixbufAnimBlinkerIterClass HNAppPixbufAnimBlinkerIterClass;

#define TYPE_HN_APP_PIXBUF_ANIM_BLINKER_ITER              (hn_app_pixbuf_anim_blinker_iter_get_type ())
#define HN_APP_PIXBUF_ANIM_BLINKER_ITER(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), TYPE_HN_APP_PIXBUF_ANIM_BLINKER_ITER, HNAppPixbufAnimBlinkerIter))
#define IS_HN_APP_PIXBUF_ANIM_BLINKER_ITER(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), TYPE_HN_APP_PIXBUF_ANIM_BLINKER_ITER))

#define HN_APP_PIXBUF_ANIM_BLINKER_ITER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_HN_APP_PIXBUF_ANIM_BLINKER_ITER, HNAppPixbufAnimBlinkerIterClass))
#define IS_HN_APP_PIXBUF_ANIM_BLINKER_ITER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_HN_APP_PIXBUF_ANIM_BLINKER_ITER))
#define HN_APP_PIXBUF_ANIM_BLINKER_ITER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_HN_APP_PIXBUF_ANIM_BLINKER_ITER, HNAppPixbufAnimBlinkerIterClass))

GType hn_app_pixbuf_anim_blinker_iter_get_type (void) G_GNUC_CONST;


struct _HNAppPixbufAnimBlinkerIterClass
{
        GdkPixbufAnimationIterClass parent_class;
};

struct _HNAppPixbufAnimBlinkerIter
{
        GdkPixbufAnimationIter parent_instance;
        
        HNAppPixbufAnimBlinker *hn_app_pixbuf_anim_blinker;
        
        GTimeVal start_time;
        GTimeVal current_time;
        gint length;
};

static void hn_app_pixbuf_anim_blinker_finalize (GObject *object);

static gboolean   is_static_image  (GdkPixbufAnimation *animation);
static GdkPixbuf *get_static_image (GdkPixbufAnimation *animation);

static void       get_size         (GdkPixbufAnimation *anim,
                                    gint               *width, 
                                    gint               *height);
static GdkPixbufAnimationIter *get_iter (GdkPixbufAnimation *anim,
                                         const GTimeVal     *start_time);


G_DEFINE_TYPE(HNAppPixbufAnimBlinker, hn_app_pixbuf_anim_blinker,
	      GDK_TYPE_PIXBUF_ANIMATION);

static void
hn_app_pixbuf_anim_blinker_init (HNAppPixbufAnimBlinker *anim)
{
}

static void
hn_app_pixbuf_anim_blinker_class_init (HNAppPixbufAnimBlinkerClass *klass)
{
        GObjectClass *object_class;
        GdkPixbufAnimationClass *anim_class;

        object_class = G_OBJECT_CLASS (klass);
        anim_class = GDK_PIXBUF_ANIMATION_CLASS (klass);
        
        object_class->finalize = hn_app_pixbuf_anim_blinker_finalize;
        
        anim_class->is_static_image = is_static_image;
        anim_class->get_static_image = get_static_image;
        anim_class->get_size = get_size;
        anim_class->get_iter = get_iter;
}

static void
hn_app_pixbuf_anim_blinker_finalize (GObject *object)
{
        HNAppPixbufAnimBlinker *anim;
        
        anim = HN_APP_PIXBUF_ANIM_BLINKER (object);        
        
        g_object_unref (anim->pixbuf);
        g_object_unref (anim->blended);
        
        G_OBJECT_CLASS (hn_app_pixbuf_anim_blinker_parent_class)->
        	finalize (object);
}

static gboolean
is_static_image (GdkPixbufAnimation *animation)
{
        return FALSE;
}

static GdkPixbuf *
get_static_image (GdkPixbufAnimation *animation)
{
        HNAppPixbufAnimBlinker *anim;
        
        anim = HN_APP_PIXBUF_ANIM_BLINKER (animation);
 
 	return anim->pixbuf;       
}

static void
get_size (GdkPixbufAnimation *animation,
          gint               *width, 
          gint               *height)
{
        HNAppPixbufAnimBlinker *anim;

        anim = HN_APP_PIXBUF_ANIM_BLINKER (animation);
        
        if (width)
        	*width = gdk_pixbuf_get_width (anim->pixbuf);
        if (height)
        	*height = gdk_pixbuf_get_height (anim->pixbuf);
}

static GdkPixbufAnimationIter *
get_iter (GdkPixbufAnimation *anim,
          const GTimeVal    *start_time)
{
	HNAppPixbufAnimBlinkerIter *iter;
	HNAppPixbufAnimBlinker *blinker = HN_APP_PIXBUF_ANIM_BLINKER (anim);
	gint i;

	iter = g_object_new (TYPE_HN_APP_PIXBUF_ANIM_BLINKER_ITER, NULL);

	iter->hn_app_pixbuf_anim_blinker = blinker;

	g_object_ref (iter->hn_app_pixbuf_anim_blinker);

	iter->start_time = *start_time;
	iter->current_time = *start_time;

	/* Find out how many seconds it is before a period repeats */
	/* (e.g. for 500ms, 1 second, for 333ms, 2 seconds, etc.) */
	for (i = 1; ((blinker->period * i) % 2000) != 0; i++);
	i = iter->start_time.tv_sec % ((blinker->period*i)/1000);

	/* Make sure to offset length as well as start time */
	iter->length = blinker->length + (i * 1000);
	iter->start_time.tv_sec -= i;
	iter->start_time.tv_usec = 0;

	return GDK_PIXBUF_ANIMATION_ITER (iter);
}

static void hn_app_pixbuf_anim_blinker_iter_finalize (GObject *object);

static gint       get_delay_time             (GdkPixbufAnimationIter *iter);
static GdkPixbuf *get_pixbuf                 (GdkPixbufAnimationIter *iter);
static gboolean   on_currently_loading_frame (GdkPixbufAnimationIter *iter);
static gboolean   advance                    (GdkPixbufAnimationIter *iter,
					      const GTimeVal *current_time);

G_DEFINE_TYPE (HNAppPixbufAnimBlinkerIter, hn_app_pixbuf_anim_blinker_iter,
	       GDK_TYPE_PIXBUF_ANIMATION_ITER);

static void
hn_app_pixbuf_anim_blinker_iter_init (HNAppPixbufAnimBlinkerIter *iter)
{
}

static void
hn_app_pixbuf_anim_blinker_iter_class_init (
	HNAppPixbufAnimBlinkerIterClass *klass)
{
        GObjectClass *object_class;
        GdkPixbufAnimationIterClass *anim_iter_class;

        object_class = G_OBJECT_CLASS (klass);
        anim_iter_class = GDK_PIXBUF_ANIMATION_ITER_CLASS (klass);
        
        object_class->finalize = hn_app_pixbuf_anim_blinker_iter_finalize;
        
        anim_iter_class->get_delay_time = get_delay_time;
        anim_iter_class->get_pixbuf = get_pixbuf;
        anim_iter_class->on_currently_loading_frame =
        	on_currently_loading_frame;
        anim_iter_class->advance = advance;
}

static void
hn_app_pixbuf_anim_blinker_iter_finalize (GObject *object)
{
        HNAppPixbufAnimBlinkerIter *iter;
        
        iter = HN_APP_PIXBUF_ANIM_BLINKER_ITER (object);
        
        g_object_unref (iter->hn_app_pixbuf_anim_blinker);
        
        G_OBJECT_CLASS (hn_app_pixbuf_anim_blinker_iter_parent_class)->
        	finalize (object);
}

static gboolean
advance (GdkPixbufAnimationIter *anim_iter,
         const GTimeVal         *current_time)
{
        HNAppPixbufAnimBlinkerIter *iter;
        gint elapsed;
        
        iter = HN_APP_PIXBUF_ANIM_BLINKER_ITER (anim_iter);
        
        iter->current_time = *current_time;
        
        if (iter->hn_app_pixbuf_anim_blinker->stopped)
        	return FALSE;
        	
        if (iter->hn_app_pixbuf_anim_blinker->length == -1)
        	return TRUE;
        
        /* We use milliseconds for all times */
        elapsed = (((iter->current_time.tv_sec - iter->start_time.tv_sec) *
        	G_USEC_PER_SEC + iter->current_time.tv_usec -
        	iter->start_time.tv_usec)) / 1000;
        
        if (elapsed < 0) {
                /* Try to compensate; probably the system clock
                 * was set backwards
                 */
                iter->start_time = iter->current_time;
                elapsed = 0;
        }

	if (elapsed < iter->length)
		return TRUE;
	else
		return FALSE;
}

static gint
get_delay_time (GdkPixbufAnimationIter *anim_iter)
{
        HNAppPixbufAnimBlinkerIter *iter =
        	HN_APP_PIXBUF_ANIM_BLINKER_ITER (anim_iter);
        gint elapsed;
#ifndef FIXED_PERIOD	
        gint period = iter->hn_app_pixbuf_anim_blinker->period /
        	iter->hn_app_pixbuf_anim_blinker->frequency;
#endif	
        
        elapsed = (((iter->current_time.tv_sec - iter->start_time.tv_sec) *
        	G_USEC_PER_SEC + iter->current_time.tv_usec -
        	iter->start_time.tv_usec)) / 1000;

	if (((elapsed < iter->length) ||
	    (iter->hn_app_pixbuf_anim_blinker->length == -1)) &&
	    (!iter->hn_app_pixbuf_anim_blinker->stopped))
        {		
#ifndef FIXED_PERIOD		
		return period - (elapsed % period);
#else		
		return FIXED_PERIOD;
#endif		
        }		
	else
		return -1;
}

static GdkPixbuf *
get_pixbuf (GdkPixbufAnimationIter *anim_iter)
{
        HNAppPixbufAnimBlinkerIter *iter =
        	HN_APP_PIXBUF_ANIM_BLINKER_ITER (anim_iter);
        gint elapsed, alpha;
        gint period = iter->hn_app_pixbuf_anim_blinker->period;
        
        elapsed = (((iter->current_time.tv_sec - iter->start_time.tv_sec) *
        	G_USEC_PER_SEC + iter->current_time.tv_usec -
        	iter->start_time.tv_usec)) / 1000;
	
	if ((iter->hn_app_pixbuf_anim_blinker->stopped) ||
	    (elapsed > iter->length &&
	     iter->hn_app_pixbuf_anim_blinker->length != -1))
	  return iter->hn_app_pixbuf_anim_blinker->pixbuf;

	gdk_pixbuf_fill (iter->hn_app_pixbuf_anim_blinker->blended,
		0x00000000);
	/* Use period * 2 and 512 so that alpha pulses down as well as up */
	alpha = MIN (((elapsed % (period*2)) * 511) / (period*2), 511);
	if (alpha > 255) alpha = 511-alpha;
        if (alpha < 0) alpha = 0;
	gdk_pixbuf_composite (iter->hn_app_pixbuf_anim_blinker->pixbuf,
		iter->hn_app_pixbuf_anim_blinker->blended,
		0, 0,
		gdk_pixbuf_get_width (iter->hn_app_pixbuf_anim_blinker->pixbuf),
		gdk_pixbuf_get_height (iter->hn_app_pixbuf_anim_blinker->pixbuf),
		0, 0,
		1, 1,
		GDK_INTERP_NEAREST,
		alpha);
	return iter->hn_app_pixbuf_anim_blinker->blended;
}

static gboolean
on_currently_loading_frame (GdkPixbufAnimationIter *anim_iter)
{
	return FALSE;
}


/* vals in millisecs, length = -1 for infinity */
GdkPixbufAnimation *
hn_app_pixbuf_anim_blinker_new (GdkPixbuf *pixbuf, gint period, gint length,
				gint frequency)
{
  HNAppPixbufAnimBlinker *anim;

  anim = g_object_new (TYPE_HN_APP_PIXBUF_ANIM_BLINKER, NULL);
  anim->pixbuf = g_object_ref (pixbuf);
  anim->period = period;
  anim->length = length;
  anim->frequency = frequency;
  anim->blended = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
  	gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf));

  return GDK_PIXBUF_ANIMATION (anim);
}

void
hn_app_pixbuf_anim_blinker_stop (HNAppPixbufAnimBlinker *anim)
{
	anim->stopped = TRUE;
}

void
hn_app_pixbuf_anim_blinker_restart (HNAppPixbufAnimBlinker *anim)
{
	g_warning ("Restarting blinking unimplemented");
}

