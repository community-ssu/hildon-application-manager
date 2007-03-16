/*
 * This file is part of the hildon-application-manager.
 *
 * Parts of this file are derived from apt.  Apt is copyright 1997,
 * 1998, 1999 Jason Gunthorpe and others.
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
#include <assert.h>
#include <iostream>
#include <libintl.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "apt-worker-client.h"
#include "apt-worker-proto.h"

#include "main.h"
#include "util.h"
#include "details.h"
#include "menu.h"
#include "log.h"
#include "settings.h"
#include "search.h"
#include "instr.h"
#include "repo.h"

#define MAX_PACKAGES_NO_CATEGORIES 7

#define _(x) gettext (x)

extern "C" {
  #include "hildonbreadcrumbtrail.h"
  #include <hildon/hildon-window.h>
  #include <hildon/hildon-note.h>
  #include <libosso.h>
  #include <osso-helplib.h>
}

using namespace std;

static guint apt_source_id;

static void set_details_callback (void (*func) (gpointer), gpointer data);
static void set_operation_label (const char *label, const char *insens);
static void set_operation_callback (void (*func) (gpointer), gpointer data);
static void enable_search (bool f);
static void set_current_help_topic (const char *topic);

void get_package_list_info (GList *packages);

struct view {
  view *parent;
  const gchar *label;
  GtkWidget *(*maker) (view *);
};

GtkWidget *main_vbox = NULL;
GtkWidget *main_trail = NULL;
GtkWidget *device_label = NULL;
GtkWidget *cur_view = NULL;
view *cur_view_struct = NULL;
GList *cur_path = NULL;
gboolean something_started = FALSE;

static GList *
make_view_path (view *v)
{
  if (v == NULL)
    return NULL;
  else
    return g_list_append (make_view_path (v->parent), v);
}

void
show_view (view *v)
{
  if (cur_view)
    {
      gtk_container_remove(GTK_CONTAINER(main_vbox), cur_view);
      cur_view = NULL;
    }

  set_details_callback (NULL, NULL);
  set_operation_label (NULL, NULL);
  set_operation_callback (NULL, NULL);

  allow_updating ();

  cur_view = v->maker (v);
  cur_view_struct = v;

  g_list_free (cur_path);
  cur_path = make_view_path (v);
  hildon_bread_crumb_trail_set_path (main_trail, cur_path);
  
  gtk_box_pack_start (GTK_BOX (main_vbox), cur_view, TRUE, TRUE, 10);
  gtk_widget_show(main_vbox);
}

static void
show_view_callback (GtkWidget *btn, gpointer data)
{
  view *v = (view *)data;
  
  show_view (v);
}

static const gchar *
get_view_label (GList *node)
{
  return gettext (((view *)node->data)->label);
}

static void
view_clicked (GList *node)
{
  show_view ((view *)node->data);
}

GtkWidget *make_main_view (view *v);
GtkWidget *make_install_applications_view (view *v);
GtkWidget *make_install_section_view (view *v);
GtkWidget *make_upgrade_applications_view (view *v);
GtkWidget *make_uninstall_applications_view (view *v);
GtkWidget *make_search_results_view (view *v);

view main_view = {
  NULL,
  "ai_ti_main",
  make_main_view
};

view install_applications_view = {
  &main_view,
  "ai_li_install",
  make_install_applications_view
};

view upgrade_applications_view = {
  &main_view,
  "ai_li_update",
  make_upgrade_applications_view
};

view uninstall_applications_view = {
  &main_view,
  "ai_li_uninstall",
  make_uninstall_applications_view
};

view install_section_view = {
  &install_applications_view,
  NULL,
  make_install_section_view
};

view search_results_view = {
  &main_view,
  "ai_ti_search_results",
  make_search_results_view
};

void
show_main_view ()
{
  show_view (&main_view);
}

void
show_parent_view ()
{
  if (cur_view_struct && cur_view_struct->parent)
    show_view (cur_view_struct->parent);
}

static GtkWidget *
make_padded_button (const char *label)
{
  GtkWidget *l = gtk_label_new (label);
  gtk_misc_set_padding (GTK_MISC (l), 15, 15);
  GtkWidget *btn = gtk_button_new ();
  gtk_container_add (GTK_CONTAINER (btn), l);
  return btn;
}

static gboolean
expose_main_view (GtkWidget *w, GdkEventExpose *ev, gpointer data)
{
  /* This puts the background pixmap for the ACTIVE state into the
     lower right corner.  Using bg_pixmap[ACTIVE] is a hack to
     communicate which pixmap to use from the gtkrc file.  The widget
     will never actually be in the ACTIVE state.
  */

  GtkStyle *style = gtk_rc_get_style (w);
  GdkPixmap *pixmap = style->bg_pixmap[GTK_STATE_ACTIVE];
  gint ww, wh, pw, ph;

  if (pixmap)
    {
      gdk_drawable_get_size (pixmap, &pw, &ph);
      gdk_drawable_get_size (w->window, &ww, &wh);
      
      gdk_draw_drawable (w->window, style->fg_gc[GTK_STATE_NORMAL],
			 pixmap, 0, 0, ww-pw, wh-ph, pw, ph);
    }

  gtk_container_propagate_expose (GTK_CONTAINER (w),
				  gtk_bin_get_child (GTK_BIN (w)),
				  ev);

  return TRUE;
}

static void
device_label_destroyed (GtkWidget *widget, gpointer data)
{
  if (device_label == widget)
    device_label = NULL;
}

GtkWidget *
make_main_view (view *v)
{
  GtkWidget *view;
  GtkWidget *vbox, *hbox;
  GtkWidget *btn, *label, *image;
  GtkSizeGroup *btn_group;

  btn_group = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

  view = gtk_event_box_new ();
  gtk_widget_set_name (view, "osso-application-installer-main-view");

  g_signal_connect (view, "expose-event",
                    G_CALLBACK (expose_main_view), NULL);

  vbox = gtk_vbox_new (FALSE, 10);
  gtk_container_add (GTK_CONTAINER (view), vbox);

  // first label
  hbox = gtk_hbox_new (FALSE, 10);
  image = gtk_image_new_from_icon_name ("qgn_list_filesys_divc_cls",
					HILDON_ICON_SIZE_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

  device_label = gtk_label_new (device_name ());
  gtk_label_set_ellipsize(GTK_LABEL (device_label), PANGO_ELLIPSIZE_END);
  gtk_misc_set_alignment (GTK_MISC (device_label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox),  device_label, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox,  FALSE, FALSE, 0);
  
  g_signal_connect (device_label, "destroy",
		    G_CALLBACK (device_label_destroyed), NULL);

  // first button
  hbox = gtk_hbox_new (FALSE, 0);
  btn = make_padded_button (_("ai_li_uninstall"));
  g_signal_connect (G_OBJECT (btn), "clicked",
		    G_CALLBACK (show_view_callback),
		    &uninstall_applications_view);
  gtk_size_group_add_widget(btn_group, btn);
  // 36 padding = 26 icon size + 10 padding
  gtk_box_pack_start (GTK_BOX (hbox),  btn,  FALSE, FALSE, 36); 
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  grab_focus_on_map (btn);

  // second label
  hbox = gtk_hbox_new (FALSE, 10);
  image = gtk_image_new_from_icon_name ("qgn_list_browser",
					HILDON_ICON_SIZE_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

  label = gtk_label_new (_("ai_li_repository"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox),  label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox,  FALSE, FALSE, 0);

  // second button
  hbox = gtk_hbox_new (FALSE, 0);
  btn = make_padded_button (_("ai_li_install"));
  g_signal_connect (G_OBJECT (btn), "clicked",
		    G_CALLBACK (show_view_callback),
		    &install_applications_view);
  gtk_size_group_add_widget(btn_group, btn);
  // 36 padding = 26 icon size + 10 padding
  gtk_box_pack_start (GTK_BOX (hbox),  btn,  FALSE, FALSE, 36);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  
  // third button
  hbox = gtk_hbox_new (FALSE, 0);
  btn = make_padded_button (_("ai_li_update"));
  g_signal_connect (G_OBJECT (btn), "clicked",
		    G_CALLBACK (show_view_callback),
		    &upgrade_applications_view);
  gtk_size_group_add_widget(btn_group, btn);
  // 36 padding = 26 icon size + 10 padding
  gtk_box_pack_start (GTK_BOX (hbox),  btn,  FALSE, FALSE, 36);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  gtk_widget_show_all (view);
  g_object_unref(btn_group);

  get_package_list_info (NULL);

  enable_search (false);
  set_current_help_topic (AI_TOPIC ("mainview"));

  prevent_updating ();

  return view;
}

static GList *install_sections = NULL;
static GList *upgradeable_packages = NULL;
static GList *installed_packages = NULL;
static GList *search_result_packages = NULL;
static GList *temp_packages = NULL;

static char *cur_section_name;

package_info::package_info ()
{
  ref_count = 1;
  name = NULL;
  installed_version = NULL;
  installed_section = NULL;
  available_version = NULL;
  available_section = NULL;
  installed_short_description = NULL;
  available_short_description = NULL;
  installed_icon = NULL;
  available_icon = NULL;

  have_info = false;

  have_detail_kind = no_details;
  maintainer = NULL;
  description = NULL;
  summary = NULL;
  for (int i = 0; i < sumtype_max; i++)
    summary_packages[i] = NULL;
  dependencies = NULL;

  model = NULL;
  
  filename = NULL;
}

package_info::~package_info ()
{
  g_free (name);
  g_free (installed_version);
  g_free (installed_section);
  g_free (available_version);
  g_free (available_section);
  g_free (installed_short_description);
  g_free (available_short_description);
  if (installed_icon)
    g_object_unref (installed_icon);
  if (available_icon)
    g_object_unref (available_icon);
  g_free (maintainer);
  g_free (description);
  g_free (summary);
  for (int i = 0; i < sumtype_max; i++)
    {
      g_list_foreach (summary_packages[i], (GFunc) g_free, NULL);
      g_list_free (summary_packages[i]);
    }
  g_free (dependencies);
  g_free (filename);
}

void
package_info::ref ()
{
  ref_count += 1;
}

void
package_info::unref ()
{
  ref_count -= 1;
  if (ref_count == 0)
    delete this;
}

static void
free_packages (GList *list)
{
  for (GList *p = list; p; p = p->next)
    ((package_info *)p->data)->unref ();
  g_list_free (list);
}

section_info::section_info ()
{
  ref_count = 1;
  symbolic_name = NULL;
  name = NULL;
  packages = NULL;
}

section_info::~section_info ()
{
  g_free (symbolic_name);
  free_packages (packages);
}

void
section_info::ref ()
{
  ref_count += 1;
}

void
section_info::unref ()
{
  ref_count -= 1;
  if (ref_count == 0)
    delete this;
}

static void
free_sections (GList *list)
{
  for (GList *s = list; s; s = s->next)
    {
      section_info *si = (section_info *) s->data;
      si->unref ();
    }
  g_list_free (list);
}

static void
free_all_packages_temp ()
{
  if (temp_packages)
    {
      free_packages (temp_packages);
      temp_packages = NULL;
    }
}

static void
free_all_packages_default ()
{
  if (install_sections)
    {
      free_sections (install_sections);
      install_sections = NULL;
    }

  if (upgradeable_packages)
    {
      free_packages (upgradeable_packages);
      upgradeable_packages = NULL;
    }

  if (installed_packages)
    {
      free_packages (installed_packages);
      installed_packages = NULL;
    }

  if (search_result_packages)
    {
      free_packages (search_result_packages);
      search_result_packages = NULL;
    }
}

static void
free_all_packages (int state)
{
  switch (state)
    {
    case APTSTATE_DEFAULT:
      free_all_packages_default ();
      break;
    case APTSTATE_TEMP:
      free_all_packages_temp ();
      break;
    default:
      break;
    }
}

static const char *
canonicalize_section_name (const char *name)
{
  if (name == NULL || (red_pill_mode && red_pill_show_all))
    return name;

  if (g_str_has_prefix (name, "maemo/"))
    return name + 6;

  if (g_str_has_prefix (name, "user/"))
    return name += 5;

  return name;
}

const char *
nicify_section_name (const char *name)
{
  if (red_pill_mode && red_pill_show_all)
    return name;

  name = canonicalize_section_name (name);

  if (*name == '\0')
    return "-";

  char *logical_id = g_strdup_printf ("ai_category_%s", name);
  const char *translated_name = gettext (logical_id);
  if (translated_name != logical_id)
    name = translated_name;
  g_free (logical_id);
  return name;
}

static section_info *
find_section_info (GList **list_ptr, const char *name,
		   bool create, bool allow_all)
{
  name = canonicalize_section_name (name);

  if (name == NULL)
    name = "other";

  if (!allow_all && !strcmp (name, "all"))
    name = "other";

  for (GList *ptr = *list_ptr; ptr; ptr = ptr->next)
    if (!strcmp (((section_info *)ptr->data)->symbolic_name, name))
      return (section_info *)ptr->data;

  if (!create)
    return NULL;
	    
  section_info *si = new section_info;
  si->symbolic_name = g_strdup (name);
  si->name = nicify_section_name (si->symbolic_name);
  si->packages = NULL;
  *list_ptr = g_list_prepend (*list_ptr, si);
  return si;
}

static gint
compare_section_names (gconstpointer a, gconstpointer b)
{
  section_info *si_a = (section_info *)a;
  section_info *si_b = (section_info *)b;

  // The sorting of sections can not be configured.

  return g_ascii_strcasecmp (si_a->name, si_b->name);
}

static gint
compare_package_names (gconstpointer a, gconstpointer b)
{
  package_info *pi_a = (package_info *)a;
  package_info *pi_b = (package_info *)b;

  return package_sort_sign * g_ascii_strcasecmp (pi_a->name, pi_b->name);
}

static gint
compare_versions (const gchar *a, const gchar *b)
{
  // XXX - wrong, of course
  return package_sort_sign * g_ascii_strcasecmp (a, b);
}

static gint
compare_package_installed_versions (gconstpointer a, gconstpointer b)
{
  package_info *pi_a = (package_info *)a;
  package_info *pi_b = (package_info *)b;

  return compare_versions (pi_a->installed_version, pi_b->installed_version);
}

static gint
compare_package_available_versions (gconstpointer a, gconstpointer b)
{
  package_info *pi_a = (package_info *)a;
  package_info *pi_b = (package_info *)b;

  return compare_versions (pi_a->available_version, pi_b->available_version);
}

static gint
compare_package_installed_sizes (gconstpointer a, gconstpointer b)
{
  package_info *pi_a = (package_info *)a;
  package_info *pi_b = (package_info *)b;

  return (package_sort_sign *
	  (pi_a->installed_size - pi_b->installed_size));
}

static gint
compare_package_download_sizes (gconstpointer a, gconstpointer b)
{
  package_info *pi_a = (package_info *)a;
  package_info *pi_b = (package_info *)b;

  // Download size might not be known when we sort so we sort by name
  // instead in that case.
  
  if (pi_a->have_info && pi_b->have_info)
    return (package_sort_sign * 
	    (pi_a->info.download_size - pi_b->info.download_size));
  else
    return compare_package_names (a, b);
}

void
sort_all_packages ()
{
  // If the first section is the "All" section, exclude it from the
  // sort.
  
  GList **section_ptr;
  if (install_sections
      && !strcmp (((section_info *)install_sections->data)->symbolic_name,
		  "all"))
    section_ptr = &(install_sections->next);
  else
    section_ptr = &install_sections;

  *section_ptr = g_list_sort (*section_ptr, compare_section_names);

  GCompareFunc compare_packages_inst = compare_package_names;
  GCompareFunc compare_packages_avail = compare_package_names;
  if (package_sort_key == SORT_BY_VERSION)
    {
      compare_packages_inst = compare_package_installed_versions;
      compare_packages_avail = compare_package_available_versions;
    }
  else if (package_sort_key == SORT_BY_SIZE)
    {
      compare_packages_inst = compare_package_installed_sizes;
      compare_packages_avail = compare_package_download_sizes;
    }

  for (GList *s = install_sections; s; s = s->next)
    {
      section_info *si = (section_info *)s->data;
      si->packages = g_list_sort (si->packages,
				  compare_packages_avail);
    }

  installed_packages = g_list_sort (installed_packages,
				    compare_packages_inst);

  upgradeable_packages = g_list_sort (upgradeable_packages,
				      compare_packages_avail);

  if (search_results_view.parent == &install_applications_view
      || search_results_view.parent == &upgrade_applications_view)
    search_result_packages = g_list_sort (search_result_packages,
					  compare_packages_avail);
  else
    search_result_packages = g_list_sort (search_result_packages,
					  compare_packages_inst);

  show_view (cur_view_struct);
}

static void
save_keys_callback (int cmd, apt_proto_decoder *dec, void *data)
{
  /* No action required */
}

/* Save the list of installed packages, in the Single-click
   install file format. It's used to store a backup of installed files.
*/
static void
save_installed_packages_file ()
{
  apt_worker_save_applications_install_file (save_keys_callback, NULL);
}


struct gpl_closure {
  int state;
  void (*cont) (void *data);
  void *data;
};

static package_info *
get_package_list_entry (apt_proto_decoder *dec)
{
  const char *installed_icon, *available_icon;
  package_info *info = new package_info;
  
  info->name = dec->decode_string_dup ();
  info->broken = dec->decode_int ();
  info->installed_version = dec->decode_string_dup ();
  info->installed_size = dec->decode_int ();
  info->installed_section = dec->decode_string_dup ();
  info->installed_short_description = dec->decode_string_dup ();
  installed_icon = dec->decode_string_in_place ();
  info->available_version = dec->decode_string_dup ();
  info->available_section = dec->decode_string_dup ();
  info->available_short_description = dec->decode_string_dup ();
  available_icon = dec->decode_string_in_place ();
  
  info->installed_icon = pixbuf_from_base64 (installed_icon);
  if (available_icon)
    info->available_icon = pixbuf_from_base64 (available_icon);
  else
    {
      info->available_icon = info->installed_icon;
      if (info->available_icon)
	g_object_ref (info->available_icon);
    }

  return info;
}

static void
get_package_list_reply_default (int cmd, apt_proto_decoder *dec, void *data)
{
  gpl_closure *c = (gpl_closure *)data;

  hide_updating ();

  if (dec == NULL)
    ;
  else if (dec->decode_int () == 0)
    annoy_user_with_log (_("ai_ni_operation_failed"));
  else
    {
      section_info *all_si = new section_info;
      all_si->symbolic_name = g_strdup ("all");
      all_si->name = nicify_section_name (all_si->symbolic_name);

      while (!dec->at_end ())
	{
	  package_info *info = NULL;

	  info = get_package_list_entry (dec);

	  if (info->installed_version && info->available_version)
	    {
	      info->ref ();
	      upgradeable_packages = g_list_prepend (upgradeable_packages,
						     info);
	    }
	  else if (info->available_version)
	    {
	      section_info *sec = find_section_info (&install_sections,
						     info->available_section,
						     true, false);
	      info->ref ();
	      sec->packages = g_list_prepend (sec->packages, info);

	      info->ref ();
	      all_si->packages = g_list_prepend (all_si->packages, info);

	    }
	  
	  if (info->installed_version)
	    {
	      info->ref ();
	      installed_packages = g_list_prepend (installed_packages,
						   info);
	    }

	  info->unref ();
	}

      if (g_list_length (all_si->packages) <= MAX_PACKAGES_NO_CATEGORIES)
	{
	  free_sections (install_sections);
	  install_sections = g_list_prepend (NULL, all_si);
	}
      else  if (g_list_length (install_sections) >= 2)
	install_sections = g_list_prepend (install_sections, all_si);
      else
	all_si->unref ();
    }

  sort_all_packages ();

  save_installed_packages_file ();

  /* We switch to the parent view if the current one is the search
     results view.
     
     We also switch to the parent when the current view shows a
     section and that section is no longer available, or when no
     sections should be shown because there are too few.
  */

  if (cur_view_struct == &search_results_view
      || (cur_view_struct == &install_section_view
	  && (find_section_info (&install_sections, cur_section_name,
				false, true) == NULL
	      || (install_sections && !install_sections->next))))
    show_view (cur_view_struct->parent);

  if (c->cont)
    c->cont (c->data);

  delete c;
}

static void
get_package_list_reply_temp (int cmd, apt_proto_decoder *dec, void *data)
{
  gpl_closure *c = (gpl_closure *) data;

  hide_updating ();

  if (dec == NULL)
    ;
  else if (dec->decode_int () == 0)
    annoy_user_with_log (_("ai_ni_operation_failed"));
  else
    {

      while (!dec->at_end ())
	{
	  package_info *info = NULL;

	  info = get_package_list_entry (dec);

	  info->ref ();
	  temp_packages = g_list_prepend (temp_packages,
					  info);
	  info->unref ();
	}
      
    }

  if (c->cont)
    c->cont (c->data);

  delete c;
}

static void
get_package_list_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  gpl_closure *c = (gpl_closure *)data;

  switch (c->state)
    {
    case APTSTATE_DEFAULT:
      get_package_list_reply_default (cmd, dec, data);
      break;
    case APTSTATE_TEMP:
      get_package_list_reply_temp (cmd, dec, data);
      break;
    default:
      delete c;
    }
}

void
get_package_list_with_cont (int state, void (*cont) (void *data), void *data)
{
  gpl_closure *c = new gpl_closure;
  c->cont = cont;
  c->data = data;
  c->state = state;

  if (state == APTSTATE_DEFAULT)
    {
      clear_global_package_list ();
      clear_global_section_list ();
    }
  free_all_packages (state);

  show_updating ();
  apt_worker_get_package_list (state,
			       !(red_pill_mode && red_pill_show_all),
			       false, 
			       false, 
			       NULL,
			       red_pill_mode && red_pill_show_magic_sys,
			       get_package_list_reply, c);
}

void
get_package_list (int state)
{
  get_package_list_with_cont (state, NULL, NULL);
}

struct gpi_closure {
  void (*func) (package_info *, void *, bool);
  void *data;
  package_info *pi;
};

static void
gpi_reply  (int cmd, apt_proto_decoder *dec, void *clos)
{
  gpi_closure *c = (gpi_closure *)clos;
  void (*func) (package_info *, void *, bool) = c->func;
  void *data = c->data;
  package_info *pi = c->pi;
  delete c;

  pi->have_info = false;
  if (dec)
    {
      dec->decode_mem (&(pi->info), sizeof (pi->info));
      if (!dec->corrupted ())
	{
	  pi->have_info = true;
	}
    }

  func (pi, data, true);
  pi->unref ();
}

void
call_with_package_info (package_info *pi,
			bool only_installable_info,
			void (*func) (package_info *, void *, bool),
			void *data,
			int state)
{
  if (pi->have_info
      && (only_installable_info
	  || pi->info.removable_status != status_unknown))
    func (pi, data, false);
  else
    {
      gpi_closure *c = new gpi_closure;
      c->func = func;
      c->data = data;
      c->pi = pi;
      pi->ref ();
      apt_worker_get_package_info (state, pi->name, only_installable_info,
				   gpi_reply, c);
    }
}

struct cwpl_closure
{
  GList *package_list;
  GList *current_node;
  bool only_installable_info;
  void (*cont) (void *);
  void *data;
  int state;
};

void
call_with_package_list_info_cont (package_info *pi, void *data, bool unused2)
{
  cwpl_closure *closure = (cwpl_closure *) data;

  if (pi && !pi->have_info)
    {
      pi->unref ();
      return;
    }
  
  if (closure->current_node == NULL)
    {
      closure->cont(closure->data);
      delete closure;
    }
  else
    {
      GList * current_package_node = closure->current_node;
      closure->current_node = g_list_next (closure->current_node);
      call_with_package_info ((package_info *) current_package_node->data,
			      closure->only_installable_info,
			      call_with_package_list_info_cont,
			      data,
			      closure->state);
    }
}

void
call_with_package_list_info (GList *package_list,
			      bool only_installable_info,
			      void (*cont) (void *),
			      void *data,
			      int state)
{
  cwpl_closure *closure = new cwpl_closure;
  closure->package_list = package_list;
  closure->current_node = package_list;
  closure->only_installable_info = only_installable_info;
  closure->cont = cont;
  closure->data = data;
  closure->state = state;

  call_with_package_list_info_cont (NULL, closure, TRUE);
}

static GList *cur_packages_for_info;
static GList *next_packages_for_info;

void row_changed (GtkTreeModel *model, GtkTreeIter *iter);

static package_info *intermediate_info;
static bool intermediate_only_installable;
static void (*intermediate_callback) (package_info *, void*, bool);
static void *intermediate_data;
static int intermediate_state;

static void
get_next_package_info (package_info *pi, void *unused, bool changed)
{
  int state = APTSTATE_DEFAULT;

  if (pi && !pi->have_info)
    {
      pi->unref ();
      return;
    }

  if (pi && changed)
    global_package_info_changed (pi);

  if (pi && pi == intermediate_info)
    {
      intermediate_info = NULL;
      if (intermediate_callback)
	{
	  intermediate_callback (pi, intermediate_data, changed);
	  state = intermediate_state;
	}
    }

  if (intermediate_info)
    call_with_package_info (intermediate_info, intermediate_only_installable,
			    get_next_package_info, NULL, state);
  else
    {
      cur_packages_for_info = next_packages_for_info;
      if (cur_packages_for_info)
	{
	  next_packages_for_info = cur_packages_for_info->next;
	  pi = (package_info *)cur_packages_for_info->data;
	  call_with_package_info (pi, true, get_next_package_info, NULL, state);
	}
    }
}

void
get_package_list_info (GList *packages)
{
  next_packages_for_info = packages;
  if (cur_packages_for_info == NULL && intermediate_info == NULL)
    get_next_package_info (NULL, NULL, false);
}

void
get_intermediate_package_info (package_info *pi,
			       bool only_installable_info,
			       void (*callback) (package_info *, void *, bool),
			       void *data,
			       int state)
{
  package_info *old_intermediate_info = intermediate_info;

  if (pi->have_info
      && (only_installable_info
	  || pi->info.removable_status != status_unknown))
    {
      if (callback)
	callback (pi, data, false);
      else
	pi->unref ();
    }
  else if (intermediate_info == NULL || intermediate_callback == NULL)
    {
      intermediate_info = pi;
      intermediate_only_installable = only_installable_info;
      intermediate_callback = callback;
      intermediate_data = data;
      intermediate_state = state;
    }
  else
    {
      printf ("package info request already pending.\n");
      pi->unref();
    }

  if (cur_packages_for_info == NULL && old_intermediate_info == NULL)
    get_next_package_info (NULL, NULL, false);
}

static void
annoy_user_with_result_code (int result_code, const char *failure,
			     bool upgrading,
			     void (*cont) (void *data), void *data)
{
  if (result_code == rescode_success) {
    if (cont)
      cont(data);
    return;
  }

  if (result_code == rescode_download_failed)
    annoy_user_with_cont (_("ai_ni_error_download_failed"), cont, data);
  else if (result_code == rescode_packages_not_found)
    annoy_user_with_cont (_("ai_ni_error_download_missing"), cont, data);
  else if (result_code == rescode_package_corrupted)
    {
      if (upgrading)
	annoy_user_with_cont (_("ai_ni_error_update_corrupted"), cont, data);
      else
	annoy_user_with_cont (_("ai_ni_error_install_corrupted"), cont, data);
    }
  else if (result_code == rescode_out_of_space)
    annoy_user_with_cont (dgettext ("hildon-common-strings",
				    "sfil_ni_not_enough_memory"), cont, data);
  else
    annoy_user_with_cont (failure, cont, data);
}

struct rpc_clos {
  bool res;
  const char *message;
  int state;
  void (*cont) (bool res, void *data);
  void *data;
};

static void
refresh_package_cache_cont3 (void *data)
{
  rpc_clos *c = (rpc_clos *) data;
  if (c->cont)
    c->cont (c->res, c->data);
  delete c;
}

static void
refresh_package_cache_cont2 (void *data)
{
  rpc_clos *c = (rpc_clos *) data;
  if (!c->res && c->message)
    annoy_user_with_cont (c->message, refresh_package_cache_cont3, c);
  else
    refresh_package_cache_cont3 (c);
}

static void
refresh_package_cache_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  rpc_clos *c = (rpc_clos *) data;
  bool success = true;

  /* The updating might have been 'cancelled' or it might have failed,
     and we want to distinguish between those two situations in the
     message shown to the user.

     It would be good to consider all explicit user actions as
     cancelling, but we don't get enough information for that.  Thus,
     the updating is considered cancelled only when the user hits our
     own cancel button, and not for example the cancel button in the
     "Select connection" dialog, or when the user explicitely
     disconnects the network.
  */

  hide_progress ();

  if (dec == NULL)
    {
      /* Network connection failed or apt-worker crashed.  An error
	 message has already been displayed.
      */
      c->message = NULL;
      success = false;
    }
  else if (progress_was_cancelled ())
    {
      /* The user hit cancel.  We don't care whether the operation was
	 successful or not.
      */
      c->message = _("ai_ni_update_list_cancelled");
      success = false;
    }
  else
    {
      int result_code = dec->decode_int ();

      if (result_code == rescode_download_failed)
	{
	  c->message = _("ai_ni_error_download_failed");
	  success = false;
	}
      else if (result_code != rescode_success)
	{
	  c->message = _("ai_ni_update_list_not_successful");
	  success = false;
	}
    }

  c->res = success;

  if (success)
    {
      last_update = time (NULL);
      save_settings ();
    }

  // We get a new list of available packages even when the refresh
  // failed because it might have partially succeeded and changed
  // anyway.  So we need to resynchronize.
  
  get_package_list_with_cont (c->state, refresh_package_cache_cont2, c);
}

static bool refreshed_this_session = false;

static void
refresh_package_cache_cont (bool res, void *data)
{
  rpc_clos *c = (rpc_clos *)data;

  if (res)
    {
      apt_worker_update_cache (c->state, refresh_package_cache_reply, c);
    }
  else
    {
      if (c->cont)
	c->cont (false, c->data);
      delete c;
    }
}

void
refresh_package_cache_with_cont (int state,
				 bool ask,
				 void (*cont) (bool res, void *data), 
				 void *data)
{
  refreshed_this_session = true;

  rpc_clos *c = new rpc_clos;
  c->cont = cont;
  c->data = data;
  c->state = state;

  if (ask)
    ask_yes_no (_("ai_nc_confirm_update"), refresh_package_cache_cont, c);
  else
    refresh_package_cache_cont (true, c);
}

void
refresh_package_cache (int state, bool ask)
{
  refresh_package_cache_with_cont (state, ask, NULL, NULL);
}

static int
days_elapsed_since (time_t past)
{
  time_t now = time (NULL);
  if (now == (time_t)-1)
    return 0;

  /* Truncating is the right rounding mode here.
   */
  return (now - past) / (24*60*60);
}

void
maybe_refresh_package_cache ()
{
  /* Never ask more than once per session.
   */
  if (refreshed_this_session)
    return;

  if (update_interval_index == UPDATE_INTERVAL_NEVER)
    return;

  if (update_interval_index == UPDATE_INTERVAL_WEEK
      && days_elapsed_since (last_update) < 7)
    return;

  if (update_interval_index == UPDATE_INTERVAL_MONTH
      && days_elapsed_since (last_update) < 30)
    return;

  refresh_package_cache (APTSTATE_DEFAULT, true);
}

static void
confirm_install (package_info *pi,
		 void (*cont) (bool res, void *data), void *data)
{
  GString *text = g_string_new ("");
  char download_buf[20];
  
  size_string_general (download_buf, 20, pi->info.download_size);

  g_string_printf (text,
		   (pi->installed_version
		    ? _("ai_nc_update")
		    : _("ai_nc_install")),
		   pi->name, pi->available_version, download_buf);
  
  ask_yes_no_with_details ((pi->installed_version
			    ? _("ai_ti_confirm_update")
			    : _("ai_ti_confirm_install")),
			   text->str, pi, install_details, cont, data);
  g_string_free (text, 1);
}

static void
clean_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  /* Failure messages are in the log.  We don't annoy the user here.
     However, if cleaning takes really long, the user might get
     confused since apt-worker is not responding.
   */
}

struct ip_closure {
  package_info *pi;
  GSList *upgrade_names;
  GSList *upgrade_versions;
  char *cur_name;
  void (*cont) (void *data);
  void *data;
  int state;
};

static void
install_package_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  ip_closure *c = (ip_closure *) data;
  package_info *pi = c->pi;

  hide_progress ();

  if (dec == NULL)
    {
      pi->unref ();
      end_dialog_flow ();
      delete c;
      return;
    }

  apt_proto_result_code result_code =
    apt_proto_result_code (dec->decode_int ());

  if (clean_after_install)
    {
      apt_worker_clean (c->state, clean_reply, NULL);
    }

  bool upgrading = (pi->installed_version != NULL);

  if (result_code == rescode_success)
    {
      char *str = g_strdup_printf ((upgrading
				    ? _("ai_ni_update_successful")
				    : _("ai_ni_install_successful")),
				   pi->name);
      annoy_user_with_cont (str, c->cont, c->data);
      g_free (str);
    }
  else
    {
      if (progress_was_cancelled ())
	{
	  if (c->cont)
	    c->cont (c->data);
	  else
	    end_dialog_flow ();
	}
      else
	{
	  char *str =
	    g_strdup_printf ((upgrading
			      ? _("ai_ni_error_update_failed")
			      : _("ai_ni_error_installation_failed")),
			     pi->name);

	  result_code = scan_log_for_result_code (result_code);

	  annoy_user_with_result_code (result_code, str, upgrading, c->cont, c->data);
	  g_free (str);
	}
    }

  delete c;

  pi->unref ();

  get_package_list (APTSTATE_DEFAULT);
}

struct ipc_closure2 {
  package_info *pi;
  void (*cont) (void *data);
  void *data;
  int state;
};
  
static void install_package_cont3 (bool res, void *data);

static void
install_package_cont4 (int status, void *data)
{
  ip_closure *c = (ip_closure *)data;

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 111)
    {
      char *str =
	g_strdup_printf (_("ai_ni_error_uninstall_applicationrunning"),
			 c->cur_name);
      annoy_user (str);
      g_free (str);

      install_package_cont3 (false, data);
    }
  else
    install_package_cont3 (true, data);

}

static void
install_package_cont3 (bool res, void *data)
{
  ip_closure *c = (ip_closure *)data;

  if (c->upgrade_names == NULL)
    {
      if (res)
	{
	  set_log_start ();
	  apt_worker_install_package (c->state, c->pi->name,
				      c->pi->installed_version != NULL,
				      install_package_reply, data);
	}
      else {
	c->pi->unref ();
	if (c->cont)
	  c->cont(c->data);
	else
	  end_dialog_flow ();
	delete c;
      }

      g_free (c->cur_name);
    }
  else
    {
      char *name = (char *)pop (c->upgrade_names);
      char *version = (char *)pop (c->upgrade_versions);

      if (res)
	{
	  char *cmd =
	    g_strdup_printf ("/var/lib/hildon-application-manager/info/%s.checkrm",
			     name);
      
	  char *argv[] = { cmd, "upgrade", version, NULL };
	  run_cmd (argv, install_package_cont4, c);
	  g_free (cmd);
	}
      else
	install_package_cont3 (false, data);

      g_free (c->cur_name);
      c->cur_name = name;
      g_free (version);
    }
}

static void
install_check_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  GList *notauth = NULL, *notcert = NULL;
  ipc_closure2 *closure2 = (ipc_closure2 *) data;

  ip_closure *c = new ip_closure;
  c->pi = closure2->pi;
  c->cont = closure2->cont;
  c->data = closure2->data;
  c->state = closure2->state;
  c->upgrade_names = c->upgrade_versions = NULL;
  c->cur_name = NULL;
  delete closure2;

  if (dec == NULL)
    {
      c->pi->unref ();
      delete c;
      end_dialog_flow ();
      return;
    }

  while (!dec->corrupted ())
    {
      apt_proto_preptype prep = apt_proto_preptype (dec->decode_int ());
      if (prep == preptype_end)
	break;

      const char *string = dec->decode_string_in_place ();
      if (prep == preptype_notauth)
	notauth = g_list_append (notauth, (void*)string);
      else if (prep == preptype_notcert)
	notcert = g_list_append (notcert, (void*)string);
    }

  while (!dec->corrupted ())
    {
      char *name = dec->decode_string_dup ();
      if (name == NULL)
	break;

      char *version = dec->decode_string_dup ();

      push (c->upgrade_names, name);
      push (c->upgrade_versions, version);
    }

  int success = dec->decode_int ();

  if (success)
    {
      if (notauth)
	scare_user_with_legalese (false, install_package_cont3, c);
      else if (notcert)
	scare_user_with_legalese (true, install_package_cont3, c);
      else
	install_package_cont3 (true, c);
    }
  else
    {
      char *str = g_strdup_printf ((c->pi->installed_version
				    ? _("ai_ni_error_update_failed")
				    : _("ai_ni_error_installation_failed")),
				   c->pi->name);
      annoy_user_with_log (str);
      g_free (str);
      install_package_cont3 (false, c);
    }

  g_list_free (notauth);
  g_list_free (notcert);
}

static void
annoy_user_with_installable_status_details (package_info *pi, void (*cont) (void *data), void *data)
{
  if (pi->info.installable_status == status_missing)
    {
      annoy_user_with_details_with_cont ((pi->installed_version
					  ? _("ai_ni_error_update_missing")
					  : _("ai_ni_error_install_missing")),
					 pi, install_details, cont, data);
    }
  else if (pi->info.installable_status == status_conflicting)
    {
      annoy_user_with_details_with_cont ((pi->installed_version
					  ? _("ai_ni_error_update_conflict")
					  : _("ai_ni_error_install_conflict")),
					 pi, install_details, cont, data);
    }
  else if (pi->info.installable_status == status_corrupted)
    {
      annoy_user_with_cont ((pi->installed_version
			     ? _("ai_ni_error_update_corrupted")
			     : _("ai_ni_error_install_corrupted")),
			    cont, data);
    }
  else if (pi->info.installable_status == status_incompatible)
    {
      annoy_user_with_cont ((pi->installed_version
			     ? _("ai_ni_error_update_incompatible")
			     : _("ai_ni_error_install_incompatible")),
			    cont, data);
    }
  else if (pi->info.installable_status == status_incompatible_current)
    {
      annoy_user_with_cont (_("ai_ni_error_n770package_incompatible"), cont, data);
    }
  else
    {
      char *str =
	g_strdup_printf ((pi->installed_version
			  ? _("ai_ni_error_update_failed")
			  : _("ai_ni_error_installation_failed")),
			 pi->name);
      annoy_user_with_details_with_cont (str, pi, install_details, cont, data);
      g_free (str);
    }
}

static void
install_package_with_net (bool res, void *data)
{
  ipc_closure2 * closure2 = (ipc_closure2 *)data;
  package_info *pi = (package_info *)closure2->pi;

  if (res)
    {
      if (pi->info.installable_status == status_able)
	{
	  add_log ("-----\n");
	  if (pi->installed_version)
	    add_log ("Upgrading %s %s to %s\n", pi->name,
		     pi->installed_version, pi->available_version);
	  else
	    add_log ("Installing %s %s\n", pi->name, pi->available_version);
	  
	  apt_worker_install_check (closure2->state, pi->name, install_check_reply, closure2);
	}
      else
	{
	  annoy_user_with_installable_status_details (pi, closure2->cont, closure2->data);
	  pi->unref ();
	  delete closure2;
	}
    }
  else {
    pi->unref ();
    if (closure2->cont)
      closure2->cont(closure2->data);
    else
      end_dialog_flow ();
    delete closure2;
  }
}

static void
install_package_cont2 (bool res, void *data)
{
  ipc_closure2 * closure2 = (ipc_closure2 *)data;
  package_info *pi = (package_info *)closure2->pi;
  
  if (res)
    {
      // We ask for the network here although we only really need it for
      // the APTCMD_INSTALL_PACKAGE request.  We do it this early to let
      // the user cancel the network connection procedure very early.  The
      // network is requested again when it is actually needed.

      ensure_network (install_package_with_net, closure2);
    }
  else
    {
      if (closure2->cont != NULL)
	closure2->cont(closure2->data);
      else
	end_dialog_flow ();
      pi->unref ();
      delete closure2;
    }
}

struct ipc_closure {
  void (*cont) (void *data);
  void *data;
  int state;
  bool ask;
};

static void
install_package_cont (package_info *pi, void *data, bool changed)
{
  ipc_closure *closure = (ipc_closure *) data;
  ipc_closure2 *closure2 = new ipc_closure2;
  bool ask = closure->ask;
  closure2->pi = pi;
  closure2->cont = closure->cont;
  closure2->data = closure->data;
  closure2->state = closure->state;
  delete closure;
  if (ask)
    confirm_install (pi, install_package_cont2, closure2);
  else
    install_package_cont2 (true, closure2);
}

static void
install_package (int state, bool ask, package_info *pi,
		 void (*cont) (void *data), void *data)
{
  ipc_closure * closure = new ipc_closure;
  closure->cont = cont;
  closure->data = data;
  closure->state = state;
  closure->ask = ask;
  pi->ref ();
  get_intermediate_package_info (pi, true, install_package_cont, closure, state);
}

struct install_packages_closure {
  int state;
  GList *package_list;
  int install_type;
};

static void
install_packages_cont (void *data)
{
  install_packages_closure *closure = (install_packages_closure *) data;
  package_info *pi = NULL;
  if (closure->package_list != NULL)
    {
      pi = (package_info *) closure->package_list->data;
      closure->package_list = g_list_delete_link (closure->package_list, closure->package_list);
      install_package (closure->state, false, pi,
		       install_packages_cont, closure);
      pi->unref ();
    }
  else
    {
      delete closure;
      end_dialog_flow ();
    }
}

static void
install_packages_response (gboolean res,
			   GList *package_list,
			   void *data)
{
  GList *node = NULL;
  package_info *pi = NULL;
  install_packages_closure *closure = (install_packages_closure *) data;

  for (node = closure->package_list; node != NULL; node = g_list_next (node))
    {
      if (node->data != NULL)
	{
	  pi = (package_info *) node->data;
	  pi->unref ();
	}
    }
  g_list_free (closure->package_list);

  if (res)
    {
      closure->package_list = package_list;
      install_packages_cont (closure);
    }
  else
    {
      for (node = package_list; node != NULL; node = g_list_next (node))
	{
	  pi = (package_info *) node->data;
	  pi->unref ();
	}
      g_list_free (package_list);
      delete closure;
      end_dialog_flow ();
    }
}

static void
install_packages_with_package_info (void *data)
{
  install_packages_closure *closure = (install_packages_closure *) data;
  char *select_dialog_message = NULL;

  switch (closure->install_type)
    {
    case INSTALL_TYPE_BACKUP:
      select_dialog_message = _("ai_ti_restore");
      break;
    case INSTALL_TYPE_MEMORY_CARD:
      select_dialog_message = _("ai_ti_memory");
      break;
      /* Should only go to the branches below in red pill mode */
    case INSTALL_TYPE_STANDARD:
    default:
      select_dialog_message = "The following applications are available:";
      break;
    };

  select_package_list (closure->package_list, 
		       _("ai_ti_install_apps"), 
		       select_dialog_message,
		       install_packages_response,
		       closure);
}

static void
install_packages (int state, GList *package_list, int install_type)
{
  install_packages_closure *closure = new install_packages_closure;
  closure->state = state;
  closure->package_list = package_list;
  closure->install_type = install_type;
  call_with_package_list_info (package_list, FALSE, 
			       install_packages_with_package_info, closure, state);
}

static void
available_package_details (gpointer data)
{
  package_info *pi = (package_info *)data;
  show_package_details (pi, install_details, false);
}

static void
install_operation_callback (gpointer data)
{
  install_package (APTSTATE_DEFAULT, true, (package_info *)data, NULL, NULL);
}

void
available_package_selected (package_info *pi)
{
  if (pi)
    {
      set_details_callback (available_package_details, pi);
      set_operation_callback (install_operation_callback, pi);
      pi->ref ();
      get_intermediate_package_info (pi, true, NULL, NULL, APTSTATE_DEFAULT);
    }
  else
    {
      set_details_callback (NULL, NULL);
      set_operation_callback (NULL, NULL);
    }
}

static void
available_package_activated (package_info *pi)
{
  install_package (APTSTATE_DEFAULT, true, pi, NULL, NULL);
}

static void
installed_package_details (gpointer data)
{
  package_info *pi = (package_info *)data;
  show_package_details (pi, remove_details, false);
}

static void uninstall_package (int state, package_info *, void (*cont) (void * data), void *data);

static void
uninstall_operation_callback (gpointer data)
{
  uninstall_package (APTSTATE_DEFAULT, (package_info *)data, NULL, NULL);
}

void
installed_package_selected (package_info *pi)
{
  if (pi)
    {
      set_details_callback (installed_package_details, pi);
      set_operation_callback (uninstall_operation_callback, pi);
    }
  else
    {
      set_details_callback (NULL, NULL);
      set_operation_callback (NULL, NULL);
    }
}

static void
installed_package_activated (package_info *pi)
{
  uninstall_package (APTSTATE_DEFAULT, pi, NULL, NULL);
}

static GtkWidget *
make_last_update_label ()
{
  char time_string[1024];

  if (last_update == 0)
    strncpy (time_string, _("ai_li_never"), 1024);
  else
    {
      time_t t = last_update;
      struct tm *broken = localtime (&t);
      strftime (time_string, 1024, "%x", broken);
    }

  char *text = g_strdup_printf (_("ai_li_updated_%s"), time_string);
  GtkWidget *label = gtk_label_new (text);
  g_free (text);

  return label;
}

static GtkWidget *
make_package_list_view (GtkWidget *list_widget,
			bool with_updated_label)
{
  GtkWidget *view;

  if (with_updated_label)
    {
      GtkWidget *label = make_last_update_label ();
      view = gtk_vbox_new (FALSE, 5);
      gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
      gtk_box_pack_start (GTK_BOX (view), label, FALSE, FALSE, 0);
      gtk_box_pack_start (GTK_BOX (view), list_widget, TRUE, TRUE, 0);
    }
  else
    view = list_widget;
  
  return view;
}

GtkWidget *
make_install_section_view (view *v)
{
  GtkWidget *view;

  set_operation_label (_("ai_me_package_install"),
		       _("ai_ib_nothing_to_install"));

  section_info *si = find_section_info (&install_sections,
					cur_section_name, false, true);

  GtkWidget *list =
    make_global_package_list (si? si->packages : NULL,
			      false,
			      _("ai_li_no_applications_available"),
			      _("ai_me_cs_install"),
			      available_package_selected, 
			      available_package_activated);

  view = make_package_list_view (list, true);
  gtk_widget_show_all (view);

  if (si)
    get_package_list_info (si->packages);

  enable_search (true);
  set_current_help_topic (AI_TOPIC ("packagesview"));

  return view;
}

static void
view_section (section_info *si)
{
  g_free (cur_section_name);
  cur_section_name = g_strdup (si->symbolic_name);

  install_section_view.label = nicify_section_name (cur_section_name);

  show_view (&install_section_view);
}

static void
check_catalogues_reply (xexp *catalogues, void *data)
{
  if (dec == NULL)
    return;

  for (xexp *c = xexp_first (catalogues); c; c = xexp_rest (c))
    if (xexp_is (c, "source")
	|| (xexp_is (c, "catalogue") && !xexp_aref_bool (c, "disabled")))
      {
	/* Found something in sources.list or an active catalogue.
	   Maybe we need to refresh the cache.
	*/
	maybe_refresh_package_cache ();
	xexp_free (catalogues);
	return;
      }

  /* No catalogues active.
   */
  irritate_user (_("ai_ib_no_repositories"));
  xexp_free (catalogues);
}

static void
check_catalogues ()
{
  get_catalogues (check_catalogues_reply, NULL);
}

GtkWidget *
make_install_applications_view (view *v)
{
  GtkWidget *list, *view, *label;

  check_catalogues ();

  set_operation_label (_("ai_me_package_install"),
		       _("ai_ib_nothing_to_install"));

  if (install_sections && install_sections->next == NULL)
    {
      section_info *si = (section_info *)install_sections->data;
      GtkWidget *list =
	make_global_package_list (si->packages,
				  false,
				  _("ai_li_no_applications_available"),
				  _("ai_me_cs_install"),
				  available_package_selected, 
				  available_package_activated);
      get_package_list_info (si->packages);
      set_current_help_topic (AI_TOPIC ("packagesview"));

      view = make_package_list_view (list, true);
    }
  else
    {
      list = make_global_section_list (install_sections, view_section);
      label = make_last_update_label ();
      view = gtk_vbox_new (FALSE, 10);
      
      gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
      gtk_box_pack_start (GTK_BOX (view), label, FALSE, FALSE, 0);
      gtk_box_pack_start (GTK_BOX (view), list, TRUE, TRUE, 0);

      set_current_help_topic (AI_TOPIC ("sectionsview"));
    }

  gtk_widget_show_all (view);

  enable_search (true);
  
  return view;
}

GtkWidget *
make_upgrade_applications_view (view *v)
{
  GtkWidget *view;

  check_catalogues ();

  set_operation_label (_("ai_me_package_update"),
		       _("ai_ib_nothing_to_update"));

  GtkWidget *list =
    make_global_package_list (upgradeable_packages,
			      false,
			      _("ai_li_no_updates_available"),
			      _("ai_me_cs_update"),
			      available_package_selected,
			      available_package_activated);

  view = make_package_list_view (list, true);
  gtk_widget_show_all (view);

  get_package_list_info (upgradeable_packages);

  enable_search (true);
  set_current_help_topic (AI_TOPIC ("updateview"));

  return view;
}

static bool
confirm_uninstall (package_info *pi,
		   void (*cont) (bool res, void *data), void *data)
{
  GString *text = g_string_new ("");
  char size_buf[20];
  
  size_string_general (size_buf, 20, pi->installed_size);
  g_string_printf (text, _("ai_nc_uninstall"),
		   pi->name, pi->installed_version, size_buf);

  ask_yes_no_with_details (_("ai_ti_confirm_uninstall"), text->str,
			   pi, remove_details, cont, data);
  g_string_free (text, 1);

  return TRUE;
}

static void
uninstall_package_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  package_info *pi = (package_info *)data;

  hide_progress ();

  if (dec == NULL)
    {
      pi->unref ();
      return;
    }

  int success = dec->decode_int ();
  get_package_list (APTSTATE_DEFAULT);

  if (success)
    {
      char *str = g_strdup_printf (_("ai_ni_uninstall_successful"),
				   pi->name);
      annoy_user (str);
      g_free (str);
    }
  else
    {
      char *str = g_strdup_printf (_("ai_ni_error_uninstallation_failed"),
				   pi->name);
      annoy_user_with_log (str);
      g_free (str);
    }

  pi->unref ();
}

static void
annoy_user_with_removable_status_details (package_info *pi)
{
  if (pi->info.removable_status == status_needed)
    annoy_user_with_details (_("ai_ni_error_uninstall_packagesneeded"),
			     pi, remove_details);
  else
    {
      char *str = g_strdup_printf (_("ai_ni_error_uninstallation_failed"),
				   pi->name);
      annoy_user_with_details (str, pi, remove_details);
      g_free (str);
    }
}


static void
uninstall_package_doit (package_info *pi)
{
  if (pi->info.removable_status == status_able)
    {
      add_log ("-----\n");
      add_log ("Uninstalling %s %s\n", pi->name, pi->installed_version);
      
      show_progress (_("ai_nw_uninstalling"));
      apt_worker_remove_package (pi->name, uninstall_package_reply, pi);
    }
  else
    {
      annoy_user_with_removable_status_details (pi);
      pi->unref ();
    }
}

struct uip_closure {
  package_info *pi;
  GSList *to_remove;
  char *cur_name;
};

static void
check_uninstall_scripts2 (int status, void *data)
{
  uip_closure *c = (uip_closure *)data;

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 111)
    {
      char *str =
	g_strdup_printf (_("ai_ni_error_uninstall_applicationrunning"),
			 c->cur_name);
      annoy_user (str);
      g_free (str);

      c->pi->unref ();
      g_free (c->cur_name);
      // XXX delete c->to_remove list
      delete c;
    }
  else if (c->to_remove)
    {
      GSList *next = c->to_remove->next;
      char *name = (char *)c->to_remove->data;
      g_slist_free_1 (c->to_remove);
      c->to_remove = next;

      g_free (c->cur_name);
      c->cur_name = name;

      char *cmd =
	g_strdup_printf ("/var/lib/hildon-application-manager/info/%s.checkrm",
			 name);
      
      char *argv[] = { cmd, "remove", NULL };
      run_cmd (argv, check_uninstall_scripts2, c);
      g_free (cmd);
    }
  else
    {
      uninstall_package_doit (c->pi);
      g_free (c->cur_name);
      delete c;
    }
}

static void
get_packages_to_remove_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  package_info *pi = (package_info *)data;
  GSList *names = NULL;
  
  if (dec == NULL)
    {
      pi->unref ();
      return;
    }

  while (true)
    {
      char *name = dec->decode_string_dup ();
      if (name == NULL)
	break;
      names = g_slist_prepend (names, name);
    }

  uip_closure *c = new uip_closure;
  c->pi = pi;
  c->to_remove = names;
  c->cur_name = NULL;
  check_uninstall_scripts2 (0, c);
}

static void
check_uninstall_scripts (package_info *pi)
{
  apt_worker_get_packages_to_remove (pi->name,
				     get_packages_to_remove_reply, pi);
}

static void
uninstall_package_cont2 (bool res, void *data)
{
  package_info *pi = (package_info *)data;

  if (res)
    check_uninstall_scripts (pi);
  else
    pi->unref ();
}

static void
uninstall_package_cont (package_info *pi, void *data, bool changed)
{
  confirm_uninstall (pi, uninstall_package_cont2, pi);
}

static void
uninstall_package (int state, package_info *pi, void (*cont) (void *data), void *data)
{
  pi->ref ();
  get_intermediate_package_info (pi, false, uninstall_package_cont, NULL, state);
}

GtkWidget *
make_uninstall_applications_view (view *v)
{
  GtkWidget *view;

  set_operation_label (_("ai_me_package_uninstall"),
		       _("ai_ib_nothing_to_uninstall"));

  view = make_global_package_list (installed_packages,
				   true,
				   _("ai_li_no_installed_applications"),
				   _("ai_me_cs_uninstall"),
				   installed_package_selected,
				   installed_package_activated);
  gtk_widget_show_all (view);

  enable_search (true);
  set_current_help_topic (AI_TOPIC ("uninstallview"));

  return view;
}

GtkWidget *
make_search_results_view (view *v)
{
  GtkWidget *view;

  if (v->parent == &install_applications_view
      || v->parent == &upgrade_applications_view)
    {
      set_operation_label (v->parent == &install_applications_view
			   ? _("ai_me_package_install")
			   : _("ai_me_package_update"),
			   v->parent == &install_applications_view
			   ? _("ai_ib_nothing_to_install")
			   : _("ai_ib_nothing_to_update"));

      view = make_global_package_list (search_result_packages,
				       false,
				       NULL,
				       (v->parent == &install_applications_view
					? _("ai_me_cs_install")
					: _("ai_me_cs_update")),
				       available_package_selected,
				       available_package_activated);
      get_package_list_info (search_result_packages);
    }
  else
    {
      set_operation_label (_("ai_me_package_uninstall"),
			   _("ai_ib_nothing_to_uninstall"));

      view = make_global_package_list (search_result_packages,
				       true,
				       NULL,
				       _("ai_me_cs_uninstall"),
				       installed_package_selected,
				       installed_package_activated);
    }
  gtk_widget_show_all (view);

  enable_search (true);
  set_current_help_topic (AI_TOPIC ("searchresultsview"));

  return view;
}

static void
search_package_list (GList **result,
		     GList *packages, const char *pattern)
{
  while (packages)
    {
      package_info *pi = (package_info *)packages->data;
      
      // XXX
      if (strcasestr (pi->name, pattern))
	{
	  pi->ref ();
	  *result = g_list_append (*result, pi);
	}

      packages = packages->next;
    }
}

static void
find_in_package_list (GList **result,
		      GList *packages, const char *name)
{
  while (packages)
    {
      package_info *pi = (package_info *)packages->data;
      
      if (!strcmp (pi->name, name))
	{
	  pi->ref ();
	  *result = g_list_append (*result, pi);
	}

      packages = packages->next;
    }
}

static void
find_in_section_list (GList **result,
		      GList *sections, const char *name)
{
  while (sections)
    {
      section_info *si = (section_info *)sections->data;
      find_in_package_list (result, si->packages, name);

      sections = sections->next;
    }
}

static void
find_package_in_lists (int state,
		       GList **result,
		       const char *package_name)
{
  switch (state)
    {
    case APTSTATE_DEFAULT:
      find_in_section_list (result, install_sections, package_name);
      find_in_package_list (result, upgradeable_packages, package_name);
      find_in_package_list (result, installed_packages, package_name);
      break;
    case APTSTATE_TEMP:
      find_in_package_list (result, temp_packages, package_name);
      break;
    default:
      break;
    }
}

static void
search_packages_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  view *parent = (view *)data;

  hide_updating ();

  if (dec == NULL)
    return;

  int success = dec->decode_int ();

  if (!success)
    {
      annoy_user_with_log (_("ai_ni_operation_failed"));
      return;
    }

  GList *result = NULL;

  while (!dec->at_end ())
    {
      const char *name = dec->decode_string_in_place ();
      dec->decode_string_in_place (); // installed_version
      dec->decode_int ();             // broken
      dec->decode_int ();             // installed_size
      dec->decode_string_in_place (); // installed_section
      dec->decode_string_in_place (); // installed_short_description
      dec->decode_string_in_place (); // installed_icon
      dec->decode_string_in_place (); // available_version
      dec->decode_string_in_place (); // available_section
      dec->decode_string_in_place (); // available_short_description
      dec->decode_string_in_place (); // available_icon

      if (parent == &install_applications_view)
	find_in_section_list (&result,
			      install_sections, name);
      else if (parent == &upgrade_applications_view)
	find_in_package_list (&result,
			      upgradeable_packages, name);
      else if (parent == &uninstall_applications_view)
	find_in_package_list (&result,
			      installed_packages, name);
    }

  if (result)
    {
      clear_global_package_list ();
      free_packages (search_result_packages);
      search_result_packages = result;
      show_view (&search_results_view);
      irritate_user (_("ai_ib_search_complete"));
    }
  else
    irritate_user (_("ai_ib_no_matches"));
}

void
search_packages (const char *pattern, bool in_descriptions)
{
  view *parent;

  if (cur_view_struct == &search_results_view)
    parent = search_results_view.parent;
  else if (cur_view_struct == &install_section_view)
    parent = &install_applications_view;
  else if (cur_view_struct == &main_view)
    parent = &uninstall_applications_view;
  else
    parent = cur_view_struct;

  search_results_view.parent = parent;

  if (!in_descriptions)
    {
      GList *result = NULL;

      if (parent == &install_applications_view)
	{
	  // We only search the first section in INSTALL_SECTIONS.
	  // The first section in the list is either the special "All"
	  // section that contains all packages, or there is only one
	  // section.  In both cases, it is correct to only search the
	  // first section.

	  if (install_sections)
	    {
	      section_info *si = (section_info *)install_sections->data;
	      search_package_list (&result, si->packages, pattern);
	    }
	}
      else if (parent == &upgrade_applications_view)
	search_package_list (&result,
			     upgradeable_packages, pattern);
      else if (parent == &uninstall_applications_view)
	search_package_list (&result,
			     installed_packages, pattern);

      if (result)
	{
	  clear_global_package_list ();
	  free_packages (search_result_packages);
	  search_result_packages = result;
	  show_view (&search_results_view);
	  irritate_user (_("ai_ib_search_complete"));
	}
      else
	irritate_user (_("ai_ib_no_matches"));
    }
  else
    {
      show_updating (_("ai_nw_searching"));

      bool only_installed = (parent == &uninstall_applications_view
			     || parent == &upgrade_applications_view);
      bool only_available = (parent == &install_applications_view
			     || parent == &upgrade_applications_view);
      apt_worker_get_package_list (APTSTATE_DEFAULT,
				   !(red_pill_mode && red_pill_show_all),
				   only_installed,
				   only_available, 
				   pattern,
				   red_pill_mode && red_pill_show_magic_sys,
				   search_packages_reply, parent);
    }
}

void
install_named_package (int state, const char *package, void (*cont) (void *data), void *data)
{
  GList *p = NULL;
  GList *node = NULL;

  find_package_in_lists (state, &p, package);

  if (p == NULL)
    annoy_user_with_cont (_("ai_ni_error_download_missing"), cont, data);
  else
    {
      package_info *pi = (package_info *) p->data;
      if (pi->available_version == NULL)
	{
	  char *text = g_strdup_printf (_("ai_ni_package_installed"),
					package);
	  annoy_user_with_cont (text, cont, data);
	  pi->unref ();
	  g_free (text);
	}
      else
	install_package (state, true, pi, cont, data);
      for (node = p->next; node != NULL; node = g_list_next (node))
	{
	  pi = (package_info *) node->data;
	  pi->unref ();
	}
      g_list_free (p);
    }
}

void
install_named_packages (int state, const char **packages, int install_type)
{
  GList *package_list = NULL;
  char **current_package = NULL;

  /* Get packages information */
  for (current_package = (char **) packages; current_package != NULL && *current_package != NULL; current_package++)
    {
      GList *search_list = NULL;
      GList *node = NULL;

      g_strchug (*current_package);

      find_package_in_lists (state, &search_list, *current_package);

      if (search_list != NULL)
	{
	  /* Only offers non-installed packages */
	  package_info *pi = (package_info *)search_list->data;
	  if (pi->available_version != NULL)
	    {
	      package_list = g_list_append (package_list, pi);
	    }
	  else
	    {
	      pi->unref ();
	    }
	  for (node = search_list->next; node != NULL; node = g_list_next (node))
	    {
	      pi = (package_info *) node->data;
	      pi->unref ();
	    }
	}
      else
	add_log ("Package %s not found\n", *current_package);

      g_list_free (search_list);
    }
  
  if (package_list == NULL)
    {
      if (install_type == INSTALL_TYPE_BACKUP)
	annoy_user ("No applications found in the Catalogue");
      else
	annoy_user ("All applications are up-to-date");
      return;
    }

  install_packages (state, package_list, install_type);
}

static GtkWidget *details_button;
static void (*details_func) (gpointer);
static gpointer details_data;

void
show_current_details ()
{
  if (details_func)
    details_func (details_data);
}

static void
set_details_callback (void (*func) (gpointer), gpointer data)
{
  details_data = data;
  details_func = func;
  gtk_widget_set_sensitive (details_button, func != NULL);
  set_details_menu_sensitive (func != NULL);
}

static void (*operation_func) (gpointer);
static gpointer operation_data;
static const char *operation_label;
static const char *insensitive_operation_press_label = NULL;

void
do_current_operation ()
{
  if (operation_func)
    operation_func (operation_data);
}

static void set_operation_toolbar_label (const char *label, bool enable);

static void
set_operation_label (const char *label, const char *insens)
{
  if (label == NULL)
    {
      label = _("ai_me_package_install");
      insens = _("ai_ib_nothing_to_install");
    }

  if (insens == NULL)
    insens = _("ai_ib_not_available");

  operation_label = label;
  insensitive_operation_press_label = insens;
  set_operation_menu_label (label, operation_func != NULL, insens);
  set_operation_toolbar_label (label, operation_func != NULL);
}

static void
set_operation_callback (void (*func) (gpointer), gpointer data)
{
  operation_data = data;
  operation_func = func;
  set_operation_label (operation_label,
		       insensitive_operation_press_label);
}

static void
install_from_file_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  package_info *pi = (package_info *)data;

  hide_progress ();
  cleanup_temp_file ();

  if (dec == NULL)
    {
      pi->unref ();
      end_dialog_flow ();
      return;
    }

  int success = dec->decode_int ();

  get_package_list (APTSTATE_DEFAULT);

  if (success)
    {
      char *str = g_strdup_printf (pi->installed_version
				   ? _("ai_ni_update_successful")
				   : _("ai_ni_install_successful"),
				   pi->name);
      annoy_user (str);
      g_free (str);
    }
  else
    {
      char *str = g_strdup_printf (pi->installed_version
				   ? _("ai_ni_error_update_failed")
				   : _("ai_ni_error_installation_failed"),
				   pi->name);

      apt_proto_result_code result_code = rescode_failure;

      result_code = scan_log_for_result_code (result_code);

      annoy_user_with_result_code (result_code, str,
				   pi->installed_version != NULL, NULL, NULL);
      g_free (str);
    }

  pi->unref ();
}

void
install_from_file_cont4 (bool res, void *data)
{
  package_info *pi = (package_info *)data;

  if (res)
    {
      show_progress (pi->installed_version
		     ? _("ai_nw_updating")
		     : _("ai_nw_installing"));
      set_log_start ();
      apt_worker_install_file (pi->filename,
			       install_from_file_reply, pi);
    }
  else
    {
      cleanup_temp_file ();
      pi->unref ();
      end_dialog_flow ();
    }
}

void
install_from_file_cont3 (bool res, void *data)
{
  package_info *pi = (package_info *)data;

  if (res)
    scare_user_with_legalese (false, install_from_file_cont4, pi);
  else
    {
      cleanup_temp_file ();
      pi->unref ();
      end_dialog_flow ();
    }
}

static void
install_from_file_fail_end_dialog_flow (void *data)
{
  end_dialog_flow ();
}

void
install_from_file_fail (bool res, void *data)
{
  package_info *pi = (package_info *)data;

  if (res)
    annoy_user_with_installable_status_details (pi, install_from_file_fail_end_dialog_flow, NULL);

  pi->unref ();
}

static char *
first_line_of (const char *text)
{
  const char *end = strchr (text, '\n');
  if (end == NULL)
    return g_strdup (text);
  else
    return g_strndup (text, end-text);
}

static void
file_details_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  char *filename = (char *)data;

  if (dec == NULL)
    {
      cleanup_temp_file ();
      end_dialog_flow ();
      return;
    }

  package_info *pi = new package_info;

  pi->name = dec->decode_string_dup ();
  pi->broken = false;
  pi->filename = filename;
  pi->installed_version = dec->decode_string_dup ();
  pi->installed_size = dec->decode_int ();;
  pi->available_version = dec->decode_string_dup ();
  pi->maintainer = dec->decode_string_dup ();
  pi->available_section = dec->decode_string_dup ();
  pi->info.installable_status = dec->decode_int ();
  pi->info.install_user_size_delta = dec->decode_int ();
  pi->info.removable_status = status_unable; // not used
  pi->info.remove_user_size_delta = 0;
  pi->info.download_size = 0;
  pi->description = dec->decode_string_dup ();
  nicify_description_in_place (pi->description);
  pi->available_short_description = first_line_of (pi->description);
  pi->available_icon = pixbuf_from_base64 (dec->decode_string_in_place ());

  pi->have_info = true;
  pi->have_detail_kind = install_details;

  
  if (pi->info.installable_status == status_incompatible)
    pi->summary = g_strdup (_("ai_ni_error_install_incompatible"));
  else if (pi->info.installable_status == status_incompatible_current)
    pi->summary = g_strdup (_("ai_ni_error_n770package_incompatible"));
  else if (pi->info.installable_status == status_corrupted)
    pi->summary = g_strdup (_("ai_ni_error_install_corrupted"));
  else
    decode_summary (dec, pi, install_details);

  GString *text = g_string_new ("");

  char size_buf[20];
  size_string_general (size_buf, 20, pi->info.install_user_size_delta);
  if (pi->installed_version)
    g_string_printf (text, _("ai_nc_update"),
		     pi->name, pi->available_version, size_buf);
  else
    g_string_printf (text, _("ai_nc_install"),
		     pi->name, pi->available_version, size_buf);

  void (*cont) (bool res, void *);

  if (pi->info.installable_status == status_able)
    cont = install_from_file_cont3;
  else
    {
      cleanup_temp_file ();
      cont = install_from_file_fail;
    }

  ask_yes_no_with_details ((pi->installed_version
			    ? _("ai_ti_confirm_update")
			    : _("ai_ti_confirm_install")),
			   text->str,
			   pi, install_details, cont, pi);
    
  g_string_free (text, 1);
}

static void
install_from_file_cont2 (char *filename, void *unused)
{
  if (filename)
    {
      if (g_str_has_suffix (filename, ".install"))
	{
	  open_local_install_instructions (filename);
	  g_free (filename);
	}
      else
	apt_worker_get_file_details (!(red_pill_mode && red_pill_show_all),
				     filename,
				     file_details_reply, filename);
    }
}

static void
install_from_file_cont (char *uri, void *unused)
{
  localize_file_and_keep_it_open (uri, install_from_file_cont2, NULL);
  g_free (uri);
}

void
install_from_file ()
{
  show_deb_file_chooser (install_from_file_cont, NULL);
}

static void
window_destroy (GtkWidget* widget, gpointer data)
{
  gtk_main_quit ();
}

static void
main_window_realized (GtkWidget* widget, gpointer unused)
{
  GdkWindow *win = widget->window;

  /* Some utilities search for our main window so that they can make
     their dialogs transient for it.  They identify our window by
     name.
  */
  XStoreName (GDK_WINDOW_XDISPLAY (win), GDK_WINDOW_XID (win),
	      "hildon-application-manager");
}

static void
apt_status_callback (int cmd, apt_proto_decoder *dec, void *unused)
{
  if (dec == NULL)
    return;

  int op = dec->decode_int ();
  int already = dec->decode_int ();
  int total = dec->decode_int ();

  set_progress ((apt_proto_operation)op, already, total);
}

static gboolean
handle_apt_worker (GIOChannel *channel, GIOCondition cond, gpointer data)
{
  if (apt_worker_is_running ())
    {
      handle_one_apt_worker_response ();
      return TRUE;
    }
  else
    return FALSE;
}

void
add_apt_worker_handler ()
{
  GIOChannel *channel = g_io_channel_unix_new (apt_worker_in_fd);
  apt_source_id = g_io_add_watch (channel,
				  GIOCondition (G_IO_IN | G_IO_HUP | G_IO_ERR),
				  handle_apt_worker, NULL);
  g_io_channel_unref (channel);
}

static GtkWindow *main_window = NULL;

static void
mime_open_handler (gpointer raw_data, int argc, char **argv)
{
  if (argc > 0)
    {
      const char *filename = argv[0];

//       if (something_started)
// 	{
// 	  present_main_window ();
// 	}
      push_no_parent ();
      install_from_file_cont (g_strdup (filename), NULL);
      gtk_window_iconify (GTK_WINDOW(main_window));
      something_started = TRUE;
    }
}

static void
hw_state_handler (osso_hw_state_t *state, gpointer data)
{
  if (state->shutdown_ind)
    gtk_main_quit ();
}

static GtkWidget *toolbar_operation_label = NULL;
static GtkWidget *toolbar_operation_item = NULL;

static void
set_operation_toolbar_label (const char *label, bool sensitive)
{
  if (toolbar_operation_label)
    gtk_label_set_text (GTK_LABEL (toolbar_operation_label), label);
  if (toolbar_operation_item)
    gtk_widget_set_sensitive (toolbar_operation_item, sensitive);
}

static GtkWidget *main_toolbar;

static bool is_fullscreen = false;

GtkWindow *
get_main_window ()
{
  return main_window;
}

GtkWidget *
get_main_trail ()
{
  return main_trail;
}

GtkWidget *
get_device_label ()
{
  return device_label;
}

void
present_main_window ()
{
  if (main_window)
    gtk_window_present (main_window);
}

static void
set_current_toolbar_visibility (bool f)
{
  if (main_toolbar)
    {
      if (f)
	gtk_widget_show (main_toolbar);
      else
	gtk_widget_hide (main_toolbar);
    }
}

void
set_fullscreen (bool f)
{
  if (f)
    gtk_window_fullscreen (main_window);
  else
    gtk_window_unfullscreen (main_window);
}

void
toggle_fullscreen ()
{
  set_fullscreen (!is_fullscreen);
}

void
set_toolbar_visibility (bool fullscreen, bool visibility)
{
  if (fullscreen)
    {
      fullscreen_toolbar = visibility;
      if (is_fullscreen)
	set_current_toolbar_visibility (visibility);
    }
  else
    {
      normal_toolbar = visibility;
      if (!is_fullscreen)
	set_current_toolbar_visibility (visibility);
    }
  save_settings ();
}

/* If application becomes topmost, app manager should be
 * marked to keep running even when there's a dialog flow
 * running and it finishes. */
static void
window_property_topmost_notify_event (GObject *object,
				      GParamSpec *param_spec,
				      gpointer user_data)
{
  gboolean is_topmost = FALSE;
  
  g_object_get (object, "is-topmost", &is_topmost, NULL);

  if (is_topmost)
    mark_keep_running ();
}

static gboolean
window_state_event (GtkWidget *widget, GdkEventWindowState *event,
		    gpointer unused)
{
  bool f = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN);

  if (is_fullscreen != f)
    {
      is_fullscreen = f;
      set_fullscreen_menu_check (f);
      if (is_fullscreen)
	{
	  gtk_container_set_border_width (GTK_CONTAINER (widget), 15);
	  set_current_toolbar_visibility (fullscreen_toolbar);
	}
      else
	{
	  gtk_container_set_border_width (GTK_CONTAINER (widget), 0);
	  set_current_toolbar_visibility (normal_toolbar);
	}
    }

  return FALSE;
}

static gboolean
key_event (GtkWidget *widget,
	   GdkEventKey *event,
	   gpointer data)
{
  static bool fullscreen_key_repeating = false;

  if (event->type == GDK_KEY_PRESS &&
      event->keyval == HILDON_HARDKEY_FULLSCREEN &&
      !fullscreen_key_repeating)
    {
      toggle_fullscreen ();
      fullscreen_key_repeating = true;
      return TRUE;
    }

  if (event->type == GDK_KEY_RELEASE &&
      event->keyval == HILDON_HARDKEY_FULLSCREEN)
    {
      fullscreen_key_repeating = false;
      return TRUE;
    }
      
  if (event->type == GDK_KEY_PRESS &&
      event->keyval == HILDON_HARDKEY_ESC)
    {
      if (cur_view_struct->parent)
	show_view (cur_view_struct->parent);

      /* We must return FALSE here since the long-press handling code
	 in HildonWindow needs to see the key press as well to start
	 the timer.
      */
      return FALSE;
    }

  return FALSE;
}

static GtkWidget *search_button;

static void
enable_search (bool f)
{
  if (search_button)
    gtk_widget_set_sensitive (search_button, f);
  set_search_menu_sensitive (f);
}

static void
insensitive_press (GtkButton *button, gpointer data)
{
  char *text = (char *)data;
  irritate_user (text);
}

static void
insensitive_operation_press (GtkButton *button, gpointer data)
{
  irritate_user (insensitive_operation_press_label);
}

static osso_context_t *osso_ctxt;

void
set_dialog_help (GtkWidget *dialog, const char *topic)
{
  if (osso_ctxt)
    ossohelp_dialog_help_enable (GTK_DIALOG (dialog), topic, osso_ctxt);
}

static const char *current_topic;

void
show_help ()
{
  if (osso_ctxt && current_topic)
    ossohelp_show (osso_ctxt, current_topic, 0);
}

static void
set_current_help_topic (const char *topic)
{
  current_topic = topic;
}

static void
call_refresh_package_cache (GtkWidget *button, gpointer data)
{
  refresh_package_cache (APTSTATE_DEFAULT, true);
}

static void
start_apt_worker_reply (gboolean res, void *data)
{
  GtkWidget *note = GTK_WIDGET (data);

  gtk_widget_destroy (note);
  if (res)
    {
      apt_worker_set_status_callback (apt_status_callback, NULL);
      add_apt_worker_handler ();
      
      get_package_list (APTSTATE_DEFAULT);
    }
  else
    {
      annoy_user (_("ai_ni_operation_failed"));
    }
}

static gboolean
xxx_open_file_when_idle (gpointer data)
{
  char *filename = (char *)data;
  open_local_install_instructions (filename);
  return FALSE;
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *start_note;
  GtkWidget *progressbar;
  GtkWidget *toolbar, *image;
  GtkMenu *main_menu;
  char *apt_worker_prog = "/usr/libexec/apt-worker";
  char *file_to_open = NULL;

  setlocale (LC_ALL, "");
  bind_textdomain_codeset ("osso-application-installer", "UTF-8");
  textdomain ("osso-application-installer");

  load_settings ();

  gtk_init (&argc, &argv);
  setup_dbus();

  // XXX - We don't want a two-part title and this seems to be the
  //       only way to get rid of it.  Hopefully, setting an empty
  //       application name doesn't break other stuff.
  //
  g_set_application_name ("");

  clear_log ();

  // XXX - stupid option parsing, should be improved but options are
  //       only used for testing right now.
  //
  if (argc > 1)
    {
      apt_worker_prog = argv[1];
      argc--;
      argv++;
    }
  if (argc > 1)
    {
      file_to_open = argv[1];
      argc--;
    }

  window = hildon_window_new ();
  gtk_window_set_title (GTK_WINDOW (window), _("ai_ap_application_installer"));
//   push_dialog_parent (window);

  main_window = GTK_WINDOW (window);

  g_signal_connect (window, "window_state_event",
		    G_CALLBACK (window_state_event), NULL);
  g_signal_connect (window, "key_press_event",
		    G_CALLBACK (key_event), NULL);
  g_signal_connect (window, "key_release_event",
		    G_CALLBACK (key_event), NULL);

  toolbar = gtk_toolbar_new ();

  main_toolbar = toolbar;

  toolbar_operation_label = gtk_label_new ("");
  toolbar_operation_item =
    GTK_WIDGET (gtk_tool_button_new (toolbar_operation_label, NULL));
  gtk_tool_item_set_expand (GTK_TOOL_ITEM (toolbar_operation_item), TRUE);
  gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (toolbar_operation_item), TRUE);
  g_signal_connect (toolbar_operation_item, "clicked",
		    G_CALLBACK (do_current_operation),
		    NULL);
  g_signal_connect (G_OBJECT (toolbar_operation_item), "insensitive_press",
		    G_CALLBACK (insensitive_operation_press), NULL);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
		      GTK_TOOL_ITEM (toolbar_operation_item),
		      -1);

  gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
		      GTK_TOOL_ITEM (gtk_separator_tool_item_new ()),
		      -1);

  image = gtk_image_new_from_icon_name ("qgn_toolb_gene_detailsbutton",
					HILDON_ICON_SIZE_TOOLBAR);
  details_button = GTK_WIDGET (gtk_tool_button_new (image, NULL));
  gtk_tool_item_set_expand (GTK_TOOL_ITEM (details_button), TRUE);
  gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (details_button), TRUE);
  g_signal_connect (details_button, "clicked",
		    G_CALLBACK (show_current_details),
		    NULL);
  g_signal_connect (G_OBJECT (details_button), "insensitive_press",
		    G_CALLBACK (insensitive_press),
		    _("ai_ib_nothing_to_view"));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
		      GTK_TOOL_ITEM (details_button),
		      -1);

  image = gtk_image_new_from_icon_name ("qgn_toolb_gene_findbutton",
					HILDON_ICON_SIZE_TOOLBAR);
  search_button = GTK_WIDGET (gtk_tool_button_new (image, NULL));
  gtk_tool_item_set_expand (GTK_TOOL_ITEM (search_button), TRUE);
  gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (search_button), TRUE);
  g_signal_connect (search_button, "clicked",
		    G_CALLBACK (show_search_dialog),
		    NULL);
  g_signal_connect (G_OBJECT (search_button), "insensitive_press",
		    G_CALLBACK (insensitive_press),
		    _("ai_ib_unable_search"));
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
		      GTK_TOOL_ITEM (search_button),
		      -1);

  if (red_pill_mode)
    {
      image = gtk_image_new_from_icon_name ("qgn_toolb_gene_refresh",
					    HILDON_ICON_SIZE_TOOLBAR);
      GtkWidget *button = GTK_WIDGET (gtk_tool_button_new (image, NULL));
      gtk_tool_item_set_expand (GTK_TOOL_ITEM (button), TRUE);
      gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (button), TRUE);
      g_signal_connect (button, "clicked",
			G_CALLBACK (call_refresh_package_cache),
			NULL);
      gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
			  GTK_TOOL_ITEM (button),
			  -1);
    }

  hildon_window_add_toolbar (HILDON_WINDOW (window),
			     GTK_TOOLBAR (toolbar));

  main_vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), main_vbox);

  main_trail = hildon_bread_crumb_trail_new (get_view_label, view_clicked);
  gtk_box_pack_start (GTK_BOX (main_vbox), main_trail, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), gtk_hseparator_new (), 
		      FALSE, FALSE, 0);

  g_signal_connect (G_OBJECT (window), "destroy",
		    G_CALLBACK (window_destroy), NULL);

  g_signal_connect (G_OBJECT (window), "realize",
		    G_CALLBACK (main_window_realized), NULL);

  main_menu = GTK_MENU (gtk_menu_new ());
  create_menu (main_menu);
  hildon_window_set_menu (HILDON_WINDOW (window), GTK_MENU (main_menu));

  osso_ctxt = osso_initialize ("hildon_application_manager",
			       PACKAGE_VERSION, TRUE, NULL);

  show_view (&main_view);
  set_toolbar_visibility (true, fullscreen_toolbar);
  set_toolbar_visibility (false, normal_toolbar);

  /* XXX - check errors.
   */
  osso_hw_state_t state = { 0 };
  state.shutdown_ind = true;
  osso_hw_set_event_cb (osso_ctxt, &state, hw_state_handler, NULL);

  progressbar = gtk_progress_bar_new ();
  gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (progressbar), 0.1);
  start_note = hildon_note_new_cancel_with_progress_bar (GTK_WINDOW (window), "Starting", GTK_PROGRESS_BAR (progressbar));
  g_signal_connect (G_OBJECT (start_note), "response", gtk_main_quit, NULL);

  start_apt_worker (apt_worker_prog, start_apt_worker_reply, (void *) start_note, 
		    (apt_worker_start_callback_tick *) gtk_progress_bar_pulse, (void *) progressbar);

  osso_mime_set_cb (osso_ctxt, mime_open_handler, NULL);

  gtk_widget_show_all (window);
  gtk_widget_show_all (start_note);
  push_dialog_parent (window);

  while (g_main_context_pending (NULL))
    g_main_context_iteration (NULL, FALSE);

  if (!something_started)
    {
      something_started = TRUE;
      mark_keep_running ();
    }
  else
    {
      gtk_window_iconify (GTK_WINDOW (window));
    }

  g_signal_connect (G_OBJECT (window), "notify::is-topmost", 
		    G_CALLBACK (window_property_topmost_notify_event), NULL);

  if (file_to_open)
    g_timeout_add (500, xxx_open_file_when_idle, file_to_open);

  gtk_main ();
}
