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

/* /\* signals *\/ */
/* enum { */
/* 	LAST_SIGNAL */
/* }; */

enum {
        PROP_ZERO,
        PROP_PIXBUF,
        PROP_PIXBUF_SIZE,
        PROP_PKG_NAME,
        PROP_PKG_VERSION,
        PROP_PKG_DESCRIPTION
};


typedef struct _PackageInfoCellRendererPrivate PackageInfoCellRendererPrivate;

struct _PackageInfoCellRendererPrivate
{
  GdkPixbuf *pixbuf;
  guint pixbuf_size;
  gchar *pkg_name;
  gchar *pkg_version;
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

  priv->pixbuf = NULL;
  priv->pkg_name = NULL;
  priv->pkg_version = NULL;
  priv->pkg_description = NULL;
  priv->pixbuf_size = DEFAULT_ICON_SIZE;

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

  if (priv->pixbuf)
    g_object_unref (priv->pixbuf);

  g_free (priv->pkg_name);
  g_free (priv->pkg_version);
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
                                   PROP_PIXBUF,
                                   g_param_spec_object ("pixbuf",
                                                        "Pixbuf Object",
                                                        "The package's icon",
                                                        GDK_TYPE_PIXBUF,
                                                        (G_PARAM_READABLE | G_PARAM_WRITABLE)));
  
  g_object_class_install_property (object_class,
                                   PROP_PIXBUF_SIZE,
                                   g_param_spec_uint ("pixbuf-size",
                                                      "Size",
                                                      "The size of the rendered icon",
                                                      0,
                                                      G_MAXUINT,
                                                      DEFAULT_ICON_SIZE,
                                                      (G_PARAM_READABLE | G_PARAM_WRITABLE)));

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
  case PROP_PIXBUF:
    g_value_set_object (value, G_OBJECT (priv->pixbuf));
    break;
  case PROP_PIXBUF_SIZE:
    g_value_set_uint (value, priv->pixbuf_size);
    break;
  case PROP_PKG_NAME:
    g_value_set_string (value, priv->pkg_name);
    break;
  case PROP_PKG_VERSION:
    g_value_set_string (value, priv->pkg_version);
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
  const GdkPixbuf* px;
  
  switch (param_id)
    {
    case PROP_PIXBUF:
      
      if (priv->pixbuf)
        g_object_unref (priv->pixbuf);
      
      px = (GdkPixbuf*) g_value_get_object (value);
      gint px_w = gdk_pixbuf_get_width (px);
      gint px_h = gdk_pixbuf_get_height (px);
      if (px_w > priv->pixbuf_size || px_h > priv->pixbuf_size)
        {
          priv->pixbuf = gdk_pixbuf_scale_simple (px, priv->pixbuf_size, priv->pixbuf_size, GDK_INTERP_BILINEAR);
        }
      else
        {
          priv->pixbuf = (GdkPixbuf*) g_value_get_object (value);
          g_object_ref (priv->pixbuf);
        }
      break;
    case PROP_PIXBUF_SIZE:
      priv->pixbuf_size = g_value_get_uint (value);
      break;
    case PROP_PKG_NAME:
      priv->pkg_name = g_value_dup_string (value);
      break;
    case PROP_PKG_VERSION:
      priv->pkg_version = g_value_dup_string (value);
      break;
    case PROP_PKG_DESCRIPTION:
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
  PackageInfoCellRendererPrivate *priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (cell);
  
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
          priv->single_line_height = 2*cell->ypad + MAX (PANGO_PIXELS (row_height), priv->pixbuf_size);
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

static void 
package_info_cell_renderer_render       (GtkCellRenderer      *cell,
                                         GdkDrawable          *window,
                                         GtkWidget            *widget,
                                         GdkRectangle         *background_area,
                                         GdkRectangle         *cell_area,
                                         GdkRectangle         *expose_area,
                                         GtkCellRendererState flags)
{
  PackageInfoCellRendererPrivate *priv = PACKAGE_INFO_CELL_RENDERER_GET_PRIVATE (cell);

  /* example code from eog-pixbuf-cell-renderer.c : */
  cairo_t *cr;
  gint name_w = 0;
  gint version_w = 0;
  gint h = 0;
  gint tmp_h = 0;
  gint available_width;
  PangoLayout *name_layout, *version_layout, *description_layout;
  
  cr = gdk_cairo_create (GDK_DRAWABLE (window));
  name_layout = gtk_widget_create_pango_layout (widget, NULL);
  version_layout = gtk_widget_create_pango_layout (widget, NULL);

  cairo_save (cr);
  cairo_set_source_rgb (cr,
                        widget->style->fg[widget->state].red / 65535.0,
                        widget->style->fg[widget->state].green / 65535.0,
                        widget->style->fg[widget->state].blue / 65535.0);

  if (priv->pkg_name)
    {
      pango_layout_set_text (name_layout, priv->pkg_name, -1);
      pango_layout_get_pixel_size (name_layout, &name_w, &tmp_h);
      pango_layout_set_attributes (name_layout, priv->scale_medium_attr_list);
      h = tmp_h;
    }
  if (priv->pkg_version)
    {
      pango_layout_set_alignment (version_layout, PANGO_ALIGN_RIGHT);
      pango_layout_set_text (version_layout, priv->pkg_version, -1);
      pango_layout_set_attributes (version_layout, priv->scale_medium_attr_list);
      pango_layout_get_pixel_size (version_layout, &version_w, &tmp_h);
    }
  h = MAX (h, tmp_h);

  available_width = cell_area->width - 2*DEFAULT_MARGIN - priv->pixbuf_size;

  if (name_w + version_w > available_width)
    {
      if (name_w < 2*available_width/3)
        version_w = available_width - name_w;
      else if (version_w < available_width/3)
        name_w = available_width - version_w;
      else
        {
          name_w = 2*available_width/3;
          version_w = available_width/3;
        }
      
      pango_layout_set_ellipsize (name_layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_width (name_layout, name_w * PANGO_SCALE);
      pango_layout_set_ellipsize (version_layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_width (version_layout, version_w * PANGO_SCALE);
    }

  if (priv->pkg_name)
    {
      cairo_move_to(cr, cell_area->x + priv->pixbuf_size + DEFAULT_MARGIN, cell_area->y);
      pango_cairo_show_layout (cr, name_layout);
      cairo_stroke (cr);
    }
  if (priv->pkg_version)
    {
      cairo_move_to(cr, cell_area->x + cell_area->width - version_w, cell_area->y);
      pango_cairo_show_layout (cr, version_layout);
      cairo_stroke (cr);
    }

  if (priv->pkg_description != NULL && 
      priv->pkg_description[0] != '\0' && 
      (flags & GTK_CELL_RENDERER_SELECTED) != 0) 
    {
      description_layout = gtk_widget_create_pango_layout (widget, NULL);
      pango_layout_set_text (description_layout, priv->pkg_description, -1);
      pango_layout_set_attributes (description_layout, priv->scale_small_attr_list);
      pango_layout_set_ellipsize (description_layout, PANGO_ELLIPSIZE_END);
      pango_layout_set_width (description_layout, 
                              (cell_area->width - priv->pixbuf_size - cell->ypad) * PANGO_SCALE);
      cairo_move_to(cr, cell_area->x + priv->pixbuf_size + DEFAULT_MARGIN, cell_area->y + h);
      pango_cairo_show_layout (cr, description_layout);
      cairo_stroke (cr);
      g_object_unref (description_layout);
    }

  if (priv->pixbuf)
    {
      cairo_new_path (cr);
      gdk_cairo_set_source_pixbuf (cr, priv->pixbuf, 
                                   (double) cell_area->x, 
                                   (double) cell_area->y); // 
      cairo_move_to(cr, cell_area->x, 
                    cell_area->y);
      cairo_paint (cr);
    }

  cairo_restore (cr);
  g_object_unref (name_layout);
  g_object_unref (version_layout);
  cairo_destroy (cr);
}
