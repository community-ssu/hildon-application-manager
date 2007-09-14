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

#include <string.h>
#include <libintl.h>

#include <gtk/gtk.h>
#include <hildon/hildon-caption.h>
#include <hildon/hildon-banner.h>

#include "search.h"
#include "util.h"
#include "main.h"

#define _(x) gettext (x)

struct search_dialog_closure {
  GtkWidget *search_words_entry;
  GtkWidget *search_area_combo;
};

#define MAX_SAVED_SEARCH_WORDS 5

GList *saved_search_words = NULL;

static void
fill_with_saved_search_words (GtkComboBox *combo)
{
  for (GList *s = saved_search_words; s; s = s->next)
    gtk_combo_box_append_text (combo, (gchar *)(s->data));
}

static GList *
find_saved_search_words (const char *words)
{
  for (GList *s = saved_search_words; s; s = s->next)
    if (!strcmp ((gchar *)(s->data), words))
      return s;
  return NULL;
}

static void
save_search_words (const char *words)
{
  GList *s = find_saved_search_words (words);
  if (s)
    {
      saved_search_words = g_list_remove_link (saved_search_words, s);
      saved_search_words = g_list_concat (s, saved_search_words);
      return;
    }

  saved_search_words = g_list_prepend (saved_search_words,
				       g_strdup (words));

  while (g_list_length (saved_search_words) > MAX_SAVED_SEARCH_WORDS)
    {
      GList *last = g_list_last (saved_search_words);
      g_free (last->data);
      saved_search_words = g_list_delete_link (saved_search_words, last);
    }
}

static void
search_dialog_response (GtkDialog *dialog, gint response, gpointer clos)
{
  search_dialog_closure *c = (search_dialog_closure *)clos;
  GtkWidget *search_words_entry = c->search_words_entry;
  GtkWidget *search_area_combo = c->search_area_combo;

  if (response == GTK_RESPONSE_OK)
    {
      const char *pattern;
      bool in_descriptions;

      pattern =
	gtk_combo_box_get_active_text (GTK_COMBO_BOX (search_words_entry));
      in_descriptions =
	(gtk_combo_box_get_active (GTK_COMBO_BOX (search_area_combo)) == 1);

      if (*pattern == '\0')
	{
	  irritate_user (_("ai_ib_enter_text"));
	  return;
	}

      save_search_words (pattern);
      search_packages (pattern, in_descriptions);
    }

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));
  end_interaction_flow ();
  delete c;
}

/* This function sets the search area to the default option.
   See below for why we do this.
*/
static void
set_search_area_default (GtkWidget *widget, gpointer data)
{
  gtk_combo_box_set_active (GTK_COMBO_BOX (widget), 0);
}

void
emit_dialog_response (GtkEntry *entry, gpointer dialog)
{
  g_return_if_fail (GTK_IS_DIALOG (dialog));

  /* Emit response signal for dialog */
  gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

void
show_search_dialog_flow ()
{
  if (start_interaction_flow ())
    {
      GtkWidget *dialog, *vbox;
      GtkWidget *entry, *combo, *caption;
      GtkSizeGroup *group;

      search_dialog_closure *c = new search_dialog_closure;

      dialog = gtk_dialog_new_with_buttons (_("ai_ti_search"),
					    NULL,
					    GTK_DIALOG_MODAL,
					    _("ai_bd_search_ok"),
					    GTK_RESPONSE_OK,
					    _("ai_bd_search_cancel"),
					    GTK_RESPONSE_CANCEL,
					    NULL);
      gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
      push_dialog (dialog);
      gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
      set_dialog_help (dialog, AI_TOPIC ("search"));
      //gtk_widget_set_usize (dialog, 400, -1);
      vbox = GTK_DIALOG (dialog)->vbox;

      group = GTK_SIZE_GROUP (gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL));

      entry = gtk_combo_box_entry_new_text ();
      fill_with_saved_search_words (GTK_COMBO_BOX (entry));
      caption = hildon_caption_new (group, _("ai_fi_search_words"), entry,
				    NULL, HILDON_CAPTION_OPTIONAL);
      gtk_box_pack_start_defaults (GTK_BOX (vbox), caption);
      c->search_words_entry = entry;

      combo = gtk_combo_box_new_text ();
      gtk_combo_box_append_text (GTK_COMBO_BOX (combo),
				 _("ai_va_search_name"));
      gtk_combo_box_append_text (GTK_COMBO_BOX (combo),
				 _("ai_va_search_name_description"));
      caption = hildon_caption_new (group, _("ai_fi_search_area"), combo,
				    NULL, HILDON_CAPTION_OPTIONAL);
      gtk_box_pack_start_defaults (GTK_BOX (vbox), caption);
      c->search_area_combo = combo;

      /* XXX - We want the dialog to be large enough for all combobox
	 options, but by default it is only made large enough for
	 the one that is active when the widget is created.  As a
	 workaround, we make the second option active here, which is
	 the longer one, and change to the first one when the widget
	 has been mapped.
      */
      gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 1);

      g_signal_connect (GTK_WIDGET (gtk_bin_get_child (GTK_BIN (entry))),
			"activate",
			G_CALLBACK (emit_dialog_response),
			dialog);

      g_signal_connect (combo, "map",
			G_CALLBACK (set_search_area_default), NULL);

      g_signal_connect (dialog, "response",
			G_CALLBACK (search_dialog_response),
			c);

      gtk_widget_show_all (dialog);
      g_object_unref (group);
    }
}
