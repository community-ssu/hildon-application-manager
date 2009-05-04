/*
 * This file is part of the hildon-application-manager.
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

#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <libintl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/statvfs.h>
#include <signal.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <hildon/hildon-note.h>
#include <hildon/hildon-file-chooser-dialog.h>
#include <hildon/hildon-check-button.h>
#include <hildon/hildon-banner.h>
#include <hildon/hildon-pannable-area.h>
#include <gdk/gdkkeysyms.h>
#include <hildon/hildon-defines.h>
#include <conic.h>
#include <libgnomevfs/gnome-vfs.h>
#include <dbus/dbus.h>

#include "main.h"
#include "util.h"
#include "details.h"
#include "log.h"
#include "settings.h"
#include "menu.h"
#include "operations.h"
#include "apt-worker-client.h"
#include "user_files.h"
#include "update-notifier-conf.h"
#include "package-info-cell-renderer.h"

#define _(x) gettext (x)
#define _FM(x) dgettext ("hildon-fm", x)
#define _HCS(x) dgettext ("hildon-common-strings", x)

static Window parent_xid = None;
static GSList *dialog_stack = NULL;
static bool main_window_shown = false;

static time_t idle_since = 0;
static guint idle_timeout_id = 0;
static void (*idle_cont) (void *) = NULL;
static void *idle_data = NULL;

#define IDLE_TIMEOUT_SECS 60

static void
dialog_realized (GtkWidget *dialog, gpointer data)
{
  GdkWindow *win = dialog->window;

  XSetTransientForHint (GDK_WINDOW_XDISPLAY (win), GDK_WINDOW_XID (win),
			parent_xid);
}

void
push_dialog (GtkWidget *dialog)
{
  /* Setting a dialog application modal affects us, not the
     application that the dialog is transient for.  So we only make it
     application modal when the dialog stack is shown on top of us.
  */
  g_assert (dialog != NULL);

  gtk_window_set_modal (GTK_WINDOW (dialog), parent_xid == None);

  if (dialog_stack)
    {
      g_warning ("parent %p", dialog_stack->data);
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                    GTK_WINDOW (dialog_stack->data));
    }
  else if (parent_xid != None)
    g_signal_connect (dialog, "realize",
		      G_CALLBACK (dialog_realized), NULL);
  else
    {
      /* This happens for dialogs outside of a interaction flow.
       */
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
				    get_main_window ());
    }

  g_warning ("pushing dialog %p", dialog);
  dialog_stack = g_slist_prepend (dialog_stack, dialog);
}

void
pop_dialog (GtkWidget *dialog)
{
  g_assert (dialog_stack);
  g_warning ("child = %p ~ parent = %p", dialog, dialog_stack->data);
  g_assert (dialog_stack->data == dialog);

  {
    GSList *old = dialog_stack;
    dialog_stack = dialog_stack->next;
    g_slist_free_1 (old);
  }
}

static bool interaction_flow_active = false;

bool
is_idle ()
{
  return dialog_stack == NULL && !interaction_flow_active;
}

bool
start_interaction_flow ()
{
  /* XXX - We don't allow interaction flows to start when a dialog is
           open.  This is a bit too restrictive since we should be
           able to run one just fine in parallel to showing the
           "Details" or "Catalogues" dialog, say.  We don't allow it
           since it would mess with having a single stack of dialogs.
  */

  if (dialog_stack != NULL
      || interaction_flow_active)
    {
      irritate_user (_("ai_ni_operation_progress"));
      return false;
    }

  g_assert (dialog_stack == NULL);

  interaction_flow_active = true;
  parent_xid = None;
  dialog_stack = g_slist_prepend (dialog_stack, get_main_window ());

  if (idle_timeout_id)
    {
      g_source_remove (idle_timeout_id);
      idle_timeout_id = 0;
    }

  return true;
}

bool
start_foreign_interaction_flow (Window parent)
{
  if (dialog_stack != NULL
      || interaction_flow_active)
    return false;

  g_assert (dialog_stack == NULL);

  interaction_flow_active = true;
  parent_xid = parent;
  return true;
}

bool
start_interaction_flow_when_idle (void (*cont) (void *), void *data)
{
  if (is_idle () && time (NULL) > idle_since + IDLE_TIMEOUT_SECS)
    {
      if (start_interaction_flow ())
	cont (data);
      return true;
    }
  else if (idle_cont != NULL)
    {
      return false;
    }
  else
    {
      idle_cont = cont;
      idle_data = data;
      return true;
    }
}

static gboolean
idle_callback (gpointer unused)
{
  if (idle_cont)
    {
      if (start_interaction_flow ())
	{
	  void (*cont) (void *) = idle_cont;
	  void *data = idle_data;
	  
	  idle_cont = NULL;
	  idle_data = NULL;

	  cont (data);
	}
    }

  return FALSE;
}

void
reset_idle_timer ()
{
  if (idle_timeout_id)
    g_source_remove (idle_timeout_id);

  if (is_idle ())
    {
      idle_since = time (NULL);
      idle_timeout_id = g_timeout_add (IDLE_TIMEOUT_SECS * 1000,
				       idle_callback,
				       NULL);
    }
}

void
end_interaction_flow ()
{
  g_assert (interaction_flow_active);

  if (parent_xid == None)
    {
      g_assert (g_slist_length (dialog_stack) == 1);

      GtkWidget* initmainwin = GTK_WIDGET (dialog_stack->data);
      GtkWidget* curmainwin = GTK_WIDGET (get_main_window ());
      if (initmainwin != curmainwin)
        g_warning ("We lose the initial interaction flow window!");

      pop_dialog (initmainwin);
    }

  interaction_flow_active = false;
  parent_xid = None;

  reset_idle_timer ();
}

bool
is_interaction_flow_active ()
{
  return interaction_flow_active;
}

void
present_main_window ()
{
  reset_idle_timer ();
  main_window_shown = true;
  gtk_window_present (get_main_window ());
}

void
hide_main_window ()
{
  gtk_widget_hide (GTK_WIDGET (get_main_window ()));
  main_window_shown = false;
  if (!interaction_flow_active)
    exit (0);
}

void
maybe_exit ()
{
  if (!main_window_shown && !interaction_flow_active)
    exit (0);
}

struct ayn_closure {
  package_info *pi;
  detail_kind kind;
  void (*cont) (bool res, void *data);
  void (*details) (void *data);
  void *data;
};

static void
yes_no_details_done (void *data)
{
}

static void
yes_no_response (GtkDialog *dialog, gint response, gpointer clos)
{
  ayn_closure *c = (ayn_closure *)clos;

  if (response == 1)
    {
      if (c->pi)
        show_package_details (c->pi, c->kind, false,  yes_no_details_done, c);
      else if (c->details)
	c->details (c->data);
      return;
    }

  void (*cont) (bool res, void *data) = c->cont;
  void *data = c->data;
  if (c->pi)
    c->pi->unref ();
  delete c;

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));
  if (cont)
    cont (response == GTK_RESPONSE_OK, data);
}

void
ask_yes_no (const gchar *question,
	    void (*cont) (bool res, void *data),
	    void *data)
{
  GtkWidget *dialog;
  ayn_closure *c = new ayn_closure;
  c->pi = NULL;
  c->cont = cont;
  c->details = NULL;
  c->data = data;

  dialog = hildon_note_new_confirmation (NULL, question);
  push_dialog (dialog);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (yes_no_response), c);
  gtk_widget_show_all (dialog);
}

void
ask_yes_no_with_title (const gchar *title,
		       const gchar *question,
		       void (*cont) (bool res, void *data),
		       void *data)
{
  GtkWidget *dialog;
  ayn_closure *c = new ayn_closure;
  c->pi = NULL;
  c->cont = cont;
  c->details = NULL;
  c->data = data;

  dialog = gtk_dialog_new_with_buttons
    (title,
     NULL,
     GTK_DIALOG_MODAL,
     _("ai_bd_confirm_ok"),      GTK_RESPONSE_OK,
     NULL);
  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
		     gtk_label_new (question));

  g_signal_connect (dialog, "response",
		    G_CALLBACK (yes_no_response), c);
  gtk_widget_show_all (dialog);
}

void
ask_custom (const gchar *question,
	    const gchar *ok_label, const gchar *cancel_label,
	    void (*cont) (bool res, void *data),
	    void *data)
{
  GtkWidget *dialog;
  ayn_closure *c = new ayn_closure;
  c->pi = NULL;
  c->cont = cont;
  c->details = NULL;
  c->data = data;

  dialog = hildon_note_new_confirmation_add_buttons
    (NULL,
     question,
     ok_label, GTK_RESPONSE_OK,
     cancel_label, GTK_RESPONSE_CANCEL,
     NULL);
  push_dialog (dialog);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (yes_no_response), c);
  gtk_widget_show_all (dialog);
}

void
ask_yes_no_with_details (const gchar *title,
			 const gchar *question,
			 package_info *pi, detail_kind kind,
			 void (*cont) (bool res, void *data),
			 void *data)
{
  GtkWidget *dialog;
  ayn_closure *c = new ayn_closure;
  c->pi = pi;
  pi->ref ();
  c->kind = kind;
  c->cont = cont;
  c->details = NULL;
  c->data = data;

  char *ok = (kind == remove_details) ?
    _("ai_bd_confirm_uninstall") : _("ai_bd_confirm_ok");

  dialog = gtk_dialog_new_with_buttons
    (title,
     NULL,
     GTK_DIALOG_MODAL,
     ok, GTK_RESPONSE_OK,
     _("ai_bd_confirm_details"), 1,
     NULL);
  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
		     gtk_label_new (question));

  g_signal_connect (dialog, "response",
		    G_CALLBACK (yes_no_response), c);
  gtk_widget_show_all (dialog);
}

void
ask_yes_no_with_arbitrary_details (const gchar *title,
				   const gchar *question,
				   void (*cont) (bool res, void *data),
				   void (*details) (void *data),
				   void *data)
{
  GtkWidget *dialog;
  ayn_closure *c = new ayn_closure;
  c->pi = NULL;
  c->cont = cont;
  c->details = details;
  c->data = data;

  dialog = gtk_dialog_new_with_buttons
    (title,
     NULL,
     GTK_DIALOG_MODAL,
     _("ai_bd_add_catalogue_ok"),      GTK_RESPONSE_OK,
     _("ai_bd_add_catalogue_details"), 1,
     NULL);
  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  GtkWidget *label = gtk_label_new (question);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
		     label);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (yes_no_response), c);
  gtk_widget_show_all (dialog);
}

struct auwc_closure {
  void (*cont) (void *);
  void *data;
};

static void
annoy_user_response (GtkDialog *dialog, gint response, gpointer data)
{
  auwc_closure * closure_data = (auwc_closure *) data;

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (closure_data != NULL)
    {
      if (closure_data->cont != NULL)
	closure_data->cont (closure_data->data);
      delete closure_data;
    }
}

void
annoy_user (const gchar *text, void (*cont) (void *data), void *data)
{
  GtkWidget *dialog;
  auwc_closure * closure_data = new auwc_closure;

  dialog = hildon_note_new_information (NULL, text);
  push_dialog (dialog);
  closure_data->cont = cont;
  closure_data->data = data;
  g_signal_connect (dialog, "response",
		    G_CALLBACK (annoy_user_response), closure_data);
  gtk_widget_show_all (dialog);
}

struct auwd_closure {
  package_info *pi;
  detail_kind kind;
  int variant;
  void (*details) (void *data);
  void (*cont) (void *data);
  void *data;
};

static void
annoy_details_done (void *data)
{
}

static void
annoy_user_with_details_response (GtkDialog *dialog, gint response,
				  gpointer data)
{
  auwd_closure *c = (auwd_closure *)data;

  if (response == 1)
    {
      if (c->pi)
        show_package_details (c->pi, c->kind, true, annoy_details_done, c);
      else
	{
	  if (c->variant == 2)
	    {
	      pop_dialog (GTK_WIDGET (dialog));
	      gtk_widget_destroy (GTK_WIDGET (dialog));
	      if (c->pi)
		c->pi->unref ();
	      if (c->details)
		c->details (c->data);
	      delete c;
	    }
	  else
	    {
	      if (c->details)
		c->details (c->data);
	    }
	}
    }
  else
    {
      pop_dialog (GTK_WIDGET (dialog));
      gtk_widget_destroy (GTK_WIDGET (dialog));
      if (c->pi)
	c->pi->unref ();
      if (c->cont)
	c->cont(c->data);
      delete c;
    }
}

/* There are two variants of the "with-details" dialog (which might
   get unified soon, or not).  Variant one uses the "Close" label and
   runs the details callback as a subroutine of the dialog.  Variant 2
   uses the "Ok" label and closes the dialog before invoking the
   details callback.
*/

static void
annoy_user_with_details_1 (const gchar *text,
			   void (*details) (void *data),
			   package_info *pi, detail_kind kind,
			   int variant,
			   void (*cont) (void *data),
			   void *data)
{
  GtkWidget *dialog, *label;
  auwd_closure *c = new auwd_closure;

  dialog = gtk_dialog_new_with_buttons (NULL, NULL, GTK_DIALOG_MODAL,
                                        _("ai_ni_bd_details"), 1,
                                        NULL);
  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  label = gtk_label_new (text);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label,
                      TRUE, TRUE, HILDON_MARGIN_DEFAULT);

  if (pi)
    pi->ref ();
  c->pi = pi;
  c->kind = kind;
  c->variant = variant;
  c->details = details;
  c->cont = cont;
  c->data = data;
  g_signal_connect (dialog, "response",
		    G_CALLBACK (annoy_user_with_details_response), c);
  gtk_widget_show_all (dialog);
}

void
annoy_user_with_details (const gchar *text,
			 package_info *pi, detail_kind kind,
			 void (*cont) (void *data),
			 void *data)
{
  annoy_user_with_details_1 (text,
			     NULL,
			     pi, kind, 1,
			     cont,
			     data);
}

void
annoy_user_with_arbitrary_details (const gchar *text,
				   void (*details) (void *data),
				   void (*cont) (void *data),
				   void *data)
{
  annoy_user_with_details_1 (text,
			     details,
			     NULL, no_details, 1,
			     cont,
			     data);
}

void
annoy_user_with_arbitrary_details_2 (const gchar *text,
				     void (*details) (void *data),
				     void (*cont) (void *data),
				     void *data)
{
  annoy_user_with_details_1 (text,
			     details,
			     NULL, no_details, 2,
			     cont,
			     data);
}

void
annoy_user_with_errno (int err, const gchar *detail,
		       void (*cont) (void *), void *data)
{
  add_log ("%s: %s\n", detail, strerror (err));

  const char *msg;
  if (err == ENAMETOOLONG)
    msg = _HCS ("file_ib_name_too_long");
  else if (err == EPERM || err == EACCES)
    msg = _FM ("sfil_ib_saving_not_allowed");
  else if (err == ENOENT)
    msg = _HCS ("sfil_ni_cannot_continue_target_folder_deleted");
  else if (err == ENOSPC)
    msg = _HCS ("sfil_ni_not_enough_memory");
  else
    msg = _("ai_ni_operation_failed");

  annoy_user (msg, cont, data);
}

void
annoy_user_with_gnome_vfs_result (GnomeVFSResult result, const gchar *detail,
				  void (*cont) (void *), void *data)
{
  add_log ("%s: %s\n", detail, gnome_vfs_result_to_string (result));

  if (result == GNOME_VFS_ERROR_NAME_TOO_LONG)
    {
      irritate_user (_HCS ("file_ib_name_too_long"));
      cont (data);
    }
  else if (result == GNOME_VFS_ERROR_ACCESS_DENIED
	   || result == GNOME_VFS_ERROR_NOT_PERMITTED)
    {
      irritate_user (_FM ("sfil_ib_saving_not_allowed"));
      cont (data);
    }
  else if (result == GNOME_VFS_ERROR_NOT_FOUND)
    annoy_user (_HCS ("sfil_ni_cannot_continue_target_folder_deleted"),
		cont, data);
  else if (result == GNOME_VFS_ERROR_NO_SPACE)
    annoy_user (_HCS ("sfil_ni_not_enough_memory"),
		cont, data);
  else if (result == GNOME_VFS_ERROR_READ_ONLY)
    annoy_user (_FM ("ckdg_fi_properties_read_only"),
                cont, data);
  else if (result == GNOME_VFS_ERROR_READ_ONLY_FILE_SYSTEM)
    annoy_user (_FM ("sfil_ib_readonly_location"),
                cont, data);
  else
    annoy_user (_("ai_ni_operation_failed"), cont, data);
}

void
irritate_user (const gchar *text)
{
  hildon_banner_show_information (GTK_WIDGET (get_main_window()),
				  NULL, text);
}

void
what_the_fock_p ()
{
  irritate_user (_("ai_ni_operation_failed"));
}

void
scare_user_with_legalese (bool sure,
			  void (*cont) (bool res, void *data),
			  void *data)
{
  ayn_closure *c = new ayn_closure;
  c->pi = NULL;
  c->cont = cont;
  c->data = data;

  GtkWidget *dialog;

  const char *text = (sure
		      ? _("ai_nc_non_verified_package")
		      : _("ai_nc_unsure_package"));

  dialog = hildon_note_new_confirmation (NULL, text);
  push_dialog (dialog);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (yes_no_response), c);
  gtk_widget_show_all (dialog);
}

static GtkWidget *
make_scare_user_with_legalese (bool multiple)
{
  GtkWidget *scroll;
  GtkWidget *label;

  label = make_small_label ((multiple) ?
                            _("ai_nc_non_verified_package_multiple") :
                            _("ai_nc_non_verified_package"));
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);

  scroll = hildon_pannable_area_new ();
  hildon_pannable_area_add_with_viewport (HILDON_PANNABLE_AREA (scroll), label);
  hildon_pannable_area_set_size_request_policy (HILDON_PANNABLE_AREA (scroll),
                                                HILDON_SIZE_REQUEST_CHILDREN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  return scroll;
}

static void
user_agreed (GtkToggleButton *button, GtkWidget *dialog)
{
  gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
                                     GTK_RESPONSE_OK,
                                     hildon_check_button_get_active
                                     (HILDON_CHECK_BUTTON (button)));
}

void
install_confirm (bool scare_user, package_info *pi, bool multiple,
                 void (*cont) (bool res, void *data),
                 void (*details) (void *data),
                 void *data)
{
  ayn_closure *c = new ayn_closure;
  c->pi = NULL;
  c->cont = cont;
  c->details = details;
  c->data = data;

  GtkWidget *dialog, *label;
  char *text = NULL;
  char size_buf[20];

  if (pi->info.download_size > 0)
    size_string_general (size_buf, 20, pi->info.download_size);
  else
    size_string_general (size_buf, 20, pi->info.install_user_size_delta);

  text = g_strdup_printf ((pi->installed_version
                           ? _("ai_nc_update") : _("ai_nc_install")),
                          pi->get_display_name (false),
                          pi->get_display_version (false), size_buf);

  label = gtk_label_new (text);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  g_free (text);

  dialog = gtk_dialog_new_with_buttons ((pi->installed_version
                                         ? _("ai_ti_confirm_update")
                                         : _("ai_ti_confirm_install")),
                                        NULL,
                                        GTK_DIALOG_MODAL,
                                        _("ai_bd_confirm_ok"), GTK_RESPONSE_OK,
                                        _("ai_bd_confirm_details"), 1,
                                        NULL);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label,
                      TRUE, TRUE, 1);

  if (scare_user)
    {
      GtkWidget *legalese = make_scare_user_with_legalese (multiple);

      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), legalese,
                          TRUE, TRUE, 1);

      gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
                                         GTK_RESPONSE_OK, FALSE);

      GtkWidget *check = hildon_check_button_new (HILDON_SIZE_FINGER_HEIGHT);
      gtk_button_set_label (GTK_BUTTON (check),
                            _("ai_ti_confirmation_checkbox"));
      gtk_box_pack_end (GTK_BOX (GTK_DIALOG (dialog)->vbox), check,
                          TRUE, TRUE, 1);
      g_signal_connect (check, "toggled", G_CALLBACK (user_agreed), dialog);
    }

  push_dialog (dialog);

  g_signal_connect (dialog, "response", G_CALLBACK (yes_no_response), c);

  gtk_widget_show_all (dialog);
}

/* Entertaining the user.
 */

static PangoFontDescription *
get_small_font (GtkWidget *widget)
{
  static PangoFontDescription *small_font = NULL;
  gint size = 0;

  if (small_font == NULL)
    {
      GtkStyle *fontstyle = NULL;

      fontstyle = gtk_rc_get_style_by_paths (gtk_widget_get_settings (GTK_WIDGET(widget)),
                                             "osso-SystemFont", NULL,
                                             G_TYPE_NONE);

      if (fontstyle) {
        small_font = pango_font_description_copy (fontstyle->font_desc);
      } else {
        small_font = pango_font_description_from_string ("Nokia Sans 16.75");
      }
      size = pango_font_description_get_size(small_font);
      size = gint (size * PANGO_SCALE_SMALL);
      pango_font_description_set_size(small_font, size);

    }

  return small_font;
}

static void
progressbar_dialog_realized (GtkWidget *widget, gpointer data)
{
  GdkWindow *win = widget->window;
  gdk_window_set_decorations (win, GDK_DECOR_BORDER);
}

struct entertainment_data {
  gint depth;

  GtkWidget *dialog, *bar, *cancel_button;
  gint pulse_id;

  char *main_title, *sub_title;
  gboolean strong_main_title;

  int n_games;
  entertainment_game *games;
  int current_game;
  double completed_fraction;

  int64_t already, total;

  void (*cancel_callback) (void *);
  void *cancel_data;
  bool was_cancelled;
  bool was_broke;
};

static entertainment_data entertainment;

static gboolean
entertainment_pulse (gpointer data)
{
  entertainment_data *ent = (entertainment_data *)data;

  if (ent->bar)
    gtk_progress_bar_pulse (GTK_PROGRESS_BAR (ent->bar));
  return TRUE;
}

static void
entertainment_start_pulsing ()
{
  if (entertainment.pulse_id == 0)
    entertainment.pulse_id =
      gtk_timeout_add (500, entertainment_pulse, &entertainment) + 1;
}

static void
entertainment_stop_pulsing ()
{
  if (entertainment.pulse_id != 0)
    {
      g_source_remove (entertainment.pulse_id - 1);
      entertainment.pulse_id = 0;
    }
}

static void
entertainment_update_progress ()
{
  if (entertainment.bar)
    {
      if (entertainment.already < 0)
	entertainment_start_pulsing ();
      else
	{
	  entertainment_game *game = 
	    &entertainment.games[entertainment.current_game];
	  double fraction =
	    (entertainment.completed_fraction 
	     + game->fraction * (((double)entertainment.already)
				 / entertainment.total));

	  entertainment_stop_pulsing ();
	  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (entertainment.bar),
					 fraction);
	}
    }
}

static void
entertainment_update_cancel ()
{
  if (entertainment.cancel_button)
    gtk_widget_set_sensitive (entertainment.cancel_button,
			      entertainment.cancel_callback != NULL);
}

static void
entertainment_update_title ()
{
  if (entertainment.dialog)
    {
      if (entertainment.sub_title && !entertainment.strong_main_title)
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (entertainment.bar),
                                   entertainment.sub_title);
      else
        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (entertainment.bar),
                                   entertainment.main_title);
    }
}

static void
entertainment_response (GtkWidget *widget, int response, void *data)
{
  if (response == GTK_RESPONSE_CANCEL)
    cancel_entertainment ();
}

static gboolean
entertainment_delete (GtkDialog *dialog, GdkEventAny *event, gpointer data)
{
  return TRUE;
}

void
entertainment_insensitive_press (GtkWidget *widget, gpointer unused)
{
  irritate_user (_("ai_ib_unable_cancel"));
}

void
start_entertaining_user_silently ()
{
  entertainment.depth++;
}

static entertainment_game default_entertainment_game = {
  -1, 1.0
};

void
start_entertaining_user (gboolean with_button)
{
  entertainment.depth++;

  if (entertainment.games == NULL)
    set_entertainment_games (1, &default_entertainment_game);

  if (entertainment.dialog == NULL)
    {
      GtkWidget *box;

      /* Build a custom dialog with two labels for main title and
	 subtitle (only in red-pill mode), a progress bar and a
	 'Cancel' button, and save references to all the needed
	 widgets in the entertainment_data struct */

      /* Create the dialog */
      entertainment.was_cancelled = false;
      entertainment.was_broke = false;
      entertainment.dialog = gtk_dialog_new ();
      gtk_window_set_modal (GTK_WINDOW (entertainment.dialog), TRUE);
      gtk_window_set_decorated (GTK_WINDOW (entertainment.dialog), FALSE);
      gtk_dialog_set_has_separator (GTK_DIALOG (entertainment.dialog), FALSE);
      gtk_window_set_position (GTK_WINDOW (entertainment.dialog),
                               GTK_WIN_POS_CENTER_ON_PARENT);
//       gtk_window_set_type_hint (GTK_WINDOW (entertainment.dialog),
//                                 GDK_WINDOW_TYPE_HINT_NOTIFICATION);

      /* Add the internal box */
      box = gtk_hbox_new (FALSE, HILDON_MARGIN_DOUBLE);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (entertainment.dialog)->vbox),
                          box, TRUE, TRUE, HILDON_MARGIN_DEFAULT);

      /* Add the progress bar */
      entertainment.bar = gtk_progress_bar_new ();
      gtk_progress_bar_set_text (GTK_PROGRESS_BAR (entertainment.bar),
                                 entertainment.main_title);
      g_object_set (G_OBJECT (entertainment.bar), "text-xalign", 0.5, NULL);
      gtk_box_pack_start (GTK_BOX (box), entertainment.bar, TRUE, TRUE,
                          HILDON_MARGIN_DOUBLE);

      gtk_dialog_set_default_response (GTK_DIALOG (entertainment.dialog),
                                       GTK_RESPONSE_CANCEL);

      entertainment.cancel_button = NULL;
      if (with_button)
	{
	  /* Cancel button: add to action area and set default response */
	  entertainment.cancel_button =
	    gtk_dialog_add_button (GTK_DIALOG (entertainment.dialog),
                                   dgettext ("hildon-libs", "wdgt_bd_stop"),
                                   GTK_RESPONSE_CANCEL);

	  g_signal_connect (entertainment.cancel_button,
			    "insensitive-press",
			    G_CALLBACK (entertainment_insensitive_press),
			    &entertainment);

          gtk_widget_show (entertainment.cancel_button);
          gtk_widget_set_no_show_all (entertainment.cancel_button, FALSE);
	}

      /* Connect signals */
      g_signal_connect (entertainment.dialog, "delete-event",
                        G_CALLBACK (entertainment_delete), NULL);

      g_signal_connect (entertainment.dialog, "realize",
		    G_CALLBACK (progressbar_dialog_realized), NULL);

      g_signal_connect (entertainment.dialog, "response",
			G_CALLBACK (entertainment_response), &entertainment);

      respond_on_escape (GTK_DIALOG (entertainment.dialog),
                         GTK_RESPONSE_CANCEL);

      /* Update info */
      entertainment_update_progress ();

      if (with_button)
	entertainment_update_cancel ();

      /* Show the dialog */
      push_dialog (entertainment.dialog);
      gtk_widget_show_all (entertainment.dialog);
    }
}

void
stop_entertaining_user ()
{
  entertainment.depth--;

  if (entertainment.depth == 0
      && entertainment.dialog != NULL)
    {
      entertainment_stop_pulsing ();

      pop_dialog (entertainment.dialog);
      gtk_widget_destroy (entertainment.dialog);

      entertainment.dialog = NULL;
      entertainment.bar = NULL;
      entertainment.cancel_button = NULL;

      entertainment.cancel_callback = NULL;
      entertainment.cancel_data = NULL;

      set_entertainment_games (1, &default_entertainment_game);
    }
}

void
cancel_entertainment ()
{
  entertainment.was_cancelled = true;
  if (entertainment.cancel_callback)
    entertainment.cancel_callback (entertainment.cancel_data);
}

void
break_entertainment ()
{
  entertainment.was_broke = true;
  cancel_entertainment ();
}

void
set_entertainment_main_title (const char *main_title, bool strong)
{
  /* Free memory if needed */
  if (entertainment.main_title)
    {
      g_free (entertainment.main_title);
      entertainment.main_title = NULL;
    }

  entertainment.strong_main_title = strong;
  entertainment.main_title = g_strdup (main_title);
  entertainment_update_title ();
}

void
set_entertainment_sub_title (const char *sub_title)
{
  /* Free memory if needed */
  if (entertainment.sub_title)
    {
      g_free (entertainment.sub_title);
      entertainment.sub_title = NULL;
    }

  entertainment.sub_title = g_strdup (sub_title);
  entertainment_update_title ();
}
void
set_entertainment_games (int n_games, entertainment_game *games)
{
  entertainment.n_games = n_games;
  entertainment.games = games;
  entertainment.current_game = 0;
  entertainment.completed_fraction = 0.0;
}

void
set_entertainment_fun (const char *sub_title,
		       int game, int64_t already, int64_t total)
{
  if (game != -1
      && entertainment.games
      && entertainment.games[entertainment.current_game].id != -1 
      && entertainment.games[entertainment.current_game].id != game)
    {
      int next_game;

      for (next_game = entertainment.current_game + 1;
	   next_game < entertainment.n_games; next_game++)
	{
	  if (entertainment.games[next_game].id == -1 
	      || entertainment.games[next_game].id == game)
	    break;
	}

      if (next_game < entertainment.n_games)
	{
	  entertainment.completed_fraction +=
	    entertainment.games[entertainment.current_game].fraction;
	  entertainment.current_game = next_game;
	}
    }

  if ((sub_title &&
       (entertainment.sub_title == NULL ||
	strcmp (entertainment.sub_title, sub_title))) ||
      (sub_title == NULL && entertainment.sub_title != NULL))
    {
      /* Set the subtitle */
      set_entertainment_sub_title (sub_title);
    }

  entertainment.already = already;
  entertainment.total = total;
  entertainment_update_progress ();
}

void
set_entertainment_download_fun (int game, int64_t already, int64_t total)
{
  static char *sub_title = NULL;
  static int64_t last_total = 0;

  if (total != last_total || sub_title == NULL)
    {
      char size_buf[20];
      size_string_detailed (size_buf, 20, total);
      g_free (sub_title);
      sub_title = g_strdup_printf (_("ai_nw_downloading"), size_buf);
    }

  set_entertainment_fun (sub_title, game, already, total);
}

void
set_entertainment_cancel (void (*callback) (void *data),
			  void *data)
{
  entertainment.cancel_callback = callback;
  entertainment.cancel_data = data;
  entertainment_update_cancel ();
}

void
set_entertainment_system_modal (void)
{
  if (entertainment.dialog != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (entertainment.dialog),
				    NULL);

      /* Force the transient hint to be applied */
      gtk_widget_hide (entertainment.dialog);
      gtk_widget_show (entertainment.dialog);
    }
}

bool
entertainment_was_cancelled ()
{
  return entertainment.was_cancelled;
}

bool
entertainment_was_broke ()
{
  return entertainment.was_broke;
}


/* Progress banners
 */

static GtkWidget *updating_banner = NULL;
static int updating_level = 0;
static const char *updating_label = NULL;
static bool allow_updating_banner = true;

static void
refresh_updating_banner ()
{
  bool show_it = (updating_level > 0 && allow_updating_banner);

  if (show_it && updating_banner == NULL)
    {
      updating_banner =
	hildon_banner_show_animation (GTK_WIDGET (get_main_window ()),
				      NULL,
				      updating_label);
      g_object_ref (updating_banner);
    }

  if (!show_it && updating_banner != NULL)
    {
      gtk_widget_destroy (updating_banner);
      g_object_unref (updating_banner);
      updating_banner = NULL;
    }
}

static gboolean
updating_timeout (gpointer unused)
{
  updating_level++;
  refresh_updating_banner ();
  return FALSE;
}

void
show_updating (const char *label)
{
  if (label == NULL)
    label = _HCS ("ckdg_pb_updating");

  updating_label = label;

  // We must never cancel this timeout since otherwise the
  // UPDATING_LEVEL will get out of sync.
  gtk_timeout_add (2000, updating_timeout, NULL);
}

void
hide_updating ()
{
  updating_level--;
  refresh_updating_banner ();
}

void
allow_updating ()
{
  allow_updating_banner = true;
  refresh_updating_banner ();
}

void
prevent_updating ()
{
  allow_updating_banner = false;
  refresh_updating_banner ();
}

static gboolean
no_button_events (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  g_signal_stop_emission_by_name (widget, "button-press-event");
  return FALSE;
}

GtkWidget *
make_small_text_view (const char *text)
{
  GtkWidget *scroll;
  GtkWidget *view;
  GtkTextBuffer *buffer;

  scroll = gtk_scrolled_window_new (NULL, NULL);
  view = gtk_text_view_new ();
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  if (text)
    gtk_text_buffer_set_text (buffer, text, -1);
  gtk_text_view_set_editable (GTK_TEXT_VIEW (view), 0);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), 0);
  g_signal_connect (view, "button-press-event",
		    G_CALLBACK (no_button_events), NULL);
  gtk_container_add (GTK_CONTAINER (scroll), view);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_widget_modify_font (view, get_small_font (view));

  return scroll;
}

void
set_small_text_view_text (GtkWidget *scroll, const char *text)
{
  GtkWidget *view = gtk_bin_get_child (GTK_BIN (scroll));
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
  gtk_text_buffer_set_text (buffer, text, -1);
}

GtkWidget *
make_small_label (const char *text)
{
  GtkWidget *label = gtk_label_new (text);
  gtk_widget_modify_font (label, get_small_font (label));
  return label;
}

static GtkListStore *global_list_store = NULL;
static bool global_installed;

static bool global_icons_initialized = false;
static GdkPixbuf *default_icon = NULL;
static GdkPixbuf *broken_icon = NULL;

static void
emit_row_changed (GtkTreeModel *model, GtkTreeIter *iter)
{
  GtkTreePath *path;

  path = gtk_tree_model_get_path (model, iter);
  g_signal_emit_by_name (model, "row-changed", path, iter);

  gtk_tree_path_free (path);
}

static void
size_string_general_or_empty (char *buf, size_t n, int64_t bytes)
{
  if (bytes == 0)
    buf[0] = '\0';
  else
    size_string_general (buf, n, bytes);
}

static void
global_size_func (GtkTreeViewColumn *column,
		  GtkCellRenderer *cell,
		  GtkTreeModel *model,
		  GtkTreeIter *iter,
		  gpointer data)
{
  package_info *pi;
  char buf[20];

  gtk_tree_model_get (model, iter, 0, &pi, -1);
  if (!pi)
    return;

  if (global_installed)
    size_string_general_or_empty (buf, 20, pi->installed_size);
  else if (pi->have_info)
    size_string_general_or_empty (buf, 20, pi->info.download_size);
  else
    strcpy (buf, "-");
  g_object_set (cell, "text", buf, NULL);
}

static void
package_info_func (GtkTreeViewColumn *column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer data)
{
  GtkTreeView *tree = (GtkTreeView *)data;
  GtkTreeSelection *selection = gtk_tree_view_get_selection (tree);
  package_info *pi;
  const gchar *package_name = NULL;
  const gchar *package_version = NULL;
  const gchar *package_description = NULL;

  gtk_tree_model_get (model, iter, 0, &pi, -1);
  if (!pi)
    return;

  package_name = pi->get_display_name (global_installed);
  package_version = pi->get_display_version (global_installed);

  if (gtk_tree_selection_iter_is_selected (selection, iter))
    {
      if (global_installed)
        package_description = pi->installed_short_description;
      else
        {
          package_description = pi->available_short_description;
          if (package_description == NULL)
            package_description = pi->installed_short_description;
        }
    }

  if (!global_icons_initialized)
    {
      GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();

      default_icon = gtk_icon_theme_load_icon (icon_theme,
                                               "general_application_manager",
                                               26,
                                               GtkIconLookupFlags (0),
                                               NULL);

      broken_icon = gtk_icon_theme_load_icon (icon_theme,
                                              "app_install_broken_application",
                                              26,
                                              GtkIconLookupFlags (0),
                                              NULL);

      global_icons_initialized = true;
    }

  GdkPixbuf *icon;
  if (pi->broken)
    icon = broken_icon;
  else
    {
      if (global_installed)
        icon = pi->installed_icon;
      else
	icon = pi->available_icon;
    }

  g_object_set (cell,
		"package-name", package_name,
                "package-version", package_version,
                "package-description", package_description,
                "pixbuf", icon ? icon : default_icon,
		NULL);
}

static bool global_have_last_selection;
static GtkTreeIter global_last_selection;
static GtkTreePath *global_target_path = NULL;
static package_info_callback *global_selection_callback;
static package_info *current_package_info = NULL;

void
reset_global_target_path ()
{
  if (global_target_path)
    gtk_tree_path_free (global_target_path);
  global_target_path = NULL;
}

static void
global_selection_changed (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  package_info *pi = NULL;

  if (global_have_last_selection)
    emit_row_changed (GTK_TREE_MODEL (global_list_store),
		      &global_last_selection);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      assert (model == GTK_TREE_MODEL (global_list_store));

      emit_row_changed (model, &iter);
      global_last_selection = iter;
      global_have_last_selection = true;

      reset_global_target_path ();
      global_target_path = gtk_tree_model_get_path (model, &iter);

      if (global_selection_callback)
        gtk_tree_model_get (model, &iter, 0, &pi, -1);
    }

  /* Update the global variable */
  current_package_info = pi;

  /* Use the callback, if present */
  if (global_selection_callback)
    global_selection_callback (pi);
}

static package_info_callback *global_activation_callback;

static void
global_row_activated (GtkTreeView *treeview,
		      GtkTreePath *path,
		      GtkTreeViewColumn *column,
		      gpointer data)
{
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeIter iter;

  assert (model == GTK_TREE_MODEL (global_list_store));

  if (global_activation_callback &&
      gtk_tree_model_get_iter (model, &iter, path))
    {
      package_info *pi;
      gtk_tree_model_get (model, &iter, 0, &pi, -1);
      if (pi)
	global_activation_callback (pi);
    }
}

static void set_global_package_list (GList *packages,
				     bool installed,
				     package_info_callback *selected,
				     package_info_callback *activated);

static GList *global_packages = NULL;

static gboolean
global_package_list_key_pressed (GtkWidget * widget,
				 GdkEventKey * event)
{
  GtkTreePath *cursor_path = NULL;
  GtkTreeIter iter;
  bool result = FALSE;

  switch (event->keyval)
    {
    case HILDON_HARDKEY_LEFT:
      return TRUE;
      break;
    case HILDON_HARDKEY_RIGHT:
      return TRUE;
      break;
    case HILDON_HARDKEY_UP:
      // we set the focus to the last button of the main_trail
//       gtk_tree_view_get_cursor (GTK_TREE_VIEW (widget), &cursor_path, NULL);

//       if (cursor_path)
//         {
//           if (!gtk_tree_path_prev (cursor_path))
//             {
//               GList *children = NULL;

//               children =
//                 gtk_container_get_children (GTK_CONTAINER (get_main_trail()));

//               if (children)
//                 {
//                   GList *last_child = g_list_last (children);

//                   while (last_child &&
//                          ((!GTK_WIDGET_CAN_FOCUS (last_child->data)) ||
//                          (!GTK_WIDGET_IS_SENSITIVE (last_child->data))))
//                     last_child = g_list_previous (last_child);

//                   if (last_child)
//                     gtk_widget_grab_focus (GTK_WIDGET (last_child->data));

//                   g_list_free (children);
// 		  result = TRUE;
//                 }
//             }

//           gtk_tree_path_free(cursor_path);
//         }

      break;
    case HILDON_HARDKEY_DOWN:
      /* Avoid to jump to the trail when pressing down while in the last item */
      gtk_tree_view_get_cursor (GTK_TREE_VIEW (widget), &cursor_path, NULL);
      if (cursor_path)
	{
	  gtk_tree_model_get_iter (GTK_TREE_MODEL (global_list_store),
				   &iter,
				   cursor_path);

	  result = !gtk_tree_model_iter_next (GTK_TREE_MODEL (global_list_store),
					      &iter);

	  gtk_tree_path_free(cursor_path);
	}
    }

  return result;
}

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
static void
tap_and_hold_cb (GtkWidget *treeview, gpointer data)
{
  g_return_if_fail (treeview != NULL && GTK_IS_TREE_VIEW (treeview));
  g_return_if_fail (data != NULL && GTK_IS_MENU (data));

  GtkWidget *menu = GTK_WIDGET (data);
  package_info *pi = current_package_info;

  if (pi != NULL)
    {
      /* Set sensitiveness for the first item of the CSM: the operation item */
      GList *items = gtk_container_get_children (GTK_CONTAINER (menu));
      if (items != NULL)
        {
          if (items->data != NULL && GTK_IS_WIDGET (items->data))
            {
              /* Sensitiveness depending on the system_update flag */
              gtk_widget_set_sensitive (GTK_WIDGET (items->data),
                                        !(pi->flags & pkgflag_system_update));
            }
          g_list_free (items);
        }
    }
}
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */

GtkWidget *
make_global_package_list (GList *packages,
			  bool installed,
			  const char *empty_label,
			  const char *op_label,
			  package_info_callback *selected,
			  package_info_callback *activated)
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *tree, *scroller;
#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
  GtkWidget *menu = NULL;
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */

  if (global_list_store == NULL)
    {
      global_list_store = gtk_list_store_new (1, G_TYPE_POINTER);
      g_object_ref (global_list_store);
    }

  if (packages == NULL)
    {
      GtkWidget *label = gtk_label_new (empty_label);
      gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.0);
      return label;
    }

  tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (global_list_store));

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree), TRUE);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("ai_li_version"));
  gtk_tree_view_column_set_alignment (column, 1.0);

  renderer = package_info_cell_renderer_new ();
  g_object_set (renderer,
                "xpad", 0,
                "xalign", 0.0,
                "ypad", 0,
                "yalign", 0.0,
                "visible", TRUE, NULL);
  gtk_tree_view_column_pack_end (column, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (column, renderer,
                                           package_info_func, tree, NULL);

  /* Set odd/even rows in different colors */
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW (tree), TRUE);

  gtk_tree_view_insert_column (GTK_TREE_VIEW (tree), column, -1);

  // Setting the sizing of this columne to FIXED but not specifying
  // the width is a workaround for some bug in GtkTreeView.  If we
  // don't do this, the name will not get ellipsized and the size
  // and/or version columns might disappear completely.  With this
  // workaround, the name gets properly elipsized.
  //
  gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_expand (column, TRUE);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_alignment (column, 1.0);
  gtk_tree_view_column_set_expand (column, FALSE);
  g_object_set (column, "spacing", 14, NULL);
  gtk_tree_view_column_set_title (column, _("ai_li_size"));

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "yalign", 0.0, NULL);
  g_object_set (renderer, "xalign", 1.0, NULL);
  gtk_tree_view_column_pack_end (column, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (column, renderer,
                                           global_size_func, tree,
                                           NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_end (column, renderer, FALSE);

  gtk_tree_view_insert_column (GTK_TREE_VIEW (tree), column, -1);

  scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
				  GTK_POLICY_NEVER,
				  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scroller), tree);

  global_have_last_selection = false;
  g_signal_connect
    (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree))),
     "changed",
     G_CALLBACK (global_selection_changed), NULL);

  g_signal_connect (tree, "row-activated",
		    G_CALLBACK (global_row_activated), NULL);

  g_signal_connect (tree, "key-press-event",
                    G_CALLBACK (global_package_list_key_pressed), NULL);

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
  /* Create the contextual menu */
  if (installed)
    {
      menu = create_package_menu (op_label);

      /* Connect the tap_and_hold signal to change the sensitiveness of
         the first CSM item when it's not uninstallable */
      g_signal_connect (tree, "tap_and_hold",
                        G_CALLBACK (tap_and_hold_cb), menu);
    }
  else
    {
      /* If not in uninstall view, don't set an insensitive text */
      menu = create_package_menu (op_label);
    }

  gtk_widget_show_all (menu);

  gtk_widget_tap_and_hold_setup (tree, menu, NULL,
				 GtkWidgetTapAndHoldFlags (0));
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */

  set_global_package_list (packages, installed, selected, activated);

  grab_focus_on_map (tree);

  if (global_target_path)
    {
      GtkTreeIter iter;
      GtkTreePath *target_path = gtk_tree_path_copy (global_target_path);

      if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (global_list_store),
				    &iter, global_target_path))
	gtk_tree_path_prev (global_target_path);

      gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree),
				target_path,
				NULL, FALSE);
      gtk_tree_view_get_cursor (GTK_TREE_VIEW (tree),
				&global_target_path,
				NULL);
      gtk_tree_path_free (target_path);

      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tree),
				    global_target_path,
				    NULL, FALSE, 0, 0);
    }

  return scroller;
}

static void
set_global_package_list (GList *packages,
			 bool installed,
			 package_info_callback *selected,
			 package_info_callback *activated)
{
  global_have_last_selection = false;

  for (GList *p = global_packages; p; p = p->next)
    {
      package_info *pi = (package_info *)p->data;
      pi->model = NULL;
    }
  if (global_list_store)
    gtk_list_store_clear (global_list_store);

  global_installed = installed;
  global_selection_callback = selected;
  global_activation_callback = activated;
  global_packages = packages;

  int pos = 0;
  for (GList *p = global_packages; p; p = p->next)
    {
      package_info *pi = (package_info *)p->data;

      pi->model = GTK_TREE_MODEL (global_list_store);
      gtk_list_store_insert_with_values (global_list_store, &pi->iter,
					 pos,
					 0, pi,
					 -1);
      pos++;
    }
}

void
clear_global_package_list ()
{
  set_global_package_list (NULL, false, NULL, NULL);
}

void
global_package_info_changed (package_info *pi)
{
  if (pi->model)
    emit_row_changed (pi->model, &pi->iter);
}

static GtkWidget *global_section_list = NULL;
static section_activated *global_section_activated;

static void
section_clicked (GtkWidget *widget, gpointer data)
{
  if (!GTK_IS_WIDGET (widget) || (data == NULL))
    return;

  section_info *si = (section_info *)data;
  if (global_section_activated)
    global_section_activated (si);
}

static gboolean
scroll_to_widget (GtkWidget *w, GdkEvent *, gpointer data)
{
  GtkWidget *scroller = (GtkWidget *)data;
  GtkAdjustment *adj =
    gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroller));

  // XXX - this assumes that the adjustement unit is 'pixels'.

  gtk_adjustment_clamp_page (adj,
			     w->allocation.y,
			     w->allocation.y + w->allocation.height);

  return FALSE;
}

static void
unref_section_info (gpointer data, GClosure *closure)
{
  if (data == NULL)
    return;
  section_info *si = (section_info *)data;
  si->unref();
}

#define GRID_COLUMNS 3

GtkWidget *
make_global_section_list (GList *sections, section_activated *act)
{
  global_section_activated = act;

  if (sections == NULL)
    {
      GtkWidget *label = gtk_label_new (_("ai_li_no_applications_available"));
      gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.0);
      return label;
    }

  GtkWidget *table = gtk_table_new (1, GRID_COLUMNS, TRUE);
  GtkWidget *scroller;

  bool first_button = true;

  scroller = gtk_scrolled_window_new (NULL, NULL);

  int row = 0;
  int col = 0;
  for (GList *s = sections; s; s = s ->next)
    {
      section_info *si = (section_info *)s->data;
      GtkWidget *label = gtk_label_new (si->name);
      gtk_misc_set_padding (GTK_MISC (label), 0, 14);
      gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
      GtkWidget *btn = gtk_button_new ();
      gtk_container_add (GTK_CONTAINER (btn), label);
      gtk_table_attach_defaults (GTK_TABLE (table), btn,
				 col, col+1,
				 row, row+1);
      col += 1;
      if (col >= GRID_COLUMNS)
	{
	  col = 0;
	  row++;
	}
      
      si->ref(); 
      g_signal_connect_data (btn, "clicked",
                             G_CALLBACK (section_clicked), si,
                             unref_section_info, G_CONNECT_AFTER);
      
      if (first_button)
	grab_focus_on_map (btn);
      first_button = false;

      g_signal_connect (btn, "focus-in-event",
			G_CALLBACK (scroll_to_widget), scroller);
    }

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
				  GTK_POLICY_NEVER,
				  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scroller),
					 table);

  global_section_list = scroller;
  g_object_ref (scroller);

  return scroller;
}

void
clear_global_section_list ()
{
  if (global_section_list)
    g_object_unref (global_section_list);

  global_section_list = NULL;
}

enum {
  COLUMN_SP_INSTALLABLE,
  COLUMN_SP_SELECTED,
  COLUMN_SP_NAME,
  COLUMN_SP_SIZE,
  COLUMN_SP_PACKAGE_INFO,
  SP_N_COLUMNS
};

static GtkListStore *
make_select_package_list_store (GList *package_list, int64_t *total_size)
{
  GtkListStore *list_store = NULL;
  GList * node = NULL;
  int64_t acc_size = 0;

  list_store = gtk_list_store_new (SP_N_COLUMNS,
				   G_TYPE_BOOLEAN,
                                   G_TYPE_BOOLEAN,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_POINTER);

  for (node = package_list; node != NULL; node = g_list_next (node))
    {
      package_info *pi = (package_info *) node->data;
      GtkTreeIter iter;
      gboolean installable = (pi->available_version != NULL) && (strlen(pi->available_version) > 0); 
      char package_size_str[20] = "";
      if (installable)
        size_string_general (package_size_str, 20,
                             pi->info.install_user_size_delta);
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          COLUMN_SP_INSTALLABLE, installable,
			  COLUMN_SP_SELECTED, installable,
			  COLUMN_SP_NAME, pi->get_display_name (false),
			  COLUMN_SP_SIZE, package_size_str,
			  COLUMN_SP_PACKAGE_INFO, pi,
			  -1);
      acc_size += pi->info.install_user_size_delta;
      pi->ref ();
    }

  if (total_size != NULL)
    *total_size = acc_size;

  return list_store;
}

void fill_required_space_label (GtkWidget *label,
				int64_t size)
{
  gchar * text = NULL;
  gchar size_str[20];

  size_string_general (size_str, 20, size);

  text = g_strdup_printf (_("ai_ia_storage"), size_str);
  gtk_label_set_text (GTK_LABEL(label), text);
  g_free (text);
}

struct upls_closure {
  GtkWidget *button;
  GtkWidget *required_space_label;
};

void update_packages_list_selection (GtkTreeModel *model,
				     GtkTreePath *path, GtkTreeIter *i,
				     gpointer data)
{
  gboolean has_iter = FALSE;
  gboolean selected_packages = FALSE;
  int64_t acc_size = 0;
  GtkWidget *button;
  GtkWidget *label = NULL;
  GtkTreeIter iter;
  upls_closure *closure = (upls_closure *)data;

  has_iter = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(model), &iter);

  button = closure->button;
  label = closure->required_space_label;

  while (has_iter)
    {
      gboolean selected = FALSE;
      package_info *pi = NULL;

      gtk_tree_model_get (GTK_TREE_MODEL(model), &iter,
			  COLUMN_SP_SELECTED, &selected,
			  COLUMN_SP_PACKAGE_INFO, &pi,
			  -1);
      if (selected)
	{
	  selected_packages = TRUE;
	  acc_size += pi->info.install_user_size_delta;
	}
      has_iter = gtk_tree_model_iter_next (GTK_TREE_MODEL(model), &iter);
    }

  /* Set sensitiveness to the OK button and update required size label */
  gtk_widget_set_sensitive (button, selected_packages);
  fill_required_space_label (label, acc_size);
}

struct spl_closure
{
  GList *package_list;
  char *title;
  char *question;

  GtkListStore *list_store;
  void (*cont) (gboolean res, GList *package_list, void *data);
  void *data;
};

void select_package_list_response (GtkDialog *dialog,
				   gint response,
				   gpointer user_data)
{
  spl_closure *closure = (spl_closure *)user_data;
  gboolean res = FALSE;
  GList *package_list = NULL;
  GtkTreeIter iter;
  gboolean has_iter = FALSE;

  res = (response == GTK_RESPONSE_OK);

  has_iter = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(closure->list_store), &iter);
  while (has_iter)
    {
      gboolean selected = FALSE;
      package_info *pi = NULL;

      gtk_tree_model_get (GTK_TREE_MODEL(closure->list_store), &iter,
			  COLUMN_SP_SELECTED, &selected,
			  COLUMN_SP_PACKAGE_INFO, &pi,
			  -1);
      if (selected)
	package_list = g_list_prepend (package_list, pi);
      else
	pi->unref ();
      has_iter = gtk_tree_model_iter_next (GTK_TREE_MODEL(closure->list_store), &iter);
    }
  package_list = g_list_reverse (package_list);
  g_object_unref(closure->list_store);

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (closure->cont)
    {
      closure->cont (res, package_list, closure->data);
    }
  else
    {
      GList *node = NULL;
      for (node = package_list; node != NULL; node = g_list_next (node))
	{
	  if (node->data != NULL)
	    {
	      ((package_info *) (node->data))->unref ();
	    }
	}
      g_list_free (package_list);
    }

  delete closure;
}

void
package_selected_toggled_callback (GtkCellRendererToggle *cell,
				   char *path_string,
				   gpointer user_data)
{
  GtkTreePath *path;
  GtkTreeIter iter;
  gboolean selected;
  GtkTreeView *tree_view;

  tree_view = GTK_TREE_VIEW (user_data);

  path = gtk_tree_path_new_from_string (path_string);
  gtk_tree_model_get_iter (gtk_tree_view_get_model (tree_view),
			   &iter, path);
  gtk_tree_model_get (gtk_tree_view_get_model (tree_view),
		      &iter, COLUMN_SP_SELECTED, &selected, -1);
  gtk_list_store_set (GTK_LIST_STORE(gtk_tree_view_get_model (tree_view)),
		      &iter, COLUMN_SP_SELECTED, !selected, -1);
  gtk_tree_path_free (path);
}

static void
select_package_list_with_info (void *data)
{
  spl_closure *c = (spl_closure *)data;

  GtkWidget *dialog;
  GtkWidget *ok_button;
  GtkListStore *list_store;
  GtkWidget *tree_view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkWidget *scroller;
  GtkWidget *message_label;
  GtkWidget *required_space_label;
  int64_t total_size;
  gint result;
  gint padding = 4;

  /* Dialog creation: we can't use gtk_dialog_new_with_buttons ()
     because we need to get access to the OK GtkButton in order to
     connect to the 'insensitive_press' signal emmited when clicking
     on it while it's insensitve
  */
  dialog = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (dialog), c->title);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                     dgettext ("hildon-libs", "wdgt_bd_yes"),
                                     GTK_RESPONSE_OK);
  gtk_dialog_add_button (GTK_DIALOG (dialog),
                         dgettext ("hildon-libs", "wdgt_bd_no"),
                         GTK_RESPONSE_CANCEL);

  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  list_store = make_select_package_list_store (c->package_list, &total_size);

  /* Set the message dialog */
  message_label = gtk_label_new (c->question);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), message_label,
		      FALSE, FALSE, padding);

  /* Add required space label */
  required_space_label = gtk_label_new (NULL);
  gtk_box_pack_end (GTK_BOX(GTK_DIALOG(dialog)->vbox), required_space_label,
		    FALSE, FALSE, padding);
  fill_required_space_label (required_space_label, total_size);

  /* Set up treeview */
  tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (list_store));
  renderer = gtk_cell_renderer_toggle_new ();
  gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE (renderer), FALSE);
  g_signal_connect (G_OBJECT (renderer), "toggled",
		    G_CALLBACK (package_selected_toggled_callback), tree_view);
  column = gtk_tree_view_column_new_with_attributes ("Marked", renderer,
						     "active", COLUMN_SP_SELECTED,
						     "activatable", COLUMN_SP_INSTALLABLE,
						     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
			       column);
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Name", renderer,
						     "text", COLUMN_SP_NAME,
                                                     "sensitive", COLUMN_SP_INSTALLABLE,
						     NULL);
  gtk_tree_view_column_set_expand(column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
			       column);
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Size", renderer,
						     "text", COLUMN_SP_SIZE,
                                                     "sensitive", COLUMN_SP_INSTALLABLE,
						     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
			       column);

  /* Set up an scrolled window for the treeview */
  scroller = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
				  GTK_POLICY_NEVER,
				  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scroller), tree_view);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), scroller,
		      TRUE, TRUE, padding);

  /* Add separator */
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                      gtk_hseparator_new (),
                      FALSE, FALSE, padding);

  c->list_store = list_store;

  /* Prepare data to be passed to the right signal handler */
  upls_closure *upls_data = new upls_closure;
  upls_data->button = ok_button;
  upls_data->required_space_label = required_space_label;

  update_packages_list_selection(GTK_TREE_MODEL (c->list_store),
                                 NULL, NULL, upls_data);

  /* Connect signals */
  g_signal_connect (list_store, "row-changed",
		    G_CALLBACK (update_packages_list_selection),
		    upls_data);

  gtk_widget_set_usize (dialog, 600, 320);
  gtk_widget_show_all (dialog);

  /* Run dialog, waiting for a response */
  result = gtk_dialog_run (GTK_DIALOG (dialog));
  select_package_list_response (GTK_DIALOG (dialog), result, c);

  delete upls_data;
}

void
select_package_list (GList *package_list,
                     const gchar *title,
                     const gchar *question,
                     void (*cont) (gboolean res, GList *pl, void *data),
                     void *data)
{
  spl_closure *closure = new spl_closure;

  closure->package_list = package_list;
  closure->title = g_strdup (title);
  closure->question = g_strdup (question);
  closure->cont = cont;
  closure->data = data;

  get_package_infos (package_list,
		     true,
		     select_package_list_with_info,
		     closure);
}

#define KILO 1024.0
#define MEGA KILO * KILO
#define GIGA KILO * MEGA

void
size_string_general (char *buf, size_t n, int64_t bytes)
{
  double num = (double) bytes;

  if (num == 0)
    snprintf (buf, n, _FM ("sfil_li_size_kb"), 0);
  else if (num < 1 * KILO)
    snprintf (buf, n, _FM ("sfil_li_size_kb"), 1);
  else
    {
      // round to nearest KILO
      // bytes ~ num * KILO
      num = (bytes + KILO / 2.0) / KILO;
      if (num < 100)
	snprintf (buf, n, _FM ("sfil_li_size_1kb_99kb"), (int) num);
      else
	{
	  // round to nearest 100 KILO
	  // bytes ~ num * 100 * KILO
	  num = (bytes + 50.0 * KILO) / (100.0 * KILO);
	  if (num < 10)
	    snprintf (buf, n, _FM ("sfil_li_size_100kb_1mb"),
                      (float) num / 10.0);
	  else
	    {
              // round to nearest MEGA
              // bytes ~ num * MEGA
              num = (bytes + MEGA / 2) / (MEGA);
              if (num < 10)
                snprintf (buf, n, _FM ("sfil_li_size_1mb_10mb"), (float) num);
              else if (num < 1000)
                snprintf (buf, n, _FM ("sfil_li_size_10mb_1gb"), (int) num);
              else
                snprintf (buf, n, _FM ("sfil_li_size_1gb_or_greater"),
                          (float) 1.0 * bytes / GIGA);
	    }
	}
    }
}

void
size_string_detailed (char *buf, size_t n, int64_t bytes)
{
  double num = (double) bytes;

  if (num == 0)
    snprintf (buf, n, _FM ("ckdg_va_properties_size_kb"), 0);
  else if (num < 1 * KILO)
    snprintf (buf, n, _FM ("ckdg_va_properties_size_kb"), 1);
  else
    {
      // round to nearest KILO
      // bytes ~ num * KILO
      num = (bytes + KILO / 2.0) / KILO;
      if (num < 100)
	snprintf (buf, n, _FM ("ckdg_va_properties_size_1kb_99kb"), (int) num);
      else
	{
	  // round to nearest 100 KILO
	  // bytes ~ num * 100 * KILO
	  num = (bytes + 50.0 * KILO) / (100.0 * KILO);
	  if (num < 10)
	    snprintf (buf, n, _FM ("ckdg_va_properties_size_100kb_1mb"),
                      (int) num / 10.0);
	  else
	    {
              // round to nearest MEGA
              // bytes ~ num * MEGA
              num = (bytes + MEGA / 2) / (MEGA);
              if (num < 10)
                snprintf (buf, n, _FM ("ckdg_va_properties_size_1mb_10mb"),
                          (float) num);
              else if (num < 1000)
                snprintf (buf, n, _FM ("ckdg_va_properties_size_10mb_1gb"),
                          (float) num);
              else
                snprintf (buf, n,
                          _FM ("ckdg_va_properties_size_1gb_or_greater"),
                          (float) 1.0 * bytes / GIGA);
	    }
	}
    }
}

struct fcd_closure {
  void (*cont) (char *uri, void *data);
  void *data;
};

static void
fcd_response (GtkDialog *dialog, gint response, gpointer clos)
{
  fcd_closure *c = (fcd_closure *)clos;
  void (*cont) (char *uri, void *data) = c->cont;
  void *data = c->data;
  delete c;

  char *uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog));

  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (response == GTK_RESPONSE_OK)
    {
      if (cont)
	cont (uri, data);
    }
  else
    {
      g_free (uri);
      if (cont)
	cont (NULL, data);
    }
}

void
show_deb_file_chooser (void (*cont) (char *uri, void *data),
		       void *data)
{
  fcd_closure *c = new fcd_closure;
  c->cont = cont;
  c->data = data;

  GtkWidget *fcd;
  GtkFileFilter *filter;

  fcd = hildon_file_chooser_dialog_new_with_properties
    (NULL,
     "action",            GTK_FILE_CHOOSER_ACTION_OPEN,
     "title",             _("ai_ti_select_package"),
     "empty_text",        _("ai_ia_select_package_no_packages"),
     "open_button_text",  _("ai_bd_select_package"),
     NULL);
  gtk_window_set_modal (GTK_WINDOW (fcd), TRUE);
  push_dialog (fcd);

  filter = gtk_file_filter_new ();
  gtk_file_filter_add_mime_type (filter, "application/x-deb");
  gtk_file_filter_add_mime_type (filter, "application/x-debian-package");
  gtk_file_filter_add_mime_type (filter, "application/x-install-instructions");
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER(fcd), filter);
  // XXX - gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER(fcd), TRUE);

  g_signal_connect (fcd, "response",
		    G_CALLBACK (fcd_response), c);

  gtk_widget_show_all (fcd);
}

void
show_file_chooser_for_save (const char *title,
			    GtkWindow *parent,
			    const char *default_folder,
			    const char *default_filename,
			    void (*cont) (char *uri, void *data),
			    void *data)
{
  fcd_closure *c = new fcd_closure;
  c->cont = cont;
  c->data = data;

  GtkWidget *fcd;

  fcd = hildon_file_chooser_dialog_new_with_properties
    (NULL,
     "action",            GTK_FILE_CHOOSER_ACTION_SAVE,
     "title",             title,
     NULL);
  push_dialog (fcd);
  gtk_window_set_modal (GTK_WINDOW (fcd), TRUE);

  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (fcd), default_filename);

  if (default_folder)
    gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (fcd), default_folder);

  g_signal_connect (fcd, "response",
		    G_CALLBACK (fcd_response), c);

  gtk_widget_show_all (fcd);
}

static void
b64decode (const unsigned char *str, GdkPixbufLoader *loader)
{
  unsigned const char *cur, *start;
  int d, dlast, phase;
  unsigned char c;
  static int table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 00-0F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 10-1F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  /* 20-2F */
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  /* 30-3F */
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  /* 40-4F */
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  /* 50-5F */
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  /* 60-6F */
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  /* 70-7F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 80-8F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* 90-9F */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* A0-AF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* B0-BF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* C0-CF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* D0-DF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  /* E0-EF */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   /* F0-FF */
  };

  const size_t loader_size = 2048;
  unsigned char loader_buf[loader_size], *loader_ptr;
  GError *error = NULL;

  d = dlast = phase = 0;
  start = str;
  loader_ptr = loader_buf;

  for (cur = str; *cur != '\0'; ++cur )
    {
      d = table[(int)*cur];
      if(d != -1)
        {
	  switch(phase)
            {
            case 0:
	      ++phase;
	      break;
            case 1:
	      c = ((dlast << 2) | ((d & 0x30) >> 4));
	      *loader_ptr++ = c;
	      ++phase;
	      break;
            case 2:
	      c = (((dlast & 0xf) << 4) | ((d & 0x3c) >> 2));
	      *loader_ptr++ = c;
	      ++phase;
	      break;
            case 3:
	      c = (((dlast & 0x03 ) << 6) | d);
	      *loader_ptr++ = c;
	      phase = 0;
	      break;
            }
	  dlast = d;
	  if (loader_ptr == loader_buf + loader_size)
	    {
	      gdk_pixbuf_loader_write (loader, loader_buf, loader_size,
				       &error);
	      if (error)
		{
		  fprintf (stderr, "PX: %s\n", error->message);
		  g_error_free (error);
		  return;
		}

	      loader_ptr = loader_buf;
	    }
        }
    }

  gdk_pixbuf_loader_write (loader, loader_buf, loader_ptr - loader_buf,
			   &error);
  if (error)
    {
      fprintf (stderr, "PX: %s\n", error->message);
      g_error_free (error);
      return;
    }
}

GdkPixbuf *
pixbuf_from_base64 (const char *base64)
{
  if (base64 == NULL)
    return NULL;

  GError *error = NULL;

  GdkPixbufLoader *loader = gdk_pixbuf_loader_new ();
  b64decode ((const unsigned char *)base64, loader);
  gdk_pixbuf_loader_close (loader, &error);
  if (error)
    {
      fprintf (stderr, "PX: %s\n", error->message);
      g_error_free (error);
      g_object_unref (loader);
      return NULL;
    }

  GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
  if (pixbuf)
    g_object_ref (pixbuf);
  g_object_unref (loader);
  return pixbuf;
}

/* XXX - there seems to be no good way to really stop copy_progress
         from being called; I just can not tame gnome_vfs_async_xfer,
         at least not in its ovu_async_xfer costume.  Thus, I simple
         punt the issue and use global state.
*/

GnomeVFSResult copy_result;
static void (*copy_cont) (char *local, void *data);
static void *copy_cont_data;
static char *copy_local;
static char *copy_target;
static char *copy_tempdir;
static GnomeVFSHandle *copy_vfs_handle;

static void
fail_copy_cont (void *data)
{
  copy_cont (NULL, copy_cont_data);
  copy_cont = NULL;
}

static void
call_copy_cont (GnomeVFSResult result)
{
  if (copy_local)
    stop_entertaining_user ();

  if (result == GNOME_VFS_OK)
    {
      copy_cont (copy_target, copy_cont_data);
      copy_cont = NULL;
    }
  else
    {
      cleanup_temp_file ();
      g_free (copy_target);

      if (result == GNOME_VFS_ERROR_IO)
	{
	  annoy_user (_HCS ("sfil_ni_cannot_open_no_connection"),
		      fail_copy_cont, NULL);
	}
      else if (result == GNOME_VFS_ERROR_CANCELLED)
	{
	  copy_cont (NULL, copy_cont_data);
	  copy_cont = NULL;
	}
      else if (result != GNOME_VFS_OK)
	{
	  annoy_user (_("ai_ni_operation_failed"),
		      fail_copy_cont, NULL);
	}
    }
}

static bool copy_cancel_requested;

static gboolean
copy_progress (GnomeVFSAsyncHandle *handle,
	       GnomeVFSXferProgressInfo *info,
	       gpointer unused)
{
#if 0
  fprintf (stderr, "phase %d, status %d, vfs_status %s\n",
	   info->phase, info->status,
	   gnome_vfs_result_to_string (info->vfs_status));
#endif

  if (info->file_size > 0)
    set_entertainment_download_fun (op_downloading,
				    info->bytes_copied, info->file_size);

  if (info->phase == GNOME_VFS_XFER_PHASE_COMPLETED)
    {
      struct stat buf;

      if (stat (copy_target, &buf) < 0)
	{
	  /* If a obex connection is refused before the downloading is
	     started, gnome_vfs_async_xfer seems to report success
	     without actually creating the file.  We treat that
	     situation as an I/O error.
	  */
	  call_copy_cont (GNOME_VFS_ERROR_IO);
	}
      else
	call_copy_cont (copy_cancel_requested
			? GNOME_VFS_ERROR_CANCELLED
			: GNOME_VFS_OK);

      gnome_vfs_async_cancel (handle);
    }

  /* Produce an appropriate return value depending on the status.
   */
  if (info->status == GNOME_VFS_XFER_PROGRESS_STATUS_OK)
    {
      return !copy_cancel_requested;
    }
  else if (info->status == GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR)
    {
      call_copy_cont (info->vfs_status);
      gnome_vfs_async_cancel (handle);
      return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
    }
  else
    {
      add_log ("unexpected status %d in copy_progress\n", info->status);
      return 1;
    }
}

static void
cancel_copy (void *unused)
{
  copy_cancel_requested = true;
}

static void
do_copy (const char *source, GnomeVFSURI *source_uri,
	 gchar *target)
{
  GnomeVFSAsyncHandle *handle;
  GnomeVFSURI *target_uri;
  GList *source_uri_list, *target_uri_list;
  GnomeVFSResult result;

  target_uri = gnome_vfs_uri_new (target);
  if (target_uri == NULL)
    {
      call_copy_cont (GNOME_VFS_ERROR_NO_MEMORY);
      return;
    }

  source_uri_list = g_list_append (NULL, (gpointer) source_uri);
  target_uri_list = g_list_append (NULL, (gpointer) target_uri);

  copy_cancel_requested = false;

  set_entertainment_fun (NULL, -1, -1, 0);
  set_entertainment_cancel (cancel_copy, NULL);
  set_entertainment_main_title (_FM ("docm_nw_opening_file"));

  start_entertaining_user (TRUE);

  result = gnome_vfs_async_xfer (&handle,
				 source_uri_list,
				 target_uri_list,
				 GNOME_VFS_XFER_DEFAULT,
				 GNOME_VFS_XFER_ERROR_MODE_QUERY,
				 GNOME_VFS_XFER_OVERWRITE_MODE_REPLACE,
				 GNOME_VFS_PRIORITY_DEFAULT,
				 copy_progress,
				 NULL,
				 NULL,
				 NULL);

  if (result != GNOME_VFS_OK)
    call_copy_cont (result);
}

void
localize_file_and_keep_it_open (const char *uri,
				void (*cont) (char *local, void *data),
				void *data)
{
  if (copy_cont != NULL)
    {
      add_log ("Unexpected reentry\n");
      if (cont)
	cont (NULL, data);
      return;
    }

  copy_cont = cont;
  copy_cont_data = data;
  copy_target = NULL;
  copy_local = NULL;
  copy_tempdir = NULL;
  copy_vfs_handle = NULL;

  if (!gnome_vfs_init ())
    {
      call_copy_cont (GNOME_VFS_ERROR_GENERIC);
      return;
    }

  GnomeVFSURI *vfs_uri = gnome_vfs_uri_new (uri);

  if (vfs_uri == NULL)
    {
      call_copy_cont (GNOME_VFS_ERROR_NO_MEMORY);
      return;
    }

  /* The apt-worker can access all "file://" URIs, whether they are
     considered local by GnomeVFS or not.  (GnomeVFS considers a
     file:// URI pointing to a NFS mounted volume as remote, but we
     can read that just fine of course.)
  */

  const gchar *scheme = gnome_vfs_uri_get_scheme (vfs_uri);
  if (scheme && !strcmp (scheme, "file"))
    {
      /* Open the file to protect against unmounting of the MMC, etc.
       */
      GnomeVFSResult result;

      result = gnome_vfs_open_uri (&copy_vfs_handle, vfs_uri,
				   GNOME_VFS_OPEN_READ);
      if (result != GNOME_VFS_OK)
	call_copy_cont (result);
      else
	{
	  const gchar *path = gnome_vfs_uri_get_path (vfs_uri);
	  copy_target = gnome_vfs_unescape_string (path, NULL);
	  call_copy_cont (GNOME_VFS_OK);
	}
    }
  else
    {
      /* We need to copy.
       */

      char tempdir_template[] = "/var/tmp/osso-ai-XXXXXX";
      gchar *basename;

      /* Make a temporary directory and allow everyone to read it.
       */

      copy_target = NULL;
      copy_local = NULL;

      copy_tempdir = g_strdup (mkdtemp (tempdir_template));
      if (copy_tempdir == NULL)
	{
	  add_log ("Can not create %s: %m", copy_tempdir);
	  call_copy_cont (GNOME_VFS_ERROR_GENERIC);
	}
      else if (chmod (copy_tempdir, 0755) < 0)
	{
	  add_log ("Can not chmod %s: %m", copy_tempdir);
	  call_copy_cont (GNOME_VFS_ERROR_GENERIC);
	}
      else
	{
	  basename = gnome_vfs_uri_extract_short_path_name (vfs_uri);
	  copy_local = g_strdup_printf ("%s/%s", copy_tempdir, basename);
	  free (basename);

	  copy_target = g_strdup (copy_local);
	  do_copy (uri, vfs_uri, copy_target);
	}
    }

  gnome_vfs_uri_unref (vfs_uri);
}

void
cleanup_temp_file ()
{
  /* We don't put up dialogs for errors that happen now.  From the
     point of the user, the installation has been completed and he
     has seen the report already.
  */

  if (copy_vfs_handle)
    {
      gnome_vfs_close (copy_vfs_handle);
      copy_vfs_handle = NULL;
    }

  if (copy_local)
    {
      if (unlink (copy_local) < 0)
	add_log ("error unlinking %s: %m\n", copy_local);
      if (rmdir (copy_tempdir) < 0)
	add_log ("error removing %s: %m\n", copy_tempdir);

      g_free (copy_local);
      g_free (copy_tempdir);
      copy_local = NULL;
      copy_tempdir = NULL;
    }
}

struct rc_closure {
  void (*cont) (int status, void *data);
  void *data;
};

static void
reap_process (GPid pid, int status, gpointer raw_data)
{
  rc_closure *c = (rc_closure *)raw_data;
  void (*cont) (int status, void *data) = c->cont;
  void *data = c->data;
  delete c;

  cont (status, data);
}

void
run_cmd (char **argv,
	 bool ignore_nonexisting,
	 void (*cont) (int status, void *data),
	 void *data)
{
  int stdout_fd, stderr_fd;
  GError *error = NULL;
  GPid child_pid;

  if (!g_spawn_async_with_pipes (NULL,
				 argv,
				 NULL,
				 GSpawnFlags (G_SPAWN_DO_NOT_REAP_CHILD),
				 NULL,
				 NULL,
				 &child_pid,
				 NULL,
				 &stdout_fd,
				 &stderr_fd,
				 &error))
    {
      if (!ignore_nonexisting
	  || error->domain != G_SPAWN_ERROR
	  || error->code != G_SPAWN_ERROR_NOENT)
	add_log ("Can't run %s: %s\n", argv[0], error->message);
      g_error_free (error);
      cont (-1, data);
      return;
    }

  log_from_fd (stdout_fd);
  log_from_fd (stderr_fd);

  rc_closure *c = new rc_closure;
  c->cont = cont;
  c->data = data;
  g_child_watch_add (child_pid, reap_process, c);
}

void
close_apps (void)
{
  DBusConnection *conn;
  DBusMessage    *msg;

  /* Ignoring SIGTERM */
  if (signal (SIGTERM, SIG_IGN) != SIG_IGN)
    {
      add_log ("Can't ignore the TERM signal\n");
      return;
    }

  conn = dbus_bus_get (DBUS_BUS_SESSION, NULL);
  if (!conn)
    {
      add_log ("Could not get session bus.\n");
      return;
    }

  /*
   * This signal will close all non shown applications...
   */
  msg = dbus_message_new_signal ("/com/nokia/osso_app_killer",
                                 "com.nokia.osso_app_killer",
                                 "exit");

  dbus_connection_send (conn, msg, NULL);
  dbus_connection_flush (conn);

  dbus_connection_unref (conn);
}

const char *
skip_whitespace (const char *str)
{
  while (isspace (*str))
    str++;
  return str;
}

bool
all_whitespace (const char *str)
{
  return (*skip_whitespace (str)) == '\0';
}

static ConIcConnection *connection_object = NULL;

struct en_closure {
  void (*callback) (bool success, void *data);
  void *data;
};

/* XXX - we can not rely on the osso_iap functions to deliver our user
         data to the callback.  Instead we store the continuation
         callback in a global variable and make sure that it is called
         exactly once.
*/

static void (*en_callback) (bool success, void *data) = NULL;
static void *en_data;

static void
ensure_network_cont (bool success)
{
  void (*callback) (bool success, void *data) = en_callback;
  void *data = en_data;

  set_entertainment_fun (NULL, -1, -1, 0);

  en_callback = NULL;
  en_data = NULL;

  if (callback)
    callback (success, data);
}

static void
iap_callback (ConIcConnection *connection,
	      ConIcConnectionEvent *event,
	      gpointer user_data)
{
  bool success = false;

  switch (con_ic_connection_event_get_status (event))
    {
    case CON_IC_STATUS_CONNECTED:
      // add_log ("CON_IC_STATUS_CONNECTED\n");
      success = true;
      break;

    case CON_IC_STATUS_DISCONNECTED:
      // add_log ("CON_IC_STATUS_DISCONNECTED\n");
      break_entertainment ();
      break;

    default:
      add_log ("ConIc Error: unexpected event type\n");
      what_the_fock_p ();
      break;
    }

  ensure_network_cont (success);
}

void
ensure_network (void (*callback) (bool success, void *data), void *data)
{
  /* Silently cancel a pending request, if any.
   */
  ensure_network_cont (false);

  en_callback = callback;
  en_data = data;

  if (assume_connection)
    {
      ensure_network_cont (true);
      return;
    }

  if (connection_object == NULL)
    {
      connection_object = con_ic_connection_new ();
      g_signal_connect (connection_object, "connection-event",
 			G_CALLBACK (iap_callback), NULL);
    }

  if (con_ic_connection_connect (connection_object, CON_IC_CONNECT_FLAG_NONE))
    {
      set_entertainment_fun (_("ai_nw_connecting"), -1, -1, 0);
      return;
    }
  else
    add_log ("con_ic_connection_connect failed\n");

  what_the_fock_p ();
  ensure_network_cont (false);
}

char *
get_http_proxy ()
{
  char *proxy;

  GConfClient *conf = gconf_client_get_default ();

  /* We clear the cache here in order to force a fresh fetch of the
     values.  Otherwise, there is a race condition with the
     iap_callback: the OSSO_IAP_CONNECTED message might come before
     the GConf cache has picked up the new proxy settings.

     At least, that's the theory.
  */
  gconf_client_clear_cache (conf);

  if (gconf_client_get_bool (conf, "/system/http_proxy/use_http_proxy",
			     NULL))
    {
      char *user = NULL;
      char *password = NULL;
      char *host = NULL;
      gint port;

      if (gconf_client_get_bool (conf, "/system/http_proxy/use_authentication",
				 NULL))
	{
	  user = gconf_client_get_string
	    (conf, "/system/http_proxy/authentication_user", NULL);
	  password = gconf_client_get_string
	    (conf, "/system/http_proxy/authentication_password", NULL);
	}

      host = gconf_client_get_string (conf, "/system/http_proxy/host", NULL);
      port = gconf_client_get_int (conf, "/system/http_proxy/port", NULL);

      if (user)
	{
	  // XXX - encoding of '@', ':' in user and password?

	  if (password)
	    proxy = g_strdup_printf ("http://%s:%s@%s:%d",
				     user, password, host, port);
	  else
	    proxy = g_strdup_printf ("http://%s@%s:%d", user, host, port);
	}
      else
	proxy = g_strdup_printf ("http://%s:%d", host, port);

      g_free (user);
      g_free (password);
      g_free (host);
    }
  else
    proxy = g_strdup (getenv ("http_proxy"));

  /* XXX - there is also ignore_hosts, which we ignore for now, since
           transcribing it to no_proxy is hard... mandatory,
           non-transparent proxies are evil anyway.
   */

  g_object_unref (conf);

  return proxy;
}

char *
get_https_proxy ()
{
  char *proxy = NULL;

  GConfClient *conf = gconf_client_get_default ();

  /* We clear the cache here in order to force a fresh fetch of the
     values.  Otherwise, there is a race condition with the
     iap_callback: the OSSO_IAP_CONNECTED message might come before
     the GConf cache has picked up the new proxy settings.

     At least, that's the theory.
  */
  gconf_client_clear_cache (conf);

  char *host =
    gconf_client_get_string (conf, "/system/proxy/secure_host", NULL);
  int port = gconf_client_get_int (conf, "/system/proxy/secure_port", NULL);

  if (host && host[0])
    proxy = g_strdup_printf ("http://%s:%d", host, port);
  else
    proxy = g_strdup (getenv ("https_proxy"));
  g_free(host);

  g_object_unref (conf);

  return proxy;
}

void
push (GSList *&ptr, void *data)
{
  ptr = g_slist_prepend (ptr, data);
}

void *
pop (GSList *&ptr)
{
  void *data = ptr->data;
  GSList *next = ptr->next;
  g_slist_free_1 (ptr);
  ptr = next;
  return data;
}

const char *
gettext_alt (const char *id, const char *english)
{
  const char *tr = gettext (id);
  if (tr == id)
    return english;
  else
    return tr;
}

static gboolean
escape_key_press_event (GtkWidget *widget,
			  GdkEventKey *event,
			  gpointer data)
{
  GtkDialog *dialog = GTK_DIALOG (widget);
  int response = (int)data;

  if (event->keyval == HILDON_HARDKEY_ESC)
    {
      gtk_dialog_response (dialog, response);
      return TRUE;
    }

  return FALSE;
}

void
respond_on_escape (GtkDialog *dialog, int response)
{
  g_signal_connect (dialog, "key_press_event",
		    G_CALLBACK (escape_key_press_event),
		    (gpointer)response);
}

static void
grab_focus (GtkWidget *widget, gpointer data)
{
  gtk_widget_grab_focus (widget);
}

void
grab_focus_on_map (GtkWidget *widget)
{
  g_signal_connect (widget, "map", G_CALLBACK (grab_focus), NULL);
}

int64_t
get_free_space ()
{
  return get_free_space_at_path ("/");
}

int64_t
get_free_space_at_path (const char *path)
{
  struct statvfs buf;

  if (statvfs(path, &buf) != 0)
    return -1;

  int64_t res = (int64_t)buf.f_bfree * (int64_t)buf.f_bsize;
  printf ("FREE: %Ld\n", res);
  return res;
}

void
save_last_update_time (time_t t)
{
  char *text = g_strdup_printf ("%d", t);
  xexp *x = xexp_text_new ("time", text);
  g_free (text);
  user_file_write_xexp (UFILE_LAST_UPDATE, x);
  xexp_free (x);
}

int
load_last_update_time ()
{
  int t = 0;
  xexp *x = user_file_read_xexp (UFILE_LAST_UPDATE);
  if (x && xexp_is_text (x) && xexp_is (x, "time"))
    t = xexp_text_as_int (x);
  xexp_free (x);
  return t;
}

gboolean
is_package_cache_updated ()
{
  GConfClient *conf;
  int last_update, interval;

  /* Check the LAST_UPDATE timstamp */
  conf = gconf_client_get_default ();
  last_update = load_last_update_time ();
  interval = gconf_client_get_int (conf,
				   UPNO_GCONF_CHECK_INTERVAL,
				   NULL);

  if (interval <= 0)
    interval = UPNO_DEFAULT_CHECK_INTERVAL;

  if (last_update + interval*60 < time (NULL))
    return FALSE;

  return TRUE;
}
