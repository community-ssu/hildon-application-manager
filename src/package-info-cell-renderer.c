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

#include <gtk/gtkwidget.h>
#include <gtk/gtkcellrenderertext.h>

#include "package-info-cell-renderer.h"

#define DEFAULT_ICON_SIZE 30
#define DEFAULT_MARGIN 6

static GObjectClass *parent_class = NULL;

enum {
  PROP_ZERO,
  PROP_PKG_NAME,
  PROP_PKG_VERSION,
  PROP_PKG_SIZE,
  PROP_PKG_DESCRIPTION
};


typedef struct _PackageInfoCellRendererPrivate PackageInfoCellRendererPrivate;

struct _PackageInfoCellRendererPrivate
{
  gchar *pkg_name;
  gchar *pkg_version;
  gchar *pkg_size;
  gchar *pkg_description;

  gint single_line_height;
  gint double_line_height;

  PangoAttrList *scale_medium_attr_list;
  PangoAttrList *scale_small_attr_list;
};

#define PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE(o)	\
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TYPE_PACKAGE_INFO_CELL_RENDERER, PackageInfoCellRendererPrivate))

/* static guint signals[LAST_SIGNAL] = {0}; */

/* static functions: GObject */
static void package_info_cell_renderer_instance_init (GTypeInstance *instance, gpointer g_class);
static void package_info_cell_renderer_finalize      (GObject *object);
static void package_info_cell_renderer_class_init    (PackageInfoCellRendererClass *klass);

static void package_info_cell_renderer_get_property (GObject              *object,
                                                     guint                param_id,
                                                     GValue               *value,
                                                     GParamSpec           *pspec);

static void package_info_cell_renderer_set_property (GObject              *object,
                                                     guint                param_id,
                                                     const GValue         *value,
                                                     GParamSpec           *pspec);

/* static functions: GtkCellRenderer */
static void package_info_cell_renderer_get_size     (GtkCellRenderer      *cell,
                                                     GtkWidget            *widget,
                                                     GdkRectangle         *rectangle,
                                                     gint                 *x_offset,
                                                     gint                 *y_offset,
                                                     gint                 *width,
                                                     gint                 *height);

static void package_info_cell_renderer_render       (GtkCellRenderer      *cell,
                                                     GdkDrawable          *window,
                                                     GtkWidget            *widget,
                                                     GdkRectangle         *background_area,
                                                     GdkRectangle         *cell_area,
                                                     GdkRectangle         *expose_area,
                                                     GtkCellRendererState flags);


/**
 * package_info_cell_renderer_new:
 *
 * Return value: a new #PackageInfoCellRenderer instance implemented for Gtk+
 **/
GtkCellRenderer*
package_info_cell_renderer_new (void)
{
  PackageInfoCellRenderer *self = g_object_new (TYPE_PACKAGE_INFO_CELL_RENDERER, NULL);

  return GTK_CELL_RENDERER (self);
}

static void
package_info_cell_renderer_instance_init (GTypeInstance *instance, gpointer g_class)
{
  PackageInfoCellRendererPrivate *priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (instance);
  PangoAttribute *normal_attr, *small_attr;

  priv->pkg_name = NULL;
  priv->pkg_version = NULL;
  priv->pkg_size = NULL;
  priv->pkg_description = NULL;

  priv->single_line_height = -1;
  priv->double_line_height = -1;

  normal_attr = pango_attr_scale_new (PANGO_SCALE_MEDIUM);
  normal_attr->start_index = 0;
  normal_attr->end_index = G_MAXINT;

  small_attr = pango_attr_scale_new (PANGO_SCALE_SMALL);
  small_attr->start_index = 0;
  small_attr->end_index = G_MAXINT;

  priv->scale_medium_attr_list = pango_attr_list_new ();
  pango_attr_list_insert (priv->scale_medium_attr_list,
                          normal_attr);

  priv->scale_small_attr_list = pango_attr_list_new ();
  pango_attr_list_insert (priv->scale_small_attr_list,
                          small_attr);

  return;
}

static void
package_info_cell_renderer_finalize (GObject *object)
{
  PackageInfoCellRendererPrivate *priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (object);

  if (priv->pkg_name)
    g_free (priv->pkg_name);

  if (priv->pkg_version)
    g_free (priv->pkg_version);

  if (priv->pkg_size)
    g_free (priv->pkg_size);

  if (priv->pkg_description)
    g_free (priv->pkg_description);

  pango_attr_list_unref (priv->scale_medium_attr_list);
  pango_attr_list_unref (priv->scale_small_attr_list);

  (*parent_class->finalize) (object);

  return;
}

static void
package_info_cell_renderer_class_init (PackageInfoCellRendererClass *klass)
{
  GObjectClass *object_class;
  GtkCellRendererClass *renderer_class;

  parent_class = g_type_class_peek_parent (klass);
  object_class = (GObjectClass*) klass;
  renderer_class = (GtkCellRendererClass*) klass;

  object_class->finalize = package_info_cell_renderer_finalize;
  object_class->get_property = package_info_cell_renderer_get_property;
  object_class->set_property = package_info_cell_renderer_set_property;

  renderer_class->get_size = package_info_cell_renderer_get_size;
  renderer_class->render = package_info_cell_renderer_render;

  g_object_class_install_property (object_class,
                                   PROP_PKG_NAME,
                                   g_param_spec_string ("package-name",
                                                        "Package Name",
                                                        "The name of the package",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property (object_class,
                                   PROP_PKG_VERSION,
                                   g_param_spec_string ("package-version",
                                                        "Package version",
                                                        "The version of the package",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property (object_class,
                                   PROP_PKG_SIZE,
                                   g_param_spec_string ("package-size",
                                                        "Package size",
                                                        "The size of the package",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property (object_class,
                                   PROP_PKG_DESCRIPTION,
                                   g_param_spec_string ("package-description",
                                                        "Package description",
                                                        "The description of the package",
                                                        NULL,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_type_class_add_private (object_class, sizeof (PackageInfoCellRendererPrivate));

  return;
}

static void
package_info_cell_renderer_get_property (GObject              *object,
                                         guint                param_id,
                                         GValue               *value,
                                         GParamSpec           *pspec)
{
  PackageInfoCellRendererPrivate *priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (object);

  switch (param_id)
  {
  case PROP_PKG_NAME:
    g_value_set_string (value, priv->pkg_name);
    break;
  case PROP_PKG_VERSION:
    g_value_set_string (value, priv->pkg_version);
    break;
  case PROP_PKG_SIZE:
    g_value_set_string (value, priv->pkg_size);
    break;
  case PROP_PKG_DESCRIPTION:
    g_value_set_string (value, priv->pkg_description);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
    break;
  }
}

static void
package_info_cell_renderer_set_property (GObject              *object,
                                         guint                param_id,
                                         const GValue         *value,
                                         GParamSpec           *pspec)
{
  PackageInfoCellRendererPrivate *priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (object);

  switch (param_id)
    {
    case PROP_PKG_NAME:
      if (priv->pkg_name)
        g_free (priv->pkg_name);
      priv->pkg_name = g_value_dup_string (value);
      break;
    case PROP_PKG_VERSION:
      if (priv->pkg_version)
        g_free (priv->pkg_version);
      priv->pkg_version = g_value_dup_string (value);
      break;
    case PROP_PKG_SIZE:
      if (priv->pkg_size)
        g_free (priv->pkg_size);
      priv->pkg_size = g_value_dup_string (value);
      break;
    case PROP_PKG_DESCRIPTION:
      if (priv->pkg_description)
        g_free (priv->pkg_description);
      priv->pkg_description = g_value_dup_string (value);
      if (priv->pkg_description != NULL)
        g_strstrip (priv->pkg_description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

GType
package_info_cell_renderer_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY(type == 0))
    {
      static const GTypeInfo info =
        {
            sizeof (PackageInfoCellRendererClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            (GClassInitFunc) package_info_cell_renderer_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof (PackageInfoCellRenderer),
            0,      /* n_preallocs */
            package_info_cell_renderer_instance_init    /* instance_init */
        };

      type = g_type_register_static (GTK_TYPE_CELL_RENDERER,
                                     "PackageInfoCellRenderer",
                                     &info, 0);
    }

  return type;
}

static void
package_info_cell_renderer_get_size     (GtkCellRenderer      *cell,
                                         GtkWidget            *widget,
                                         GdkRectangle         *rectangle,
                                         gint                 *x_offset,
                                         gint                 *y_offset,
                                         gint                 *width,
                                         gint                 *height)

{
  PackageInfoCellRendererPrivate *priv;

  priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (cell);

  /* only height will be set */
  if (height)
    {
      PangoContext *context;
      PangoFontMetrics *metrics;
      PangoFontDescription *font_desc;
      gint row_height;

      if (priv->single_line_height < 0)
        {
          font_desc = pango_font_description_copy_static (widget->style->font_desc);

          /* one line, PANGO_SCALE_MEDIUM */
          pango_font_description_set_size (font_desc,
              PANGO_SCALE_MEDIUM * pango_font_description_get_size (font_desc));

          context = gtk_widget_get_pango_context (widget);

          metrics = pango_context_get_metrics (context,
              font_desc,
              pango_context_get_language (context));

          row_height = (pango_font_metrics_get_ascent (metrics) +
              pango_font_metrics_get_descent (metrics));

          pango_font_metrics_unref (metrics);
          pango_font_description_free (font_desc);

          priv->single_line_height = 2 * cell->ypad + PANGO_PIXELS (row_height);
        }

      if ((priv->pkg_description == NULL) || (priv->pkg_description[0] == '\0'))
        {
          *height = priv->single_line_height;
          return;
        }
      else
        {
          if (priv->double_line_height < 0)
            {
              font_desc = pango_font_description_copy_static (widget->style->font_desc);

              /* two lines, PANGO_SCALE_MEDIUM and PANGO_SCALE_SMALL*/
              pango_font_description_set_size (font_desc,
                  PANGO_SCALE_SMALL * pango_font_description_get_size (font_desc));

              context = gtk_widget_get_pango_context (widget);

              metrics = pango_context_get_metrics (context,
                  font_desc,
                  pango_context_get_language (context));

              row_height = (pango_font_metrics_get_ascent (metrics) +
                  pango_font_metrics_get_descent (metrics));

              pango_font_metrics_unref (metrics);
              pango_font_description_free (font_desc);

              priv->double_line_height = priv->single_line_height + cell->ypad + PANGO_PIXELS (row_height);
            }

          *height = priv->double_line_height;
          return;
        }
    }
}

static GtkStateType
cell_get_state (GtkCellRenderer *cell,
                GtkWidget *widget, GtkCellRendererState flags)
{
  GtkStateType state;

  if (!cell->sensitive)
    {
      state = GTK_STATE_INSENSITIVE;
    }
  else if ((flags & GTK_CELL_RENDERER_SELECTED) == GTK_CELL_RENDERER_SELECTED)
    {
      if (GTK_WIDGET_HAS_FOCUS (widget))
        state = GTK_STATE_SELECTED;
      else
        state = GTK_STATE_ACTIVE;
    }
  else if ((flags & GTK_CELL_RENDERER_PRELIT) == GTK_CELL_RENDERER_PRELIT &&
	   GTK_WIDGET_STATE (widget) == GTK_STATE_PRELIGHT)
    {
#ifdef MAEMO_CHANGES
      state = GTK_STATE_NORMAL;
#else
      state = GTK_STATE_PRELIGHT;
#endif /* MAEMO_CHANGES */
    }
  else
    {
      if (GTK_WIDGET_STATE (widget) == GTK_STATE_INSENSITIVE)
        state = GTK_STATE_INSENSITIVE;
      else
        state = GTK_STATE_NORMAL;
    }

  return state;
}

static PangoLayout *
maybe_make_layout (GtkWidget *widget, 
      	      	   char *str, 
		   PangoAttrList *attrs, 
		   PangoAlignment align, 
		   gint *p_width, 
		   gint *p_height)
{
  PangoLayout *layout = NULL;

  if (str && str[0] != '\0') {
    layout = gtk_widget_create_pango_layout (widget, str);
    pango_layout_set_attributes (layout, attrs);
    pango_layout_get_pixel_size (layout, p_width, p_height);
    pango_layout_set_alignment (layout, align);
  }

  return layout;
}

static void
paint_row (PangoLayout *left_layout,
      	   int left_width, int left_height,
      	   PangoLayout *right_layout,
      	   int right_width, int right_height,
	   PangoAttrList *attrs,
	   GtkCellRenderer *cell,
	   GdkDrawable *window,
	   GtkWidget *widget,
	   GdkRectangle *cell_area,
	   GdkRectangle *expose_area,
	   GtkStateType state,
	   int y_coord,
	   gboolean is_above_offset)
{
  gint available_width;

  available_width = cell_area->width - 2 * DEFAULT_MARGIN;

  if (left_width + right_width > available_width)
    {
      if (left_width < 2 * available_width / 3)
        right_width = available_width - left_width;
      else if (right_width < available_width / 3)
        left_width = available_width - right_width;
      else
        {
          left_width = 2 * available_width / 3;
          right_width = available_width / 3;
        }
    }

  if (left_layout) {
    pango_layout_set_ellipsize (left_layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_width (left_layout, left_width * PANGO_SCALE);

    gtk_paint_layout (widget->style,
                      window,
                      state,
                      TRUE,
                      expose_area,
                      widget,
                      "cellrenderertext",
                      cell_area->x + DEFAULT_MARGIN,
                      y_coord - (is_above_offset ? left_height : 0),
                      left_layout);

    g_object_unref (left_layout);
  }

  if (right_layout) {
    pango_layout_set_ellipsize (right_layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_width (right_layout, right_width * PANGO_SCALE);

    gtk_paint_layout (widget->style,
                      window,
                      state,
                      TRUE,
                      expose_area,
                      widget,
                      "cellrenderertext",
                      cell_area->x + cell_area->width - right_width,
                      y_coord - (is_above_offset ? right_height : 0),
                      right_layout);

    g_object_unref (right_layout);
  }
}

static void
package_info_cell_renderer_render       (GtkCellRenderer      *cell,
                                         GdkDrawable          *window,
                                         GtkWidget            *widget,
                                         GdkRectangle         *background_area,
                                         GdkRectangle         *cell_area,
                                         GdkRectangle         *expose_area,
                                         GtkCellRendererState flags)
{
  /* example code from eog-pixbuf-cell-renderer.c : */
  PackageInfoCellRendererPrivate *priv;
  GtkStateType state;
  PangoLayout *name = NULL, *version = NULL, *description = NULL, *size = NULL;
  int name_w = 0,     	 name_h = 0,
      version_w = 0,  	 version_h = 0,
      description_w = 0, description_h = 0,
      size_w = 0,     	 size_h = 0,
      y_coord, max_top, max_bot;

  priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (cell);

  state = cell_get_state (cell, widget, flags);

  name = maybe_make_layout(widget, 
      	      	      	   priv->pkg_name, 
			   priv->scale_medium_attr_list, 
			   PANGO_ALIGN_LEFT, 
			   &name_w, &name_h);

  version = maybe_make_layout(widget, 
      	       	      	      priv->pkg_version, 
			      priv->scale_medium_attr_list, 
			      PANGO_ALIGN_LEFT, 
			      &version_w, &version_h);

  description = maybe_make_layout(widget, 
      	       	      	      	  priv->pkg_description, 
			      	  priv->scale_small_attr_list, 
			      	  PANGO_ALIGN_LEFT, 
			      	  &description_w, &description_h);

  size = maybe_make_layout(widget, 
      	       	      	   priv->pkg_size, 
			   priv->scale_small_attr_list, 
			   PANGO_ALIGN_LEFT, 
			   &size_w, &size_h);

  max_top = MAX(name_h, version_h);
  max_bot = MAX(description_h, size_h);
  y_coord = cell_area->y + (cell_area->height - (max_top + max_bot)) / 2 + max_top;

  paint_row (name, name_w, name_h,
      	     version, version_w, version_h,
	     priv->scale_medium_attr_list, 
	     cell, window, widget, 
	     cell_area, expose_area, 
	     state, y_coord, TRUE);

  paint_row (description, description_w, description_h,
      	     size, size_w, size_h,
	     priv->scale_small_attr_list, 
	     cell, window, widget, 
	     cell_area, expose_area, 
	     state, y_coord, FALSE);
}

static void
style_set (PackageInfoCellRenderer *cr,
      	   GtkStyle *old_style,
	   GtkWidget *widget)
{
  GtkStyle *style = gtk_widget_get_style(widget);

  if (style)
    {
      GdkColor clr;

      if (gtk_style_lookup_color (style, "SecondaryTextColor", &clr))
      	{
	  PackageInfoCellRendererPrivate *priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (cr);

      	  if (priv->scale_small_attr_list)
	    {
	      PangoAttribute *small_attr, *clr_attr = NULL;

	      small_attr = pango_attr_scale_new (PANGO_SCALE_SMALL);
	      small_attr->start_index = 0;
	      small_attr->end_index = G_MAXINT;

	      clr_attr = pango_attr_foreground_new (clr.red,
	      	      	      	      	      	    clr.green,
						    clr.blue);
	      clr_attr->start_index = 0;
	      clr_attr->end_index = G_MAXINT;

	      pango_attr_list_unref (priv->scale_small_attr_list);
	      priv->scale_small_attr_list = pango_attr_list_new ();
	      pango_attr_list_insert (priv->scale_small_attr_list,
	      	      	      	      small_attr);
	      pango_attr_list_insert (priv->scale_small_attr_list,
	      	      	      	      clr_attr);
	    }
	}
    }
}

void
package_info_cell_renderer_listen_style (PackageInfoCellRenderer *cr,
      	      	      	      	      	 GtkWidget *widget)
{
  style_set(cr, NULL, widget);
  g_signal_connect_swapped(G_OBJECT(widget), "style-set", (GCallback)style_set, cr);
}
