/*
 * This file is part of osso-application-installer
 *
 * Copyright (C) 2005, 2006 Nokia Corporation.  All Right reserved.
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

/* This utility will show a given text to the user with "Ok" and
   "Cancel" buttons.  When the user clicks "Ok", it exist 0, otherwise
   it exits 1.

   The default title of the dialog is "License Agreement".
*/

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libintl.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define _(x) dgettext ("osso-application-installer", x)

static PangoFontDescription *
get_small_font (GtkWidget *widget)
{
  static PangoFontDescription *small_font = NULL;

  if (small_font == NULL)
    {
      GtkStyle *fontstyle = NULL;

      fontstyle =
	gtk_rc_get_style_by_paths (gtk_widget_get_settings (GTK_WIDGET(widget)),
				   "osso-SmallFont", NULL,
				   G_TYPE_NONE);
  
      if (fontstyle)
        small_font = pango_font_description_copy (fontstyle->font_desc);
      else
        small_font = pango_font_description_from_string ("Nokia Sans 11.625");
    }

  return small_font;
}

void
fill_text_buffer_from_file (GtkTextBuffer *text, const char *file)
{
  char buf[1024];
  int fd, n;

  fd = open (file, O_RDONLY);

  if (fd < 0)
    {
      perror (file);
      exit (2);
    }

  gtk_text_buffer_set_text (text, "", 0);
  while ((n = read (fd, buf, 1024)) > 0)
    {
      GtkTextIter iter;
      gtk_text_buffer_get_end_iter (text, &iter);
      gtk_text_buffer_insert (text, &iter, buf, n);
    }
  if (n < 0)
    {
      perror (file);
      exit (2);
    }
  close (fd);
}

static gboolean
no_button_events (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  g_signal_stop_emission_by_name (widget, "button-press-event");
  return FALSE;
}

GtkWidget *
make_small_text_view (const char *file)
{
  GtkWidget *scroll;
  GtkWidget *view;
  GtkTextBuffer *buffer;
  
  scroll = gtk_scrolled_window_new (NULL, NULL);
  view = gtk_text_view_new ();
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  fill_text_buffer_from_file (buffer, file);
  gtk_text_view_set_editable (GTK_TEXT_VIEW (view), 0);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), 0);
  gtk_container_add (GTK_CONTAINER (scroll), view);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_widget_modify_font (view, get_small_font (view));

  g_signal_connect (view, "button-press-event",
		    G_CALLBACK (no_button_events), NULL);

  return scroll;
}

Window
find_window_1 (Window win, const char *name, int level, int max_level)
{
  char *win_name;
  
  if (XFetchName (gdk_display, win, &win_name))
    {
      if (!strcmp (win_name, name))
	{
	  XFree (win_name);
	  return win;
	}
      XFree (win_name);
    }
  
  if (level < max_level)
    {
      Window root, parent, *children;
      unsigned int n_children;
      
      if (XQueryTree (gdk_display, win, &root, &parent,
		      &children, &n_children))
	{
	  int i;
	  Window w;

	  for (i = 0; i < n_children; i++)
	    {
	      w = find_window_1 (children[i], name, level+1, max_level);
	      if (w)
		{
		  XFree (children);
		  return w;
		}
	    }
	  XFree (children);
	}
    }
  
  return 0;
}

Window
find_window (const char *name, int max_level)
{
  return find_window_1 (GDK_ROOT_WINDOW (), name, 0, max_level);
}

Window
find_application_installer_window ()
{
  return find_window ("osso-application-installer", 2);
}

void
dialog_realized (GtkWidget *widget, gpointer data)
{
  GdkWindow *win = widget->window;
  Window ai_win = find_application_installer_window ();
  
  if (ai_win)
    XSetTransientForHint (GDK_WINDOW_XDISPLAY (win), GDK_WINDOW_XID (win),
			  ai_win);
}

int
main (int argc, char **argv)
{
  GtkWidget *dialog;
  char *title, *file;

  gtk_init (&argc, &argv);

  if (argc == 2)
    {
      title = _("ai_ti_license_agreement");
      file = argv[1];
    }
  else if (argc == 3)
    {
      title = argv[1];
      file = argv[2];
    }
  else
    {
      fprintf (stderr, "usage: maemo-confirm-text [title] file\n");
      return 2;
    }

  dialog = gtk_dialog_new_with_buttons
    (title,
     NULL, 0,
     _("ai_bd_license_ok"), GTK_RESPONSE_OK,
     _("ai_bd_license_cancel"), GTK_RESPONSE_CANCEL,
     NULL);

  g_signal_connect (dialog, "realize",
		    G_CALLBACK (dialog_realized), NULL);

  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
		     make_small_text_view (file));
  gtk_widget_set_usize (dialog, 600, 300);
  gtk_widget_show_all (dialog);

  return (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)? 0 : 1;
}
