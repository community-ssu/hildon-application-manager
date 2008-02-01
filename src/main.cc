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
#include <gconf/gconf-client.h>

#include "apt-worker-client.h"
#include "apt-worker-proto.h"

#include "main.h"
#include "operations.h"
#include "util.h"
#include "details.h"
#include "menu.h"
#include "log.h"
#include "settings.h"
#include "search.h"
#include "instr.h"
#include "repo.h"
#include "dbus.h"

#include "update-notifier-conf.h"

#define MAX_PACKAGES_NO_CATEGORIES 7

#define _(x) gettext (x)

#define HILDON_ENABLE_UNSTABLE_API 

extern "C" {
  #include <hildon/hildon-window.h>
  #include <hildon/hildon-note.h>
  #include <hildon/hildon-help.h>
  #include <hildon/hildon-bread-crumb-trail.h>
}

using namespace std;

static void set_details_callback (void (*func) (gpointer), gpointer data);
static void set_operation_label (const char *label, const char *insens);
static void set_operation_callback (void (*func) (gpointer), gpointer data);
static void enable_search (bool f);
static void set_current_help_topic (const char *topic);

void get_package_list_info (GList *packages);

struct view {
  view *parent;
  view_id id;
  const gchar *label;
  GtkWidget *(*maker) (view *);
};

GtkWidget *main_vbox = NULL;
GtkWidget *main_trail = NULL;
GtkWidget *device_label = NULL;
GtkWidget *cur_view = NULL;
view *cur_view_struct = NULL;
GList *cur_path = NULL;

view_id
get_current_view_id ()
{
  if (cur_view)
    return cur_view_struct->id;

  return NO_VIEW;
}

static GList *
make_view_path (view *v)
{
  if (v == NULL)
    return NULL;
  else
    return g_list_append (make_view_path (v->parent), v);
}

static const gchar *
get_view_label (GList *node)
{
  return gettext (((view *)node->data)->label);
}

struct toolbar_struct
{
  GtkWidget *toolbar;
  GtkWidget *operation_button;
  GtkWidget *operation_label;
  GtkWidget *update_all_button;
  GtkWidget *details_button;
  GtkWidget *search_button;
  GtkWidget *refresh_button;
};

static toolbar_struct *main_tb_struct = NULL;
static toolbar_struct *updates_tb_struct = NULL;
static toolbar_struct *current_tb_struct = NULL;

static void set_current_toolbar (toolbar_struct *tb_struct);

GtkWidget *make_main_view (view *v);
GtkWidget *make_install_applications_view (view *v);
GtkWidget *make_install_section_view (view *v);
GtkWidget *make_upgrade_applications_view (view *v);
GtkWidget *make_uninstall_applications_view (view *v);
GtkWidget *make_search_results_view (view *v);

view main_view = {
  NULL,
  MAIN_VIEW,
  "ai_ti_main",
  make_main_view
};

view install_applications_view = {
  &main_view,
  INSTALL_APPLICATIONS_VIEW,
  "ai_li_install",
  make_install_applications_view
};

view upgrade_applications_view = {
  &main_view,
  UPGRADE_APPLICATIONS_VIEW,
  "ai_li_update",
  make_upgrade_applications_view
};

view uninstall_applications_view = {
  &main_view,
  UNINSTALL_APPLICATIONS_VIEW,
  "ai_li_uninstall",
  make_uninstall_applications_view
};

view install_section_view = {
  &install_applications_view,
  INSTALL_SECTION_VIEW,
  NULL,
  make_install_section_view
};

view search_results_view = {
  &main_view,
  SEARCH_RESULTS_VIEW,
  "ai_ti_search_results",
  make_search_results_view
};

void
show_view (view *v)
{
  GList *p;

  if (cur_view)
    {
      gtk_container_remove(GTK_CONTAINER(main_vbox), cur_view);
      cur_view = NULL;
    }

   if (v == &upgrade_applications_view)
     set_current_toolbar (updates_tb_struct);
   else
     set_current_toolbar (main_tb_struct);

  set_details_callback (NULL, NULL);
  set_operation_label (NULL, NULL);
  set_operation_callback (NULL, NULL);

  allow_updating ();

  cur_view = v->maker (v);
  cur_view_struct = v;

  g_list_free (cur_path);
  cur_path = make_view_path (v);

  hildon_bread_crumb_trail_clear (HILDON_BREAD_CRUMB_TRAIL (main_trail));

  for (p = cur_path; p != NULL; p = p->next)
    hildon_bread_crumb_trail_push_text (HILDON_BREAD_CRUMB_TRAIL (main_trail),
                                        get_view_label (p),
                                        p, NULL);

  gtk_box_pack_start (GTK_BOX (main_vbox), cur_view, TRUE, TRUE, 10);
  gtk_widget_show(main_vbox);

  reset_idle_timer ();
}

static void
show_view_callback (GtkWidget *btn, gpointer data)
{
  view *v = (view *)data;
  
  show_view (v);
}

static gboolean
view_clicked (HildonBreadCrumbTrail *bct, gpointer node, gpointer user_data)
{
  reset_global_target_path ();
  show_view ((view *)((GList*)node)->data);

  return TRUE;
}

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
		    G_CALLBACK (show_check_for_updates_view),
		    NULL);
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
static gboolean package_list_ready = false;

static char *cur_section_name;

static bool record_seen_updates = false;

package_info::package_info ()
{
  ref_count = 1;
  name = NULL;
  broken = false;
  installed_version = NULL;
  installed_section = NULL;
  installed_pretty_name = NULL;
  available_version = NULL;
  available_section = NULL;
  available_pretty_name = NULL;
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
}

package_info::~package_info ()
{
  g_free (name);
  g_free (installed_version);
  g_free (installed_section);
  g_free (installed_pretty_name);
  g_free (available_version);
  g_free (available_section);
  g_free (available_pretty_name);
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
}

const char *
package_info::get_display_name (bool installed)
{
  const char *n;
  if (installed || available_pretty_name == NULL)
    n = installed_pretty_name;
  else
    n = available_pretty_name;
  if (n == NULL)
    n = name;
  return n;
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
  if (name == NULL)
    name = "";

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
compare_package_installed_names (gconstpointer a, gconstpointer b)
{
  const char *name_a = ((package_info *)a)->get_display_name (true);
  const char *name_b = ((package_info *)b)->get_display_name (true);
  
  return package_sort_sign * g_ascii_strcasecmp (name_a, name_b);
}

static gint
compare_package_available_names (gconstpointer a, gconstpointer b)
{
  const char *name_a = ((package_info *)a)->get_display_name (false);
  const char *name_b = ((package_info *)b)->get_display_name (false);
  
  return package_sort_sign * g_ascii_strcasecmp (name_a, name_b);
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
    return compare_package_available_names (a, b);
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

  GCompareFunc compare_packages_inst = compare_package_installed_names;
  GCompareFunc compare_packages_avail = compare_package_available_names;
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

static GList *cur_packages_for_info;
static GList *next_packages_for_info;

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
  info->installed_size = dec->decode_int64 ();
  info->installed_section = dec->decode_string_dup ();
  info->installed_pretty_name = dec->decode_string_dup ();
  info->installed_short_description = dec->decode_string_dup ();
  installed_icon = dec->decode_string_in_place ();
  info->available_version = dec->decode_string_dup ();
  info->available_section = dec->decode_string_dup ();
  info->available_pretty_name = dec->decode_string_dup ();
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
    what_the_fock_p ();
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
  package_list_ready = true;
  record_seen_updates = true;

  sort_all_packages ();

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
    what_the_fock_p ();
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

  /* Mark package list as not ready and reset some
     global values, before freeing the list */
  package_list_ready = false;
  next_packages_for_info = NULL;
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
    return;

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
			    get_next_package_info, NULL, intermediate_state);
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

struct gipl_clos {
  GList *packages;
  bool only_installable_info;
  int state;
  
  void (*cont) (void *data);
  void *data;
};

static void gipl_loop (void *data);
static void gipl_next (package_info *unused_1, void *data, bool unused_2);

void
get_intermediate_package_list_info (GList *packages,
				    bool only_installable_info,
				    void (*cont) (void *data),
				    void *data,
				    int state)
{
  gipl_clos *c = new gipl_clos;

  c->packages = packages;
  c->only_installable_info = only_installable_info;
  c->state = state;
  c->cont = cont;
  c->data = data;
  
  gipl_loop (c);
}


static void
gipl_loop (void *data)
{
  gipl_clos *c = (gipl_clos *)data;

  if (c->packages == NULL)
    {
      c->cont (c->data);
      delete c;
    }
  else
    get_intermediate_package_info ((package_info *)(c->packages->data),
				   c->only_installable_info,
				   gipl_next, c,
				   c->state);
}

static void
gipl_next (package_info *unused_1, void *data, bool unused_2)
{
  gipl_clos *c = (gipl_clos *)data;

  c->packages = c->packages->next;
  gipl_loop (c);
}

/* REFRESH_PACKAGE_CACHE_WITHOUT_USER
 */

struct rcpwu_clos {
  int state;
  void (*cont) (bool keep_going, void *data);
  void *data;
  bool keep_going;
};

static void rpcwu_reply (int cmd, apt_proto_decoder *dec, void *data);
static void rpcwu_end (void *data);

static entertainment_game rpcwu_games[] = {
  { op_downloading, 0.5 },
  { op_general, 0.5 }
};

void
refresh_package_cache_without_user (const char *title, 
				    int state,
				    void (*cont) (bool keep_going, void *data),
				    void *data)
{
  rcpwu_clos *c = new rcpwu_clos;
  c->state = state;
  c->cont = cont;
  c->data = data;
  
  if (title)
    set_entertainment_main_title (title);
  else
    set_entertainment_main_title (_("ai_nw_checking_updates"));
  set_entertainment_games (2, rpcwu_games);
  set_entertainment_fun (NULL, -1, -1, 0);
  start_entertaining_user ();

  apt_worker_update_cache (state, rpcwu_reply, c);
}

static void
rpcwu_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  rcpwu_clos *c = (rcpwu_clos *)data;
  GConfClient *conf;

  c->keep_going = !entertainment_was_cancelled ();
  stop_entertaining_user ();

  conf = gconf_client_get_default ();

  gconf_client_set_int (conf,
			UPNO_GCONF_LAST_UPDATE, time (NULL),
			NULL);

  get_package_list_with_cont (c->state, rpcwu_end, c);
}

static void
rpcwu_end (void *data)
{
  rcpwu_clos *c = (rcpwu_clos *)data;
  
  c->cont (c->keep_going, c->data);
  delete c;
}

/* refresh_package_cache_without_user inside an interaction flow
 */

static void rpcwuf_end (bool ignored, void *unused);

void
refresh_package_cache_without_user_flow ()
{
  if (start_interaction_flow ())
    refresh_package_cache_without_user (NULL, APTSTATE_DEFAULT,
					rpcwuf_end, NULL);
}

static void
rpcwuf_end (bool ignore, void *unused)
{
  end_interaction_flow ();
}

/* Call refresh_package_cache_without_user_flow when the last update
   was more than one 'check-interval' ago.
*/

void
maybe_refresh_package_cache_without_user ()
{
  GConfClient *conf;
  int last_update, interval;

  if (!is_idle ())
    return;

  conf = gconf_client_get_default ();

  last_update = gconf_client_get_int (conf,
				      UPNO_GCONF_LAST_UPDATE,
				      NULL);

  interval = gconf_client_get_int (conf,
				   UPNO_GCONF_CHECK_INTERVAL,
				   NULL);

  if (interval <= 0)
    interval = UPNO_DEFAULT_CHECK_INTERVAL;

  if (last_update + interval*60 < time (NULL))
    refresh_package_cache_without_user_flow ();
}

/* Set the catalogues and refresh.
 */

struct scar_clos {
  void (*cont) (bool keep_going, void *data);
  void *data;
  char *title;
  int state;
};

struct set_catalogues_refresh_data {
  void (*cont) (int state, xexp *catalogues,
      apt_worker_callback *callback, void *data);
  int state;
  xexp *catalogues;
  apt_worker_callback *callback;
  void *cont_data;
};

static void scar_set_catalogues_reply (int cmd, apt_proto_decoder *dec,
				       void *data);

static void set_catalogues_and_refresh_cont  (bool success, void *data)
{
  set_catalogues_refresh_data *scr_data = (set_catalogues_refresh_data *) data;

  scr_data->cont (scr_data->state,
                  scr_data->catalogues,
                  scr_data->callback,
                  scr_data->cont_data);
  
  delete scr_data;
}

void
set_catalogues_and_refresh (xexp *catalogues,
			    const char *title,
			    int state,
			    void (*cont) (bool keep_going, void *data),
			    void *data)
{
  scar_clos *c = new scar_clos;
  c->cont = cont;
  c->data = data;
  c->title = g_strdup (title);
  c->state = state;

  set_catalogues_refresh_data *scr_data = new set_catalogues_refresh_data;

  scr_data->cont = apt_worker_set_catalogues;
  scr_data->state = state;
  scr_data->catalogues = catalogues;
  scr_data->callback = scar_set_catalogues_reply;
  scr_data->cont_data = c; 

  ensure_network (set_catalogues_and_refresh_cont, scr_data);
}

static void
scar_set_catalogues_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  scar_clos *c = (scar_clos *)data;

  refresh_package_cache_without_user (c->title, c->state, c->cont, c->data);

  g_free (c->title);
  delete c;
}

static void
available_package_details (gpointer data)
{
  package_info *pi = (package_info *)data;
  show_package_details_flow (pi, install_details);
}

static void
install_package_flow_end (int n_successful, void *data)
{
  package_info *pi = (package_info *)data;
  pi->unref ();
  end_interaction_flow ();
}

static void
install_package_flow (package_info *pi)
{
  if (start_interaction_flow ())
    {
      pi->ref ();
      install_package (pi, install_package_flow_end, pi);
    }
}

static void
update_all_packages_flow_end (int n_successful, void *data)
{
  GList *packages_list = (GList *)data;
  g_list_free (packages_list);
  end_interaction_flow ();
}

static void
update_all_packages_flow ()
{
  if (start_interaction_flow ())
    {
      GList *packages_list = g_list_copy (upgradeable_packages);
      install_packages (packages_list,
			APTSTATE_DEFAULT,
			INSTALL_TYPE_UPGRADE_ALL_PACKAGES,
			false,  NULL, NULL,
			update_all_packages_flow_end, packages_list);
    }
}

static void
install_operation_callback (gpointer data)
{
  install_package_flow ((package_info *)data);
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
  install_package_flow (pi);
}

static void
update_all_packages_callback ()
{
  update_all_packages_flow ();
}

static void
installed_package_details (gpointer data)
{
  package_info *pi = (package_info *)data;
  show_package_details_flow (pi, remove_details);
}

static void
uninstall_package_flow_end (void *data)
{
  package_info *pi = (package_info *)data;
  pi->unref ();
  end_interaction_flow ();
}

static void
uninstall_package_flow (package_info *pi)
{
  if (start_interaction_flow ())
    {
      pi->ref ();
      uninstall_package (pi, uninstall_package_flow_end, pi);
    }
}

static void
uninstall_operation_callback (gpointer data)
{
  uninstall_package_flow ((package_info *)data);
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
  uninstall_package_flow (pi);
}

GtkWidget *
make_install_section_view (view *v)
{
  GtkWidget *view;

  set_operation_label (_("ai_me_package_install"),
		       _("ai_ib_nothing_to_install"));

  section_info *si = find_section_info (&install_sections,
					cur_section_name, false, true);

  view =
    make_global_package_list (si? si->packages : NULL,
			      false,
			      _("ai_li_no_applications_available"),
			      _("ai_me_cs_install"),
			      available_package_selected, 
			      available_package_activated);

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
  if (catalogues == NULL)
    return;

  for (xexp *c = xexp_first (catalogues); c; c = xexp_rest (c))
    if (xexp_is (c, "source")
	|| (xexp_is (c, "catalogue") && !xexp_aref_bool (c, "disabled")))
      {
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
  if (package_list_ready)
    {
      /* Avoid to call get_catalogues if packages_list it not ready */
      get_catalogues (check_catalogues_reply, NULL);
    }
}

GtkWidget *
make_install_applications_view (view *v)
{
  GtkWidget *view;

  check_catalogues ();

  set_operation_label (_("ai_me_package_install"),
		       _("ai_ib_nothing_to_install"));

  if (install_sections && install_sections->next == NULL)
    {
      section_info *si = (section_info *)install_sections->data;
      view = 
	make_global_package_list (si->packages,
				  false,
				  _("ai_li_no_applications_available"),
				  _("ai_me_cs_install"),
				  available_package_selected, 
				  available_package_activated);
      get_package_list_info (si->packages);
      set_current_help_topic (AI_TOPIC ("packagesview"));
    }
  else
    {
      view = make_global_section_list (install_sections, view_section);
      set_current_help_topic (AI_TOPIC ("sectionsview"));
    }

  gtk_widget_show_all (view);

  enable_search (true);

  maybe_refresh_package_cache_without_user ();
  
  return view;
}

void
show_check_for_updates_view ()
{
  show_view (&upgrade_applications_view);
}

GtkWidget *
make_upgrade_applications_view (view *v)
{
  GtkWidget *view;

  check_catalogues ();

  set_operation_label (_("ai_me_package_update"),
		       _("ai_ib_nothing_to_update"));

  view =
    make_global_package_list (upgradeable_packages,
			      false,
			      _("ai_li_no_updates_available"),
			      _("ai_me_cs_update"),
			      available_package_selected,
			      available_package_activated);

  gtk_widget_show_all (view);

  get_package_list_info (upgradeable_packages);

  enable_search (true);
  set_current_help_topic (AI_TOPIC ("updateview"));

  if (record_seen_updates
      && hildon_window_get_is_topmost (HILDON_WINDOW (get_main_window ())))
    {
      xexp *seen_updates = xexp_list_new ("updates");
      for (GList *pkg = upgradeable_packages; pkg; pkg = pkg->next)
	xexp_cons (seen_updates,
		   xexp_text_new ("pkg", 
				  ((package_info *)pkg->data)->name));
      gchar *name = g_strdup_printf ("%s/%s", getenv ("HOME"),
				     SEEN_UPDATES_FILE);
      xexp_write_file (name, seen_updates);
      xexp_free (seen_updates);
      g_free (name);

      record_seen_updates = false;
    }

  maybe_refresh_package_cache_without_user ();

  return view;
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
		     GList *packages, const char *pattern, bool installed)
{
  while (packages)
    {
      package_info *pi = (package_info *)packages->data;
      
      // XXX
      if (strcasestr (pi->get_display_name (installed), pattern))
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
      what_the_fock_p ();
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
      dec->decode_string_in_place (); // installed_pretty_name
      dec->decode_string_in_place (); // installed_short_description
      dec->decode_string_in_place (); // installed_icon
      dec->decode_string_in_place (); // available_version
      dec->decode_string_in_place (); // available_section
      dec->decode_string_in_place (); // available_pretty_name
      dec->decode_string_in_place (); // available_short_description
      dec->decode_string_in_place (); // available_icon

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
	      find_in_package_list (&result, si->packages, name);
	    }
	}
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
	      search_package_list (&result, si->packages, pattern, false);
	    }
	}
      else if (parent == &upgrade_applications_view)
	search_package_list (&result,
			     upgradeable_packages, pattern, false);
      else if (parent == &uninstall_applications_view)
	search_package_list (&result,
			     installed_packages, pattern, true);

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

struct inp_clos {
  void (*cont) (int n_successful, void *data);
  void *data;
};

static void inp_no_package (void *data);
static void inp_one_package (void *data);

void
install_named_package (int state, const char *package,
		       void (*cont) (int n_successful, void *data), void *data)
{
  GList *p = NULL;
  GList *node = NULL;

  find_package_in_lists (state, &p, package);

  inp_clos *c = new inp_clos;
  c->cont = cont;
  c->data = data;

  if (p == NULL)
    {
      char *text = g_strdup_printf (_("ai_ni_error_download_missing"),
				    package);
      annoy_user (text, inp_no_package, c);
      g_free (text);
    }
  else
    {
      package_info *pi = (package_info *) p->data;
      if (pi->available_version == NULL)
	{
	  char *text = g_strdup_printf (_("ai_ni_package_installed"),
					package);
	  annoy_user (text, inp_one_package, c);
	  pi->unref ();
	  g_free (text);
	}
      else
	{
	  delete c;
	  install_package (pi, cont, data);
	}

      for (node = p->next; node != NULL; node = g_list_next (node))
	{
	  pi = (package_info *) node->data;
	  pi->unref ();
	}
      g_list_free (p);
    }
}

static void
inp_no_package (void *data)
{
  inp_clos *c = (inp_clos *)data;

  c->cont (0, c->data);
  delete c;
}

static void
inp_one_package (void *data)
{
  inp_clos *c = (inp_clos *)data;

  c->cont (1, c->data);
  delete c;
}

void
install_named_packages (int state, const char **packages,
			int install_type, bool automatic,
			const char *title, const char *desc,
			void (*cont) (int n_successful, void *data),
			void *data)
{
  GList *package_list = NULL;
  char **current_package = NULL;

  /* Get packages information */
  for (current_package = (char **) packages;
       current_package != NULL && *current_package != NULL;
       current_package++)
    {
      GList *search_list = NULL;
      GList *node = NULL;

      g_strchug (*current_package);

      find_package_in_lists (state, &search_list, *current_package);

      if (search_list != NULL)
	{
	  package_info *pi = (package_info *)search_list->data;
	  package_list = g_list_append (package_list, pi);

	  for (node = search_list->next; node != NULL;
	       node = g_list_next (node))
	    {
	      pi = (package_info *) node->data;
	      pi->unref ();
	    }
	}
      else
	{
	  /* Create a 'fake' package_info structure so that we at
	     least have something to display.
	  */
	  package_info *pi = new package_info;
	  pi->name = g_strdup (*current_package);
	  pi->available_version = g_strdup ("");

	  pi->have_info = true;
	  pi->info.installable_status = status_not_found;
	  pi->info.download_size = 0;
	  pi->info.install_user_size_delta = 0;
	  pi->info.required_free_space = 0;
	  pi->info.install_flags = 0;
	  pi->info.removable_status = status_able;
	  pi->info.remove_user_size_delta = 0;

	  pi->have_detail_kind = install_details;
	  pi->summary = g_strdup_printf (_("ai_va_details_unable_install"),
					 pi->name);
	  pi->summary_packages[sumtype_missing] =
	    g_list_append (NULL, g_strdup (pi->name));

	  package_list = g_list_append (package_list, pi);
	}

      g_list_free (search_list);
    }
  
  install_packages (package_list,
		    state, install_type, automatic,
		    title, desc,
		    cont, data);
}

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
  gtk_widget_set_sensitive (current_tb_struct->details_button,
			    func != NULL);
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

  /* Set sensitiveness for 'Update all' button if needed */
  if (current_tb_struct->update_all_button)
    {
      gtk_widget_set_sensitive (current_tb_struct->update_all_button,
				operation_func != NULL);
    }
}

/* Reinstalling the packages form the recently restored backup.
 */

static void rp_restore (bool keep_going, void *data);
static void rp_unsuccessful (void *data);
static void rp_end (int n_successful, void *data);

struct restore_cont_data
{
  void (*restore_cont) (const char *title, int state,
      void (*cont) (bool keep_going, void *data), void *data);
  gchar* msg;
  apt_state aptstate;
  void (*restore_cont_cont) (bool keep_going, void *data);
  void *data;
};

void
rp_ensure_cont (bool success, void *data)
{
  restore_cont_data *c = (restore_cont_data *) data;
  
  c->restore_cont (c->msg,
                   c->aptstate,
                   c->restore_cont_cont,
                   c->data);
  delete c;
}

void
restore_packages_flow ()
{
  if (start_interaction_flow ())
    {
      char *filename =
	g_strdup_printf ("%s/%s",
			 g_get_home_dir (), RESTORE_BACKUP_FILENAME);
      xexp *backup = xexp_read_file (filename);
      g_free (filename);
      
      if (backup)
        {
          restore_cont_data *rc_data = new restore_cont_data;
          rc_data->restore_cont = refresh_package_cache_without_user;
          rc_data->msg = _("ai_nw_preparing_installation");
          rc_data->aptstate = APTSTATE_DEFAULT;
          rc_data->restore_cont_cont = rp_restore;
          rc_data->data = backup;
          ensure_network (rp_ensure_cont, rc_data);
        }
      else
	annoy_user (_("ai_ni_operation_failed"), rp_unsuccessful, backup);
    }
}

static void
rp_restore (bool keep_going, void *data)
{
  xexp *backup = (xexp *)data;

  if (keep_going)
    {
      int len = xexp_length (backup);
      const char **names = (const char **)new char* [len+1];
      
      xexp *p = xexp_first (backup);
      int i = 0;
      while (p)
	{
	  if (xexp_is (p, "pkg") && xexp_is_text (p))
	    names[i++] = xexp_text (p);
	  p = xexp_rest (p);
	}
      names[i] = NULL;
      install_named_packages (APTSTATE_DEFAULT, names,
			      INSTALL_TYPE_BACKUP, false,
			      NULL, NULL,
			      rp_end, backup);
      delete names;
    }
  else
    rp_end (0, backup);
}

static void
rp_unsuccessful (void *data)
{
  rp_end (0, data);
}

static void
rp_end (int n_successful, void *data)
{
  xexp *backup = (xexp *)data;

  if (backup)
    xexp_free (backup);

  end_interaction_flow ();
}

/* INSTALL_FROM_FILE_FLOW
 */

static void iff_with_filename (char *uri, void *unused);
static void iff_end (bool success, void *unused);

void
install_from_file_flow (const char *filename)
{
  if (start_interaction_flow ())
    {
      if (filename == NULL)
	show_deb_file_chooser (iff_with_filename, NULL);
      else
	{
	  /* Try to convert filename to GnomeVFS uri */
	  char *fileuri = 
	    gnome_vfs_get_uri_from_local_path (filename);

	  /* If there's an error then user filename as is */
	  if (fileuri == NULL)
	    {
	      fileuri = g_strdup (filename);
	    }

	  iff_with_filename (fileuri, NULL);
	}
    }
}

static void
iff_with_filename (char *uri, void *unused)
{
  if (uri)
    {
      install_file (uri, iff_end, NULL);
      g_free (uri);
    }
  else
    iff_end (false, NULL);
}

static void
iff_end (bool success, void *unused)
{
  end_interaction_flow ();
}

static void
window_destroy (GtkWidget* widget, gpointer data)
{
  gtk_main_quit ();
}

static gboolean
window_delete_event (GtkWidget* widget, GdkEvent *ev, gpointer data)
{
  /* Finish any interaction flow if it's still active */
  if (is_interaction_flow_active ())
    end_interaction_flow ();

  menu_close ();
  return TRUE;
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
cancel_download (void *unused)
{
  cancel_apt_worker ();
}

static void
apt_status_callback (int cmd, apt_proto_decoder *dec, void *unused)
{
  if (dec == NULL)
    return;

  int op = dec->decode_int ();
  int already = dec->decode_int ();
  int total = dec->decode_int ();

  if (total > 0)
    {
      if (op == op_downloading)
	{
	  set_entertainment_download_fun (op, already, total);
	  set_entertainment_cancel (cancel_download, NULL);
	}
      else
	{
	  set_entertainment_fun (NULL, op, already, total);
	}
    }
}

static GtkWindow *main_window = NULL;

static void
set_operation_toolbar_label (const char *label, bool sensitive)
{
  if (current_tb_struct->operation_label)
    gtk_label_set_text (GTK_LABEL (current_tb_struct->operation_label), label);
  if (current_tb_struct->operation_button)
    gtk_widget_set_sensitive (current_tb_struct->operation_button, sensitive);
}

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

static void
set_current_toolbar (toolbar_struct *tb_struct)
{
  if (tb_struct == NULL)
    return;

  if (current_tb_struct != tb_struct)
    {
      gtk_widget_hide_all (current_tb_struct->toolbar);
      gtk_widget_show_all (tb_struct->toolbar);
      current_tb_struct = tb_struct;
    }
}

static void
set_current_toolbar_visibility (bool f)
{
  if (current_tb_struct->toolbar)
    {
      if (f)
	gtk_widget_show (current_tb_struct->toolbar);
      else
	gtk_widget_hide (current_tb_struct->toolbar);
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
  save_state ();
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

static void
enable_search (bool f)
{
  if (current_tb_struct->search_button)
    gtk_widget_set_sensitive (current_tb_struct->search_button, f);
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
    hildon_help_dialog_help_enable (GTK_DIALOG (dialog), topic, osso_ctxt);
}

static const char *current_topic;

void
show_help ()
{
  if (osso_ctxt && current_topic)
    hildon_help_show (osso_ctxt, current_topic, 0);
}

static void
set_current_help_topic (const char *topic)
{
  current_topic = topic;
}

static void
call_refresh_package_cache (GtkWidget *button, gpointer data)
{
  refresh_package_cache_without_user_flow ();
}

/* Take a snapshot of the data we want to keep in a backup.
*/

static void save_backup_data_reply (int cmd, apt_proto_decoder *dec, 
				    void *data);

void
save_backup_data ()
{
  apt_worker_save_backup_data (save_backup_data_reply, NULL);
}

static void
save_backup_data_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  /* No action required */
}

struct pending_cont {
  pending_cont *next;
  void (*cont) (void *data);
  void *data;
};

static bool initial_packages_available = false;
static pending_cont *pending_for_initial_packages = NULL;

static void
notice_initial_packages_available (gpointer data)
{
  initial_packages_available = true;

  while (pending_for_initial_packages)
    {
      pending_cont *c = pending_for_initial_packages;
      pending_for_initial_packages = c->next;
      c->cont (c->data);
      delete c;
    }
}

void
with_initialized_packages (void (*cont) (void *data), void *data)
{
  if (initial_packages_available)
    cont (data);
  else
    {
      pending_cont *c = new pending_cont;
      c->cont = cont;
      c->data = data;
      c->next = pending_for_initial_packages;
      pending_for_initial_packages = c;
    }
}

osso_context_t *
get_osso_context ()
{
  return osso_ctxt;
}

static toolbar_struct *
create_toolbar (bool show_update_all_button, bool show_search_button)
{
  toolbar_struct *tb_struct = NULL;
  GtkWidget *toolbar = NULL;
  GtkWidget *image = NULL;
  GtkWidget *operation_button = NULL;
  GtkWidget *operation_label = NULL;
  GtkWidget *update_all_button = NULL;
  GtkWidget *details_button = NULL;
  GtkWidget *search_button = NULL;

  /* Create the toolba */
  toolbar = gtk_toolbar_new ();

  /* Init the global struct */
  tb_struct = new toolbar_struct;
  tb_struct->toolbar = toolbar;
  tb_struct->operation_button = NULL;
  tb_struct->operation_label = NULL;
  tb_struct->update_all_button = NULL;
  tb_struct->details_button = NULL;
  tb_struct->search_button = NULL;
  tb_struct->refresh_button = NULL;

  /* Main operation button */
  operation_label = gtk_label_new ("");
  operation_button =
    GTK_WIDGET (gtk_tool_button_new (operation_label, NULL));
  gtk_tool_item_set_expand (GTK_TOOL_ITEM (operation_button), TRUE);
  gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (operation_button), TRUE);
  g_signal_connect (operation_button, "clicked",
		    G_CALLBACK (do_current_operation),
		    NULL);
  g_signal_connect (G_OBJECT (operation_button), "insensitive_press",
		    G_CALLBACK (insensitive_operation_press), NULL);

  gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
		      GTK_TOOL_ITEM (operation_button),
		      -1);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
		      GTK_TOOL_ITEM (gtk_separator_tool_item_new ()),
		      -1);
  tb_struct->operation_label = operation_label;
  tb_struct->operation_button = operation_button;

  if (show_update_all_button)
    {
      /* 'Update all' button - XXX needs i18n */
      update_all_button =
	GTK_WIDGET (gtk_tool_button_new (gtk_label_new (_("ai_tb_update_all")), NULL));
      gtk_tool_item_set_expand (GTK_TOOL_ITEM (update_all_button), TRUE);
      gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (update_all_button), TRUE);

      g_signal_connect (update_all_button, "clicked",
			G_CALLBACK (update_all_packages_callback),
			NULL);

      g_signal_connect (G_OBJECT (update_all_button), "insensitive_press",
			G_CALLBACK (insensitive_operation_press), NULL);
      gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
			  GTK_TOOL_ITEM (update_all_button),
			  -1);
      gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
			  GTK_TOOL_ITEM (GTK_WIDGET (gtk_separator_tool_item_new ())),
			  -1);
      tb_struct->update_all_button = update_all_button;
    }

  /* Details button */
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
  tb_struct->details_button = details_button;

  if (show_search_button)
    {
      /* Search button */
      image = gtk_image_new_from_icon_name ("qgn_toolb_gene_findbutton",
					    HILDON_ICON_SIZE_TOOLBAR);
      search_button = GTK_WIDGET (gtk_tool_button_new (image, NULL));
      gtk_tool_item_set_expand (GTK_TOOL_ITEM (search_button), TRUE);
      gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (search_button), TRUE);
      g_signal_connect (search_button, "clicked",
			G_CALLBACK (show_search_dialog_flow),
			NULL);
      g_signal_connect (G_OBJECT (search_button), "insensitive_press",
			G_CALLBACK (insensitive_press),
			_("ai_ib_unable_search"));
      gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
			  GTK_TOOL_ITEM (search_button),
			  -1);
      tb_struct->search_button = search_button;
    }

  /* Refresh all packages button */
  if (red_pill_mode)
    {
      image = gtk_image_new_from_icon_name ("qgn_toolb_gene_refresh",
					    HILDON_ICON_SIZE_TOOLBAR);
      GtkWidget *refresh_button = GTK_WIDGET (gtk_tool_button_new (image, NULL));
      gtk_tool_item_set_expand (GTK_TOOL_ITEM (refresh_button), TRUE);
      gtk_tool_item_set_homogeneous (GTK_TOOL_ITEM (refresh_button), TRUE);
      g_signal_connect (refresh_button, "clicked",
			G_CALLBACK (call_refresh_package_cache),
			NULL);

      gtk_toolbar_insert (GTK_TOOLBAR (toolbar),
			  GTK_TOOL_ITEM (refresh_button),
			  -1);
      tb_struct->refresh_button = refresh_button;
    }

  return tb_struct;
}

static toolbar_struct *
create_main_toolbar ()
{
  toolbar_struct *tb_struct = create_toolbar (false, true);
  return tb_struct;
}

static toolbar_struct *
create_updates_toolbar ()
{
  toolbar_struct *tb_struct = create_toolbar (true, false);
  return tb_struct;
}

int
main (int argc, char **argv)
{
  GtkWidget *window = NULL;
  toolbar_struct *m_tb_struct = NULL;
  toolbar_struct *u_tb_struct = NULL;
  char *apt_worker_prog = "/usr/libexec/apt-worker.bin";
  bool show = true;

  if (argc > 1 && !strcmp (argv[1], "--no-show"))
    {
      show = false;
      argc--;
      argv++;
    }
  if (argc > 1)
    {
      apt_worker_prog = argv[1];
      argc--;
      argv++;
    }

  setlocale (LC_ALL, "");
  bind_textdomain_codeset ("hildon-application-manager", "UTF-8");
  textdomain ("hildon-application-manager");

  load_settings ();
  load_state ();

  gtk_init (&argc, &argv);

  init_dbus_or_die (show);

  osso_ctxt = osso_initialize ("hildon_application_manager",
			       PACKAGE_VERSION, TRUE, NULL);

  // XXX - We don't want a two-part title and this seems to be the
  //       only way to get rid of it.  Hopefully, setting an empty
  //       application name doesn't break other stuff.
  //
  g_set_application_name ("");

  clear_log ();

  window = hildon_window_new ();
  gtk_window_set_title (GTK_WINDOW (window), _("ai_ap_application_installer"));

  main_window = GTK_WINDOW (window);

  g_signal_connect (window, "window_state_event",
		    G_CALLBACK (window_state_event), NULL);
  g_signal_connect (window, "key_press_event",
		    G_CALLBACK (key_event), NULL);
  g_signal_connect (window, "key_release_event",
		    G_CALLBACK (key_event), NULL);

  /* Create the two toolbars */
  m_tb_struct = create_main_toolbar ();
  u_tb_struct = create_updates_toolbar ();

  /* Set global variables and current toolbar */
  main_tb_struct = m_tb_struct;
  updates_tb_struct = u_tb_struct;
  current_tb_struct = main_tb_struct;

  /* Add toolbars */
  hildon_window_add_toolbar (HILDON_WINDOW (window),
			     GTK_TOOLBAR (m_tb_struct->toolbar));
  hildon_window_add_toolbar (HILDON_WINDOW (window),
			     GTK_TOOLBAR (u_tb_struct->toolbar));

  main_vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), main_vbox);

  main_trail = hildon_bread_crumb_trail_new ();
  g_signal_connect (G_OBJECT (main_trail), "crumb-clicked",
                    G_CALLBACK (view_clicked), NULL);

  gtk_box_pack_start (GTK_BOX (main_vbox), main_trail, FALSE, FALSE, 0);

  g_signal_connect (G_OBJECT (window), "delete-event",
		    G_CALLBACK (window_delete_event), NULL);

  g_signal_connect (G_OBJECT (window), "destroy",
		    G_CALLBACK (window_destroy), NULL);

  g_signal_connect (G_OBJECT (window), "realize",
		    G_CALLBACK (main_window_realized), NULL);

  create_menu (HILDON_WINDOW (window));

  show_view (&main_view);
  set_toolbar_visibility (true, fullscreen_toolbar);
  set_toolbar_visibility (false, normal_toolbar);

  if (!start_apt_worker (apt_worker_prog))
    what_the_fock_p ();

  apt_worker_set_status_callback (apt_status_callback, NULL);

  get_package_list_with_cont (APTSTATE_DEFAULT,
			      notice_initial_packages_available, NULL);
  save_backup_data ();

  if (show)
    present_main_window ();

  /* Set the main toolbar visible */
  gtk_widget_show_all (m_tb_struct->toolbar);

  gtk_main ();
}
