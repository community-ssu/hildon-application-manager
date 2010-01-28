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

#include <gconf/gconf-client.h>
#include <hildon/hildon-file-chooser-dialog.h>
#include <gdk/gdkkeysyms.h>
#include <conic.h>
#include <dbus/dbus.h>

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
#include "confutils.h"

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
#define SCREENSHOT_DIR "launch"
#define HILDON_APP_MGR_SERVICE "com.nokia.hildon_application_manager"

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
      g_debug ("parent %p", dialog_stack->data);
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

  g_debug ("pushing dialog %p", dialog);
  dialog_stack = g_slist_prepend (dialog_stack, dialog);
}

void
pop_dialog (GtkWidget *dialog)
{
  g_assert (dialog_stack);
  g_debug ("child = %p ~ parent = %p", dialog, dialog_stack->data);
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
is_topmost_dialog (GtkWidget *dialog)
{
  return (dialog_stack != NULL) && (dialog_stack->data == dialog);
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
        g_debug ("We lose the initial interaction flow window!");

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

static gboolean
screenshot_already_exists ()
{
  gchar *sshot_dir = NULL;
  GDir *dir;
  gboolean res = FALSE;

  sshot_dir = g_strdup_printf ("%s/%s", g_get_user_cache_dir (), SCREENSHOT_DIR);
  dir = g_dir_open (sshot_dir, 0, NULL);
  if (dir)
    {
      const gchar *sshot_file;
      while (!res && (sshot_file = g_dir_read_name (dir)) != NULL)
        {
          if (g_str_has_prefix (sshot_file, HILDON_APP_MGR_SERVICE))
            res = TRUE;
        }
      g_dir_close (dir);
    }

  g_free (sshot_dir);
  return res;
}

void
maybe_take_screenshot (GtkWindow *win)
{
  if (!screenshot_already_exists ())
    hildon_gtk_window_take_screenshot (win, TRUE);
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
  if (GTK_IS_WIDGET (get_main_window ()))
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
     _("ai_bd_confirm_details"), 1,
     ok, GTK_RESPONSE_OK,
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
     _("ai_bd_add_catalogue_details"), 1,
     _("ai_bd_add_catalogue_ok"),      GTK_RESPONSE_OK,
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

static void
pannable_area_size_request (GtkWidget *widget, GtkRequisition *requisition,
                            gpointer user_data)
{
  GtkRequisition child_req;
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  if (child && GTK_IS_VIEWPORT (child))
    child = gtk_bin_get_child (GTK_BIN (child));

  if (child)
    {
      gtk_widget_size_request (child, &child_req);
      requisition->height = MIN (350, child_req.height);
    }
}

void
hildon_pannable_area_set_size_request_children (HildonPannableArea *area)
{
  g_signal_connect (area, "size-request",
                    G_CALLBACK (pannable_area_size_request), NULL);
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
  hildon_pannable_area_set_size_request_children (HILDON_PANNABLE_AREA (scroll));

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
                                        _("ai_bd_confirm_details"), 1,
                                        _("ai_bd_confirm_ok"), GTK_RESPONSE_OK,
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

static gboolean
entertainment_focus_in_event_cb (GtkWidget *dialog,
                                 GdkEventFocus *event,
                                 gpointer unused)
{
  entertainment_update_progress ();
  return FALSE;
}

static gboolean
entertainment_focus_out_event_cb (GtkWidget *dialog,
                                  GdkEventFocus *event,
                                  gpointer unused)
{
  g_assert (dialog != NULL);

  /* Stop progressbar activity if not the topmost one */
  if (!is_topmost_dialog (dialog))
      entertainment_stop_pulsing ();

  return FALSE;
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

      /* Add the internal box */
      box = gtk_vbox_new (FALSE, HILDON_MARGIN_DOUBLE);
      gtk_container_add
        (GTK_CONTAINER (GTK_DIALOG (entertainment.dialog)->vbox), box);

      /* Add the progress bar */
      entertainment.bar = gtk_progress_bar_new ();
      gtk_progress_bar_set_text (GTK_PROGRESS_BAR (entertainment.bar),
                                 entertainment.main_title);
      gtk_progress_bar_set_ellipsize (GTK_PROGRESS_BAR (entertainment.bar),
                                      PANGO_ELLIPSIZE_END);
      g_object_set (G_OBJECT (entertainment.bar), "text-xalign", 0.5, NULL);
      gtk_box_pack_start (GTK_BOX (box), entertainment.bar, FALSE, FALSE, 0);

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

          gtk_widget_show (GTK_DIALOG (entertainment.dialog)->action_area);
	}
      else
        gtk_widget_hide (GTK_DIALOG (entertainment.dialog)->action_area);

      /* Connect signals */
      g_signal_connect (entertainment.dialog, "delete-event",
                        G_CALLBACK (entertainment_delete), NULL);

      g_signal_connect (entertainment.dialog, "realize",
		    G_CALLBACK (progressbar_dialog_realized), NULL);

      g_signal_connect(G_OBJECT(entertainment.dialog), "focus-out-event",
                       G_CALLBACK(entertainment_focus_out_event_cb), NULL);

      g_signal_connect(G_OBJECT(entertainment.dialog), "focus-in-event",
                       G_CALLBACK(entertainment_focus_in_event_cb), NULL);

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

static int updating_level = 0;
static bool allow_updating_banner = true;

static GtkWindow*
get_topmost_window ()
{
  if (dialog_stack && dialog_stack->data && dialog_stack->data != entertainment.dialog)
    return GTK_WINDOW (dialog_stack->data);
  else
    return get_main_window ();
}

static void
refresh_updating_banner ()
{
  static GtkWindow *win = NULL;
  bool show_it = (updating_level > 0 && allow_updating_banner);

  if (show_it && win == NULL)
    {
      win = get_topmost_window ();
      hildon_gtk_window_set_progress_indicator (win, 1);
    }

  if (!show_it && win != NULL)
    {
      hildon_gtk_window_set_progress_indicator (win, 0);
      win = NULL;
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
show_updating ()
{
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

  scroll = GTK_WIDGET (g_object_new
                       (HILDON_TYPE_PANNABLE_AREA,
                        "hscrollbar-policy", GTK_POLICY_AUTOMATIC,
                        "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                        "mov-mode", HILDON_MOVEMENT_MODE_BOTH,
                        NULL));

  view = gtk_text_view_new ();
  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

  if (text)
    gtk_text_buffer_set_text (buffer, text, -1);

  gtk_text_view_set_editable (GTK_TEXT_VIEW (view), 0);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW (view), 0);
  g_signal_connect (view, "button-press-event",
		    G_CALLBACK (no_button_events), NULL);
  gtk_container_add (GTK_CONTAINER (scroll), view);
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

static GtkTreeModelFilter *global_tree_model_filter = NULL;
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
package_icon_func (GtkTreeViewColumn *column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer data)
{
  package_info *pi;

  gtk_tree_model_get (model, iter, 0, &pi, -1);
  if (!pi)
    return;

  if (!global_icons_initialized)
    {
      GtkIconTheme *icon_theme = gtk_icon_theme_get_default ();

      default_icon = gtk_icon_theme_load_icon (icon_theme,
                                               "tasklaunch_default_application",
                                               TREE_VIEW_ICON_SIZE,
                                               GtkIconLookupFlags (0),
                                               NULL);

      broken_icon = gtk_icon_theme_load_icon (icon_theme,
                                              "app_install_broken_application",
                                              TREE_VIEW_ICON_SIZE,
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
                "pixbuf", icon ? icon : default_icon,
                NULL);
}

static void
package_info_func (GtkTreeViewColumn *column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer data)
{
  package_info *pi;
  const gchar *package_name = NULL;
  const gchar *package_description = NULL;

  gtk_tree_model_get (model, iter, 0, &pi, -1);
  if (!pi)
    return;

  package_name = pi->get_display_name (global_installed);

  if (global_installed)
    package_description = pi->installed_short_description;
  else
    {
      package_description = pi->available_short_description;
      if (package_description == NULL)
        package_description = pi->installed_short_description;
    }

  g_object_set (cell,
                "package-name", package_name,
                "package-description", package_description,
                NULL);
}

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

static package_info_callback *global_activation_callback;

static void
global_row_activated (GtkTreeView *treeview,
		      GtkTreePath *path,
		      GtkTreeViewColumn *column,
		      gpointer data)
{
  GtkTreeModel *model = gtk_tree_view_get_model (treeview);
  GtkTreeIter iter;

  assert (model == GTK_TREE_MODEL (global_tree_model_filter));

  if (global_activation_callback &&
      gtk_tree_model_get_iter (model, &iter, path))
    {
      package_info *pi;

      /* Save global GtkTreePath */
      if (global_target_path != NULL)
        reset_global_target_path ();
      global_target_path = gtk_tree_model_get_path (model, &iter);

      /* Better save path for the previous element, if present */
      gtk_tree_path_prev (global_target_path);

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
	  gtk_tree_model_get_iter (GTK_TREE_MODEL (global_tree_model_filter),
                                   &iter, cursor_path);
	  result = !gtk_tree_model_iter_next (GTK_TREE_MODEL (global_tree_model_filter),
                                              &iter);
	  gtk_tree_path_free(cursor_path);
	}
    }

  return result;
}

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
static gboolean
button_press_cb (GtkWidget *treeview, GdkEventButton *event, gpointer data)
{
  g_return_val_if_fail (treeview != NULL && GTK_IS_TREE_VIEW (treeview), FALSE);

  if (event->window == gtk_tree_view_get_bin_window (GTK_TREE_VIEW (treeview)))
    {
      GtkTreeModel *model = NULL;

      model = gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
      if (model)
        {
          GtkTreePath *tp = NULL;

          gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (treeview),
                                         event->x,
                                         event->y,
                                         &tp, NULL, NULL, NULL);

          if (tp)
            {
              GtkTreeIter itr;

              if (gtk_tree_model_get_iter (model, &itr, tp))
                {
                  package_info *pi = NULL;

                  gtk_tree_model_get (model, &itr, 0, &pi, -1);

                  /* Update the global variable */
                  current_package_info = pi;

                  /* Use the callback, if present */
                  if (global_selection_callback)
                    global_selection_callback (pi);
                }

              gtk_tree_path_free (tp);
            }
        }
    }

  return FALSE;
}
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */

#if HILDON_CHECK_VERSION (2,2,5)

static gboolean
live_search_look_for_prefix (gchar **tokens, const gchar *prefix)
{
  gchar *needle = NULL;
  gboolean found = false;
  gint i = 0;

  /* We need something to look for first of all */
  if (!tokens)
    return FALSE;

  /* Casefold needle for ease of comparison */
  needle = g_utf8_casefold (prefix, -1);

  /* Look through the tokens */
  for (i = 0; tokens[i] != NULL; i++)
    {
      found = g_str_has_prefix (tokens[i], needle);

      /* Don't keep on looking if already found */
      if (found)
        break;
    }

  /* Free */
  g_free (needle);

  return found;
}

static gboolean
live_search_filter_func (GtkTreeModel *model,
                         GtkTreeIter  *iter,
                         gchar        *text,
                         gpointer      data)
{
    package_info *pi = NULL;
    gchar *text_utf8 = NULL;
    gchar *name = NULL;
    gchar *desc = NULL;
    gchar **text_tokens = NULL;
    gchar **name_tokens = NULL;
    gchar **desc_tokens = NULL;
    gboolean retvalue = FALSE;
    GtkWidget *live = GTK_WIDGET (data);
    gint i = 0;

    if (global_packages == NULL)
      return FALSE;

    /* Get package info */
    gtk_tree_model_get (model, iter, 0, &pi, -1);

    /* Row could be empty - must check */
    if (pi == NULL)
      {
        /* Must ensure consitent state if row is empty */
        reset_global_target_path ();
        gtk_widget_hide (live);
        return FALSE;
      }

    /* Casefold name, description and text for ease of comparison */
    text_utf8 = g_utf8_casefold (text, -1);
    name = g_utf8_casefold (pi->get_display_name(global_installed), -1);
    desc = g_utf8_casefold (global_installed
                            ? pi->installed_short_description
                            : pi->available_short_description,
                            -1);

    /* Tokenize name and description */
    name_tokens = g_strsplit (name, " ", -1);
    desc_tokens = g_strsplit (desc, " ", -1);

    /* Tokenize and search for *all* the tokens */
    text_tokens = g_strsplit (text, " ", -1);
    for (i = 0; text_tokens[i] != NULL; i++)
      {
        /* Check package name */
        retvalue = live_search_look_for_prefix (name_tokens, text_tokens[i]);

        /* Check short description if not found yet */
        if (!retvalue)
          retvalue = live_search_look_for_prefix (desc_tokens, text_tokens[i]);

        /* If not found reached this point, don't keep on looking */
        if (!retvalue)
          break;
      }

    /* Free */
    g_strfreev (text_tokens);
    g_strfreev (name_tokens);
    g_strfreev (desc_tokens);
    g_free (text_utf8);
    g_free (name);
    g_free (desc);

    return retvalue;
}

#endif

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
static void
tree_tap_and_hold_cb (GtkWidget *tree, gpointer data)
{
  GtkWidget *menu = GTK_WIDGET (data);

  if (GTK_IS_WIDGET (tree) && GTK_IS_MENU (menu))
    {
      gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL,
                      tree, 1, gdk_x11_get_server_time (tree->window));
    }
}
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */

static GtkWidget *
make_global_package_list (GtkWidget *window,
                          GList *packages,
			  bool installed,
			  const char *empty_label,
			  const char *op_label,
			  package_info_callback *selected,
			  package_info_callback *activated,
                          const gchar *button_label,
                          void (*button_callback) (void))
{
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *tree, *scroller, *alignment, *vbox;

#if HILDON_CHECK_VERSION (2,2,5)
  GtkWidget *live;
#endif

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
  GtkWidget *menu = NULL;
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */

  if (packages == NULL)
    {
      GtkWidget *label = gtk_label_new (empty_label);
      hildon_helper_set_logical_font (label, "LargeSystemFont");
      hildon_helper_set_logical_color (label, GTK_RC_FG, GTK_STATE_NORMAL,
                                       "SecondaryTextColor");
      gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.5);
      return label;
    }

  /* Just create a new model for the first time */
  if (global_list_store == NULL)
    global_list_store = gtk_list_store_new (1, G_TYPE_POINTER);

  if (global_tree_model_filter != NULL)
    g_object_unref (global_tree_model_filter);

  /* Create a tree model filter with the actual model inside */
  global_tree_model_filter =
    GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (GTK_TREE_MODEL (global_list_store), NULL));

  /* Insert the filter into the treeview */
  tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (global_tree_model_filter));

  column = gtk_tree_view_column_new ();

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start(column, renderer, FALSE);
  gtk_tree_view_column_set_cell_data_func (column, renderer,
                                           package_icon_func, NULL, NULL);

  renderer = package_info_cell_renderer_new ();
  package_info_cell_renderer_listen_style (PACKAGE_INFO_CELL_RENDERER(renderer),
                                           tree);
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (column, renderer,
                                           package_info_func, NULL, NULL);

  gtk_tree_view_insert_column (GTK_TREE_VIEW (tree), column, -1);

  scroller = hildon_pannable_area_new();
  gtk_container_add (GTK_CONTAINER (scroller), tree);

  alignment = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment),
                             HILDON_MARGIN_HALF,
                             0,
                             HILDON_MARGIN_DOUBLE,
                             HILDON_MARGIN_DOUBLE);
  gtk_container_add (GTK_CONTAINER (alignment), scroller);

  g_signal_connect (tree, "row-activated",
		    G_CALLBACK (global_row_activated), NULL);

  g_signal_connect (tree, "key-press-event",
                    G_CALLBACK (global_package_list_key_pressed), NULL);

  /* Check if the action area should be used */
  if (button_label && button_callback)
    {
      GtkWidget *action_area_box = NULL;
      GtkWidget *button = NULL;

      action_area_box =
        hildon_tree_view_get_action_area_box (GTK_TREE_VIEW (tree));

      button = hildon_button_new (HILDON_SIZE_FINGER_HEIGHT,
                                  HILDON_BUTTON_ARRANGEMENT_HORIZONTAL);
      hildon_button_set_title (HILDON_BUTTON (button), button_label);

      g_signal_connect (button, "clicked", G_CALLBACK (button_callback), NULL);

      gtk_box_pack_start (GTK_BOX (action_area_box), button, TRUE, TRUE, 0);
      gtk_widget_show_all (button);

      hildon_tree_view_set_action_area_visible (GTK_TREE_VIEW (tree), TRUE);
    }

#if defined (TAP_AND_HOLD) && defined (MAEMO_CHANGES)
  /* Create the contextual menu */
  menu = create_package_menu (op_label);

  /* Setup tap and hold */
  gtk_widget_tap_and_hold_setup (tree, NULL, NULL,
				 GtkWidgetTapAndHoldFlags (0));
  g_object_ref_sink (menu);
  if (gtk_menu_get_attach_widget (GTK_MENU (menu)) == NULL)
    gtk_menu_attach_to_widget (GTK_MENU (menu), tree, NULL);

  g_signal_connect (tree, "button-press-event",
                    G_CALLBACK (button_press_cb), NULL);

  g_signal_connect (tree, "tap-and-hold",
                    G_CALLBACK (tree_tap_and_hold_cb), menu);

  gtk_widget_show_all (menu);
#endif /* TAP_AND_HOLD && MAEMO_CHANGES */

  set_global_package_list (packages, installed, selected, activated);

  grab_focus_on_map (tree);

  /* Scroll to desired cell, if needed */
  if (global_target_path != NULL)
    gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tree),
                                  global_target_path,
                                  NULL, FALSE, 0, 0);

#if HILDON_CHECK_VERSION (2,2,5)
  /* Live search */
  live = hildon_live_search_new ();
  hildon_live_search_set_filter (HILDON_LIVE_SEARCH (live),
                                 global_tree_model_filter);
  hildon_live_search_set_visible_func (HILDON_LIVE_SEARCH (live),
                                       live_search_filter_func, live, NULL);
  hildon_live_search_widget_hook (HILDON_LIVE_SEARCH (live),
                                  window, tree);
#endif

  /* Pack the packages list and the live search widget toghether */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), alignment, TRUE, TRUE, 0);

#if HILDON_CHECK_VERSION (2,2,5)
  gtk_box_pack_start (GTK_BOX (vbox), live, FALSE, FALSE, 0);
#endif

  /* Prepare visibility */
  gtk_widget_show_all (alignment);

#if HILDON_CHECK_VERSION (2,2,5)
  gtk_widget_hide (live);
#endif

  gtk_widget_hide (vbox);

  return vbox;
}

GtkWidget *
make_install_apps_package_list (GtkWidget *window,
                                GList *packages,
                                gboolean show_empty_label,
                                package_info_callback *selected,
                                package_info_callback *activated)
{
  GtkWidget *view = NULL;
  gchar *empty_label = NULL;

  /* Build the label */
  empty_label = g_strdup (show_empty_label
                          ? _("ai_li_no_applications_available")
                          : NULL);
  /* Get the view */
  view = make_global_package_list (window, packages, false, empty_label,
                                   _("ai_me_cs_install"), selected, activated,
                                   NULL, NULL);
  g_free (empty_label);
  return view;
}

GtkWidget *
make_upgrade_apps_package_list (GtkWidget *window,
                                GList *packages,
                                gboolean show_empty_label,
                                gboolean show_action_area,
                                package_info_callback *selected,
                                package_info_callback *activated)
{
  GtkWidget *view = NULL;
  gchar *empty_label = NULL;
  gchar *button_label = NULL;
  gpointer button_callback = NULL;

  /* Build the label */
  empty_label = g_strdup (show_empty_label
                          ? _("ai_li_no_updates_available")
                          : NULL);

  /* Prepare button label and callback, if needed */
  if (show_action_area)
    {
      button_label = g_strdup (_("ai_me_update_all"));
      button_callback = (gpointer) update_all_packages_flow;
    }

  /* Get the view */
  view = make_global_package_list (window, packages, false, empty_label,
                                   _("ai_me_cs_update"), selected, activated,
                                   button_label, (void (*)()) button_callback);
  /* Free */
  g_free (empty_label);
  g_free (button_label);

  return view;
}

GtkWidget *
make_uninstall_apps_package_list (GtkWidget *window,
                                  GList *packages,
                                  gboolean show_empty_label,
                                  package_info_callback *selected,
                                  package_info_callback *activated)
{
  GtkWidget *view = NULL;
  gchar *empty_label = NULL;

  /* Build the label */
  empty_label = g_strdup (show_empty_label
                          ? _("ai_li_no_installed_applications")
                          : NULL);
  /* Get the view */
  view = make_global_package_list (window, packages, true, empty_label,
                                   _("ai_me_cs_uninstall"), selected, activated,
                                   NULL, NULL);
  g_free (empty_label);
  return view;
}

/*
 * This function check if the package is in section "user/hidden"
 */
bool
package_is_hidden (package_info *pi)
{
  if (red_pill_mode && red_pill_show_all)
    return false;

  if (!pi->available_section)
    return false;

  const char *hidden = "user/hidden";
  const char *section = pi->available_section;

  size_t len = strlen (hidden);
  return strlen (section) == len && !strncmp (section, hidden, len);
}

static void
set_global_package_list (GList *packages,
			 bool installed,
			 package_info_callback *selected,
			 package_info_callback *activated)
{
  if (global_list_store)
    {
      /* @FIXME:
         Gotta set entries to NULL first, because some bug in maemo-gtk/hildon
         is causing calls to cat_icon_func/cat_text_func with garbage data
      */
      GtkTreeIter itr;
      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (global_list_store), &itr))
        do
          {
            gtk_list_store_set(global_list_store, &itr, 0, NULL, -1);
          } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (global_list_store), &itr));
      gtk_list_store_clear (global_list_store);
    }

  for (GList *p = global_packages; p; p = p->next)
    {
      package_info *pi = (package_info *)p->data;
      pi->model = NULL;
    }

  global_installed = installed;
  global_selection_callback = selected;
  global_activation_callback = activated;
  global_packages = packages;

  int pos = 0;
  for (GList *p = global_packages; p; p = p->next)
    {
      package_info *pi = (package_info *)p->data;

      /* don't insert the package if it isn't installed
       * and it's in the section "user/hidden"
       */
      if (!pi->installed_version && package_is_hidden (pi))
        continue;

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

enum {
  SECTION_LS_TEXT_COLUMN,
  SECTION_LS_PIXBUF_COLUMN,
  SECTION_LS_SI_COLUMN,
  SECTION_LS_N_COLUMNS
};

static void
icon_view_item_activated (GtkWidget *icon_view,
                          GtkTreePath *tp,
                          GtkTreeModel *tm)
{
  GtkTreeIter itr;

  if (gtk_tree_model_get_iter (tm, &itr, tp))
    {
      section_info *si = NULL;

      gtk_tree_model_get (tm, &itr, SECTION_LS_SI_COLUMN, &si, -1);

      section_clicked (icon_view, si);
    }
}

static void
icon_view_is_dying (GtkTreeModel *tm, GtkIconView *stale_pointer)
{
  section_info *si;
  GtkTreeIter itr;

  if (gtk_tree_model_get_iter_first (tm, &itr))
    do
      {
        gtk_tree_model_get (tm, &itr, SECTION_LS_SI_COLUMN, &si, -1);
        si->unref ();
      }
    while (gtk_tree_model_iter_next (tm, &itr));

  g_object_unref (G_OBJECT (tm));
}

static void
set_text_cr_style (GtkWidget *widget, GtkStyle *prev_style, GObject *cr_text)
{
  GtkStyle *style = NULL;

  style = gtk_rc_get_style_by_paths (gtk_widget_get_settings (widget),
				     "SmallSystemFont",
				     NULL,
				     G_TYPE_NONE);
  if (style)
    {
      PangoAttrList *attr_list = pango_attr_list_new ();

      if (attr_list)
        {
	  PangoAttribute *attr =
	    pango_attr_font_desc_new (
	      pango_font_description_copy (style->font_desc));

	  pango_attr_list_insert (attr_list, attr);
	  g_object_set (cr_text, "attributes", attr_list, NULL);
        }
    }
}

#define ICONS_GRID_ITEM_WIDTH HILDON_ICON_PIXEL_SIZE_XLARGE \
  + HILDON_MARGIN_DEFAULT + HILDON_MARGIN_HALF

static GtkWidget *
make_my_icon_view (GtkTreeModel *tm)
{
  GList *ls_cr = NULL, *itr = NULL;
  GtkCellRenderer *cr_text = NULL;
  GtkWidget *icon_view = GTK_WIDGET (g_object_new (
                                    GTK_TYPE_ICON_VIEW,
                                    "model", tm,
                                    "text-column",    SECTION_LS_TEXT_COLUMN,
                                    "pixbuf-column",  SECTION_LS_PIXBUF_COLUMN,
                                    "column-spacing", HILDON_MARGIN_DOUBLE,
                                    "item-width", ICONS_GRID_ITEM_WIDTH,
                                    "row-spacing",    HILDON_MARGIN_DOUBLE,
                                    NULL));

  for (ls_cr = itr = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (icon_view));
       itr;
       itr = itr->next)
    {
      if (g_type_is_a (G_TYPE_FROM_INSTANCE (itr->data),
                       GTK_TYPE_CELL_RENDERER_TEXT))
        break;
    }

  if (itr)
    cr_text = GTK_CELL_RENDERER (itr->data);
  g_list_free (ls_cr);

  if (cr_text) {
    g_signal_connect (G_OBJECT (icon_view),
                      "style-set",
                      G_CALLBACK (set_text_cr_style),
                      cr_text);
    set_text_cr_style (icon_view, NULL, G_OBJECT(cr_text));
    g_object_set(G_OBJECT(cr_text),
                 "wrap-mode",  PANGO_WRAP_WORD,
                 "wrap-width", ICONS_GRID_ITEM_WIDTH,
                 NULL);
  }

  return icon_view;
}

#define SECTION_ICON_PATTERN "/etc/hildon/theme/backgrounds/app_install_%s.png"

static GdkPixbuf *
pixbuf_from_si(section_info *si)
{
  GdkPixbuf *pb = NULL;
  char *icon_fname;

  /*
   * Assumptions about section_info:
   * 1. Non-NULL canonical name and rank of 2 == "other"
   * 2. NULL canonical name == "all"
   * 3. Non-NULL canonical name is a valid pre-defined section
   */

  icon_fname = g_strdup_printf (SECTION_ICON_PATTERN,
                                si->untranslated_name
                                  ? 2 == si->rank
                                    ? "other"
                                    : si->untranslated_name
                                  : "all");
  pb = gdk_pixbuf_new_from_file (icon_fname, NULL);

  if (!pb)
    {
      /*
       * If all else fails, try "other"
       * This should never happen
       */
      g_free(icon_fname);
      icon_fname = g_strdup_printf(SECTION_ICON_PATTERN, "other");
      pb = gdk_pixbuf_new_from_file (icon_fname, NULL);
    }

  g_free (icon_fname);

  return pb;
}

GtkWidget *
make_global_section_list (GList *sections, section_activated *act)
{
  global_section_activated = act;

  if (sections == NULL)
    {
      GtkWidget *label = gtk_label_new (_("ai_li_no_applications_available"));
      hildon_helper_set_logical_font (label, "LargeSystemFont");
      hildon_helper_set_logical_color (label, GTK_RC_FG, GTK_STATE_NORMAL,
                                       "SecondaryTextColor");
      gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.5);
      return label;
    }

  GtkWidget *scroller;
  GtkListStore *ls = NULL;
  GtkTreeIter itr;
  GtkWidget *icon_view;

  scroller = hildon_pannable_area_new ();
  g_object_set (G_OBJECT (scroller), "vovershoot-max", 0, NULL);

  ls = gtk_list_store_new (SECTION_LS_N_COLUMNS,
                           G_TYPE_STRING,
                           GDK_TYPE_PIXBUF,
                           G_TYPE_POINTER);

  for (GList *s = sections; s; s = s ->next)
    {
      section_info *si = (section_info *)s->data;

      if (si->rank == SECTION_RANK_HIDDEN)
        continue;

      gtk_list_store_append (ls, &itr);
      gtk_list_store_set (ls, &itr,
                          SECTION_LS_TEXT_COLUMN,   si->name,
                          SECTION_LS_PIXBUF_COLUMN, pixbuf_from_si(si),
                          SECTION_LS_SI_COLUMN,     si,
                          -1);

      si->ref ();
    }

  icon_view = make_my_icon_view (GTK_TREE_MODEL (ls));
  g_object_weak_ref (G_OBJECT(icon_view), (GWeakNotify)icon_view_is_dying, ls);
  g_signal_connect (G_OBJECT (icon_view),
                    "item-activated",
                    G_CALLBACK (icon_view_item_activated),
                    ls);
  gtk_container_add (GTK_CONTAINER(scroller), icon_view);

  global_section_list = scroller;
  g_object_ref (scroller);

  /* Prepare visibility */
  gtk_widget_show_all (icon_view);
  gtk_widget_hide (scroller);

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

static void
package_selected_activated_callback (GtkTreeView *treeview,
                                     GtkTreePath *path,
                                     GtkTreeViewColumn *col,
                                     gpointer unused)
{
  GtkTreeIter iter;
  gboolean selected;
  gboolean installable;

  gtk_tree_model_get_iter (gtk_tree_view_get_model (treeview),
                           &iter, path);
  gtk_tree_model_get (gtk_tree_view_get_model (treeview), &iter,
                      COLUMN_SP_SELECTED, &selected,
                      COLUMN_SP_INSTALLABLE, &installable,
                      -1);

  if (installable)
    {
      gtk_list_store_set (GTK_LIST_STORE(gtk_tree_view_get_model (treeview)),
                          &iter, COLUMN_SP_SELECTED, !selected, -1);
    }
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
                                     _("ai_bd_confirm_ok"),
                                     GTK_RESPONSE_OK);

  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  list_store = make_select_package_list_store (c->package_list, &total_size);

  /* Set the message dialog */
  message_label = make_small_label (c->question);
  gtk_label_set_line_wrap (GTK_LABEL (message_label), TRUE);
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
  column = gtk_tree_view_column_new_with_attributes ("Marked", renderer,
						     "active", COLUMN_SP_SELECTED,
						     "activatable", COLUMN_SP_INSTALLABLE,
						     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
			       column);
  renderer = gtk_cell_renderer_text_new ();
  g_object_set (G_OBJECT (renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
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
  scroller = hildon_pannable_area_new ();
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
  g_signal_connect (tree_view, "row-activated",
		    G_CALLBACK (package_selected_activated_callback),
		    NULL);

  g_signal_connect (list_store, "row-changed",
		    G_CALLBACK (update_packages_list_selection),
		    upls_data);

  gtk_widget_set_size_request (dialog, -1, 350);
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
#define MEGA (KILO * KILO)
#define GIGA (KILO * MEGA)

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
                      (int) (bytes / KILO));
	  else
	    {
              // round to nearest MEGA
              // bytes ~ num * MEGA
              num = (bytes + MEGA / 2) / (MEGA);
              if (num < 10)
                snprintf (buf, n, _FM ("sfil_li_size_1mb_10mb"), (float) num);
              else if (num < 1000)
                snprintf (buf, n, _FM ("sfil_li_size_10mb_1gb"), (float) num);
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
                      (int) (bytes / KILO));
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

/*
  Causes a rectangle of width (*rectwidth) and height (*rectheight) to
  fit inside a rectangle of width and height.
  The resulting pair ((*px),(*py)) holds the coordinates of the upper left
  corner of the scaled rectangle wrt. the upper left corner of the given
  rectangle.
*/
double fit_rect_inside_rect (double width, double height,
			     double *x, double *y,
			     double *rectwidth, double *rectheight)
{
  double aratio, rectaratio, scale ;

  if (0 == width || 0 == height
      || 0 == *rectwidth || 0 == *rectheight)
    return 1.0 ;

  aratio = width / height ;
  rectaratio = *rectwidth / *rectheight ;

  if (rectaratio > aratio)
    {
      *x = 0;
      scale = width / *rectwidth;
      *rectwidth = width;
      *rectheight = *rectwidth / rectaratio;
      *y = (height - *rectheight) / 2 ;
    }
  else
    {
      *y = 0;
      scale = height / *rectheight;
      *rectheight = height;
      *rectwidth = *rectheight * rectaratio;
      *x = (width - *rectwidth) / 2;
    }

  return scale;
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

  if (pixbuf)
    {
      int width = gdk_pixbuf_get_width (pixbuf);
      int height = gdk_pixbuf_get_height (pixbuf);

      if (width != TREE_VIEW_ICON_SIZE
          || height != TREE_VIEW_ICON_SIZE)
	{
	  GdkPixbuf *right_size_pixbuf =
	    gdk_pixbuf_new (gdk_pixbuf_get_colorspace (pixbuf),
			    gdk_pixbuf_get_has_alpha (pixbuf),
			    gdk_pixbuf_get_bits_per_sample (pixbuf),
			    TREE_VIEW_ICON_SIZE,
			    TREE_VIEW_ICON_SIZE);

          if (right_size_pixbuf)
            {
              gdk_pixbuf_fill (right_size_pixbuf, 0x00000000);

              if (G_LIKELY (width <= TREE_VIEW_ICON_SIZE
                            && height <= TREE_VIEW_ICON_SIZE))
                gdk_pixbuf_composite (pixbuf, right_size_pixbuf,
                   /* dest_x */       (TREE_VIEW_ICON_SIZE - width) / 2,
                   /* dest_y */       (TREE_VIEW_ICON_SIZE - width) / 2,
                   /* dest_width */   width,
                   /* dest_height */  height,
                   /* offset_x */     (TREE_VIEW_ICON_SIZE - width) / 2,
                   /* offset_y */     (TREE_VIEW_ICON_SIZE - width) / 2,
                                      1.0, 1.0, GDK_INTERP_HYPER, 255);
              else
                {
                  double d_width = width;
                  double d_height = height;
                  double x, y, scale;

                  scale = fit_rect_inside_rect (TREE_VIEW_ICON_SIZE,
                                                TREE_VIEW_ICON_SIZE,
                                                &x, &y,
                                                &d_width, &d_height);

                  gdk_pixbuf_composite (pixbuf, right_size_pixbuf,
                                        /* dest_x */      x,
                                        /* dest_y */      y,
                                        /* dest_width */  d_width,
                                        /* dest_height */ d_height,
                                        /* offset_x */    x,
                                        /* offset_y */    y,
                                        scale, scale, GDK_INTERP_HYPER, 255);
                }

              g_object_unref (pixbuf);
              pixbuf = right_size_pixbuf;
            }
        }
  }

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

static void
run_cmd_simple_cont (int status, void *data)
{
  /* Free memory */
  gchar **argv = (gchar **) data;
  g_strfreev (argv);
}

void
run_cmd_simple (const char *cmd)
{
  g_return_if_fail (cmd != NULL);

  /* Tokenize parameters */
  gchar **argv = g_strsplit (cmd, " ", -1);
  run_cmd (argv, true, run_cmd_simple_cont, argv);
}

void
stop_dsme_service (const char *service)
{
  g_return_if_fail (service != NULL);

  char **argv = g_new (char *, 4);

  argv[0] = g_strdup ("/usr/sbin/dsmetool");
  argv[1] = g_strdup ("-k");
  argv[2] = g_strdup (service);
  argv[3] = NULL;

  run_cmd (argv, true, run_cmd_simple_cont, argv);
}

/* Took from osso-backup: ob_utils_set_prestarted_apps_enabled */
void
set_prestarted_apps_enabled (gboolean enable)
{
  DBusConnection *conn;
  DBusMessage    *msg;

  conn = dbus_bus_get (DBUS_BUS_SESSION, NULL);
  if (!conn)
    {
      add_log ("Could not get session bus.\n");
      return;
    }

  msg = dbus_message_new_method_call ("com.nokia.HildonDesktop.AppMgr",
                                      "/com/nokia/HildonDesktop/AppMgr",
                                      "com.nokia.HildonDesktop.AppMgr",
                                      "Prestart");

  dbus_message_append_args (msg, DBUS_TYPE_BOOLEAN, &enable, DBUS_TYPE_INVALID);

  dbus_connection_send (conn, msg, NULL);
  dbus_connection_flush (conn);

  dbus_message_unref (msg);
  dbus_connection_unref (conn);
}

void
close_apps (void)
{
  DBusConnection *conn;
  DBusMessage    *msg;

  /* Ignoring SIGTERM */
  if (signal (SIGTERM, SIG_IGN) == SIG_ERR)
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

/* @FIXME:
   This mechanism to obtain the http proxy seems to be
   deprecated in Fremantle
*/
static char *
get_gconf_http_proxy ()
{
  char *proxy;
  char *proxy_mode = NULL;

  GConfClient *conf = gconf_client_get_default ();

  /* We clear the cache here in order to force a fresh fetch of the
     values.  Otherwise, there is a race condition with the
     iap_callback: the OSSO_IAP_CONNECTED message might come before
     the GConf cache has picked up the new proxy settings.

     At least, that's the theory.
  */
  gconf_client_clear_cache (conf);

  proxy_mode = gconf_client_get_string (conf, "/system/proxy/mode", NULL);
  if (strcmp (proxy_mode, "none")
      && gconf_client_get_bool (conf, "/system/http_proxy/use_http_proxy",
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

  g_free (proxy_mode);
  g_object_unref (conf);

  return proxy;
}

static char *
get_gconf_https_proxy ()
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

static char *
get_conic_proxy (ConIcProxyProtocol conic_protocol)
{
  ConIcProxyMode proxy_mode;
  const gchar* host;
  gint port;
  gchar *proxy = NULL;

  proxy_mode = con_ic_connection_get_proxy_mode (connection_object);
  if (proxy_mode == CON_IC_PROXY_MODE_MANUAL)
    {
      host = con_ic_connection_get_proxy_host (connection_object, conic_protocol);
      port = con_ic_connection_get_proxy_port (connection_object, conic_protocol);

      if (host != NULL)
        proxy = g_strdup_printf ("http://%s:%d", host, port);
    }
  else if (proxy_mode == CON_IC_PROXY_MODE_AUTO)
    {
      // @TODO: shall we support this?
    }

  return proxy;
}

char *
get_http_proxy ()
{
  gchar *proxy = NULL;

  if ((proxy = getenv ("http_proxy")) != NULL)
    return g_strdup (proxy);

  /* Try libconic first or fallback to gconf if not available */
  if (connection_object != NULL)
    proxy = get_conic_proxy (CON_IC_PROXY_PROTOCOL_HTTP);
  else
    proxy = get_gconf_http_proxy ();

  return proxy;
}

char *
get_https_proxy ()
{
  gchar *proxy = NULL;

  if ((proxy = getenv ("https_proxy")) != NULL)
    return g_strdup (proxy);

  /* Try libconic first or fallback to gconf if not available */
  if (connection_object != NULL)
    proxy = get_conic_proxy (CON_IC_PROXY_PROTOCOL_HTTPS);
  else
    proxy = get_gconf_https_proxy ();

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

int
last_domain_modification_time ()
{
  struct stat dmndir;

  if (stat (PACKAGE_DOMAINS, &dmndir) == 0)
    return dmndir.st_mtime;

  return 0;
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

  if (last_update + interval * 60 < time (NULL)
      || last_update < last_domain_modification_time ())
    return FALSE;

  return TRUE;
}

bool
is_pkg_ssu (package_info *pi, bool broken)
{
  if (!pi)
    return false;

  return ((pi->have_detail_kind != remove_details)
          && (broken ? (pi->info.installable_status != status_able) : true)
          && (pi->flags & pkgflag_system_update));
}

bool
running_in_scratchbox ()
{
  return access ("/targets/links/scratchbox.config", F_OK) == 0;
}

static char*
read_cmdline (guint pid)
{
  char filename[sizeof ("/proc//cmdline") + sizeof (int) * 3];
  char *cmdline = NULL;

  sprintf (filename, "/proc/%u/cmdline", pid);
  if (g_file_get_contents (filename, &cmdline, NULL, NULL) == FALSE)
    return NULL;

  if (cmdline != NULL && cmdline[0] == '\0')
    {
      g_free (cmdline);
      return NULL;
    }

  return cmdline;
}

static pid_t*
find_pid_by_name (const char *proc_name)
{
  int i = 0;
  const char *entry = NULL;
  pid_t *pid_list = NULL;

  GDir* dir = g_dir_open ("/proc", 0, NULL);
  if (dir == NULL)
    return NULL;

  pid_list = (pid_t *) g_malloc (sizeof (pid_t));
  pid_list[0] = 0;
  while ((entry = g_dir_read_name (dir)) != NULL)
    {
      gdouble pid = g_ascii_strtod (entry, NULL);
      if (errno != 0)
        continue;
      char *cmdline = read_cmdline (pid);
      if (cmdline == NULL)
        continue;

      if (g_strrstr (cmdline, proc_name) != NULL)
        {
          pid_list = (pid_t *) g_realloc (pid_list, (i + 2) * sizeof (pid_t));
          pid_list[i++] = (pid_t) pid;
          pid_list[i] = 0;
        }

      g_free (cmdline);
    }
  g_dir_close (dir);

  return pid_list;
}

void
maybe_kill_all_by_name (const char *proc_name, int signum)
{
  pid_t *pid_list = find_pid_by_name (proc_name);
  if (pid_list == NULL)
    return;

  int i;
  for (i = 0; pid_list[i] != 0; i++)
    kill (pid_list[i], signum);

  g_free (pid_list);
}
