/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2005, 2006, 2007 Nokia Corporation.  All Rights reserved.
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

#include <hildon/hildon-note.h>

#define _(x) dgettext ("hildon-application-manager", x)

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
find_application_manager_window ()
{
  return find_window ("hildon-application-manager", 2);
}

void
dialog_realized (GtkWidget *widget, gpointer data)
{
  GdkWindow *win = widget->window;
  Window ai_win = find_application_manager_window ();
  
  if (ai_win)
    XSetTransientForHint (GDK_WINDOW_XDISPLAY (win), GDK_WINDOW_XID (win),
			  ai_win);
}

int
main (int argc, char **argv)
{
  GtkWidget *dialog;

  gtk_init (&argc, &argv);

  dialog = hildon_note_new_information (NULL, "Foo");

  g_signal_connect (dialog, "realize",
		    G_CALLBACK (dialog_realized), NULL);

  gtk_dialog_run (GTK_DIALOG (dialog));

  return 0;
}
