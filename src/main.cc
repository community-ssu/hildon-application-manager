/*
 * This file is part of the hildon-application-manager.
 *
 * Parts of this file are derived from apt.  Apt is copyright 1997,
 * 1998, 1999 Jason Gunthorpe and others.
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

#include <stdio.h>
#include <assert.h>
#include <iostream>
#include <libintl.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gconf/gconf-client.h>

#include <hildon/hildon.h>

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
#include "user_files.h"
#include "apt-utils.h"
#include "confutils.h"
#include "update-notifier-conf.h"

#define MAX_PACKAGES_NO_CATEGORIES 7

#define _(x) gettext (x)

extern "C" {
  #include <hildon/hildon-window-stack.h>
  #include <hildon/hildon-window.h>
  #include <hildon/hildon-note.h>
}

using namespace std;

static void set_details_callback (void (*func) (gpointer), gpointer data);
static void set_operation_label (const char *label);
static void set_operation_callback (void (*func) (gpointer), gpointer data);

static void get_package_infos_in_background (GList *packages);

struct view {
  view *parent;
  view_id id;
  GtkWidget *(*maker) (view *);
  GtkWidget *window;         // the associated HildonStackableWindow
  GtkWidget *cur_view;       // the view main widget
  bool dirty;                // we need to redraw the cur_view
};

GtkWidget *device_label = NULL;
view *cur_view_struct = NULL;

view_id
get_current_view_id ()
{
  if (cur_view_struct && cur_view_struct->cur_view)
    return cur_view_struct->id;

  return NO_VIEW;
}

static GtkWidget *make_new_window (view *v);
GtkWidget *make_main_view (view *v);
GtkWidget *make_install_applications_view (view *v);
GtkWidget *make_install_section_view (view *v);
GtkWidget *make_upgrade_applications_view (view *v);
GtkWidget *make_uninstall_applications_view (view *v);
GtkWidget *make_search_results_view (view *v);

view main_view = {
  NULL,
  MAIN_VIEW,
  make_main_view,
  NULL, NULL, false
};

view install_applications_view = {
  &main_view,
  INSTALL_APPLICATIONS_VIEW,
  make_install_applications_view,
  NULL, NULL, false
};

view upgrade_applications_view = {
  &main_view,
  UPGRADE_APPLICATIONS_VIEW,
  make_upgrade_applications_view,
  NULL, NULL, false
};

view uninstall_applications_view = {
  &main_view,
  UNINSTALL_APPLICATIONS_VIEW,
  make_uninstall_applications_view,
  NULL, NULL, false
};

view install_section_view = {
  &install_applications_view,
  INSTALL_SECTION_VIEW,
  make_install_section_view,
  NULL, NULL, false
};

view search_results_view = {
  &main_view,
  SEARCH_RESULTS_VIEW,
  make_search_results_view,
  NULL, NULL, false
};

static GtkWindow *main_window = NULL;

static GList *install_sections = NULL;
static GList *upgradeable_packages = NULL;
static GList *installed_packages = NULL;
static GList *search_result_packages = NULL;
static gboolean package_list_ready = false;

static int cur_section_rank;
static char *cur_section_name;

static void
set_current_view (view *v)
{
  g_return_if_fail (v != NULL);

  main_window = GTK_WINDOW (v->window);

  if (v->id == MAIN_VIEW)
    {
      enable_update_all (false);
      enable_refresh (false);
      enable_search (false);
      enable_sort (false);
    }
  else if (v->id == UNINSTALL_APPLICATIONS_VIEW)
    {
      enable_update_all (false);
      enable_refresh (false);
      enable_search (true);
      enable_sort (true);
    }
  else if (v->id == INSTALL_SECTION_VIEW
           || v->id == SEARCH_RESULTS_VIEW)
    {
      enable_update_all (false);
      enable_refresh (true);
      enable_search (true);
      enable_sort (true);
    }
  else if (v->id == INSTALL_APPLICATIONS_VIEW)
    {
      enable_update_all (false);
      enable_refresh (true);
      enable_search (true);
      enable_sort (install_sections && !install_sections->next);
    }
  else if (v->id == UPGRADE_APPLICATIONS_VIEW)
    {
      enable_update_all (package_list_ready && upgradeable_packages);
      enable_refresh (true);
      enable_search (true);
      enable_sort (true);
    }

  cur_view_struct = v;
}

void
show_view (view *v)
{
  g_return_if_fail (v != NULL);

  g_debug ("showing view %d", v->id);

  GtkWidget *main_vbox = make_new_window (v);
  main_window = GTK_WINDOW (v->window);

  if (GTK_IS_WIDGET (v->cur_view))
    {
      gtk_container_remove (GTK_CONTAINER (main_vbox), v->cur_view);
      v->cur_view = NULL;
    }

  set_details_callback (NULL, NULL);
  set_operation_label (NULL);
  set_operation_callback (NULL, NULL);

  allow_updating ();

  v->cur_view = v->maker (v);
  v->dirty = false;

  gtk_box_pack_start (GTK_BOX (main_vbox), v->cur_view, TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  reset_idle_timer ();
}

static void
show_view_callback (GtkWidget *btn, gpointer data)
{
  view *v = (view *)data;

  show_view (v);
}

void
show_main_view ()
{
  show_view (&main_view);
}

void
show_parent_view ()
{
  if (cur_view_struct->parent != NULL)
    {
      g_debug ("Showing parent view :S");

      GtkWidget *win = cur_view_struct->window;
      HildonWindowStack *stack =
        hildon_stackable_window_get_stack (HILDON_STACKABLE_WINDOW (win));
      win = hildon_window_stack_pop_1 (stack);
      gtk_widget_destroy (win);
    }
}

static GtkWidget *
make_padded_button (const char *label)
{
  GtkWidget *btn = hildon_gtk_button_new (HILDON_SIZE_FINGER_HEIGHT);
  gtk_button_set_label(GTK_BUTTON(btn), label);
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
  GtkWidget *alignment;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *btn, *label, *image;
  GtkSizeGroup *btn_group;

  btn_group = gtk_size_group_new (GTK_SIZE_GROUP_BOTH);

  view = gtk_event_box_new ();
  gtk_widget_set_name (view, "osso-application-installer-main-view");

  g_signal_connect (view, "expose-event",
                    G_CALLBACK (expose_main_view), NULL);

  alignment = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);
  gtk_container_add (GTK_CONTAINER (view), alignment);

  vbox = gtk_vbox_new (FALSE, HILDON_MARGIN_TRIPLE);
  gtk_container_set_border_width (GTK_CONTAINER(vbox), HILDON_MARGIN_TRIPLE);
  gtk_container_add (GTK_CONTAINER (alignment), vbox);

  // Applications we have
  table = gtk_table_new (2, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), HILDON_MARGIN_DEFAULT);
  gtk_table_set_row_spacings (GTK_TABLE (table), HILDON_MARGIN_DEFAULT);

  // first label
  image = gtk_image_new_from_icon_name ("general_device_root_folder",
					HILDON_ICON_SIZE_SMALL);
  gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.5);
  gtk_table_attach (GTK_TABLE (table), image, 0, 1, 0, 1,
                    GTK_FILL, GTK_FILL, 0, 0);

  device_label = gtk_label_new (device_name ());
  hildon_helper_set_logical_color (device_label, GTK_RC_FG, GTK_STATE_NORMAL,
                                   "SecondaryTextColor");
  hildon_helper_set_logical_font(device_label, "SmallSystemFont");
  gtk_label_set_ellipsize (GTK_LABEL (device_label), PANGO_ELLIPSIZE_END);
  gtk_misc_set_alignment (GTK_MISC (device_label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), device_label, 1, 2, 0, 1,
                    GtkAttachOptions (GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

  g_signal_connect (device_label, "destroy",
		    G_CALLBACK (device_label_destroyed), NULL);

  // first button
  btn = make_padded_button (_("ai_li_uninstall"));
  g_signal_connect (G_OBJECT (btn), "clicked",
		    G_CALLBACK (show_view_callback),
		    &uninstall_applications_view);
  gtk_size_group_add_widget (btn_group, btn);
  grab_focus_on_map (btn);
  gtk_table_attach (GTK_TABLE (table), btn, 1, 2, 1, 2,
                    GtkAttachOptions(GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, TRUE, 0);

  // Applications we may want
  table = gtk_table_new (3, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), HILDON_MARGIN_DEFAULT);
  gtk_table_set_row_spacings (GTK_TABLE (table), HILDON_MARGIN_DEFAULT);

  // second label
  image = gtk_image_new_from_icon_name ("general_web",
					HILDON_ICON_SIZE_SMALL);
  gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.5);
  gtk_table_attach (GTK_TABLE (table), image, 0, 1, 0, 1,
                    GTK_FILL, GTK_FILL, 0, 0);

  label = gtk_label_new (_("ai_li_repository"));
  hildon_helper_set_logical_color (label, GTK_RC_FG, GTK_STATE_NORMAL,
                                   "SecondaryTextColor");
  hildon_helper_set_logical_font(label, "SmallSystemFont");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 1, 2, 0, 1,
                    GtkAttachOptions (GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

  // second button
  btn = make_padded_button (_("ai_li_install"));
  g_signal_connect (G_OBJECT (btn), "clicked",
		    G_CALLBACK (show_view_callback),
		    &install_applications_view);
  gtk_size_group_add_widget (btn_group, btn);
  gtk_table_attach (GTK_TABLE (table), btn, 1, 2, 1, 2,
                    GtkAttachOptions (GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

  // third button
  btn = make_padded_button (_("ai_li_update"));
  g_signal_connect (G_OBJECT (btn), "clicked",
		    G_CALLBACK (show_check_for_updates_view),
		    NULL);
  gtk_size_group_add_widget (btn_group, btn);
  gtk_table_attach (GTK_TABLE (table), btn, 1, 2, 2, 3,
                    GtkAttachOptions (GTK_EXPAND | GTK_FILL), GTK_FILL, 0, 0);

  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, TRUE, 0);

  gtk_widget_show_all (view);
  g_object_unref (btn_group);

  get_package_infos_in_background (NULL);

  enable_search (false);

  prevent_updating ();

  return view;
}

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
  repository = NULL;
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
  if (repository)
    g_free (repository);
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

const char *
package_info::get_display_version (bool installed)
{
  const char *v;
  if (installed)
    v = installed_version;
  else
    v = available_version;

  const char *p = strchr (v, ':');
  if (p)
    v = p + 1;

  return v;
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
  rank = 1;
  name = NULL;
  packages = NULL;
}

section_info::~section_info ()
{
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
free_all_packages ()
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
  if (name == NULL || red_pill_mode && red_pill_show_all)
    return name;

  name = canonicalize_section_name (name);

  if (*name == '\0')
    return "-";

  const char *translated_name =
    dgettext ("hildon-application-manager-categories", name);
  if (translated_name != name)
    name = translated_name;
  else
    name = NULL;

  return name;
}

static section_info *
find_section_info (GList **list_ptr,
		   int rank, const char *name)
{
  if (list_ptr)
    {
      for (GList *ptr = *list_ptr; ptr; ptr = ptr->next)
	{
	  section_info *si = (section_info *)ptr->data;
	  if (si->rank == rank && !strcmp (si->name, name))
	    return si;
	}
    }

  return NULL;
}

static section_info *
create_section_info (GList **list_ptr,
		     int rank, const char *name)
{
  if (name)
    name = nicify_section_name (canonicalize_section_name (name));

  if (!name)
    {
      /* If we don't have a name for the section, it can't be a
	 "normal" section.  Move it to the "other" rank in that case.
      */
      if (rank == SECTION_RANK_NORMAL)
	rank = SECTION_RANK_OTHER;

      if (rank == SECTION_RANK_ALL)
	name = _("ai_category_all");
      else if (rank == SECTION_RANK_OTHER)
	name = _("ai_category_other");
    }

  section_info *si = find_section_info (list_ptr, rank, name);
  
  if (si == NULL)
    {
      si = new section_info;
      si->rank = rank;
      si->name = name;
      si->packages = NULL;
      if (list_ptr)
	*list_ptr = g_list_prepend (*list_ptr, si);
    }

  return si;
}

static gint
compare_section_names (gconstpointer a, gconstpointer b)
{
  section_info *si_a = (section_info *)a;
  section_info *si_b = (section_info *)b;

  // The sorting of sections can not be configured.

  if (si_a->rank == si_b->rank)
    return g_ascii_strcasecmp (si_a->name, si_b->name);
  else
    return si_a->rank - si_b->rank;
}

static gint
compare_system_updates (package_info *pi_a, package_info *pi_b)
{
  if (((pi_a->flags & pkgflag_system_update)
       != (pi_b->flags & pkgflag_system_update)))
    {
      if (pi_a->flags & pkgflag_system_update)
	return -1;

      if (pi_b->flags & pkgflag_system_update)
	return 1;
    }

  /* None of the two packages has a higher priority
     both of them are system updates or both of them are not */
  return 0;
}

static gint
compare_package_installed_names (gconstpointer a, gconstpointer b)
{
  package_info *pi_a = (package_info *)a;
  package_info *pi_b = (package_info *)b;

  return package_sort_sign *
    g_ascii_strcasecmp (pi_a->get_display_name (true),
			pi_b->get_display_name (true));
}

static gint
compare_package_available_names (gconstpointer a, gconstpointer b)
{
  package_info *pi_a = (package_info *)a;
  package_info *pi_b = (package_info *)b;

  gint result =
    compare_system_updates (pi_a, pi_b);

  if (!result)
    {
      result = package_sort_sign *
	g_ascii_strcasecmp (pi_a->get_display_name (false),
			    pi_b->get_display_name (false));
    }

  return result;
}

static gint
compare_versions (const gchar *a, const gchar *b)
{
  return package_sort_sign * compare_deb_versions (a, b);
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

  gint result =
    compare_system_updates (pi_a, pi_b);

  if (!result)
    {
      result =
	compare_versions (pi_a->available_version, pi_b->available_version);
    }

  return result;
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

  gint result = compare_system_updates (pi_a, pi_b);

  if (!result)
  {
    if (pi_a->have_info && pi_b->have_info)
      result = (package_sort_sign *
                (pi_a->info.download_size - pi_b->info.download_size));
    else if (pi_a->have_info)
      result = package_sort_sign;
    else if (pi_b->have_info)
      result = -1 * package_sort_sign;
    else // We don't know the download sizes
      result = compare_package_available_names (a, b);
  }

  return result;
}

void
sort_all_packages (bool refresh_view)
{
  // If the first section is the "All" section, exclude it from the
  // sort.
  
  GList **section_ptr;
  if (install_sections
      && ((section_info *)install_sections->data)->rank == SECTION_RANK_ALL)
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

  if (refresh_view)
    show_view (cur_view_struct);
}

struct gpl_closure {
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
  info->flags = dec->decode_int ();
  
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

static bool
is_user_section (const char *section)
{
  if (section == NULL)
    return false;

  if (!strncmp (section, "maemo/", 6))
    return true;

  return !strncmp (section, "user/", 5);
}

static bool
package_visible (package_info *pi, bool installed)
{
  if (red_pill_mode && red_pill_show_all)
    return true;

  return is_user_section (installed
			  ? pi->installed_section
			  : pi->available_section);
}

static void
get_package_list_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  gpl_closure *c = (gpl_closure *)data;

  hide_updating ();

  if (dec == NULL)
    ;
  else if (dec->decode_int () == 0)
    what_the_fock_p ();
  else
    {
      section_info *all_si = create_section_info (NULL, SECTION_RANK_ALL, NULL);

      while (!dec->at_end ())
	{
	  package_info *info = NULL;

	  info = get_package_list_entry (dec);

	  if (info->available_version
	      && package_visible (info, false))
	    {
	      if (info->installed_version)
		{
		  info->ref ();
		  upgradeable_packages = g_list_prepend (upgradeable_packages,
							 info);
		}
	      else
		{
		  section_info *sec =
		    create_section_info (&install_sections,
					 SECTION_RANK_NORMAL,
					 info->available_section);
		  info->ref ();
		  sec->packages = g_list_prepend (sec->packages, info);

		  info->ref ();
		  all_si->packages = g_list_prepend (all_si->packages, info);
		}
	    }

	  if (info->installed_version
	      && package_visible (info, true))
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

  sort_all_packages (true);

  /* We switch to the parent view if the current one is the search
     results view.

     We also switch to the parent when the current view shows a
     section and that section is no longer available, or when no
     sections should be shown because there are too few.
  */

  if (cur_view_struct == &search_results_view
      || (cur_view_struct == &install_section_view
	  && (find_section_info (&install_sections,
				 cur_section_rank, cur_section_name) == NULL
	      || (install_sections && !install_sections->next))))
    show_parent_view ();

  if (c->cont)
    c->cont (c->data);

  delete c;
}

void
get_package_list_with_cont (void (*cont) (void *data), void *data)
{
  gpl_closure *c = new gpl_closure;
  c->cont = cont;
  c->data = data;

  clear_global_package_list ();
  clear_global_section_list ();

  /* Mark package list as not ready and cancel the package info
     getting in the background before freeing the list
  */
  package_list_ready = false;
  get_package_infos_in_background (NULL);
  free_all_packages ();

  show_updating ();
  apt_worker_get_package_list (!(red_pill_mode && red_pill_show_all),
			       false, 
			       false, 
			       NULL,
			       red_pill_mode && red_pill_show_magic_sys,
			       get_package_list_reply, c);
}

void
get_package_list ()
{
  get_package_list_with_cont (NULL, NULL);
}

/* GET_PACKAGE_INFO
 */

struct gpi_closure {
  void (*cont) (package_info *, void *, bool);
  void *data;
  package_info *pi;
};

static void gpi_reply  (int cmd, apt_proto_decoder *dec, void *clos);

void
get_package_info (package_info *pi,
		  bool only_basic_info,
		  void (*cont) (package_info *, void *, bool),
		  void *data)
{
  if (pi->have_info && only_basic_info)
    cont (pi, data, false);
  else
    {
      gpi_closure *c = new gpi_closure;
      c->cont = cont;
      c->data = data;
      c->pi = pi;
      pi->ref ();
      apt_worker_get_package_info (pi->name, only_basic_info,
				   gpi_reply, c);
    }
}

static void
gpi_reply  (int cmd, apt_proto_decoder *dec, void *clos)
{
  gpi_closure *c = (gpi_closure *)clos;
  void (*cont) (package_info *, void *, bool) = c->cont;
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
	  global_package_info_changed (pi);
	}
    }

  cont (pi, data, true);
  pi->unref ();
}

/* GET_PACKAGE_INFOS
 */

struct gpis_closure
{
  GList *package_list;
  GList *current_node;
  bool only_basic_info;
  void (*cont) (void *);
  void *data;
};

static void gpis_loop (package_info *pi, void *data, bool unused);

void
get_package_infos (GList *package_list,
		   bool only_basic_info,
		   void (*cont) (void *),
		   void *data)
{
  gpis_closure *clos = new gpis_closure;
  clos->package_list = package_list;
  clos->current_node = package_list;
  clos->only_basic_info = only_basic_info;
  clos->cont = cont;
  clos->data = data;

  gpis_loop (NULL, clos, TRUE);
}

static void
gpis_loop (package_info *pi, void *data, bool unused)
{
  gpis_closure *clos = (gpis_closure *)data;

  if (clos->current_node == NULL)
    {
      clos->cont (clos->data);
      delete clos;
    }
  else
    {
      GList *current_package_node = clos->current_node;
      clos->current_node = g_list_next (clos->current_node);
      get_package_info ((package_info *) current_package_node->data,
			clos->only_basic_info,
			gpis_loop, clos);
    }
}

/* GET_PACKAGE_INFOS_IN_BACKGROUND
 */

static void gpiib_trigger ();
static void gpiib_done (package_info *pi, void *unused, bool changed);

static GList *gpiib_next;

static void
get_package_infos_in_background (GList *packages)
{
  gpiib_next = packages;
  gpiib_trigger ();
}

static void
gpiib_trigger ()
{
  GList *n = gpiib_next;
  if (n)
    {
      package_info *pi = (package_info *)n->data;
      gpiib_next = n->next;
      get_package_info (pi, true,
			gpiib_done, NULL);
    }
}

static void 
gpiib_done (package_info *pi, void *data, bool changed)
{
  gpiib_trigger ();

  /* Resort & refresh view
   * only needed when we are sorting by size */
  if (!gpiib_next && changed &&
      (package_sort_key == SORT_BY_SIZE))
    sort_all_packages (true);
}

/* REFRESH_PACKAGE_CACHE_WITHOUT_USER
 */

struct rpcwu_clos {
  void (*cont) (bool keep_going, void *data);
  void *data;
  bool keep_going;
};

static void rpcwu_with_network (bool success, void *data);
static void rpcwu_reply (int cmd, apt_proto_decoder *dec, void *data);
static void rpcwu_end (void *data);

static entertainment_game rpcwu_games[] = {
  { op_downloading, 0.5 },
  { op_general,     0.5 }
};

void
refresh_package_cache_without_user (const char *title,
				    void (*cont) (bool keep_going, void *data),
				    void *data)
{
  rpcwu_clos *c = new rpcwu_clos;
  c->cont = cont;
  c->data = data;

  if (title)
    set_entertainment_main_title (title, true);
  else
    set_entertainment_main_title (_("ai_nw_checking_updates"), true);
  set_entertainment_games (2, rpcwu_games);
  set_entertainment_fun (NULL, -1, -1, 0);
  start_entertaining_user (TRUE);

  ensure_network (rpcwu_with_network, c);
}

static void
rpcwu_cancel (void *data)
{
  cancel_apt_worker ();
}

static void
rpcwu_with_network (bool success, void *data)
{
  rpcwu_clos *c = (rpcwu_clos *)data;

  if (success)
    {
      set_entertainment_cancel (rpcwu_cancel, c);
      apt_worker_update_cache (rpcwu_reply, c);
    }
  else
    {
      c->keep_going = false;
      stop_entertaining_user ();
      rpcwu_end (c);
    }
}

static void
rpcwu_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  rpcwu_clos *c = (rpcwu_clos *)data;

  c->keep_going = !entertainment_was_cancelled ();
  stop_entertaining_user ();

  /* We only set update time when list download isn't interrupted */
  if (c->keep_going)
    save_last_update_time (time (NULL));

  get_package_list_with_cont (rpcwu_end, c);
}

static void
rpcwu_end (void *data)
{
  rpcwu_clos *c = (rpcwu_clos *)data;
  
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
    refresh_package_cache_without_user (NULL, rpcwuf_end, NULL);
}

static void
rpcwuf_end (bool ignore, void *unused)
{
  end_interaction_flow ();
  force_show_catalogue_errors ();
}

/* Call refresh_package_cache_without_user_flow when the last update
   was more than one 'check-interval' ago.
*/

void
maybe_refresh_package_cache_without_user ()
{
  if (!is_idle ())
    return;

  if (red_pill_check_always)
    {
      refresh_package_cache_without_user_flow ();
      return;
    }

  if (!is_package_cache_updated ())
    refresh_package_cache_without_user_flow ();
}

/* Set the system catalogue and refresh &
 * Add a temporal catalogue and refresh
 */

struct scar_clos {
  void (*cont) (bool keep_going, void *data);
  void *data;
  char *title;
};

static void scar_set_catalogues_reply (int cmd, apt_proto_decoder *dec,
                                       void *data);

void
add_temp_catalogues_and_refresh (xexp *tempcat,
                                 const char *title,
                                 void (*cont) (bool keep_going, void *data),
                                 void *data)
{
  scar_clos *c = new scar_clos;
  c->cont = cont;
  c->data = data;
  c->title = g_strdup (title);

  apt_worker_add_temp_catalogues (tempcat, scar_set_catalogues_reply, c);
}

void
set_catalogues_and_refresh (xexp *catalogues,
                            const char *title,
                            void (*cont) (bool keep_going, void *data),
                            void *data)
{
  scar_clos *c = new scar_clos;
  c->cont = cont;
  c->data = data;
  c->title = g_strdup (title);

  apt_worker_set_catalogues (catalogues, scar_set_catalogues_reply, c);
}

static void
scar_set_catalogues_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  scar_clos *c = (scar_clos *)data;

  refresh_package_cache_without_user (c->title, c->cont, c->data);

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

void
update_all_packages_flow ()
{
  if (start_interaction_flow ())
    {
      GList *packages_list = g_list_copy (upgradeable_packages);
      install_packages (packages_list,
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

static void
ignore_package_info (package_info *pi, void *data, bool changed)
{
}

void
available_package_selected (package_info *pi)
{
  if (pi)
    {
      set_details_callback (available_package_details, pi);
      set_operation_callback (install_operation_callback, pi);
      pi->ref ();
      get_package_info (pi, true, ignore_package_info, NULL);
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
      set_operation_label (_("ai_me_package_uninstall"));
    }
  else
    {
      set_details_callback (NULL, NULL);
      set_operation_callback (NULL, NULL);
      set_operation_label (_("ai_tb_uninstall"));
    }
}

static void
installed_package_activated (package_info *pi)
{
  if (pi->flags & pkgflag_system_update)
    irritate_user (_("ai_ni_unable_to_uninstall_system_update"));
  else
    uninstall_package_flow (pi);
}

GtkWidget *
make_install_section_view (view *v)
{
  GtkWidget *view;

  set_operation_label (_("ai_tb_install"));

  section_info *si = find_section_info (&install_sections,
					cur_section_rank, cur_section_name);

  view =
    make_global_package_list (si? si->packages : NULL,
			      false,
			      (package_list_ready
			       ? _("ai_li_no_applications_available")
			       : NULL),
			      _("ai_me_cs_install"),
			      available_package_selected, 
			      available_package_activated);

  gtk_widget_show_all (view);

  if (si)
    get_package_infos_in_background (si->packages);

  enable_search (true);
  enable_refresh (true);

  return view;
}

static void
view_section (section_info *si)
{
  if (si->name == NULL)
    return;
  g_free (cur_section_name);
  cur_section_name = g_strdup (si->name);
  cur_section_rank = si->rank;

  show_view (&install_section_view);
}

static bool catalogue_errors_ignored = false;
static void check_catalogues ();

struct scedf_clos {
  xexp *catalogues;
};

static void scedf_dialog_done (bool changed, void *data);
static void scedf_end (bool keep_going, void *data);

void
force_show_catalogue_errors ()
{
  catalogue_errors_ignored = false;
}

static void
show_catalogue_errors_dialog (void *data)
{
  scedf_clos *c = new scedf_clos;

  c->catalogues = NULL;
  show_catalogue_dialog (NULL, true, scedf_dialog_done, c);
}

static void
scedf_check_catalogues (bool keep_going, void *data)
{
  // let's finish the interaction flow first
  scedf_end (keep_going, data);

  if (keep_going)
    check_catalogues ();
}

static void
scedf_dialog_done (bool changed, void *data)
{
  scedf_clos *c = (scedf_clos *)data;

  if (changed)
    set_catalogues_and_refresh (c->catalogues,
				NULL, scedf_check_catalogues, c);
  else
    {
      catalogue_errors_ignored = true;
      scedf_end (true, c);
    }
}

static void
scedf_end (bool keep_going, void *data)
{
  scedf_clos *c = (scedf_clos *)data;

  xexp_free (c->catalogues);
  delete c;

  end_interaction_flow ();
}

static void
scedf_cont (void *data)
{
  catalogue_errors_ignored = true;

  end_interaction_flow ();
}

static void
show_update_partially_successfull_dialog_flow ()
{
  if (!catalogue_errors_ignored
      && start_interaction_flow ())
    {
      annoy_user_with_arbitrary_details_2 (_("ai_ni_update_partly_successful"),
                                           show_catalogue_errors_dialog,
                                           scedf_cont, NULL);
    }
}

static void
check_catalogues_reply (xexp *catalogues, void *data)
{
  bool is_there_catalogues = false;
  bool is_there_errors = false;

  for (xexp *c = xexp_first (catalogues); c; c = xexp_rest (c))
  {
    if (xexp_is (c, "source")
	|| (xexp_is (c, "catalogue") && !xexp_aref_bool (c, "disabled")))
      is_there_catalogues = true;
    if (xexp_aref_bool (c, "errors"))
      is_there_errors = true;
  }

  xexp_free (catalogues);

  if (is_there_errors)
    show_update_partially_successfull_dialog_flow ();
  else if (!is_there_catalogues) // No catalogues active.
    irritate_user (_("ai_ib_no_repositories"));
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

  set_operation_label (_("ai_tb_install"));

  if (install_sections && install_sections->next == NULL)
    {
      section_info *si = (section_info *)install_sections->data;
      view =
	make_global_package_list (si->packages,
				  false,
				  (package_list_ready
				   ? _("ai_li_no_applications_available")
				   : NULL),
				  _("ai_me_cs_install"),
				  available_package_selected,
				  available_package_activated);
      get_package_infos_in_background (si->packages);
    }
  else
    {
      view = make_global_section_list (install_sections, view_section);
    }

  gtk_widget_show_all (view);

  maybe_refresh_package_cache_without_user ();

  return view;
}

void
show_check_for_updates_view ()
{
  show_view (&upgrade_applications_view);
}

static void
update_seen_updates_file (void)
{
  /* Build the list of seen updates */
  xexp *seen_updates = xexp_list_new ("updates");
  for (GList *pkg = upgradeable_packages; pkg; pkg = pkg->next)
    xexp_cons (seen_updates,
               xexp_text_new ("pkg",
                              ((package_info *)pkg->data)->name));

  /* Write it to disk */
  user_file_write_xexp (UFILE_SEEN_UPDATES, seen_updates);
  xexp_free (seen_updates);
}

GtkWidget *
make_upgrade_applications_view (view *v)
{
  GtkWidget *view;

  check_catalogues ();

  set_operation_label (_("ai_tb_update"));

  view =
    make_global_package_list (upgradeable_packages,
			      false,
			      (package_list_ready
			       ? _("ai_li_no_updates_available")
			       : NULL),
			      _("ai_me_cs_update"),
			      available_package_selected,
			      available_package_activated);

  gtk_widget_show_all (view);

  get_package_infos_in_background (upgradeable_packages);

  enable_search (true);
  enable_refresh (true);

  if (package_list_ready
      && hildon_window_get_is_topmost (HILDON_WINDOW (get_main_window ())))
    {
      update_seen_updates_file ();
    }

  maybe_refresh_package_cache_without_user ();

  return view;
}

GtkWidget *
make_uninstall_applications_view (view *v)
{
  GtkWidget *view;

  set_operation_label (_("ai_tb_uninstall"));

  view = make_global_package_list (installed_packages,
				   true,
				   (package_list_ready
				    ? _("ai_li_no_installed_applications")
				    : NULL),
				   _("ai_me_cs_uninstall"),
				   installed_package_selected,
				   installed_package_activated);
  gtk_widget_show_all (view);

  enable_search (true);
  enable_refresh (false);

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
			   ? _("ai_tb_install")
			   : _("ai_tb_update"));

      view = make_global_package_list (search_result_packages,
				       false,
				       NULL,
				       (v->parent == &install_applications_view
					? _("ai_me_cs_install")
					: _("ai_me_cs_update")),
				       available_package_selected,
				       available_package_activated);
      get_package_infos_in_background (search_result_packages);
    }
  else
    {
      set_operation_label (_("ai_tb_uninstall"));

      view = make_global_package_list (search_result_packages,
				       true,
				       NULL,
				       _("ai_me_cs_uninstall"),
				       installed_package_selected,
				       installed_package_activated);
    }
  gtk_widget_show_all (view);

  enable_search (true);
  enable_refresh (true);

  return view;
}

static void
search_package_list (GList **result,
                     GList *packages, const char *pattern, bool installed)
{
  gchar **words;
  gboolean found;
  int i;

  if (!(words = g_strsplit (pattern, " ", 0)))
    return; /* pattern is empty */

  while (packages)
    {
      package_info *pi = (package_info *)packages->data;
      found = FALSE;

      for (i = 0; words[i] != NULL; i++)
        if (strcasestr (pi->get_display_name (installed), words[i]))
          found = TRUE;
        else
          {
            found = FALSE;
            break;
          }

      if (found)
        {
          pi->ref ();
          *result = g_list_append (*result, pi);
        }

      packages = packages->next;
    }
  g_strfreev (words);
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
find_package_in_lists (GList **result,
                       const char *package_name)
{
      find_in_section_list (result, install_sections, package_name);
      find_in_package_list (result, upgradeable_packages, package_name);
      find_in_package_list (result, installed_packages, package_name);
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
      const char *name = NULL;
      package_info *info = NULL;

      info = get_package_list_entry (dec);
      name = info->name;

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
      info->unref();
    }

  clear_global_package_list ();
  free_packages (search_result_packages);
  search_result_packages = result;

  if (result)
    {
      sort_all_packages (false);
      show_view (&search_results_view);
      irritate_user (_("ai_ib_search_complete"));
    }
  else
    {
      show_view (&search_results_view);
      irritate_user (_("ai_ib_no_matches"));
    }
}

static void
change_search_view_parent (view *new_parent)
{
  g_debug ("setting search results parent to %d", new_parent->id);

  GtkWidget *win = cur_view_struct->window;
  HildonWindowStack *stack =
    hildon_stackable_window_get_stack (HILDON_STACKABLE_WINDOW (win));

  while (win != main_view.window && win != new_parent->window)
    {
      set_current_view (cur_view_struct->parent);
      GtkWidget *hide_win = hildon_window_stack_pop_1 (stack);
      g_assert (hide_win == win);  // stack and curr_view may be different
      win = cur_view_struct->window;
      gtk_widget_destroy (hide_win);
    }

  search_results_view.parent = new_parent;
  g_debug ("the new search results parent and current view is %d",
             search_results_view.parent->id);
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

  change_search_view_parent (parent);

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

      clear_global_package_list ();
      free_packages (search_result_packages);
      search_result_packages = result;
      show_view (&search_results_view);

      if (result)
        irritate_user (_("ai_ib_search_complete"));
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
      apt_worker_get_package_list (!(red_pill_mode && red_pill_show_all),
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
install_named_package (const char *package,
                       void (*cont) (int n_successful, void *data), void *data)
{
  GList *p = NULL;
  GList *node = NULL;

  find_package_in_lists (&p, package);

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
install_named_packages (const char **packages,
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

      find_package_in_lists (&search_list, *current_package);

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
	  pi->flags = 0;

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
		    install_type, automatic,
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
}

static void (*operation_func) (gpointer);
static gpointer operation_data;
static const char *operation_label;

void
do_current_operation ()
{
  if (operation_func)
    operation_func (operation_data);
}

static void
set_operation_label (const char *label)
{
  if (label == NULL)
    label = _("ai_tb_install");

  operation_label = label;
}

static void
set_operation_callback (void (*func) (gpointer), gpointer data)
{
  operation_data = data;
  operation_func = func;
  set_operation_label (operation_label);
}

/* Reinstalling the packages form the recently restored backup.
 */

static void rp_restore (bool keep_going, void *data);
static void rp_unsuccessful (void *data);
static void rp_end (int n_successful, void *data);

struct restore_cont_data
{
  void (*restore_cont) (const char *title, 
      void (*cont) (bool keep_going, void *data), void *data);
  gchar* msg;
  void (*restore_cont_cont) (bool keep_going, void *data);
  void *data;
};

void
rp_ensure_cont (bool success, void *data)
{
  restore_cont_data *c = (restore_cont_data *) data;
  
  c->restore_cont (c->msg,
                   c->restore_cont_cont,
                   c->data);
  delete c;
}

void
restore_packages_flow ()
{
  if (start_interaction_flow ())
    {
      FILE *file = NULL;
      GError *error = NULL;
      xexp *backup = NULL;
      struct stat buf;

      /* Check if there is no base packages.backup file yet */
      if (stat (BACKUP_PACKAGES, &buf) && (errno == ENOENT))
        {
          annoy_user (_("ai_ni_all_installed"), rp_unsuccessful, NULL);
          return;
        }

      file = user_file_open_for_read (UFILE_RESTORE_BACKUP);
      if (file != NULL)
	{
	  backup = xexp_read (file, &error);
	  fclose (file);
	}

      if (backup)
        {
          restore_cont_data *rc_data = new restore_cont_data;
          rc_data->restore_cont = refresh_package_cache_without_user;
          rc_data->msg = _("ai_nw_preparing_installation");
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
      install_named_packages (names, INSTALL_TYPE_BACKUP, false,
                              NULL, NULL, rp_end, backup);
      delete [] names;
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
	  char *fileuri = NULL;
	  if (g_path_is_absolute (filename))
	    fileuri = gnome_vfs_get_uri_from_local_path (filename);

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

static void
view_set_dirty (view *v)
{
  if (v && v != &main_view)
    v->dirty = true;
}

static void
reset_view (view *v)
{
  v->window = NULL;
  v->cur_view = NULL;
  //  cur_view_struct = v->parent;
  view_set_dirty (v->parent);
}

static void
stack_window_hide (GtkWidget* widget, gpointer data)
{
  view *v = (view *) data;

  g_debug ("hide event on view %d", v->id);
  reset_view (v);
}

static gboolean
window_delete_event (GtkWidget* widget, GdkEvent *ev, gpointer data)
{
  /* Finish any interaction flow if it's still active */
  if (is_interaction_flow_active ())
    end_interaction_flow ();

  hide_main_window ();
  maybe_exit ();
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

GtkWindow *
get_main_window ()
{
  g_assert (main_window != NULL);
  g_debug ("main window pointer %p", main_window);
  return main_window;
}

GtkWidget *
get_device_label ()
{
  return device_label;
}

static void
is_topmost_cb (GtkWidget *widget, GParamSpec *arg, gpointer data)
{
  g_return_if_fail(widget != NULL && HILDON_IS_WINDOW(widget));

  HildonWindow *window = HILDON_WINDOW (widget);

  set_current_view ((view *) data);
  g_debug ("the top view is %d", cur_view_struct->id);

  /* Update the seen-updates file if the window is top most again and
     the "Check for updates" view is currently selected */
  if (package_list_ready && hildon_window_get_is_topmost (window) &&
      (cur_view_struct == &upgrade_applications_view))
    {
      update_seen_updates_file ();
    }

  if (cur_view_struct->dirty)
    show_view (cur_view_struct);
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
      show_parent_view ();

      /* We must return FALSE here since the long-press handling code
	 in HildonWindow needs to see the key press as well to start
	 the timer.
      */
      return FALSE;
    }

  return FALSE;
}

static GtkWidget *
make_new_window (view *v)
{
  if (v->window)
    {
      GtkWidget *child = gtk_bin_get_child (GTK_BIN (v->window));
      return child;
    }

  v->window = hildon_stackable_window_new ();
  gtk_window_set_title (GTK_WINDOW (v->window),
                        _("ai_ap_application_installer"));

  g_signal_connect(G_OBJECT (v->window), "notify::is-topmost",
                   G_CALLBACK (is_topmost_cb), v);
  g_signal_connect (G_OBJECT (v->window), "key_press_event",
		    G_CALLBACK (key_event), NULL);
  g_signal_connect (G_OBJECT (v->window), "key_release_event",
		    G_CALLBACK (key_event), NULL);

  GtkWidget *vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (v->window), vbox);

  g_signal_connect (G_OBJECT (v->window), "realize",
 		    G_CALLBACK (main_window_realized), NULL);

  // Add window to HildonProgram to use common menu
  hildon_program_add_window (hildon_program_get_instance (),
                             HILDON_WINDOW (v->window));

  // Add the window to the stack
  if (v->parent && v->parent->window)
    {
      HildonWindowStack *stack = hildon_stackable_window_get_stack
        (HILDON_STACKABLE_WINDOW (v->parent->window));
      hildon_window_stack_push_1 (stack, HILDON_STACKABLE_WINDOW (v->window));

      g_signal_connect (G_OBJECT (v->window), "hide",
                        G_CALLBACK (stack_window_hide), v);
    }

  return vbox;
}

static osso_context_t *osso_ctxt;

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

int
main (int argc, char **argv)
{
  const char *apt_worker_prog = "/usr/libexec/apt-worker";
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

  load_system_settings ();
  load_settings ();

  hildon_gtk_init (&argc, &argv);

  init_dbus_or_die (show);

  osso_ctxt = osso_initialize ("hildon_application_manager",
			       PACKAGE_VERSION, TRUE, NULL);

  // XXX - We don't want a two-part title and this seems to be the
  //       only way to get rid of it.  Hopefully, setting an empty
  //       application name doesn't break other stuff.
  //
  g_set_application_name ("");

  clear_log ();

  show_view (&main_view);
  main_window = GTK_WINDOW (main_view.window);
  cur_view_struct = &main_view;

  g_signal_connect (G_OBJECT (main_window), "destroy",
                    G_CALLBACK (window_destroy), NULL);

  g_signal_connect (G_OBJECT (main_window), "delete-event",
                    G_CALLBACK (window_delete_event), NULL);

  create_menu ();

  if (!start_apt_worker (apt_worker_prog))
    what_the_fock_p ();

  apt_worker_set_status_callback (apt_status_callback, NULL);

  get_package_list_with_cont (notice_initial_packages_available, NULL);
  save_backup_data ();

  if (show)
    present_main_window ();

  gtk_main ();
}
