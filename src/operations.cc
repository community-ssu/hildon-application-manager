/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007 Nokia Corporation.  All Rights reserved.
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
#include <hildon/hildon-note.h>

#include "operations.h"
#include "util.h"
#include "main.h"
#include "apt-worker-client.h"
#include "log.h"
#include "settings.h"
#include "details.h"

#define _(x) gettext (x)

/* Common utilities
 */

static char *
result_code_to_message (apt_proto_result_code result_code, bool upgrading)
{
  const char *msg = NULL;

  if (result_code == rescode_download_failed)
    msg = _("ai_ni_error_download_failed");
  else if (result_code == rescode_packages_not_found)
    msg = _("ai_ni_error_download_missing");
  else if (result_code == rescode_package_corrupted)
    {
      if (upgrading)
	msg = _("ai_ni_error_update_corrupted");
      else
	msg = _("ai_ni_error_install_corrupted");
    }
  else if (result_code == rescode_out_of_space)
    msg = dgettext ("hildon-common-strings", "sfil_ni_not_enough_memory");

  return g_strdup (msg);
}

void
installable_status_to_message (package_info *pi,
			       char *&msg, bool &with_details)
{
  const char *smsg = NULL;

  if (pi->info.installable_status == status_missing)
    {
      smsg = (pi->installed_version
	      ? _("ai_ni_error_update_missing")
	      : _("ai_ni_error_install_missing"));
      with_details = true;
    }
  else if (pi->info.installable_status == status_conflicting)
    {
      smsg = (pi->installed_version
	      ? _("ai_ni_error_update_conflict")
	      : _("ai_ni_error_install_conflict"));
      with_details = true;
    }
  else if (pi->info.installable_status == status_corrupted)
    {
      smsg = (pi->installed_version
	      ? _("ai_ni_error_update_corrupted")
	      : _("ai_ni_error_install_corrupted"));
      with_details = false;
    }
  else if (pi->info.installable_status == status_incompatible)
    {
      smsg = (pi->installed_version
	      ? _("ai_ni_error_update_incompatible")
	      : _("ai_ni_error_install_incompatible"));
      with_details = false;
    }
  else if (pi->info.installable_status == status_incompatible_current)
    {
      smsg = _("ai_ni_error_n770package_incompatible");
      with_details = false;
    }
  else
    {
      msg = g_strdup_printf ((pi->installed_version
			      ? _("ai_ni_error_update_failed")
			      : _("ai_ni_error_installation_failed")),
			     pi->get_display_name (false));
      with_details = true;
    }

  if (smsg)
    msg = g_strdup (smsg);
}

/* INSTALL_PACKAGES - Overview

   XXX - system updates should be handled specially: they should be
   installed first with their own special confirmation dialog, and
   then the rest of the packages should be processed.

   0. Filter out already installed packages.  When the list is empty
      after this, an appropriate note is shown and the process aborts.

   1. Confirm packages.  When this is a 'restore' or 'card' flow, the
      multi-package selection dialog is used, otherwise all packages
      except the first are ignored and a single package confirmation
      dialog is used.

   2. Make sure that the network is up when this is not a card install.

   3. Check for the trust status of all installed packages.  For each
      of the selected packages, a 'check_install' operation is
      performed and when one of them would install packages from a
      non-trusted source, the Notice dialog is shown.  Also, when a
      package would be upgraded from a different 'trust domain' than
      it was originally installed from, the operation is aborted.

   The following is repeated for each selected package, as indicated.
   "Aborting this package" means that an error message is shown and
   when there is another package to install, the user is asked whether
   to continue or not.

   4. Check if the package is actually installable, and abort it when
      not.

   xxx (download location)
   5. Check if enough storage is available and decide where to
      download the packages to.

   XXX
   6. Download the packages.

   XXX
   7. If the package has the 'suggest-backup' flag, suggest a backup
      to be taken.

   XXX
   8. Check the free storage again.

   xxx (actually closing all apps)
   9. If the package doesn't have the 'close-apps' flag, run the
      'checkrm' scripts of the upgraded packages and abort this package
      if the scripts asks for it.  Otherwise close all applications.

  10. Do the actual install, aborting this package if it fails.  The
      downloaded archive files are removed in any case.

  11. If there are more packages to install, go back to 3.

   At the end:

   xxx (actually rebooting)
  12. If any of the packages had the 'reboot' flag, reboot here and now.

  13. Refresh the lists of packages.
*/

struct ip_clos {
  int install_type;
  int state;
  bool automatic;

  GList *all_packages;   // all packages given to install_packages
  GList *packages;       // the ones that are not installed or uptodate
  GList *cur;            // the one currently under consideration

  // per installation iteration
  int flags;
  int64_t free_space;       // the required free storage space in bytes
  GSList *upgrade_names;    // the packages and versions that we are going
  GSList *upgrade_versions; // to upgrade to.

  // at the end
  bool entertaining;        // is the progress bar up?
  int n_successful;         // how many have been installed successfully
  bool reboot;              // whether to reboot
  
  void (*cont) (int n_successful, void *);
  void *data;
};

static void ip_confirm_install_response (bool res, void *data);
static void ip_select_package_response (gboolean res, GList *selected_packages,
					void *data);
static void ip_ensure_network (ip_clos *c);
static void ip_ensure_network_reply (bool res, void *data);
static void ip_check_cert_start (ip_clos *c);
static void ip_check_cert_loop (ip_clos *c);
static void ip_check_cert_reply (int cmd, apt_proto_decoder *dec,
				 void *data);
static void ip_legalese_response (bool res, void *data);

static void ip_install_start (ip_clos *c);
static void ip_install_loop (ip_clos *c);
static void ip_install_with_info (package_info *pi, void *data, bool changed);
static void ip_check_upgrade_reply (int cmd, apt_proto_decoder *dec,
				    void *data);
static void ip_check_upgrade_loop (ip_clos *c);
static void ip_check_upgrade_cmd_done (int status, void *data);
static void ip_install_cur (void *data);
static void ip_install_cur_reply (int cmd, apt_proto_decoder *dec, void *data);
static void ip_clean_reply (int cmd, apt_proto_decoder *dec, void *data);
static void ip_install_next (void *data);

static void ip_show_cur_details (void *data);
static void ip_show_cur_problem_details (void *data);

static void ip_abort_cur (ip_clos *c, const char *msg, bool with_details);
static void ip_abort_cur_with_status_details (ip_clos *c);
static void ip_abort_response (GtkDialog *dialog, gint response,
			       gpointer data);

static void ip_end (void *data);
static void ip_end_after_reboot (void *data);

void
install_package (package_info *pi,
		 void (*cont) (int n_successful, void *), void *data)
{
  install_packages (g_list_prepend (NULL, pi),
		    APTSTATE_DEFAULT, INSTALL_TYPE_STANDARD, false,
		    NULL, NULL,
		    cont, data);
}

void
install_packages (GList *packages,
		  int state, int install_type,
		  bool automatic,
		  const char *title, const char *desc,
		  void (*cont) (int n_successful, void *), void *data)
{
  ip_clos *c = new ip_clos;

  c->install_type = install_type;
  c->state = state;
  c->automatic = automatic;
  c->all_packages = packages;
  c->cont = cont;
  c->data = data;
  c->upgrade_names = NULL;
  c->upgrade_versions = NULL;
  c->n_successful = 0;
  c->reboot = false;
  c->entertaining = false;

  // Filter packages, stopping after the first when this is a standard
  // install.

  c->packages = NULL;
  for (GList *p = c->all_packages; p; p = p->next)
    {
      package_info *pi = (package_info *)p->data;
      if (pi->available_version != NULL)
	{
	  c->packages = g_list_append (c->packages, pi);
	  if (c->install_type == INSTALL_TYPE_STANDARD)
	    break;
	}
    }

  if (c->packages == NULL)
    {
      annoy_user (_("ai_ni_all_installed"), ip_end, c);
      return;
    }

  // Bring up the appropriate confirmation dialog.

  if (c->install_type == INSTALL_TYPE_BACKUP
      || c->install_type == INSTALL_TYPE_MEMORY_CARD
      || c->install_type == INSTALL_TYPE_MULTI)
    {
      if (title == NULL)
	{
	  switch (c->install_type)
	    {
	    case INSTALL_TYPE_BACKUP:
	      title = _("ai_ti_restore");
	      break;
	    case INSTALL_TYPE_MEMORY_CARD:
	      title = _("ai_ti_memory");
	      break;
	    default:
	      title = _("Install");
	      break;
	    }
	}
      
      if (desc == NULL)
	desc = _("ai_ti_install_apps");

      select_package_list (c->packages,
			   c->state,
			   title, desc,
			   ip_select_package_response, c);
    }
  else if (c->install_type != INSTALL_TYPE_UPDATE_SYSTEM)
    {
      GString *text = g_string_new ("");
      char download_buf[20];
      package_info *pi;

      c->cur = c->packages;
      pi = (package_info *)(c->cur->data);

      size_string_general (download_buf, 20, pi->info.download_size);

      g_string_printf (text,
		       (pi->installed_version
			? _("ai_nc_update")
			: _("ai_nc_install")),
		       pi->get_display_name (false),
		       pi->available_version, download_buf);
  
      ask_yes_no_with_arbitrary_details ((pi->installed_version
					  ? _("ai_ti_confirm_update")
					  : _("ai_ti_confirm_install")),
					 text->str,
					 ip_confirm_install_response,
					 ip_show_cur_details, c);
      g_string_free (text, 1);
    }
  else
    ip_confirm_install_response (true, c);
}

static void
ip_show_cur_details (void *data)
{
  ip_clos *c = (ip_clos *)data;
  package_info *pi = (package_info *)(c->cur->data);
  
  show_package_details (pi, install_details, false, c->state);
}

static void
ip_show_cur_problem_details (void *data)
{
  ip_clos *c = (ip_clos *)data;
  package_info *pi = (package_info *)(c->cur->data);
  
  show_package_details (pi, install_details, true, c->state);
}

static void
ip_select_package_response (gboolean res, GList *selected_packages,
			    void *data)
{
  ip_clos *c = (ip_clos *)data;

  if (!res)
    {
      if (c->install_type == INSTALL_TYPE_MEMORY_CARD
	  && c->automatic)
	annoy_user (_("ai_ni_memory_cancelled"),
		    ip_end, c);
      else
	ip_end (c);
    }
  else if (selected_packages == NULL)
    {
      ip_end (c);
    }
  else
    {
      g_list_free (c->packages);
      c->packages = selected_packages;
      ip_ensure_network (c);
    }
}

static void
ip_confirm_install_response (bool res, void *data)
{
  ip_clos *c = (ip_clos *)data;

  if (res)
    ip_ensure_network (c);
  else
    ip_end (c);
}

static void
ip_ensure_network (ip_clos *c)
{
  /* Start entertaining the user here.  We stop in ip_end, at the
     last.
   */

  set_entertainment_fun (NULL, -1, 0);
  set_entertainment_cancel (NULL, NULL);
  set_entertainment_title (_("Installing"));
  start_entertaining_user ();
  
  c->entertaining = true;

  if (c->install_type != INSTALL_TYPE_MEMORY_CARD)
    ensure_network (ip_ensure_network_reply, c);
  else
    ip_check_cert_start (c);
}

static void
ip_ensure_network_reply (bool res, void *data)
{
  ip_clos *c = (ip_clos *)data;

  if (res)
    ip_check_cert_start (c);
  else
    ip_end (c);
}

static void
ip_check_cert_start (ip_clos *c)
{
  c->cur = c->packages;
  ip_check_cert_loop (c);
}

static void
ip_check_cert_loop (ip_clos *c)
{
  if (c->cur)
    {
      package_info *pi = (package_info *)c->cur->data;
      printf ("CHECK CERT: %s\n", pi->name);
      apt_worker_install_check (c->state, pi->name,
				ip_check_cert_reply, c);
    }
  else
    {
      /* All packages passed the check.  How unusual.
       */
      ip_install_start (c);
    }
}

static void
ip_check_cert_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  ip_clos *c = (ip_clos *)data;

  if (dec == NULL)
    {
      ip_end (c);
      return;
    }

  /* XXX - any exceptional package causes us to show the 'not-so-sure'
           version of the dialog.  This needs to be rethought once
           apt-worker provides more details about the trust.
  */

  bool some_domains_violated = false;
  bool some_not_certfied = false;

  while (!dec->corrupted ())
    {
      apt_proto_pkgtrust trust = apt_proto_pkgtrust (dec->decode_int ());
      if (trust == pkgtrust_end)
	break;

      if (trust == pkgtrust_not_certified)
	some_not_certfied = true;
      if (trust == pkgtrust_domains_violated)
	some_domains_violated = true;

      dec->decode_string_in_place ();  // name
    }

  // XXX - L10N

  if (some_domains_violated)
    {
      if (red_pill_mode)
	ask_custom (_("Trusted upgrade path broken"),
		    "Continue anyway", "Stop",
		    ip_legalese_response, c);
      else
	annoy_user (_("Trusted upgrade path broken"),
		    ip_end, c);
    }
  else if (some_not_certfied)
    scare_user_with_legalese (false, ip_legalese_response, c);
  else
    {
      c->cur = c->cur->next;
      ip_check_cert_loop (c);
    }

  /* We ignore the rest of the reply, including the success
     indication and everything.
   */
}

static void
ip_legalese_response (bool res, void *data)
{
  ip_clos *c = (ip_clos *)data;

  if (res)
    {
      /* User agrees to take the risk.  Let's start the show!
       */
      ip_install_start (c);
    }
  else
    ip_end (c);
}

static void
ip_install_start (ip_clos *c)
{
  c->cur = c->packages;
  ip_install_loop (c);
}


static void
ip_install_loop (ip_clos *c)
{
  if (c->cur == NULL)
    {
      /* End of loop, show a success report to the user.

         If there is only one package in the list, talk specifically
         about that package.  Otherwise, just show the number of
         packages that have been successfully handled.
       */

      stop_entertaining_user ();
      c->entertaining = false;

      if (c->n_successful > 0)
	{
	  if (c->all_packages->next == NULL)
	    {
	      package_info *pi = (package_info *)c->all_packages->data;
	      char *str = g_strdup_printf ((pi->installed_version != NULL
					    ? _("ai_ni_update_successful")
					    : _("ai_ni_install_successful")),
					   pi->get_display_name (false));
	      annoy_user (str, ip_end, c);
	      g_free (str);
	    }
	  else
	    {
	      char *str =
		g_strdup_printf (ngettext ("ai_ni_multiple_install",
					   "ai_ni_multiple_installs", 
					   c->n_successful),
				 c->n_successful);
	      annoy_user (str, ip_end, c);
	      g_free (str);
	    }
	}
      else
	ip_end (c);
    }
  else
    {
      package_info *pi = (package_info *)(c->cur->data);
      get_intermediate_package_info (pi, true,
				     ip_install_with_info, c, c->state);
    }
}

static void
ip_install_with_info (package_info *pi, void *data, bool changed)
{
  ip_clos *c = (ip_clos *)data;
  
  if (pi->info.installable_status == status_able)
    {
      add_log ("-----\n");
      if (pi->installed_version)
	add_log ("Upgrading %s %s to %s\n", pi->name,
		 pi->installed_version, pi->available_version);
      else
	add_log ("Installing %s %s\n", pi->name, pi->available_version);

      int64_t free_space = get_free_space ();
      if (free_space < 0)
	annoy_user_with_errno (errno, "get_free_space",
			       ip_end, c);

      /* XXX - What is the right thing to do when a packages that asks
    	       for a reboot fails during installation?  Is it better
    	       to reboot or not?
      */

      if (pi->info.install_flags & pkgflag_reboot)
	c->reboot = true;

      if (pi->info.required_free_space + pi->info.download_size >= free_space)
	{
	  char free_string[20];
	  char required_string[20];
	  size_string_detailed (free_string, 20, free_space);
	  size_string_detailed (required_string, 20,
				pi->info.required_free_space
				+ pi->info.download_size);

	  char *msg = g_strdup_printf ("Not enough storage\n%s < %s",
				       free_string, required_string);

	  ip_abort_cur (c, msg, false);
	}
      else if (pi->info.install_flags & pkgflag_close_apps)
	{
	  // XXX - L10N
	  annoy_user (_("You should close all Applications now."),
		      ip_install_cur, c);
	}
      else
	{
	  apt_worker_install_check (c->state, pi->name,
				    ip_check_upgrade_reply, c);
	}
    }
  else
    ip_abort_cur_with_status_details (c);
}

static void
ip_check_upgrade_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  ip_clos *c = (ip_clos *)data;

  c->upgrade_names = NULL;
  c->upgrade_versions = NULL;

  if (dec == NULL)
    {
      ip_end (c);
      return;
    }

  /* Skip the cert information of the reply.
   */

  while (!dec->corrupted ())
    {
      apt_proto_pkgtrust trust = apt_proto_pkgtrust (dec->decode_int ());
      if (trust == pkgtrust_end)
	break;
      
      dec->decode_string_in_place ();  // name
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

  {
    package_info *pi = (package_info *)(c->cur->data);
    printf ("FLAGS: %x\n", pi->info.install_flags);
    printf ("SPACE: %lu\n", pi->info.required_free_space);
  }

  if (success)
    ip_check_upgrade_loop (c);
  else
    annoy_user (_("ai_ni_operation_failed"), ip_end, c);
}

static void
ip_check_upgrade_loop (ip_clos *c)
{
  if (c->upgrade_names)
    {
      char *name = (char *)(c->upgrade_names->data);
      char *version = (char *)(c->upgrade_versions->data);

      printf ("CHECKRM %s %s\n", name, version);

      char *cmd =
	g_strdup_printf ("/var/lib/hildon-application-manager/info/%s.checkrm",
			 name);
      
      char *argv[] = { cmd, "upgrade", version, NULL };
      run_cmd (argv, ip_check_upgrade_cmd_done, c);

      g_free (cmd);
    }
  else
    ip_install_cur (c);
}

static void
clear (GSList *&lst)
{
  while (lst)
    g_free (pop (lst));
}
  
static void
ip_check_upgrade_cmd_done (int status, void *data)
{
  ip_clos *c = (ip_clos *)data;

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 111)
    {
      /* XXX - find better package name to use in error message.
       */
      char *str =
	g_strdup_printf (_("ai_ni_error_uninstall_applicationrunning"),
			 (char *)(c->upgrade_names->data));

      clear (c->upgrade_names);
      clear (c->upgrade_versions);

      ip_abort_cur (c, str, false);
      g_free (str);
    }
  else
    {
      g_free (pop (c->upgrade_names));
      g_free (pop (c->upgrade_versions));
      ip_check_upgrade_loop (c);
    }
}

static void
ip_install_cur (void *data)
{
  ip_clos *c = (ip_clos *)data;
  package_info *pi = (package_info *)(c->cur->data);

  printf ("INSTALL %s\n", pi->name);

  char *title = g_strdup_printf (pi->installed_version
				 ? _("ai_nw_updating")
				 : _("ai_nw_installing"),
				 pi->get_display_name (false));
  set_entertainment_title (title);
  set_entertainment_fun (NULL, -1, 0);
  g_free (title);

  set_log_start ();
  apt_worker_install_package (c->state, pi->name,
			      pi->installed_version != NULL,
			      ip_install_cur_reply, c);
}

static void
ip_install_cur_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  ip_clos *c = (ip_clos *)data;
  package_info *pi = (package_info *)(c->cur->data);

  if (dec == NULL)
    {
      ip_end (c);
      return;
    }

  apt_proto_result_code result_code =
    apt_proto_result_code (dec->decode_int ());

  if (clean_after_install)
    apt_worker_clean (c->state, ip_clean_reply, NULL);

  if (result_code == rescode_success)
    {
      c->n_successful += 1;
      ip_install_next (c);
    }
  else
    {
      if (entertainment_was_cancelled ())
	ip_end (c);
      else
	{
	  result_code = scan_log_for_result_code (result_code);
	  char *msg =
	    result_code_to_message (result_code,
				    pi->installed_version != NULL);
	  if (msg == NULL)
	    msg = g_strdup_printf ((pi->installed_version != NULL
				    ? _("ai_ni_error_update_failed")
				    : _("ai_ni_error_installation_failed")),
				   pi->get_display_name (false));

	  ip_abort_cur (c, msg, false);
	  g_free (msg);
	}
    }
}

static void
ip_clean_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  /* Failure messages are in the log.  We don't annoy the user here.
     However, if cleaning takes really long, the user might get
     confused since apt-worker is not responding.
   */
}

static void
ip_install_next (void *data)
{
  ip_clos *c = (ip_clos *)data;

  c->cur = c->cur->next;
  ip_install_loop (c);
}

static void
ip_abort_cur_with_status_details (ip_clos *c)
{
  package_info *pi = (package_info *)(c->cur->data);

  char *msg;
  bool with_details;

  installable_status_to_message (pi, msg, with_details);
  ip_abort_cur (c, msg, with_details);
  g_free (msg);
}

static void
ip_abort_cur (ip_clos *c, const char *msg, bool with_details)
{
  bool is_last = (c->cur->next == NULL);

  GtkWidget *dialog;

  if (is_last)
    {
      stop_entertaining_user ();
      c->entertaining = false;
    }

  // XXX - get the button texts correct, etc.

  if (with_details)
    {
      if (is_last)
	{
	  dialog = hildon_note_new_confirmation_add_buttons 
	    (NULL,
	     msg,
	     _("ai_ni_bd_details"), 1,
	     _("ai_ni_bd_close"), GTK_RESPONSE_CANCEL,
	     NULL);
	}
      else
	{
	  dialog = hildon_note_new_confirmation_add_buttons 
	    (NULL,
	     msg,
	     _("Continue"), GTK_RESPONSE_OK,
	     _("ai_ni_bd_details"), 1,
	     _("ai_ni_bd_close"), GTK_RESPONSE_CANCEL,
	     NULL);
	}
    }
  else
    {
      if (is_last)
	{
	  dialog = hildon_note_new_confirmation_add_buttons 
	    (NULL,
	     msg,
	     _("ai_ni_bd_close"), GTK_RESPONSE_CANCEL,
	     NULL);
	}
      else
	{
	  dialog = hildon_note_new_confirmation_add_buttons 
	    (NULL,
	     msg,
	     _("Continue"), GTK_RESPONSE_OK,
	     _("ai_ni_bd_close"), GTK_RESPONSE_CANCEL,
	     NULL);
	}
    }

  push_dialog (dialog);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (ip_abort_response), c);
  gtk_widget_show_all (dialog);
}

static void
ip_abort_response (GtkDialog *dialog, gint response, gpointer data)
{
  ip_clos *c = (ip_clos *)data;

  if (response == 1)
    ip_show_cur_problem_details (c);
  else
    {
      pop_dialog (GTK_WIDGET (dialog));
      gtk_widget_destroy (GTK_WIDGET (dialog));
      
      if (response == GTK_RESPONSE_OK)
	ip_install_next (c);
      else
	ip_end (c);
    }
}

static void
ip_end (void *data)
{
  ip_clos *c = (ip_clos *)data;

  if (c->entertaining)
    stop_entertaining_user ();

  get_package_list (APTSTATE_DEFAULT);
  save_backup_data ();

  if (c->reboot)
    annoy_user (_("You should reboot now"), ip_end_after_reboot, c);
  else
    ip_end_after_reboot (c);
}

static void
ip_end_after_reboot (void *data)
{
  ip_clos *c = (ip_clos *)data;

  c->cont (c->n_successful, c->data);
  delete c;
}


/* UNINSTALL_PACKAGE - Overview

   0. Get details.

   1. Get confirmation.

   2. Run the checkrm scripts and abort if requested.

   3. Do the removal.
 */

struct up_clos {
  package_info *pi;

  int flags;
  GSList *remove_names;

  void (*cont) (void *);
  void *data;
};

static void up_confirm (package_info *pi, void *data, bool changed);
static void up_checkrm_start (bool res, void *data);
static void up_checkrm_reply (int cmd, apt_proto_decoder *dec, void *data);
static void up_checkrm_loop (up_clos *c);
static void up_checkrm_cmd_done (int status, void *data);
static void up_remove (up_clos *c);
static void up_remove_reply (int cmd, apt_proto_decoder *dec, void *data);
static void up_end (void *data);

void
uninstall_package (package_info *pi,
		   void (*cont) (void *data), void *data)
{
  up_clos *c = new up_clos;

  c->pi = pi;
  c->cont = cont;
  c->data = data;
  
  get_intermediate_package_info (c->pi, false,
				 up_confirm, c,
				 APTSTATE_DEFAULT);
}

static void
up_confirm (package_info *pi, void *data, bool changed)
{
  up_clos *c = (up_clos *)data;

  GString *text = g_string_new ("");
  char size_buf[20];
  
  size_string_general (size_buf, 20, c->pi->installed_size);
  g_string_printf (text, _("ai_nc_uninstall"),
		   c->pi->get_display_name (true),
		   c->pi->installed_version, size_buf);

  ask_yes_no_with_details (_("ai_ti_confirm_uninstall"), text->str,
			   c->pi, remove_details,
			   up_checkrm_start, c);
  g_string_free (text, 1);
}

static void
up_checkrm_start (bool res, void *data)
{
  up_clos *c = (up_clos *)data;

  if (res)
    apt_worker_remove_check (c->pi->name,
			     up_checkrm_reply, c);
  else
    up_end (c);
}

static void
up_checkrm_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  up_clos *c = (up_clos *)data;
  
  if (dec == NULL)
    {
      up_end (c);
      return;
    }

  c->remove_names = NULL;
  while (true)
    {
      char *name = dec->decode_string_dup ();
      if (name == NULL)
	break;
      push (c->remove_names, name);
    }

  up_checkrm_loop (c);
}

static void
up_checkrm_loop (up_clos *c)
{
  if (c->remove_names)
    {
      char *name = (char *)pop (c->remove_names);

      char *cmd =
	g_strdup_printf ("/var/lib/hildon-application-manager/info/%s.checkrm",
			 name);

      char *argv[] = { cmd, "remove", NULL };
      run_cmd (argv, up_checkrm_cmd_done, c);
      g_free (cmd);
    }
  else
    up_remove (c);
}

static void
up_checkrm_cmd_done (int status, void *data)
{
  up_clos *c = (up_clos *)data;

  if (status != -1 && WIFEXITED (status) && WEXITSTATUS (status) == 111)
    {
      clear (c->remove_names);

      char *str =
	g_strdup_printf (_("ai_ni_error_uninstall_applicationrunning"),
			 c->pi->get_display_name (true));
      annoy_user (str, up_end, c);
      g_free (str);
    }
  else
    up_checkrm_loop (c);
}

static void
up_remove (up_clos *c)
{
  if (c->pi->info.removable_status == status_able)
    {
      add_log ("-----\n");
      add_log ("Uninstalling %s %s\n", c->pi->name, c->pi->installed_version);
      
      char *title = g_strdup_printf (_("ai_nw_uninstalling"),
				     c->pi->get_display_name (true));
      set_entertainment_fun (NULL, -1, 0);
      set_entertainment_cancel (NULL, NULL);
      set_entertainment_title (title);
      g_free (title);

      start_entertaining_user ();

      apt_worker_remove_package (c->pi->name, up_remove_reply, c);
    }
  else
    {
      if (c->pi->info.removable_status == status_system_update_unremovable)
	annoy_user (_("ai_ni_error_system"), up_end, c);
      else if (c->pi->info.removable_status == status_needed)
	annoy_user_with_details (_("ai_ni_error_uninstall_packagesneeded"),
				 c->pi, remove_details, up_end, c);
      else
	{
	  char *str = g_strdup_printf (_("ai_ni_error_uninstallation_failed"),
				       c->pi->get_display_name (true));
	  annoy_user_with_details (str, c->pi, remove_details, up_end, c);
	  g_free (str);
	}
    }
}

static void
up_remove_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  up_clos *c = (up_clos *)data;

  stop_entertaining_user ();

  if (dec == NULL)
    {
      up_end (c);
      return;
    }

  int success = dec->decode_int ();
  get_package_list (APTSTATE_DEFAULT);
  save_backup_data ();

  if (success)
    {
      char *str = g_strdup_printf (_("ai_ni_uninstall_successful"),
				   c->pi->get_display_name (true));
      annoy_user (str, up_end, c);
      g_free (str);
    }
  else
    {
      char *str = g_strdup_printf (_("ai_ni_error_uninstallation_failed"),
				   c->pi->get_display_name (true));
      annoy_user (str, up_end, c);
      g_free (str);
    }
}

static void
up_end (void *data)
{
  up_clos *c = (up_clos *)data;

  c->cont (c->data);
  delete c;
}


/* INSTALL_LOCAL_DEB_FILE - Overview

   0. Get details of file.

   1. Get confirmation.

   2. Show legal notice.

   3. Install file.

   XXX - Installing from file is much less powerful than installing
         from repositories.  For example, checkrm scripts are not run
         and dependencies are not automatially fulfilled.  In essence,
         installing from file is the unloved step child of the
         Application Manager.  A good solution might be to create a
         apt method that can access isolated .deb files directly.
 */

struct if_clos {
  const char *filename;

  package_info *pi;

  void (*cont) (void *);
  void *data;
};

static void if_details_reply (int cmd, apt_proto_decoder *dec, void *data);
static void if_show_legalese (bool res, void *data);
static void if_install (bool res, void *data);
static void if_install_reply (int cmd, apt_proto_decoder *dec, void *data);
static void if_fail (bool res, void *data);
static void if_end (void *data);

void
install_local_deb_file (const char *filename,
			void (*cont) (void *data), void *data)
{
  if_clos *c = new if_clos;

  c->filename = filename;
  c->pi = NULL;
  c->cont = cont;
  c->data = data;

  fprintf (stderr, "INSTALL DEB: %s\n", filename);

  apt_worker_get_file_details (!(red_pill_mode && red_pill_show_all),
			       c->filename, if_details_reply, c);
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
if_details_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  if_clos *c = (if_clos *)data;

  if (dec == NULL)
    {
      if_end (c);
      return;
    }

  package_info *pi = new package_info;

  c->pi = pi;

  pi->name = dec->decode_string_dup ();
  pi->available_pretty_name = dec->decode_string_dup ();
  pi->broken = false;
  pi->installed_version = dec->decode_string_dup ();
  pi->installed_size = dec->decode_int64 ();;
  pi->available_version = dec->decode_string_dup ();
  pi->maintainer = dec->decode_string_dup ();
  pi->available_section = dec->decode_string_dup ();
  pi->info.installable_status = dec->decode_int ();
  pi->info.install_user_size_delta = dec->decode_int64 ();
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
		     pi->get_display_name (false),
		     pi->available_version, size_buf);
  else
    g_string_printf (text, _("ai_nc_install"),
		     pi->get_display_name (false),
		     pi->available_version, size_buf);

  void (*cont) (bool res, void *);

  if (pi->info.installable_status == status_able)
    cont = if_show_legalese;
  else
    cont = if_fail;

  ask_yes_no_with_details ((pi->installed_version
			    ? _("ai_ti_confirm_update")
			    : _("ai_ti_confirm_install")),
			   text->str,
			   pi, install_details, cont, c);

  g_string_free (text, 1);
}

static void
if_show_legalese (bool res, void *data)
{
  if_clos *c = (if_clos *)data;
  
  if (res)
    scare_user_with_legalese (false, if_install, c);
  else
    if_end (c);
}

static void
if_install (bool res, void *data)
{
  if_clos *c = (if_clos *)data;

  if (res)
    {
      char *title = g_strdup_printf ((c->pi->installed_version
				      ? _("ai_nw_updating")
				      : _("ai_nw_installing")),
				     c->pi->get_display_name (false));
      set_entertainment_fun (NULL, -1, 0);
      set_entertainment_cancel (NULL, NULL);
      set_entertainment_title (title);
      g_free (title);

      start_entertaining_user ();

      set_log_start ();
      apt_worker_install_file (c->filename,
			       if_install_reply, c);
    }
  else
    if_end (c);
}

static void
if_install_reply (int cmd, apt_proto_decoder *dec, void *data)
{
  if_clos *c = (if_clos *)data;

  stop_entertaining_user ();

  if (dec == NULL)
    {
      if_end (c);
      return;
    }

  int success = dec->decode_int ();

  get_package_list (APTSTATE_DEFAULT);
  save_backup_data ();

  if (success)
    {
      char *str = g_strdup_printf (c->pi->installed_version
				   ? _("ai_ni_update_successful")
				   : _("ai_ni_install_successful"),
				   c->pi->get_display_name (false));
      annoy_user (str, if_end, c);
      g_free (str);
    }
  else
    {
      apt_proto_result_code result_code = rescode_failure;
      result_code = scan_log_for_result_code (result_code);

      char *msg = result_code_to_message (result_code, 
					  c->pi->installed_version != NULL);
      if (msg == NULL)
	msg = g_strdup_printf (c->pi->installed_version
			       ? _("ai_ni_error_update_failed")
			       : _("ai_ni_error_installation_failed"),
			       c->pi->get_display_name (false));

      annoy_user (msg, if_end, c);
      g_free (msg);
    }
}

static void
if_fail (bool res, void *data)
{
  if_clos *c = (if_clos *)data;

  if (res)
    {
      char *msg;
      bool with_details;
      installable_status_to_message (c->pi, msg, with_details);
      if (with_details)
	annoy_user_with_details (msg, c->pi, install_details, if_end, c);
      else
	annoy_user (msg, if_end, c);
      g_free (msg);
    }
  else
    if_end (c);
}

static void
if_end (void *data)
{
  if_clos *c = (if_clos *)data;
  
  if (c->pi)
    c->pi->unref ();

  c->cont (c->data);
  delete c;
}
