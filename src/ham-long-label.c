/*
 * This file is part of hildon-application-manager
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contact: Marius Vollmer <marius.vollmer@nokia.com>
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

#include <gtk/gtk.h>
#include "ham-long-label.h"

static GObjectClass *parent_class = NULL;

typedef struct _HamLongLabelPrivate HamLongLabelPrivate;

struct _HamLongLabelPrivate
{
  GtkWidget *label;
  GtkButton *back_button;
  GtkButton *forth_button;
  GtkViewport *label_viewport;
};

#define HAM_LONG_LABEL_GET_PRIVATE(o)    \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), HAM_TYPE_LONG_LABEL, HamLongLabelPrivate))

/* static functions */
static void ham_long_label_class_init (HamLongLabelClass *klass);
static void ham_long_label_instance_init (GTypeInstance *instance, gpointer g_class);
static void ham_long_label_dispose (GObject *object);
static void ham_long_label_finalize (GObject *object);

static void ham_long_label_size_request (GtkWidget *widget, GtkRequisition *requisition);
static void ham_long_label_size_allocate (GtkWidget *widget, GtkAllocation *allocation);

static gboolean back_button_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static gboolean forth_button_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void update_button_sensitivity (GtkAdjustment *adjustment, gpointer user_data);

static void 
ham_long_label_class_init (HamLongLabelClass *klass)
{
  GObjectClass *object_class;
  GtkWidgetClass *widget_class;

  parent_class = g_type_class_peek_parent (klass);
  object_class = G_OBJECT_CLASS (klass);
  widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ham_long_label_dispose;
  object_class->finalize = ham_long_label_finalize;
  
  /* widget_class->realize = ham_long_label_realize; */
  widget_class->size_request = ham_long_label_size_request;
  widget_class->size_allocate = ham_long_label_size_allocate;

  g_type_class_add_private (object_class, sizeof (HamLongLabelPrivate));
  return;
}

static void
ham_long_label_instance_init (GTypeInstance *instance, gpointer g_class)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (instance);
  priv->label_viewport = GTK_VIEWPORT (gtk_viewport_new (NULL,
                                                         NULL));
  priv->label = gtk_label_new (NULL);
  gtk_label_set_selectable (GTK_LABEL (priv->label), FALSE);
  priv->back_button = GTK_BUTTON (gtk_button_new ());
  gtk_button_set_image (priv->back_button, 
                        gtk_image_new_from_stock (GTK_STOCK_GO_BACK, GTK_ICON_SIZE_SMALL_TOOLBAR));
  priv->forth_button = GTK_BUTTON (gtk_button_new ());
  gtk_button_set_image (priv->forth_button, 
                        gtk_image_new_from_stock (GTK_STOCK_GO_FORWARD, GTK_ICON_SIZE_SMALL_TOOLBAR));
  gtk_container_add (GTK_CONTAINER (priv->label_viewport), priv->label);
  gtk_viewport_set_shadow_type (priv->label_viewport, GTK_SHADOW_NONE);
  /* TODO : take RTL languaguages into account? */
  gtk_box_pack_start (GTK_BOX (instance), GTK_WIDGET (priv->back_button), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (instance), GTK_WIDGET (priv->label_viewport), TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (instance), GTK_WIDGET (priv->forth_button), FALSE, FALSE, 0);

  g_signal_connect (priv->back_button, "button-press-event",
                    (GCallback)back_button_cb, instance);
  g_signal_connect (priv->forth_button, "button-press-event",
                    (GCallback)forth_button_cb, instance);

  g_signal_connect (gtk_viewport_get_hadjustment (priv->label_viewport),
                    "changed",
                    (GCallback)update_button_sensitivity,
                    instance);
  g_signal_connect (gtk_viewport_get_hadjustment (priv->label_viewport),
                    "value-changed",
                    (GCallback)update_button_sensitivity,
                    instance);
  gtk_widget_show_all (GTK_WIDGET (instance));
}

static void
ham_long_label_dispose (GObject *object)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (object);
  
  /* clean private fields */
  if (priv->back_button)
    gtk_widget_destroy (GTK_WIDGET (priv->back_button));
  priv->back_button = NULL;
  
  if (priv->forth_button)
    gtk_widget_destroy (GTK_WIDGET (priv->forth_button));
  priv->forth_button = NULL;
  
  if (priv->label)
    gtk_widget_destroy (priv->label);
  priv->label = NULL;
  
  if (priv->label_viewport)
    gtk_widget_destroy (GTK_WIDGET (priv->label_viewport));
  priv->label_viewport = NULL;

  (*parent_class->dispose) (object);

  return;
}

static void
ham_long_label_finalize (GObject *object)
{
  (*parent_class->finalize) (object);  
}

GType 
ham_long_label_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY(type == 0))
    {
      static const GTypeInfo info = 
        {
         sizeof (HamLongLabelClass),
         NULL,   /* base_init */
         NULL,   /* base_finalize */
         (GClassInitFunc) ham_long_label_class_init,   /* class_init */
         NULL,   /* class_finalize */
         NULL,   /* class_data */
         sizeof (HamLongLabel),
         0,      /* n_preallocs */
         (GInstanceInitFunc) ham_long_label_instance_init    /* instance_init */
        };

      type = g_type_register_static (GTK_TYPE_HBOX,
                                     "HamLongLabel",
                                     &info, 0);
    }

  return type;
}

static gboolean
back_button_cb (GtkWidget *widget,
                GdkEventButton *event,
                gpointer user_data)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (user_data);
  GtkAdjustment* adj = gtk_viewport_get_hadjustment (priv->label_viewport);
  gtk_adjustment_set_value (adj, MAX (adj->lower, adj->value - adj->step_increment));
  return TRUE;
}

static gboolean
forth_button_cb (GtkWidget *widget,
                 GdkEventButton *event,
                 gpointer user_data)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (user_data);
  GtkAdjustment* adj = gtk_viewport_get_hadjustment (priv->label_viewport);
  gtk_adjustment_set_value (adj, MIN (adj->upper - adj->page_size, adj->value + adj->step_increment));
  return TRUE;
}

static void
update_button_sensitivity (GtkAdjustment *adjustment,
                           gpointer user_data)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (user_data);
  GtkAdjustment* adj = gtk_viewport_get_hadjustment (priv->label_viewport);
  if (adj->value == (adj->upper - adj->page_size))
    gtk_widget_set_sensitive (GTK_WIDGET (priv->forth_button), FALSE);
  else
    gtk_widget_set_sensitive (GTK_WIDGET (priv->forth_button), TRUE);

  if (adj->value == adj->lower)
    gtk_widget_set_sensitive (GTK_WIDGET (priv->back_button), FALSE);
  else
    gtk_widget_set_sensitive (GTK_WIDGET (priv->back_button), TRUE);

}

static void
ham_long_label_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (widget);
  GtkRequisition label_req, backb_req, forthb_req;

  gtk_widget_size_request (priv->label, &label_req);
  gtk_widget_size_request (GTK_WIDGET (priv->back_button), &backb_req);
  gtk_widget_size_request (GTK_WIDGET (priv->forth_button), &forthb_req);
  requisition->height = MAX (MAX (backb_req.height, forthb_req.height),
                             label_req.height);
  requisition->width = MIN (2*(backb_req.width + forthb_req.width), label_req.width);
  /* g_printf ("Requisition: width = %d, height = %d\n", requisition->width, requisition->height); */
}

static void
ham_long_label_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (widget);
  GtkRequisition label_req, backb_req, forthb_req;
  GtkAllocation label_alloc, backb_alloc, forthb_alloc;

  /* g_printf ("Allocation: width = %d, height = %d\n", allocation->width, allocation->height); */

  gtk_widget_size_request (priv->label, &label_req);
  if (label_req.width > allocation->width)
    {
      gtk_widget_show (GTK_WIDGET (priv->back_button));
      gtk_widget_show (GTK_WIDGET (priv->forth_button));
      gtk_widget_size_request (GTK_WIDGET (priv->back_button), &backb_req);
      gtk_widget_size_request (GTK_WIDGET (priv->forth_button), &forthb_req);

      label_alloc.height = allocation->height;
      backb_alloc.height = allocation->height;
      forthb_alloc.height = allocation->height;

      label_alloc.y = allocation->y;
      backb_alloc.y = allocation->y;
      forthb_alloc.y = allocation->y;

      backb_alloc.x = allocation->x;
      label_alloc.x = allocation->x + backb_req.width;
      forthb_alloc.x = allocation->x + allocation->width - forthb_req.width;

      backb_alloc.width = backb_req.width;
      label_alloc.width = allocation->width - backb_req.width - forthb_req.width;
      forthb_alloc.width = forthb_req.width;

      gtk_widget_size_allocate (GTK_WIDGET (priv->label_viewport), &label_alloc);
      gtk_widget_size_allocate (GTK_WIDGET (priv->back_button), &backb_alloc);
      gtk_widget_size_allocate (GTK_WIDGET (priv->forth_button), &forthb_alloc);
    } else {
      gtk_widget_hide (GTK_WIDGET (priv->back_button));
      gtk_widget_hide (GTK_WIDGET (priv->forth_button));
      allocation->width = label_req.width;
      gtk_widget_size_allocate (GTK_WIDGET (priv->label_viewport), allocation);
    }
}

/* public functions */


/**
 * ham_long_label_new:
 * @text: the text that will be shown in the label.
 *
 * Creates a #HamLongLabel with the given text. 
 *
 * @text can be NULL, and in that case the label would be empty.
 **/
GtkWidget *
ham_long_label_new (const gchar *text)
{
  HamLongLabel *self = g_object_new (HAM_TYPE_LONG_LABEL, NULL);

  ham_long_label_set_text (self, text);

  return GTK_WIDGET (self);
}

/**
 * ham_long_label_new_with_markup:
 * @markup: the Pango markup that will be shown in the label.
 *
 * Creates a #HamLongLabel with the given Pango markup. 
 *
 * @markup can be NULL, and in that case the label would be empty.
 **/
GtkWidget *
ham_long_label_new_with_markup (const gchar *markup)
{
  HamLongLabel *self = g_object_new (HAM_TYPE_LONG_LABEL, NULL);

  ham_long_label_set_markup (self, markup);

  return GTK_WIDGET (self);
}

/**
 * ham_long_label_set_text:
 * @long_label: the #HamLongLabel whose contents will be set. 
 * @text: the plain text that the label will show.
 *
 * Sets the given text in a #HamLongLabel. 
 *
 * @text can be NULL, and in that case the label would be empty.
 **/
void
ham_long_label_set_text (HamLongLabel *long_label, const gchar *text)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (long_label);

  gtk_label_set_text (GTK_LABEL (priv->label), text);
}

/**
 * ham_long_label_set_markup:
 * @long_label: the #HamLongLabel whose contents will be set. 
 * @markup: the Pango markup that the label will show.
 *
 * Sets the given Pango markup in a #HamLongLabel. 
 *
 * @markup can be NULL, and in that case the label would be empty.
 **/
void
ham_long_label_set_markup (HamLongLabel *long_label, const gchar *text)
{
  HamLongLabelPrivate *priv = HAM_LONG_LABEL_GET_PRIVATE (long_label);

  gtk_label_set_markup (GTK_LABEL (priv->label), text);
}
