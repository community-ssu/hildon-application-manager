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

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <libintl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/statvfs.h>

#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <hildon/hildon-note.h>
#include <hildon/hildon-file-chooser-dialog.h>
#include <hildon/hildon-banner.h>
#include <gdk/gdkkeysyms.h>
#include <hildon/hildon-defines.h>
#include <conic.h>
#include <libgnomevfs/gnome-vfs.h>

#include "util.h"
#include "details.h"
#include "log.h"
#include "settings.h"
#include "menu.h"
#include "apt-worker-client.h"

#define _(x) gettext (x)

/* For getting and tracking the Bluetooth name
 */
#define BTNAME_SERVICE                  "org.bluez"
#define BTNAME_REQUEST_IF               "org.bluez.Adapter"
#define BTNAME_SIGNAL_IF                "org.bluez.Adapter"
#define BTNAME_REQUEST_PATH             "/org/bluez/hci0"
#define BTNAME_SIGNAL_PATH              "/org/bluez/hci0"

#define BTNAME_REQ_GET                  "GetName"
#define BTNAME_SIG_CHANGED              "NameChanged"

#define BTNAME_MATCH_RULE "type='signal',interface='" BTNAME_SIGNAL_IF \
                          "',member='" BTNAME_SIG_CHANGED "'"

static Window parent_xid = None;
static GSList *dialog_stack = NULL;
static bool main_window_shown = false;

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

  gtk_window_set_modal (GTK_WINDOW (dialog), parent_xid == None);

  if (dialog_stack)
    {
      fprintf (stderr, "PUSH %p <- %p\n", dialog, dialog_stack->data);
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
				    GTK_WINDOW (dialog_stack->data));
    }
  else if (parent_xid != None)
    {
      fprintf (stderr, "PUSH %p <= %p\n", dialog, parent_xid);
      g_signal_connect (dialog, "realize",
			G_CALLBACK (dialog_realized), NULL);
    }
  else
    {
      /* This happens for dialogs outside of a interaction flow.
       */
      fprintf (stderr, "PUSH %p <- main\n", dialog);
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
				    get_main_window ());
    }

  dialog_stack = g_slist_prepend (dialog_stack, dialog);
}

void
pop_dialog (GtkWidget *dialog)
{
  g_assert (dialog_stack && dialog_stack->data == dialog);

  fprintf (stderr, "POP %p\n", dialog);

  {
    GSList *old = dialog_stack;
    dialog_stack = dialog_stack->next;
    g_slist_free_1 (old);
  }
}

static bool interaction_flow_active = false;

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
      irritate_user (_("Operation in progress"));
      return false;
    }

  fprintf (stderr, "START %p\n", get_main_window ());

  g_assert (dialog_stack == NULL);

  interaction_flow_active = true;
  parent_xid = None;
  dialog_stack = g_slist_prepend (dialog_stack, get_main_window ());
  return true;
}

bool
start_foreign_interaction_flow (Window parent)
{
  if (dialog_stack != NULL
      || interaction_flow_active)
    return false;

  g_assert (dialog_stack == NULL);

  fprintf (stderr, "START FOREIGN %p\n", parent);

  interaction_flow_active = true;
  parent_xid = parent;
  return true;
}

void
end_interaction_flow ()
{
  g_assert (interaction_flow_active);
  
  if (parent_xid == None)
    pop_dialog (GTK_WIDGET (get_main_window ()));

  interaction_flow_active = false;
  parent_xid = None;

  fprintf (stderr, "END\n");
}

void
present_main_window ()
{
  main_window_shown = true;
  gtk_widget_show_all (GTK_WIDGET (get_main_window ()));
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
yes_no_response (GtkDialog *dialog, gint response, gpointer clos)
{
  ayn_closure *c = (ayn_closure *)clos;
  
  if (response == 1)
    {
      if (c->pi)
	show_package_details (c->pi, c->kind, false, APTSTATE_DEFAULT);
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

  dialog = gtk_dialog_new_with_buttons
    (title,
     NULL,
     GTK_DIALOG_MODAL,
     _("ai_bd_confirm_ok"),      GTK_RESPONSE_OK,
     _("ai_bd_confirm_details"), 1,
     _("ai_bd_confirm_cancel"),  GTK_RESPONSE_CANCEL,
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
     _("ai_bd_confirm_ok"),      GTK_RESPONSE_OK,
     _("ai_bd_confirm_details"), 1,
     _("ai_bd_confirm_cancel"),  GTK_RESPONSE_CANCEL,
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
  void (*details) (void *data);
  void (*cont) (void *data);
  void *data;
};

static void
annoy_user_with_details_response (GtkDialog *dialog, gint response,
				  gpointer data)
{
  auwd_closure *c = (auwd_closure *)data;

  if (response == 1)
    {
      if (c->pi)
	show_package_details (c->pi, c->kind, true, APTSTATE_DEFAULT);
      else if (c->details)
	c->details (c->data);
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

static void
annoy_user_with_details_1 (const gchar *text,
			   void (*details) (void *data),
			   package_info *pi, detail_kind kind,
			   void (*cont) (void *data),
			   void *data)
{
  GtkWidget *dialog;
  auwd_closure *c = new auwd_closure;

  dialog = hildon_note_new_information (NULL, text);
  push_dialog (dialog);

  {
    // XXX - the buttons should be "Details" "Close", so we remove the
    //       "Ok" button from the information note and add our own ones.

    GtkWidget *button_box = GTK_DIALOG(dialog)->action_area;
    GList *kids = gtk_container_get_children (GTK_CONTAINER (button_box));
    if (kids)
      gtk_container_remove (GTK_CONTAINER (button_box),
			    GTK_WIDGET (kids->data));
    g_list_free (kids);

    gtk_dialog_add_button (GTK_DIALOG (dialog), _("ai_ni_bd_details"), 1);
    gtk_dialog_add_button (GTK_DIALOG (dialog), _("ai_ni_bd_close"),
			   GTK_RESPONSE_CANCEL);
  }

  if (pi)
    pi->ref ();
  c->pi = pi;
  c->kind = kind;
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
			     pi, kind,
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
			     NULL, no_details,
			     cont,
			     data);
}

struct auwe_closure {
  void (*func) (void *, void *);
  void *data1, *data2;
};

static void
annoy_user_with_log_response (GtkDialog *dialog, gint response,
			      gpointer data)
{
  pop_dialog (GTK_WIDGET (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (response == 1)
    show_log ();
}

void
annoy_user_with_log (const gchar *text)
{
  GtkWidget *dialog;

  dialog = hildon_note_new_information (NULL, text);
  push_dialog (dialog);
#if 0
  gtk_dialog_add_button (GTK_DIALOG (dialog), "Log", 1);
#endif

  g_signal_connect (dialog, "response", 
		    G_CALLBACK (annoy_user_with_log_response), NULL);
  gtk_widget_show_all (dialog);
}

void
annoy_user_with_errno (int err, const gchar *detail,
		       void (*cont) (void *), void *data)
{
  add_log ("%s: %s\n", detail, strerror (err));

  const char *msg;
  if (err == ENAMETOOLONG)
    msg = dgettext ("hildon-common-strings",
		    "file_ib_name_too_long");
  else if (err == EPERM || err == EACCES)
    msg = dgettext ("hildon-fm",
		    "sfil_ib_saving_not_allowed");
  else if (err == ENOENT)
    msg = dgettext ("hildon-common-strings",
		    "sfil_ni_cannot_continue_target_folder_deleted");
  else if (err == ENOSPC)
    msg = dgettext ("hildon-common-strings",
		    "sfil_ni_not_enough_memory");
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
      irritate_user (dgettext ("hildon-common-strings",
			       "file_ib_name_too_long"));
      cont (data);
    }
  else if (result == GNOME_VFS_ERROR_ACCESS_DENIED
	   || result == GNOME_VFS_ERROR_NOT_PERMITTED)
    {
      irritate_user (dgettext ("hildon-fm",
			       "sfil_ib_saving_not_allowed"));
      cont (data);
    }
  else if (result == GNOME_VFS_ERROR_NOT_FOUND)
    annoy_user (dgettext ("hildon-common-strings",
			  "sfil_ni_cannot_continue_target_folder_deleted"),
		cont, data);
  else if (result == GNOME_VFS_ERROR_NO_SPACE)
    annoy_user (dgettext ("hildon-common-strings",
			  "sfil_ni_not_enough_memory"),
		cont, data);
  else
    annoy_user (_("ai_ni_operation_failed"), cont, data);
}

void
irritate_user (const gchar *text)
{
  hildon_banner_show_information (GTK_WIDGET (NULL),
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

  dialog = gtk_dialog_new_with_buttons
    (_("ai_ti_notice"),
     NULL,
     GTK_DIALOG_MODAL,
     _("ai_bd_notice_ok"),      GTK_RESPONSE_OK,
     _("ai_bd_notice_cancel"),  GTK_RESPONSE_CANCEL,
     NULL);
  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  const char *text = (sure
		      ? _("ai_nc_non_verified_package")
		      : _("ai_nc_unsure_package"));

  GtkWidget *label = gtk_label_new (text);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), label);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (yes_no_response), c);
  gtk_widget_show_all (dialog);
}

#if 0

static GtkWidget *progress_dialog = NULL;
static bool progress_cancelled;
static GtkProgressBar *progress_bar;
static const gchar *general_title;
static apt_proto_operation current_status_operation = op_general;
static gint pulse_id = -1;
static int cancel_count;

static gboolean
pulse_progress (gpointer unused)
{
  if (progress_bar)
    gtk_progress_bar_pulse (progress_bar);
  return TRUE;
}

static void
start_pulsing ()
{
  if (pulse_id < 0)
    pulse_id = gtk_timeout_add (500, pulse_progress, NULL);
}

static void
stop_pulsing ()
{
  if (pulse_id >= 0)
    {
      g_source_remove (pulse_id);
      pulse_id = -1;
    }
}

static void
really_cancel_response (bool res, void *unused)
{
  gtk_main_quit ();
}

static void
cancel_response (GtkDialog *dialog, gint response, gpointer data)
{
  if (current_status_operation == op_downloading)
    {
      progress_cancelled = true;
      cancel_apt_worker ();
    }
  else
    {
      cancel_count++;
      if (cancel_count < 10)
	irritate_user (_("ai_ib_unable_cancel"));
      else
	ask_custom ("Cancelling now may harm your device.\n"
		    "The Application manager will be closed.",
		    "Cancel anyway", "Don't cancel",
		    really_cancel_response, NULL);
    }
}

bool
progress_was_cancelled ()
{
  return progress_cancelled;
}

void
reset_progress_was_cancelled ()
{
  progress_cancelled = false;
  cancel_count = 0;
}

static void
create_progress (const gchar *title, bool with_cancel)
{
  if (progress_dialog)
    {
      pop_dialog (GTK_WIDGET (progress_dialog));
      gtk_widget_destroy (progress_dialog);
      progress_dialog = NULL;
      progress_bar = NULL;
    }

  if (title == NULL)
    title = "";

  // XXX - we make the title slightly longer so that there is a bit
  //       room to grow without having to resize the dialog or risking
  //       ellipsization.  This only matters when the total download
  //       size changes during a download and it that case it would be
  //       better to resize the dialog, but that doesn't seem to
  //       happen...
  //
  //       The "XXXX" is never shown since we set a real title
  //       immediately.
  
  gchar *longer_title = g_strconcat (title, "XXX", NULL);
  progress_bar = GTK_PROGRESS_BAR (gtk_progress_bar_new ());
  progress_dialog =
    hildon_note_new_cancel_with_progress_bar (NULL,
					      longer_title,
					      progress_bar);
  push_dialog (progress_dialog);
  g_free (longer_title);
  g_object_set (progress_dialog, "description", title, NULL);

  if (!with_cancel)
    {
      GtkWidget *box = GTK_DIALOG (progress_dialog)->action_area;
      GList *kids = gtk_container_get_children (GTK_CONTAINER (box));
      if (kids)
	gtk_container_remove (GTK_CONTAINER (box), GTK_WIDGET (kids->data));
      g_list_free (kids);
    }
  else
    {
      respond_on_escape (GTK_DIALOG (progress_dialog), GTK_RESPONSE_CANCEL);
      g_signal_connect (progress_dialog, "response",
			G_CALLBACK (cancel_response), NULL);
    }

  gtk_widget_show (progress_dialog);
}

void
set_general_progress_title (const char *title)
{
  general_title = title;
}

void
show_progress (const char *title)
{
  create_progress (title, FALSE);

  set_general_progress_title (title);
  current_status_operation = op_general;
  set_progress (op_general, -1, 0);

  reset_progress_was_cancelled ();
}

void
set_progress (apt_proto_operation op, int already, int total)
{
  const char *title = NULL;

  // Determine if we need a new title

  if (op == op_downloading)
    {
      // The downloading title can change dynamically when the total
      // changes.

      static int last_total;
      static char *dynlabel = NULL;

      if (dynlabel == NULL
	  || total != last_total
	  || current_status_operation != op_downloading)
	{
	  char size_buf[20];
	  size_string_detailed (size_buf, 20, total);
	  g_free (dynlabel);
	  dynlabel = g_strdup_printf (_("ai_nw_downloading"), size_buf);
	  title = dynlabel;
	}
    }
  else if (op != current_status_operation)
    {
      // Otherwise the title only changes when the operation changes
      
      if (op == op_updating_cache)
	title = _("ai_nw_updating_list");
      else
	title = general_title;
    }

  // printf ("STATUS: %s -- %f\n", title, fraction);

  // We might need to change the progress title or we might need to
  // create the whole dialog here.
  //
  // XXX - The dialog will only be created when the new operation is
  // not op_updating_cache since apt-worker outputs progress messages
  // with that operation that we don't want to show (for example,
  // during startup).  The whole progress logic needs to be better
  // defined and apt-worker made more silent, etc.

  if (progress_dialog || op != op_updating_cache)
    {
      // 'title' is non-NULL when it needs to change.  Since the
      // dialog does not resize when its contents changes, we simply
      // recreate it, except when the operation is op_downloading.  In
      // that case, title changes are frequent and recreating the
      // dialog every time is annoying.  In that case, we simply set
      // the new description.  CREATE_PROGRESS takes care that there
      // is a bit of space for the title to grow.

      if (title
	  && op == op_downloading
	  && current_status_operation == op_downloading
	  && progress_dialog)
	{
	  g_object_set (progress_dialog, "description", title, NULL);
	}
      else if (title || progress_dialog == NULL)
	{
	  hide_progress ();
	  create_progress (title, op == op_downloading);
	}

      if (already >= 0)
	{
	  stop_pulsing ();
	  gtk_progress_bar_set_fraction (progress_bar,
					 ((double)already)/total);
	}
      else
	start_pulsing ();
    }

  current_status_operation = op;
}

void
hide_progress ()
{
  if (progress_dialog)
    {
      stop_pulsing ();
      pop_dialog (GTK_WIDGET (progress_dialog));
      gtk_widget_destroy (GTK_WIDGET(progress_bar));
      gtk_widget_destroy (progress_dialog);
      progress_dialog = NULL;
      progress_bar = NULL;
    }
  current_status_operation = op_general;
}

#endif

#if 0
void
show_progress (const char *)
{
}

void
hide_progress ()
{
}

void
set_progress (apt_proto_operation op, int already, int total)
{
}

void
set_general_progress_title (const char *title)
{
}

bool
progress_was_cancelled ()
{
  return false;
}

void
reset_progress_was_cancelled ()
{
}

#endif

/* Entertaining the user.
 */

struct entertainment_data {
  GtkWidget *bar, *dialog, *cancel_button;
  gint pulse_id;

  char *main_title, *sub_title;
  int64_t already, total;

  void (*cancel_callback) (void *);
  void *cancel_data;
  bool was_cancelled;
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
	  double fraction = (((double)entertainment.already)
			     / entertainment.total);

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
      char *title;
      if (entertainment.sub_title)
	title = entertainment.sub_title;
      else
	title = entertainment.main_title;
    
      g_object_set (entertainment.dialog, "description", title, NULL);
    }
}

static void
entertainment_response (GtkWidget *widget, int response, void *data)
{
  if (response == GTK_RESPONSE_CANCEL)
    cancel_entertainment ();
}

void
entertainment_insensitive_press (GtkWidget *widget, gpointer unused)
{
  irritate_user (_("Can't cancel now"));
}

void
start_entertaining_user ()
{
  g_return_if_fail (entertainment.dialog == NULL);

  entertainment.was_cancelled = false;

  entertainment.bar = gtk_progress_bar_new ();
  entertainment.dialog =
    hildon_note_new_cancel_with_progress_bar 
    (NULL,
     entertainment.main_title,
     GTK_PROGRESS_BAR (entertainment.bar));

  gtk_widget_set_usize (entertainment.dialog, 400, -1);

  {
    GtkWidget *box = GTK_DIALOG (entertainment.dialog)->action_area;
    GList *kids = gtk_container_get_children (GTK_CONTAINER (box));
    if (kids)
      {
	entertainment.cancel_button = GTK_WIDGET (kids->data);
	g_signal_connect (entertainment.cancel_button,
			  "insensitive-press",
			  G_CALLBACK (entertainment_insensitive_press),
			  &entertainment);
      }
    else
      entertainment.cancel_button = NULL;
    g_list_free (kids);
  }

  respond_on_escape (GTK_DIALOG (entertainment.dialog), GTK_RESPONSE_CANCEL);
  g_signal_connect (entertainment.dialog, "response",
		    G_CALLBACK (entertainment_response), &entertainment);

  entertainment_update_progress ();
  entertainment_update_cancel ();
  
  push_dialog (entertainment.dialog);
  gtk_widget_show (entertainment.dialog);
}

void
stop_entertaining_user ()
{
  g_return_if_fail (entertainment.dialog != NULL);

  entertainment_stop_pulsing ();

  pop_dialog (entertainment.dialog);
  gtk_widget_destroy (entertainment.dialog);
  
  entertainment.dialog = NULL;
  entertainment.bar = NULL;
  entertainment.cancel_button = NULL;
}

void
cancel_entertainment ()
{
  entertainment.was_cancelled = true;
  if (entertainment.cancel_callback)
    entertainment.cancel_callback (entertainment.cancel_data);
}

void
set_entertainment_title (const char *main_title)
{
  g_free (entertainment.main_title);
  entertainment.main_title = g_strdup (main_title);
  entertainment_update_title ();
}

void
set_entertainment_fun (const char *sub_title,
		       int64_t already, int64_t total)
{
  // fprintf (stderr, "FUN: %s %Ld %Ld\n", sub_title, already, total);
  
  if ((sub_title
       && (entertainment.sub_title == NULL
	   || strcmp (entertainment.sub_title, sub_title)))
      || (sub_title == NULL
	  && entertainment.sub_title != NULL))
    {
      g_free (entertainment.sub_title);
      entertainment.sub_title = g_strdup (sub_title);
      entertainment_update_title ();
    }

  entertainment.already = already;
  entertainment.total = total;
  entertainment_update_progress ();
}

void
set_entertainment_download_fun (int64_t already, int64_t total)
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

  set_entertainment_fun (sub_title, already, total);
}

void
set_entertainment_cancel (void (*callback) (void *data),
			  void *data)
{
  entertainment.cancel_callback = callback;
  entertainment.cancel_data = data;
  entertainment_update_cancel ();
}

bool
entertainment_was_cancelled ()
{
  return entertainment.was_cancelled;
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
    label = dgettext ("hildon-common-strings",
		      "ckdg_pb_updating");

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

static GdkPixbuf *default_icon = NULL;
static GdkPixbuf *broken_icon = NULL;

static void
global_icon_func (GtkTreeViewColumn *column,
		  GtkCellRenderer *cell,
		  GtkTreeModel *model,
		  GtkTreeIter *iter,
		  gpointer data)
{
  package_info *pi;
  gtk_tree_model_get (model, iter, 0, &pi, -1);
  if (!pi)
    return;

  if (default_icon == NULL)
    {
      GtkIconTheme *icon_theme;

      icon_theme = gtk_icon_theme_get_default ();
      default_icon = gtk_icon_theme_load_icon (icon_theme,
					       "qgn_list_gene_default_app",
					       26,
					       GtkIconLookupFlags(0),
					       NULL);
    }

  if (broken_icon == NULL)
    {
      GtkIconTheme *icon_theme;

      icon_theme = gtk_icon_theme_get_default ();
      broken_icon = gtk_icon_theme_load_icon (icon_theme,
					      "qgn_list_app_broken",
					      26,
					      GtkIconLookupFlags(0),
					      NULL);
    }

  GdkPixbuf *icon;
  if (global_installed)
    {
        if (pi->broken)
	  icon = broken_icon;
	else
	  icon = pi->installed_icon;
    }
  else
    icon = pi->available_icon;

  g_object_set (cell, "pixbuf", icon? icon : default_icon, NULL);
}

static void
global_name_func (GtkTreeViewColumn *column,
		  GtkCellRenderer *cell,
		  GtkTreeModel *model,
		  GtkTreeIter *iter,
		  gpointer data)
{
  GtkTreeView *tree = (GtkTreeView *)data;
  GtkTreeSelection *selection = gtk_tree_view_get_selection (tree);
  package_info *pi;

  gtk_tree_model_get (model, iter, 0, &pi, -1);
  if (!pi)
    return;

  const char *name = pi->get_display_name (global_installed);

  if (gtk_tree_selection_iter_is_selected (selection, iter))
    {
      const gchar *desc;
      gchar *markup;
      if (global_installed)
	desc = pi->installed_short_description;
      else
	{
	  desc = pi->available_short_description;
	  if (desc == NULL)
	    desc = pi->installed_short_description;
	}
      markup = g_markup_printf_escaped ("%s\n<small>%s</small>",
					name, desc);
      g_object_set (cell, "markup", markup, NULL);
      g_free (markup);
    }
  else
    g_object_set (cell, "text", name, NULL);
}

static void
emit_row_changed (GtkTreeModel *model, GtkTreeIter *iter)
{
  GtkTreePath *path;

  path = gtk_tree_model_get_path (model, iter);
  g_signal_emit_by_name (model, "row-changed", path, iter);
  gtk_tree_path_free (path);
}

static void
global_version_func (GtkTreeViewColumn *column,
		     GtkCellRenderer *cell,
		     GtkTreeModel *model,
		     GtkTreeIter *iter,
		     gpointer data)
{
  package_info *pi;
  gtk_tree_model_get (model, iter, 0, &pi, -1);
  if (!pi)
    return;

  g_object_set (cell, "text", (global_installed
			       ? pi->installed_version
			       : pi->available_version),
		NULL);
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
    size_string_general (buf, 20, pi->installed_size);
  else if (pi->have_info)
    size_string_general (buf, 20, pi->info.download_size);
  else
    strcpy (buf, "-");
  g_object_set (cell, "text", buf, NULL);
}

static bool global_have_last_selection;
static GtkTreeIter global_last_selection;
static GtkTreePath *global_target_path = NULL;
static package_info_callback *global_selection_callback;

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

  if (global_have_last_selection)
    emit_row_changed (GTK_TREE_MODEL (global_list_store),
		      &global_last_selection);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      package_info *pi;

      assert (model == GTK_TREE_MODEL (global_list_store));

      emit_row_changed (model, &iter);
      global_last_selection = iter;
      global_have_last_selection = true;

      reset_global_target_path ();
      global_target_path = gtk_tree_model_get_path (model, &iter);

      if (global_selection_callback)
	{
	  gtk_tree_model_get (model, &iter, 0, &pi, -1);
	  if (pi)
	    global_selection_callback (pi);
	}
    }
  else
    {
      if (global_selection_callback)
	global_selection_callback (NULL);
    }
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
      gtk_tree_view_get_cursor (GTK_TREE_VIEW (widget), &cursor_path, NULL);
        
      if (cursor_path) 
        {
          if (!gtk_tree_path_prev (cursor_path))
            {
              GList *children = NULL;

              children = 
                gtk_container_get_children (GTK_CONTAINER (get_main_trail()));

              if (children) 
                {
                  GList *last_child = g_list_last (children);

                  while (last_child && 
                         ((!GTK_WIDGET_CAN_FOCUS (last_child->data)) ||
                         (!GTK_WIDGET_IS_SENSITIVE (last_child->data))))
                    last_child = g_list_previous (last_child);

                  if (last_child)
                    gtk_widget_grab_focus (GTK_WIDGET (last_child->data));
                  
                  g_list_free (children);
                  gtk_tree_path_free(cursor_path);
                  return TRUE;
                }
            }

          gtk_tree_path_free(cursor_path);
        }
    
      break;
    }

  return FALSE;
}

GtkWidget *
make_global_package_list (GList *packages,
			  bool installed,
			  const char *empty_label,
			  const char *op_label,
			  package_info_callback *selected,
			  package_info_callback *activated)
{
  if (global_list_store == NULL)
    {
      global_list_store = gtk_list_store_new (1, GTK_TYPE_POINTER);
      g_object_ref (global_list_store);
    }

  if (packages == NULL)
    {
      GtkWidget *label = gtk_label_new (empty_label);
      gtk_misc_set_alignment (GTK_MISC (label), 0.5, 0.0);
      return label;
    }

  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *tree, *scroller;

  tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (global_list_store));

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree), TRUE);

  column = gtk_tree_view_column_new ();

  renderer = gtk_cell_renderer_pixbuf_new ();
  g_object_set (renderer, "yalign", 0.0, NULL);
  gtk_cell_renderer_set_fixed_size (renderer, 30, -1);
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_cell_data_func (column, renderer, global_icon_func, tree, NULL);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "yalign", 0.0, NULL);
  gtk_tree_view_column_pack_end (column, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (column, renderer, global_name_func, tree, NULL);

  gtk_tree_view_insert_column (GTK_TREE_VIEW (tree), column, -1);

  column = gtk_tree_view_get_column (GTK_TREE_VIEW (tree), 0);

  // Setting the sizing of this columne to FIXED but not specifying
  // the width is a workaround for some bug in GtkTreeView.  If we
  // don't do this, the name will not get ellipsized and the size
  // and/or version columns might disappear completely.  With this
  // workaround, the name gets properly elipsized.
  //
  gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_expand (column, TRUE);

  renderer = gtk_cell_renderer_text_new ();
  g_object_set (renderer, "yalign", 0.0, NULL);
  g_object_set (renderer, "xalign", 1.0, NULL);
  gtk_tree_view_insert_column_with_data_func (GTK_TREE_VIEW (tree),
					      -1,
					      _("ai_li_version"),
					      renderer,
					      global_version_func,
					      tree,
					      NULL);
  column = gtk_tree_view_get_column (GTK_TREE_VIEW (tree), 1);
  gtk_tree_view_column_set_alignment (column, 1.0);
  gtk_tree_view_column_set_expand (column, FALSE);

  {
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
  }

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

  GtkWidget *menu = create_package_menu (op_label);
  gtk_widget_show_all (menu);
  gtk_widget_tap_and_hold_setup (tree, menu, NULL,
				 GtkWidgetTapAndHoldFlags (0));

  set_global_package_list (packages, installed, selected, activated);

  grab_focus_on_map (tree);

  if (global_target_path)
    {
      GtkTreeIter iter;
      
      if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (global_list_store),
				    &iter, global_target_path))
	gtk_tree_path_prev (global_target_path);

      gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree),
				global_target_path,
				NULL, FALSE);
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

  GtkWidget *hbox = gtk_hbox_new (FALSE, 0);
  GtkWidget *vbox = gtk_vbox_new (FALSE, 5);
  GtkWidget *scroller;

  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 10);

  bool first_button = true;

  scroller = gtk_scrolled_window_new (NULL, NULL);

  for (GList *s = sections; s; s = s ->next)
    {
      section_info *si = (section_info *)s->data;
      GtkWidget *label = gtk_label_new (si->name);
      gtk_misc_set_padding (GTK_MISC (label), 15, 15);
      GtkWidget *btn = gtk_button_new ();
      gtk_container_add (GTK_CONTAINER (btn), label);
      gtk_box_pack_start (GTK_BOX (vbox), btn, FALSE, FALSE, 0);
      g_signal_connect (btn, "clicked",
			G_CALLBACK (section_clicked), si);
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
					 hbox);

  global_section_list = scroller;
  g_object_ref (scroller);

  return scroller;
}

void
clear_global_section_list ()
{
  if (global_section_list)
    {
      gtk_widget_destroy (global_section_list);
      g_object_unref (global_section_list);
    }
  global_section_list = NULL;
}

enum {
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
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_POINTER);

  for (node = package_list; node != NULL; node = g_list_next (node))
    {
      package_info *pi = (package_info *) node->data;
      GtkTreeIter iter;
      char package_size_str[20] = "";
      size_string_general (package_size_str, 20,
			   pi->info.install_user_size_delta);
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
			  COLUMN_SP_SELECTED, TRUE,
			  COLUMN_SP_NAME, pi->name,
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

void update_required_space_label (GtkTreeModel *model,
				  GtkTreePath *path, GtkTreeIter *i,
				  void *data)
{
  gboolean has_iter = FALSE;
  int64_t acc_size = 0;
  GtkWidget *label = NULL;
  GtkTreeIter iter;

  has_iter = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(model), &iter);
  label = GTK_WIDGET(data);

  while (has_iter)
    {
      gboolean selected = FALSE;
      package_info *pi = NULL;

      gtk_tree_model_get (GTK_TREE_MODEL(model), &iter,
			  COLUMN_SP_SELECTED, &selected,
			  COLUMN_SP_PACKAGE_INFO, &pi,
			  -1);
      if (selected)
	acc_size += pi->info.install_user_size_delta;
      has_iter = gtk_tree_model_iter_next (GTK_TREE_MODEL(model), &iter);
    }

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
  GtkListStore *list_store;
  GtkWidget *tree_view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkWidget *message_label;
  GtkWidget *required_space_label;
  int64_t total_size;

  dialog = gtk_dialog_new_with_buttons (c->title,
					NULL,
					GTK_DIALOG_MODAL,
					_("ai_bd_ok"),     GTK_RESPONSE_OK,
					_("ai_bd_cancel"), GTK_RESPONSE_CANCEL,
					NULL);
  push_dialog (dialog);

  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
  list_store = make_select_package_list_store (c->package_list, &total_size);

  /* Set the message dialog */
  message_label = gtk_label_new (c->question);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), message_label);
  
  /* Add required space label */
  required_space_label = gtk_label_new (NULL);
  gtk_box_pack_end_defaults (GTK_BOX(GTK_DIALOG(dialog)->vbox), required_space_label);
  fill_required_space_label (required_space_label, total_size);

  /* Set up treeview */
  tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (list_store));
  g_object_set (G_OBJECT(tree_view), "allow-checkbox-mode", FALSE, NULL);
  renderer = gtk_cell_renderer_toggle_new ();
  gtk_cell_renderer_toggle_set_radio (GTK_CELL_RENDERER_TOGGLE (renderer), FALSE);
  g_signal_connect (G_OBJECT (renderer), "toggled", 
		    G_CALLBACK (package_selected_toggled_callback), tree_view);
  column = gtk_tree_view_column_new_with_attributes ("Marked", renderer, 
						     "active", COLUMN_SP_SELECTED, 
						     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
			       column);
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Name", renderer, 
						     "text", COLUMN_SP_NAME, 
						     NULL);
  gtk_tree_view_column_set_expand(column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
			       column);
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Size", renderer, 
						     "text", COLUMN_SP_SIZE, 
						     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view),
			       column);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), tree_view);
  
  /* Add separator */
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox),
		     gtk_hseparator_new ());

  c->list_store = list_store;

  g_signal_connect (list_store, "row-changed",
		    G_CALLBACK (update_required_space_label),
		    required_space_label);
  g_signal_connect (dialog, "response", 
		    G_CALLBACK (select_package_list_response), 
		    c);

  gtk_widget_set_usize (dialog, 600, 320);
  gtk_widget_show_all (dialog);
}

void
select_package_list (GList *package_list,
		     int state,
		     const gchar *title,
		     const gchar *question,
		     void (*cont) (gboolean res, GList *pl,
				   void *data),
		     void *data)
{
  spl_closure *closure = new spl_closure;

  closure->package_list = package_list;
  closure->title = g_strdup (title);
  closure->question = g_strdup (question);
  closure->cont = cont;
  closure->data = data;

  get_intermediate_package_list_info (package_list,
				      false,
				      select_package_list_with_info,
				      closure,
				      state);
}

#define KILO 1000

void
size_string_general (char *buf, size_t n, int64_t bytes)
{
  uint64_t num = bytes;

  if (num == 0)
    snprintf (buf, n, _("ai_li_size_max_99kb"), 0);
  else if (num < 1*KILO)
    snprintf (buf, n, _("ai_li_size_max_99kb"), 1);
  else
    {
      // round to nearest KILO
      // bytes ~ num * KILO
      num = (bytes + KILO/2) / KILO;
      if (num < 100)
	snprintf (buf, n, _("ai_li_size_max_99kb"), (int)num);
      else
	{
	  // round to nearest 100 KILO
	  // bytes ~ num * 100 * KILO
	  num = (bytes + 50*KILO) / (100*KILO);
	  if (num < 100)
	    snprintf (buf, n, _("ai_li_size_100kb_10mb"), num/10.0);
	  else
	    {
	      // round to nearest KILO KILO
	      // bytes ~ num * KILO * KILO
	      num = (bytes + KILO*KILO/2) / (KILO*KILO);
	      if (num < KILO)
		snprintf (buf, n, _("ai_li_size_10mb_1gb"), (int)num);
	      else
		snprintf (buf, n, _("ai_li_size_larger_than_1gb"),
			  ((float)num)/KILO);
	    }
	}
    }
}

void
size_string_detailed (char *buf, size_t n, int64_t bytes)
{
  uint64_t num = bytes;

  if (num == 0)
    snprintf (buf, n, _("ai_li_de_size_max_999kb"), 0);
  else if (num < 1*KILO)
    snprintf (buf, n, _("ai_li_de_size_max_999kb"), 1);
  else
    {
      // round to nearest KILO
      // bytes ~ num * KILO
      num = (bytes + KILO/2) / KILO;
      if (num < 1000)
	snprintf (buf, n, _("ai_li_de_size_max_999kb"), (int)num);
      else
	{
	  // round to nearest 10 KILO
	  // bytes ~ num * 10 * KILO
	  num = (bytes + 5*KILO) / (10*KILO);
	  if (num < 1000)
	    snprintf (buf, n, _("ai_li_de_size_1mb_10mb"), num/100.0);
	  else
	    {
	      if (num < 100000)
		snprintf (buf, n, _("ai_li_de_size_10mb_1gb"), num/100.0);
	      else
		snprintf (buf, n, _("ai_li_de_size_larger_than_1gb"),
			  ((float)num)/(100*KILO));
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
    if (cont)
      cont (uri, data);
  else
    g_free (uri);
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
  const char *home = getenv ("HOME");
  if (home)
    {
      char *folder = g_strdup_printf ("%s/MyDocs/.documents", home);
      gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (fcd), folder);
      g_free (folder);
    }

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
	  annoy_user (dgettext ("hildon-common-strings",
				"sfil_ni_cannot_open_no_connection"),
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
    set_entertainment_download_fun (info->bytes_copied, info->file_size);

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

  set_entertainment_fun (NULL, -1, 0);
  set_entertainment_cancel (cancel_copy, NULL);
  set_entertainment_title (dgettext ("hildon-fm",
				     "docm_nw_opening_file"));
  
  start_entertaining_user ();

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
      if (error->domain != G_SPAWN_ERROR 
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
      cancel_entertainment ();
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
      set_entertainment_fun (_("ai_nw_connecting"), -1, 0);
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

static char *btname = NULL;

const char *
device_name ()
{
  if (btname != NULL)
    return btname;
  else
    return dgettext ("hildon-fm", "sfil_li_folder_root");
}

static void
set_bt_name_from_message (DBusMessage *message)
{
  DBusMessageIter iter;
  const char *name = NULL;
  GtkWidget *label = NULL;

  g_return_if_fail (message != NULL);

  if (!dbus_message_iter_init (message, &iter))
    {
      add_log ("message did not have argument\n");
      return;
    }
  dbus_message_iter_get_basic (&iter, &name);

  if (btname) 
    g_free (btname);

  btname = g_strdup (name);

  label = get_device_label ();

  if (label)
    gtk_label_set_text (GTK_LABEL (label), btname);
}

static void 
btname_received(DBusPendingCall *call, void *user_data)
{
  DBusMessage *message;
  DBusError error;

  g_assert (dbus_pending_call_get_completed (call));
  message = dbus_pending_call_steal_reply (call);
  if (message == NULL)
    {
      add_log ("no reply\n");
      return;
    }

  dbus_error_init (&error);

  if (dbus_set_error_from_message (&error, message))
    {
      add_log ("get btname: %s\n", error.message);
      dbus_error_free (&error);
    }
  else   
    set_bt_name_from_message (message);

  dbus_message_unref (message);
}

static DBusHandlerResult
handle_dbus_signal (DBusConnection *conn,
		    DBusMessage *msg,
		    gpointer data)
{
  if (dbus_message_is_signal(msg, BTNAME_SIGNAL_IF, BTNAME_SIG_CHANGED))
    set_bt_name_from_message(msg);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void 
setup_dbus ()
{
  static DBusConnection *conn = NULL;
  DBusMessage *request;
  DBusError error;
  DBusPendingCall *call = NULL;

  dbus_error_init (&error);
  if (conn == NULL)
    {
      conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
      if (conn == NULL)
        {
          add_log ("%s: %s\n", error.name, error.message);
	  dbus_error_free (&error);
          return;
        }
    }

  /* Let's query initial state.  These calls are async, so they do not
     consume too much startup time.
   */
  request = dbus_message_new_method_call (BTNAME_SERVICE,
					  BTNAME_REQUEST_PATH,
					  BTNAME_REQUEST_IF,
					  BTNAME_REQ_GET);
  if (request == NULL)
    {
      add_log ("dbus_message_new_method_call failed\n");
      return;
    }
  dbus_message_set_auto_start (request, TRUE);

  if (dbus_connection_send_with_reply (conn, request, &call, -1))
    {
      dbus_pending_call_set_notify (call, btname_received, NULL, NULL);
      dbus_pending_call_unref (call);
    }

  dbus_message_unref (request);

  dbus_connection_setup_with_g_main (conn, NULL);
  dbus_bus_add_match (conn, BTNAME_MATCH_RULE, &error);
  if (dbus_error_is_set(&error))
    {
      add_log ("dbus_bus_add_match failed: %s\n", error.message);
      dbus_error_free (&error);
    }

  if (!dbus_connection_add_filter(conn, handle_dbus_signal, NULL, NULL))
    add_log ("dbus_connection_add_filter failed\n");
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
  struct statvfs buf;

  if (statvfs("/", &buf) != 0)
    return -1;
  
  int64_t res = (int64_t)buf.f_bfree * (int64_t)buf.f_bsize;
  printf ("FREE: %Ld\n", res);
  return res;
}

/* D-Bus
 */

struct dip_clos {
  DBusConnection *conn;
  DBusMessage *message;
};

static void dbus_install_package (DBusConnection *conn, DBusMessage *message);
static void dip_install_done (void *data);
static void dip_end (bool success, void *data);

static void
dbus_install_package (DBusConnection *conn, DBusMessage *message)
{
  DBusError error;
  
  dbus_int32_t xid;
  char *package;
  
  dbus_error_init (&error);
  if (dbus_message_get_args (message, &error,
			     DBUS_TYPE_INT32, &xid,
			     DBUS_TYPE_STRING, &package,
			     DBUS_TYPE_INVALID))
    {
      dip_clos *c = new dip_clos;
      
      dbus_connection_ref (conn);
      c->conn = conn;
      dbus_message_ref (message);
      c->message = message;
      
      if (xid)
	{
	  if (start_foreign_interaction_flow (xid))
	    install_named_package (APTSTATE_DEFAULT, package,
				   dip_install_done, c);
	  else
	    dip_end (false, c);
	}
      else
	{
	  present_main_window ();
	  if (start_interaction_flow ())
	    install_named_package (APTSTATE_DEFAULT, package,
				   dip_install_done, c);
	  else
	    dip_end (false, c);
	}
    }
  else
    {
      DBusMessage *reply;
      reply = dbus_message_new_error (message,
				      DBUS_ERROR_INVALID_ARGS,
				      error.message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);
    }
}

static void
dip_install_done (void *data)
{
  dip_clos *c = (dip_clos *)data;

  end_interaction_flow ();

  // XXX - too optimistic
  dip_end (true, c);
}

static void
dip_end (bool success, void *data)
{
  dip_clos *c = (dip_clos *)data;

  DBusMessage *reply;
  dbus_int32_t result = success;
	  
  reply = dbus_message_new_method_return (c->message);
  dbus_message_append_args (reply,
			    DBUS_TYPE_INT32, &result,
			    DBUS_TYPE_INVALID);
  
  dbus_connection_send (c->conn, reply, NULL);
  dbus_message_unref (reply);

  // So that we don't lose the reply when we exit below.
  dbus_connection_flush (c->conn);

  dbus_message_unref (c->message);
  dbus_connection_unref (c->conn);
  delete c;

  maybe_exit ();
}

static DBusHandlerResult 
dbus_handler (DBusConnection *conn, DBusMessage *message, void *data)
{
  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "xxx_install_package"))
    {
      dbus_install_package (conn, message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

  if (dbus_message_is_method_call (message,
				   "com.nokia.hildon_application_manager",
				   "show"))
    {
      DBusMessage *reply;

      fprintf (stderr, "SHOW\n");

      present_main_window ();

      reply = dbus_message_new_method_return (message);
      dbus_connection_send (conn, reply, NULL);
      dbus_message_unref (reply);

      return DBUS_HANDLER_RESULT_HANDLED;
    }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void
init_dbus_handlers ()
{
  DBusError error;
  DBusConnection *connection;

  dbus_error_init (&error);
  connection = dbus_bus_get (DBUS_BUS_SESSION, &error);
  if (connection == NULL)
    {
      add_log ("Can't get session dbus: %s", error.message);
      what_the_fock_p ();
      return;
    }

  dbus_connection_setup_with_g_main (connection, NULL);

  if (!dbus_connection_add_filter (connection, dbus_handler, NULL, NULL))
    {
      add_log ("Can't add dbus filter");
      what_the_fock_p ();
      return;
    }

  dbus_error_init (&error);
  if (dbus_bus_request_name (connection,
			     "com.nokia.hildon_application_manager", 0,
			     &error) < 0)
    {
      fprintf (stderr, "Can't request name on dbus: %s\n", error.message);
      what_the_fock_p ();
      return;
    }
}
