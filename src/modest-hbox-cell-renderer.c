/* This file is part of the hildon-application-manager.
 * 
 * Parts of this file are derived from Modest.
 * 
 * Modest's legal notice:
 * Copyright (c) 2007, 2008 Nokia Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of the Nokia Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <gtk/gtkwidget.h>
#include <gtk/gtkcellrenderertext.h>

#include <modest-hbox-cell-renderer.h>

#define RENDERER_EXPAND_ATTRIBUTE "box-expand"

static GObjectClass *parent_class = NULL;

/* /\* signals *\/ */
/* enum { */
/* 	LAST_SIGNAL */
/* }; */

typedef struct _ModestHBoxCellRendererPrivate ModestHBoxCellRendererPrivate;

struct _ModestHBoxCellRendererPrivate
{
	GList *renderers_list;
};

#define MODEST_HBOX_CELL_RENDERER_GET_PRIVATE(o)	\
	(G_TYPE_INSTANCE_GET_PRIVATE ((o), MODEST_TYPE_HBOX_CELL_RENDERER, ModestHBoxCellRendererPrivate))

/* static guint signals[LAST_SIGNAL] = {0}; */

/* static functions: GObject */
static void modest_hbox_cell_renderer_instance_init (GTypeInstance *instance, gpointer g_class);
static void modest_hbox_cell_renderer_finalize (GObject *object);
static void modest_hbox_cell_renderer_class_init (ModestHBoxCellRendererClass *klass);

/* static functions: GtkCellRenderer */
static void modest_hbox_cell_renderer_get_size     (GtkCellRenderer       *cell,
						    GtkWidget             *widget,
						    GdkRectangle          *rectangle,
						    gint                  *x_offset,
						    gint                  *y_offset,
						    gint                  *width,
						    gint                  *height);
static void modest_hbox_cell_renderer_render       (GtkCellRenderer       *cell,
						    GdkDrawable           *window,
						    GtkWidget             *widget,
						    GdkRectangle          *background_area,
						    GdkRectangle          *cell_area,
						    GdkRectangle          *expose_area,
						    GtkCellRendererState  flags);
						

/**
 * modest_hbox_cell_renderer_new:
 *
 * Return value: a new #ModestHBoxCellRenderer instance implemented for Gtk+
 **/
GtkCellRenderer*
modest_hbox_cell_renderer_new (void)
{
	ModestHBoxCellRenderer *self = g_object_new (MODEST_TYPE_HBOX_CELL_RENDERER, NULL);

	return GTK_CELL_RENDERER (self);
}

static void
modest_hbox_cell_renderer_instance_init (GTypeInstance *instance, gpointer g_class)
{
	ModestHBoxCellRendererPrivate *priv = MODEST_HBOX_CELL_RENDERER_GET_PRIVATE (instance);

	priv->renderers_list = NULL;
	
	return;
}

static void
modest_hbox_cell_renderer_finalize (GObject *object)
{
	ModestHBoxCellRendererPrivate *priv = MODEST_HBOX_CELL_RENDERER_GET_PRIVATE (object);

	if (priv->renderers_list != NULL) {
		g_list_foreach (priv->renderers_list, (GFunc) g_object_unref, NULL);
		g_list_free (priv->renderers_list);
		priv->renderers_list = NULL;
	}

	(*parent_class->finalize) (object);

	return;
}

static void 
modest_hbox_cell_renderer_class_init (ModestHBoxCellRendererClass *klass)
{
	GObjectClass *object_class;
	GtkCellRendererClass *renderer_class;

	parent_class = g_type_class_peek_parent (klass);
	object_class = (GObjectClass*) klass;
	renderer_class = (GtkCellRendererClass*) klass;

	object_class->finalize = modest_hbox_cell_renderer_finalize;
	renderer_class->get_size = modest_hbox_cell_renderer_get_size;
	renderer_class->render = modest_hbox_cell_renderer_render;

	g_type_class_add_private (object_class, sizeof (ModestHBoxCellRendererPrivate));

	return;
}

GType 
modest_hbox_cell_renderer_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0))
	{
		static const GTypeInfo info = 
		{
		  sizeof (ModestHBoxCellRendererClass),
		  NULL,   /* base_init */
		  NULL,   /* base_finalize */
		  (GClassInitFunc) modest_hbox_cell_renderer_class_init,   /* class_init */
		  NULL,   /* class_finalize */
		  NULL,   /* class_data */
		  sizeof (ModestHBoxCellRenderer),
		  0,      /* n_preallocs */
		  modest_hbox_cell_renderer_instance_init    /* instance_init */
		};

		type = g_type_register_static (GTK_TYPE_CELL_RENDERER,
			"ModestHBoxCellRenderer",
			&info, 0);

	}

	return type;
}


/**
 * modest_hbox_cell_renderer_append:
 * @hbox_renderer: a #ModestHBoxCellRenderer
 * @cell: a #GtkCellRenderer
 *
 * Appends @cell to the end of the list of renderers shown in @hbox_renderer
 */
void 
modest_hbox_cell_renderer_append (ModestHBoxCellRenderer *hbox_renderer, 
				  GtkCellRenderer *cell,
				  gboolean expand)
{
	ModestHBoxCellRendererPrivate *priv = MODEST_HBOX_CELL_RENDERER_GET_PRIVATE (hbox_renderer);
	
	priv->renderers_list = g_list_append (priv->renderers_list, cell);
	g_object_set_data (G_OBJECT (cell), RENDERER_EXPAND_ATTRIBUTE, GINT_TO_POINTER (expand));
	
#if GLIB_CHECK_VERSION(2, 10, 0) /* g_object_ref_sink() was added in glib 2.10: */
	g_object_ref_sink (G_OBJECT (cell));
#else
	g_object_ref (G_OBJECT (cell));
	gtk_object_sink (GTK_OBJECT (cell));
#endif
}

static void 
modest_hbox_cell_renderer_get_size     (GtkCellRenderer       *cell,
					GtkWidget             *widget,
					GdkRectangle          *rectangle,
					gint                  *x_offset,
					gint                  *y_offset,
					gint                  *width,
					gint                  *height)
{
	gint calc_width, calc_height;
	gint full_width, full_height;
	GList *node;
	ModestHBoxCellRendererPrivate *priv = MODEST_HBOX_CELL_RENDERER_GET_PRIVATE (cell);

	calc_width = 0;
	calc_height = 0;

	for (node = priv->renderers_list; node != NULL; node = g_list_next (node)) {
		gint renderer_width, renderer_height;
		GtkCellRenderer *renderer = (GtkCellRenderer *) node->data;

		gtk_cell_renderer_get_size (renderer, widget, NULL, NULL, NULL,
					    &renderer_width, &renderer_height);
		if ((renderer_width > 0)&&(renderer_height > 0)) {
			calc_height = MAX (calc_height, renderer_height);
			calc_width += renderer_width;
		}
	}

	full_width = (gint) cell->xpad * 2 + calc_width;
	full_height = (gint) cell->ypad * 2 + calc_height;

	if (rectangle && calc_width > 0 && calc_height > 0) {
		if (x_offset) {
			*x_offset = (((gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL) ?
				      (1.0 - cell->xalign) : cell->xalign) *
				     (rectangle->width - full_width));
			*x_offset = MAX (*x_offset, 0);
		}
		if (y_offset) {
			*y_offset = ((cell->yalign) *
				     (rectangle->height - full_height));
			*y_offset = MAX (*y_offset, 0);
		}
	} else {
		if (x_offset)
			*x_offset = 0;
		if (y_offset)
			*y_offset = 0;
	}

	if (width)
		*width = full_width;
	if (height)
		*height = full_height;
}

static void 
modest_hbox_cell_renderer_render       (GtkCellRenderer       *cell,
					GdkDrawable           *window,
					GtkWidget             *widget,
					GdkRectangle          *background_area,
					GdkRectangle          *cell_area,
					GdkRectangle          *expose_area,
					GtkCellRendererState  flags)
{
	ModestHBoxCellRendererPrivate *priv = MODEST_HBOX_CELL_RENDERER_GET_PRIVATE (cell);
	gint nvis_children = 0;
	gint nexpand_children = 0;
	gint nfat_children = 0;
	GtkTextDirection direction;
	GList *node = NULL;
	GtkCellRenderer *child;
	gint width, extra;
	
	direction = gtk_widget_get_direction (widget);

	/* Counts visible and expandable children cell renderers */
	for (node = priv->renderers_list; node != NULL; node = g_list_next (node)) {
		gboolean visible, expand;
		gint req_width = 0;
		child = (GtkCellRenderer *) node->data;
		g_object_get (G_OBJECT (child), "visible", &visible, NULL);
		expand = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (child), RENDERER_EXPAND_ATTRIBUTE));
		gtk_cell_renderer_get_size (child, widget, NULL, NULL, NULL, &req_width, NULL);

		if (visible) {
			nvis_children++;
			if (expand) {
				nexpand_children++;
				if (req_width > cell_area->width/2)
					nfat_children ++;
			}
		}
	}

	if (nvis_children > 0) {
		gint x_pad, y_pad;
		gint x;
		GdkRectangle child_alloc;

		if (nexpand_children > 0) {
			GtkRequisition req;

			/* retrieve the requisition of the children cell renderers */
			modest_hbox_cell_renderer_get_size (cell, widget, NULL, NULL, NULL, &(req.width), &(req.height));
			width = cell_area->width - req.width;
			extra = width / nexpand_children;
		} else {
			width = 0;
			extra = 0;
		}

		g_object_get (cell, "xpad", &x_pad, "ypad", &y_pad, NULL);
		x = cell_area->x + x_pad;
		child_alloc.y = cell_area->y + y_pad;
		child_alloc.height = MAX (1, cell_area->height - y_pad * 2);

		for (node = priv->renderers_list; node != NULL; node = g_list_next (node)) {
			gboolean visible, expand;

			child = (GtkCellRenderer *) node->data;
			g_object_get (G_OBJECT (child), "visible", &visible, NULL);
			expand = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (child), RENDERER_EXPAND_ATTRIBUTE));

			if (visible) {
				GtkRequisition child_req;
				gint child_xpad, child_ypad;
				GdkRectangle child_expose_area;

				gtk_cell_renderer_get_size (child, widget, NULL, NULL, NULL, &(child_req.width), &(child_req.height));
				g_object_get (child, "xpad", &child_xpad, "ypad", &child_ypad, NULL);

				if (expand) {
					if (GTK_IS_CELL_RENDERER_TEXT (child) && width < 0) {
						/* The fattest renderers get slimmed down */
						if (child_req.width > cell_area->width/2) {
							child_req.width += width/nfat_children;
							width -= width/nfat_children;
							nfat_children--;
							g_object_set (child, 
							              "width", child_req.width, 
							              "ellipsize-set", TRUE, 
							              "ellipsize", PANGO_ELLIPSIZE_END, 
							              NULL);
						} else if (nfat_children == 0) {
							child_req.width += extra;
							width -= extra;
							g_object_set (child,
							              "width", child_req.width,
							              "ellipsize-set", TRUE,
							              "ellipsize", PANGO_ELLIPSIZE_END, 
							              NULL);
						}
					} else {
						child_req.width += extra;
						width -= extra;
					}
					nexpand_children --;
					if (nexpand_children != 0)
						extra = width/nexpand_children;
				}

				child_alloc.width = MAX (1, child_req.width);
				child_alloc.x = x;

				if (direction == GTK_TEXT_DIR_RTL)
					child_alloc.x = cell_area->x + cell_area->width - (child_alloc.x - cell_area->x) - child_alloc.width;

				if (gdk_rectangle_intersect (&child_alloc, expose_area, &child_expose_area))
					gtk_cell_renderer_render (child, window, widget, background_area, &child_alloc, &child_expose_area, flags);
				x += child_req.width;
			}
		}
	}
	
}
