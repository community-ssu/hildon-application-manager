#include "hildonbreadcrumbtrail.h"

static hildon_bread_crumb_trail_get_label *get_label;
static hildon_bread_crumb_trail_clicked *clicked;

GtkWidget *
hildon_bread_crumb_trail_new (hildon_bread_crumb_trail_get_label *gl,
			      hildon_bread_crumb_trail_clicked *cl)
{
  get_label = gl;
  clicked = cl;
  return gtk_hbox_new (FALSE, 0);
}

static void
btn_callback (GtkWidget *btn, gpointer data)
{
  if (clicked)
    clicked (data);
}

void
hildon_bread_crumb_trail_set_path (GtkWidget *trail, GList *path)
{
  GList *children = gtk_container_get_children (GTK_CONTAINER (trail));
  while (children)
    {
      GList *next;
      gtk_widget_destroy (GTK_WIDGET (children->data));
      next = children->next;
      g_list_free_1 (children);
      children = next;
    }

  while (path)
    {
      const gchar *label = get_label (path);
      GtkWidget *lbl, *btn, *arw;

      lbl = gtk_label_new (label);
      gtk_misc_set_padding (GTK_MISC (lbl), 0, 3);
      btn = gtk_button_new ();
      gtk_widget_set_name (btn, "osso-breadcrumb-button");
      gtk_container_add (GTK_CONTAINER (btn), lbl);
      gtk_box_pack_start (GTK_BOX (trail), btn, FALSE, FALSE, 0);
      gtk_widget_show_all (btn);
      
      if (path->next)
	{
	  g_signal_connect (G_OBJECT (btn), "clicked",
			    G_CALLBACK (btn_callback),
			    path);
	  arw = gtk_arrow_new (GTK_ARROW_RIGHT, GTK_SHADOW_OUT);
	  gtk_widget_set_size_request (arw, 30, 30);
	  gtk_box_pack_start (GTK_BOX (trail), arw, FALSE, FALSE, 0);
	  gtk_widget_show (arw);
	}
      else
	gtk_widget_set_sensitive (btn, FALSE);

      path = path->next;
    }
}
