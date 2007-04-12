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

#include <stdio.h>
#include <gtk/gtk.h>
#include <assert.h>
#include <libintl.h>

#include "details.h"
#include "util.h"
#include "settings.h"
#include "apt-worker-client.h"
#include "apt-worker-proto.h"

#define _(x) gettext (x)

struct spd_closure {
  package_info *pi;
  detail_kind kind;
  bool show_problems;
  int state;
  void (*cont) (void * data);
  void *data;
};

static const char *
deptype_name (apt_proto_deptype dep)
{
  switch (dep)
    {
    case deptype_depends:
      return "Depends";
    case deptype_conflicts:
      return "Conflicts";
    default:
      return "Unkown";
    }
}

static char *
decode_dependencies (apt_proto_decoder *dec)
{
  GString *str = g_string_new ("");
  char *chars;

  while (true)
    {
      apt_proto_deptype type = (apt_proto_deptype) dec->decode_int ();
      if (dec->corrupted () || type == deptype_end)
	break;
      
      const char *target = dec->decode_string_in_place ();
      g_string_append_printf (str, "%s: %s\n", deptype_name (type), target);
    }

  chars = str->str;
  g_string_free (str, 0);
  return chars;
}

void
decode_summary (apt_proto_decoder *dec, package_info *pi, detail_kind kind)
{
  char size_buf[20];

  g_free (pi->summary);
  pi->summary = NULL;

  for (int i = 0; i < sumtype_max; i++)
    {
      //g_free (pi->summary_packages[i]);
      pi->summary_packages[i] = NULL;
    }

  while (true)
    {
      apt_proto_sumtype type = (apt_proto_sumtype) dec->decode_int ();
      if (dec->corrupted () || type == sumtype_end)
	break;
      
      char *target = dec->decode_string_dup ();
      if (type >= 0 && type < sumtype_max)
	pi->summary_packages[type] = g_list_append (pi->summary_packages[type],
						    target);
      else
	g_free (target);
    }

  bool possible = true;
  if (kind == remove_details)
    {
      if (pi->info.removable_status == status_able)
	{
	  size_string_detailed (size_buf, 20,
				-pi->info.remove_user_size_delta);
	  pi->summary =
	    g_strdup_printf (_("ai_va_details_uninstall_frees"),
			     pi->name, size_buf);
	}
      else
	{
	  pi->summary =
	    g_strdup_printf (_("ai_va_details_unable_uninstall"), pi->name);
	  possible = false;
	}
    }
  else
    {
      if (pi->installed_version)
	{
	  if (pi->info.installable_status == status_able)
	    {
	      if (pi->info.install_user_size_delta >= 0)
		{
		  size_string_detailed (size_buf, 20,
					pi->info.install_user_size_delta);
		  pi->summary =
		    g_strdup_printf (_("ai_va_details_update_requires"),
				     pi->name, size_buf);
		}
	      else
		{
		  size_string_detailed (size_buf, 20,
					-pi->info.install_user_size_delta);
		  pi->summary =
		    g_strdup_printf (_("ai_va_details_update_frees"),
				     pi->name, size_buf);
		}
	    }
	  else
	    {
	      pi->summary =
		g_strdup_printf (_("ai_va_details_unable_update"), pi->name);
	      possible = false;
	    }
	}
      else
	{
	  if (pi->info.installable_status == status_able)
	    {
	      if (pi->info.install_user_size_delta >= 0)
		{
		  size_string_detailed (size_buf, 20,
					pi->info.install_user_size_delta);
		  pi->summary =
		    g_strdup_printf (_("ai_va_details_install_requires"),
				     pi->name, size_buf);
		}
	      else
		{
		  size_string_detailed (size_buf, 20,
					-pi->info.install_user_size_delta);
		  pi->summary =
		    g_strdup_printf (_("ai_va_details_install_frees"),
				     pi->name, size_buf);
		}
	    }
	  else
	    {
	      pi->summary =
		g_strdup_printf (_("ai_va_details_unable_install"),
				 pi->name, size_buf);
	      possible = false;
	    }
	}
    }
}

static void
add_table_field (GtkWidget *table, int row,
		 const char *field, const char *value,
		 PangoEllipsizeMode em = PANGO_ELLIPSIZE_END)
{
  GtkWidget *label;

  if (value && !all_whitespace (value))
    {
      if (field)
	{
	  label = make_small_label (field);
	  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.0);
	  gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row+1,
			    GTK_FILL, GTK_FILL, 0, 0);
	}

      label = make_small_label (value);
      gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
      gtk_label_set_line_wrap (GTK_LABEL (label), FALSE);
      gtk_label_set_ellipsize (GTK_LABEL (label), em);

      gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row+1,
			GtkAttachOptions (GTK_EXPAND | GTK_FILL), GTK_FILL,
			0, 0);
    }
}

static int
add_table_list (GtkWidget *table, int row,
		const char *title,
		GList *list)
{
  while (list)
    {
      add_table_field (table, row++, title, (char *)list->data);
      title = NULL;
      list = list->next;
    }
  return row;
}

/* Same as add_table_list, but when there is only one entry and it is
   equal to MYSELF, the list is not shown.
*/

static int
add_table_list_1 (GtkWidget *table, int row,
		  const char *title,
		  gchar *myself_name, gchar *myself_version,
		  GList *list)
{
  if (list && list->next == NULL)
    {
      gchar *myself_target = g_strdup_printf ("%s %s",
					      myself_name, myself_version);
      bool only_me = !g_ascii_strcasecmp ((gchar *)list->data, myself_target);
      g_free (myself_target);
      if (only_me)
	return row;
    }

  return add_table_list (table, row, title, list);
}

struct details_closure {
  void (*cont) (void *data);
  void *data;
};

static void
details_response (GtkDialog *dialog, gint response, gpointer clos)
{
  details_closure * closure = (details_closure *) clos;
  pop_dialog_parent (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));
  if (closure->cont)
    closure->cont(closure->data);
  delete closure;
}

static void
show_with_details_with_cont (package_info *pi, bool show_problems,
			     void (*cont) (void *data), void *data)
{
  GtkWidget *dialog, *notebook;
  GtkWidget *table, *common;
  GtkWidget *summary_table, *summary_tab;
  details_closure *closure = new details_closure;

  gchar *status;

  if (pi->installed_version && pi->available_version)
    {
      if (pi->broken)
	{
	  if (pi->info.installable_status == status_able)
	    status = _("ai_va_details_status_broken_updateable");
	  else
	    status = _("ai_va_details_status_broken_not_updateable");
	}
      else
	{
	  if (pi->info.installable_status == status_able)
	    status = _("ai_va_details_status_updateable");
	  else
	    status = _("ai_va_details_status_not_updateable");
	}
    }
  else if (pi->installed_version)
    {
      if (pi->broken)
	status = _("ai_va_details_status_broken");
      else
	status = _("ai_va_details_status_installed");
    }
  else if (pi->available_version)
    {
      if (pi->info.installable_status == status_able)
	status = _("ai_va_details_status_installable");
      else
	status = _("ai_va_details_status_not_installable");
    }
  else
    status = "?";

  table = gtk_table_new (9, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 10);
  gtk_table_set_row_spacings (GTK_TABLE (table), 0);

  add_table_field (table, 0,
		   _("ai_fi_details_package"), pi->name);

  gchar *short_description = (pi->have_detail_kind == remove_details
			      ? pi->installed_short_description
			      : pi->available_short_description);
  if (short_description == NULL)
    short_description = pi->installed_short_description;
  add_table_field (table, 1, "", short_description);

  add_table_field (table, 2, _("ai_fi_details_maintainer"),
		   pi->maintainer, PANGO_ELLIPSIZE_START);

  add_table_field (table, 3, _("ai_fi_details_status"), status);

  add_table_field (table, 4, _("ai_fi_details_category"),
		   nicify_section_name (pi->have_detail_kind == remove_details
					? pi->installed_section
					: pi->available_section));

  add_table_field (table, 5, _("ai_va_details_installed_version"),
		   (pi->installed_version
		    ? pi->installed_version
		    : _("ai_va_details_no_info")));

  if (pi->installed_version)
    {
      char size_buf[20];
      size_string_detailed (size_buf, 20, pi->installed_size);
      add_table_field (table, 6, _("ai_va_details_size"), size_buf);
    }

  add_table_field (table, 7, _("ai_va_details_available_version"),
		   (pi->available_version 
		    ? pi->available_version
		    : _("ai_va_details_no_info")));

  if (pi->available_version)
    {
      char size_buf[20];
      size_string_detailed (size_buf, 20, pi->info.download_size);
      add_table_field (table, 8, _("ai_va_details_download_size"), size_buf);
    }

  common = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (common), table, TRUE, TRUE, 0);

  const gchar *summary_label;
  if (pi->have_detail_kind == remove_details)
    summary_label = _("ai_ti_details_noteb_uninstalling");
  else if (pi->info.installable_status != status_able)
    summary_label = _("ai_ti_details_noteb_problems");
  else if (pi->installed_version && pi->available_version)
    summary_label = _("ai_ti_details_noteb_updating");
  else
    summary_label = _("ai_ti_details_noteb_installing");

  bool possible;
  if (pi->have_detail_kind == remove_details)
    possible = (pi->info.removable_status == status_able);
  else
    possible = (pi->info.installable_status == status_able);

  summary_table = gtk_table_new (1, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (summary_table), 10);
  gtk_table_set_row_spacings (GTK_TABLE (summary_table), 0);

  GtkWidget *header_label = make_small_label (pi->summary);
  gtk_misc_set_alignment (GTK_MISC (header_label), 0.0, 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (header_label), FALSE);
  gtk_label_set_ellipsize (GTK_LABEL (header_label), PANGO_ELLIPSIZE_MIDDLE);
  gtk_table_attach (GTK_TABLE (summary_table), header_label, 0, 2, 0, 1,
		    GtkAttachOptions (GTK_EXPAND | GTK_FILL), GTK_FILL,
		    0, 0);

  int r = 1;
  if (possible)
    {
      r = add_table_list_1 (summary_table, r,
			    _("ai_fi_details_packages_install"),
			    pi->name, pi->available_version,
			    pi->summary_packages[sumtype_installing]);
      r = add_table_list_1 (summary_table, r,
			    _("ai_fi_details_packages_update"),
			    pi->name, pi->available_version,
			    pi->summary_packages[sumtype_upgrading]);
      r = add_table_list_1 (summary_table, r,
			    _("ai_fi_details_packages_uninstall"),
			    pi->name, pi->installed_version,
			    pi->summary_packages[sumtype_removing]);
    }
  else
    {
      r = add_table_list (summary_table, r,
			  _("ai_fi_details_packages_missing"),
			  pi->summary_packages[sumtype_missing]);
      r = add_table_list (summary_table, r,
			  _("ai_fi_details_packages_conflicting"),
			  pi->summary_packages[sumtype_conflicting]);
      r = add_table_list (summary_table, r,
			  _("ai_fi_details_packages_needing"),
			  pi->summary_packages[sumtype_needed_by]);
    }

  summary_tab = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (summary_tab),
					summary_table);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (summary_tab),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  dialog = gtk_dialog_new_with_buttons (_("ai_ti_details"),
					get_dialog_parent (),
					GTK_DIALOG_MODAL,
					_("ai_bd_details_close"),
					GTK_RESPONSE_OK,
					NULL);
  push_dialog_parent (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  set_dialog_help (dialog, AI_TOPIC ("packagedetailsview"));
  respond_on_escape (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  notebook = gtk_notebook_new ();
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), notebook);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    common,
			    gtk_label_new (_("ai_ti_details_noteb_common")));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    make_small_text_view (pi->description),
			    gtk_label_new
			    (_("ai_ti_details_noteb_description")));
  int problems_page =
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			      summary_tab,
			      gtk_label_new (summary_label));
  if (pi->dependencies)
    {
      gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
				make_small_text_view (pi->dependencies),
				gtk_label_new ("Dependencies"));
    }

  pi->unref ();

  closure->cont = cont;
  closure->data = data;
  g_signal_connect (dialog, "response",
		    G_CALLBACK (details_response), closure);

  gtk_widget_set_usize (dialog, 600, 320);
  gtk_widget_show_all (dialog);

  if (show_problems)
    gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook),
				   problems_page);
}

void
nicify_description_in_place (char *desc)
{
  if (desc == NULL)
    return;

  /* The nicifications are this:
     
     - the first space of a line is removed.

     - if after that a line consists solely of a '.', that dot is
       removed.
  */

  char *src = desc, *dst = desc;
  char *bol = src;

  while (*src)
    {
      if (src == bol && *src == ' ')
	{
	  src++;
	  continue;
	}

      if (*src == '\n')
	{
	  if (bol+2 == src && bol[0] == ' ' && bol[1] == '.')
	    dst--;
	  bol = src + 1;
	}

      *dst++ = *src++;
    }

  *dst = '\0';
}

static void
get_package_details_reply (int cmd, apt_proto_decoder *dec, void *clos)
{
  spd_closure *c = (spd_closure *)clos;
  package_info *pi = c->pi;
  detail_kind kind = c->kind;
  bool show_problems = c->show_problems;
  void (*cont) (void *data) = c->cont;
  void *data = c->data;
  delete c;

  if (dec == NULL)
    {
      pi->unref ();
      if (cont != NULL) {
	cont(data);
      }
      return;
    }

  g_free (pi->maintainer);
  g_free (pi->description);
  g_free (pi->dependencies);

  pi->maintainer = dec->decode_string_dup ();
  pi->description = dec->decode_string_dup ();
  nicify_description_in_place (pi->description);

  pi->dependencies = decode_dependencies (dec);
  if (!red_pill_mode || !red_pill_show_deps)
    {
      // Too much information can kill you.
      g_free (pi->dependencies);
      pi->dependencies = NULL;
    }
      
  decode_summary (dec, pi, kind);

  pi->have_detail_kind = kind;

  show_with_details_with_cont (pi, show_problems, cont, data);
}

void
spd_cont (package_info *pi, void *data, bool changed)
{
  spd_closure *c = (spd_closure *)data;

  if (pi->have_detail_kind != c->kind)
    apt_worker_get_package_details (pi->name, (c->kind == remove_details
					       ? pi->installed_version
					       : pi->available_version),
				    c->kind, c->state,
				    get_package_details_reply,
				    data);
  else
    {
      show_with_details_with_cont (pi, c->show_problems, c->cont, c->data);
      delete c;
    }
}

void
show_package_details (package_info *pi, detail_kind kind,
		      bool show_problems, int state)
{
  spd_closure *c = new spd_closure;
  c->pi = pi;
  c->kind = kind;
  c->show_problems = show_problems;
  c->state = state;
  c->cont = NULL;
  c->data = NULL;
  pi->ref ();
  get_intermediate_package_info (pi, false, spd_cont, c, c->state);
}
