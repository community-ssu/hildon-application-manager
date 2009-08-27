#include <hildon/hildon.h>

#include "hildon-fancy-button.h"

#define BUTTON_HILIGHT_FILE_NAME "/etc/hildon/theme/mediaplayer/Button.png"
#define HILDON_FANCY_BUTTON_ICON_SIZE 164

enum {
  HILDON_FANCY_BUTTON_FIRST_PROPERY,
  HILDON_FANCY_BUTTON_IMAGE_NAME,
  HILDON_FANCY_BUTTON_PRESSED_IMAGE_NAME,
  HILDON_FANCY_BUTTON_IMAGE_WIDGET_NAME,
  HILDON_FANCY_BUTTON_CAPTION,
  HILDON_FANCY_BUTTON_MAX_WIDTH,
  HILDON_FANCY_BUTTON_LAST_PROPERTY
};

struct _HildonFancyButton {
  GtkEventBox __parent_instance__;
  gboolean pressed;
  char *image_name;
  char *pressed_image_name;
  gboolean label_width_invalid;
  GtkImage *image;
  GtkLabel *label;
};

struct _HildonFancyButtonClass {
  GtkEventBoxClass __parent_class__;
  void (*clicked) (GtkWidget *widget);
};

G_DEFINE_TYPE (HildonFancyButton, hildon_fancy_button, GTK_TYPE_EVENT_BOX);

static GdkPixbuf *pb_button_hilight = NULL;
static int cx_button_hilight = 0;
static int cy_button_hilight = 0;

static void
hildon_fancy_button_set_image_name (HildonFancyButton *fancy_button,
                                    const char *str)
{
  if (g_strcmp0 (fancy_button->image_name, str))
    {
      g_free (fancy_button->image_name);
      fancy_button->image_name = g_strdup (str);
      if (GTK_WIDGET_STATE (fancy_button->image) != GTK_STATE_ACTIVE)
        g_object_set (G_OBJECT (fancy_button->image), "icon-name", str, NULL);
      g_object_notify (G_OBJECT (fancy_button), "image-name");
    }
}

static void
hildon_fancy_button_set_pressed_image_name (HildonFancyButton *fancy_button,
                                            const char *str)
{
  if (g_strcmp0 (fancy_button->pressed_image_name, str))
    {
      g_free (fancy_button->pressed_image_name);
      fancy_button->pressed_image_name = g_strdup (str);
      if (GTK_WIDGET_STATE (fancy_button->image) == GTK_STATE_ACTIVE)
        g_object_set (G_OBJECT (fancy_button->image), "icon-name", str, NULL);
      g_object_notify (G_OBJECT (fancy_button), "pressed-image-name");
    }
}

static void
hildon_fancy_button_set_image_widget_name (HildonFancyButton *fancy_button,
                                           const char *str)
{
  g_object_set (G_OBJECT (fancy_button->image), "name", str, NULL);
  g_object_set (G_OBJECT (fancy_button), "name", str, NULL);
}

static void
hildon_fancy_button_set_caption (HildonFancyButton *fancy_button,
                                 const char *str)
{
  g_object_set (G_OBJECT (fancy_button->label), "label", str, NULL);
}

static void
set_property (GObject *obj,
              guint property_id,
              const GValue *val,
              GParamSpec *pspec)
{
  HildonFancyButton *fancy_button = HILDON_FANCY_BUTTON (obj);

  switch (property_id)
    {
      case HILDON_FANCY_BUTTON_IMAGE_NAME:
        hildon_fancy_button_set_image_name (fancy_button,
                                            g_value_get_string (val));
        break;

      case HILDON_FANCY_BUTTON_PRESSED_IMAGE_NAME:
        hildon_fancy_button_set_pressed_image_name (fancy_button,
                                                   g_value_get_string (val));
        break;

      case HILDON_FANCY_BUTTON_IMAGE_WIDGET_NAME:
        hildon_fancy_button_set_image_widget_name (fancy_button,
                                                   g_value_get_string (val));
        break;

      case HILDON_FANCY_BUTTON_CAPTION:
        hildon_fancy_button_set_caption (fancy_button,
                                         g_value_get_string (val));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
    }
}

static gboolean
expose_event (GtkWidget *widget, GdkEventExpose *event)
{
  if (GTK_WIDGET_STATE (widget) == GTK_STATE_ACTIVE)
    {
      gboolean failed_to_paint = TRUE;

      if (pb_button_hilight)
        {
          GdkGC *gc = NULL;

          gc = gdk_gc_new (widget->window);
          if (gc)
            {
              gdk_draw_pixbuf (widget->window,
                               gc,
                               pb_button_hilight,
                               0, 0,
                               widget->allocation.x, widget->allocation.y,
                               cx_button_hilight, cy_button_hilight,
                               GDK_RGB_DITHER_NONE, 0, 0);
              g_object_unref (gc);
              failed_to_paint = FALSE;
            }
        }

      if (failed_to_paint)
        gtk_paint_flat_box (widget->style,
			    event->window,
			    GTK_STATE_ACTIVE,
			    GTK_SHADOW_NONE,
			    &(event->area),
			    widget,
			    "eventbox",
			    event->area.x,
			    event->area.y,
			    event->area.width,
			    event->area.height);
    }
  return FALSE;
}

static gboolean
button_press_event (GtkWidget *widget, GdkEventButton *event)
{
  GtkWidgetClass *gtk_widget_parent_class =
    GTK_WIDGET_CLASS (hildon_fancy_button_parent_class);
  HildonFancyButton *fancy_button = HILDON_FANCY_BUTTON (widget);

  fancy_button->pressed = TRUE;

  g_object_set (G_OBJECT (fancy_button->image),
                "icon-name", fancy_button->pressed_image_name,
                NULL);
  gtk_widget_set_state (GTK_WIDGET (fancy_button->image), GTK_STATE_ACTIVE);

  return gtk_widget_parent_class->button_press_event
    ? gtk_widget_parent_class->button_press_event (widget, event)
    : TRUE;
}

static gboolean
button_release_event (GtkWidget *widget, GdkEventButton *event)
{
  GtkWidgetClass *gtk_widget_parent_class =
    GTK_WIDGET_CLASS (hildon_fancy_button_parent_class);
  HildonFancyButton *fancy_button = HILDON_FANCY_BUTTON (widget);

  if (fancy_button->pressed
      && GTK_WIDGET_STATE (fancy_button->image) == GTK_STATE_ACTIVE)
    g_signal_emit_by_name (widget, "clicked");

  g_object_set (G_OBJECT (fancy_button->image),
                "icon-name", fancy_button->image_name,
                NULL);
  gtk_widget_set_state (GTK_WIDGET (fancy_button->image), GTK_STATE_NORMAL);

  fancy_button->pressed = FALSE;

  return gtk_widget_parent_class->button_release_event
    ? gtk_widget_parent_class->button_release_event (widget, event)
    : TRUE;
}

static gboolean
enter_notify_event (GtkWidget *widget, GdkEventCrossing *event)
{
  GtkWidgetClass *gtk_widget_parent_class =
    GTK_WIDGET_CLASS (hildon_fancy_button_parent_class);
  HildonFancyButton *fancy_button = HILDON_FANCY_BUTTON (widget);

  if (fancy_button->pressed)
    {
      g_object_set (G_OBJECT (fancy_button->image),
                    "icon-name", fancy_button->pressed_image_name,
                    NULL);
      gtk_widget_set_state (GTK_WIDGET (fancy_button->image),
                            GTK_STATE_ACTIVE);
    }

  return gtk_widget_parent_class->enter_notify_event
    ? gtk_widget_parent_class->enter_notify_event (widget, event)
    : TRUE;
}

static gboolean
leave_notify_event (GtkWidget *widget, GdkEventCrossing *event)
{
  GtkWidgetClass *gtk_widget_parent_class =
    GTK_WIDGET_CLASS (hildon_fancy_button_parent_class);
  HildonFancyButton *fancy_button = HILDON_FANCY_BUTTON (widget);

  g_object_set (G_OBJECT (fancy_button->image),
               "icon-name", fancy_button->image_name,
               NULL);
  gtk_widget_set_state (GTK_WIDGET (fancy_button->image), GTK_STATE_NORMAL);

  return gtk_widget_parent_class->leave_notify_event
    ? gtk_widget_parent_class->leave_notify_event (widget, event)
    : TRUE;
}

static void
theme_changed (GObject *obj, GParamSpec *pspec, gpointer null)
{
  if (pb_button_hilight)
    {
      g_object_unref (pb_button_hilight);
      pb_button_hilight = NULL;
    }

  pb_button_hilight = gdk_pixbuf_new_from_file (BUTTON_HILIGHT_FILE_NAME,
                                                NULL);
  if (pb_button_hilight)
    {
      cx_button_hilight = gdk_pixbuf_get_width (pb_button_hilight);
      cy_button_hilight = gdk_pixbuf_get_height (pb_button_hilight);
    }
}

static void
hildon_fancy_button_class_init (HildonFancyButtonClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *gtkwidget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->set_property = set_property;
  gtkwidget_class->button_press_event   = button_press_event;
  gtkwidget_class->button_release_event = button_release_event;
  gtkwidget_class->enter_notify_event   = enter_notify_event;
  gtkwidget_class->leave_notify_event   = leave_notify_event;

  g_signal_connect (G_OBJECT (gtk_settings_get_default ()),
                    "notify",
                    (GCallback)theme_changed,
                    NULL);
  theme_changed (NULL, NULL, NULL);

  g_signal_new ("clicked",
                HILDON_TYPE_FANCY_BUTTON,
                G_SIGNAL_RUN_FIRST,
                0,
                NULL,
                NULL,
                g_cclosure_marshal_VOID__VOID,
                G_TYPE_NONE,
                0);

  g_object_class_install_property (gobject_class,
                                   HILDON_FANCY_BUTTON_IMAGE_NAME,
                                   g_param_spec_string ("image-name",
                                                        "Image Name",
                                                        "Name of image "
                                                        "when released",
                                                        NULL,
                                                        G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   HILDON_FANCY_BUTTON_PRESSED_IMAGE_NAME,
                                   g_param_spec_string ("pressed-image-name",
                                                        "Pressed Image Name",
                                                        "Name of image "
                                                        "when pressed",
                                                        NULL,
                                                        G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   HILDON_FANCY_BUTTON_IMAGE_WIDGET_NAME,
                                   g_param_spec_string ("image-widget-name",
                                                        "Image Widget Name",
                                                        "Name of image widget",
                                                        NULL,
                                                        G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   HILDON_FANCY_BUTTON_CAPTION,
                                   g_param_spec_string ("caption",
                                                        "Caption",
                                                        "Button caption",
                                                        NULL,
                                                        G_PARAM_WRITABLE));
}

static void
propagate_notify (GObject *src,
                  GParamSpec *pspec,
                  HildonFancyButton *fancy_button)
{
  if (src == G_OBJECT (fancy_button->image))
    g_object_notify (G_OBJECT (fancy_button), "image-widget-name");
  else
  if (src == G_OBJECT (fancy_button->label))
    g_object_notify (G_OBJECT (fancy_button), "caption");
}

static void
hildon_fancy_button_init (HildonFancyButton *fancy_button)
{
  GtkWidget *vbox;

  fancy_button->pressed = FALSE;
  fancy_button->image_name = NULL;
  fancy_button->pressed_image_name = NULL;
  fancy_button->label_width_invalid = FALSE;
  fancy_button->image = g_object_new (GTK_TYPE_IMAGE,
                                      "visible", TRUE,
                                      "pixel-size",
                                        HILDON_FANCY_BUTTON_ICON_SIZE,
                                      NULL);
  fancy_button->label = g_object_new (GTK_TYPE_LABEL,
                                      "visible", TRUE,
                                      "xalign", 0.0, "yalign", 1.0,
                                      "justify", GTK_JUSTIFY_LEFT,
                                      "wrap", TRUE,
                                      NULL);
  hildon_helper_set_logical_font (GTK_WIDGET (fancy_button->label),
                                  "SmallSystemFont");

  gtk_event_box_set_above_child (GTK_EVENT_BOX (fancy_button), TRUE);
  gtk_event_box_set_visible_window (GTK_EVENT_BOX (fancy_button), FALSE);

  vbox = g_object_new (GTK_TYPE_VBOX,
                       "visible", TRUE,
                       "spacing", HILDON_MARGIN_HALF,
                       NULL);
  gtk_container_add (GTK_CONTAINER (fancy_button), vbox);

  gtk_box_pack_start (GTK_BOX (vbox),
                      g_object_new (GTK_TYPE_ALIGNMENT,
                                    "visible", TRUE,
                                    "xalign", 0.5, "yalign", 0.0,
                                    "xscale", 0.0, "yscale", 0.0,
                                    "child", fancy_button->image,
                                    NULL),
                      FALSE,
                      TRUE,
                      0);

  gtk_box_pack_start (GTK_BOX (vbox),
                      g_object_new (GTK_TYPE_ALIGNMENT,
                                    "visible", TRUE,
                                    "left-padding", HILDON_MARGIN_TRIPLE,
                                    "right-padding", HILDON_MARGIN_TRIPLE,
                                    "xalign", 0.5, "yalign", 0.0,
                                    "xscale", 0.0, "yscale", 0.0,
                                    "child", fancy_button->label,
                                    NULL),
                      TRUE,
                      TRUE,
                      0);


  g_signal_connect (G_OBJECT (fancy_button->image),
                    "notify::icon-name",
                    (GCallback)propagate_notify,
                    fancy_button);

  g_signal_connect (G_OBJECT (fancy_button->label),
                    "notify::label",
                    (GCallback)propagate_notify,
                    fancy_button);

  g_signal_connect (G_OBJECT (fancy_button->image),
                    "expose-event",
                    (GCallback)expose_event,
                    NULL);
}
