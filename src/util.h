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

#ifndef UTIL_H
#define UTIL_H

#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs.h>

#include "main.h"

/** General dialog helpers

  The following functions construct and show different kinds of
  dialogs.  They do not wait for the user to answer these dialog;
  instead, you need to provide a callback that will be called with the
  result.  This callback is called the 'continuation'.  Continuations
  have a void* parameter, usually called DATA, that can be used in the
  usual fashion to pass arbitrary additional information to it.

  The dialogs are application modal.

  PUSH_DIALOG_PARENT and POP_DIALOG_PARENT are used to keep track of
  the top-most window to be used as the parent for new dialogs.
  GET_DIALOG_PARENT returns that window. PUSH_NO_PARENT is used to
  specify a non-parented flow. It happens when the flow is started
  from another application. END_DIALOG_FLOW removes no parent mark.

  ASK_YES_NO shows QUESTION in a confirmation note.  The result RES
  passed to the continuation CONT is true when the user clicks "Ok",
  of course, and false otherwise.

  ASK_CUSTOM is like ask_yes_no but allows the texts in the buttons to
  be specified.

  ASK_YES_NO_WITH_DETAILS is like ask_yes_no but it constructs a real
  dialog with title TITLE and adds a third button, "Details".
  Clicking this button opens the "Details" dialog with the given
  package info PI and hint INSTALLED.  See show_package_details for
  the meaning of PI and INSTALLED.

  ANNOY_USER display TEXT in a information note.  Clicking "Ok"
  removes that information note.  No continuation can be specified.

  ANNOY_USER_WITH_DETAILS is like annoy_user but adds a "Details"
  button like ask_yes_no_with_details.

  ANNOY_USER_WITH_LOG is the same as annoy_user.  It used to add a
  "Log" button that would open the log dialog.  This "Log" button is
  no longer used in order not to push the scary log into the face of
  the user too much.  The code, however, still uses
  annoy_user_with_log in preference to annoy_user when showing an
  error message where the log is expected to contain more details.

  ANNOY_USER_WITH_ERRNO shows a notification that is appropriate for
  the given errno value ERR.  The DETAIL string will be put into the
  Log together with a detailed error message.

  If a 'annoy_user' dialog is already active when any of the
  annoy_user function is called, no new dialog is displayed.

  IRRITATE_USER shows TEXT in a information banner which goes away
  automatically after a certain time.

  SCARE_USER_WITH_LEGALESE shows one of two legal disclaimers,
  depending on the SURE parameter.  When SURE is true, the disclaimer
  reflects the fact that you know for sure that the user is about to
  install uncertified software.  Setting SURE to false means that the
  software might or might not be certified.  CONT is called with RES
  set true when the user agrees to take the risk of installing
  uncertified software.

*/

void push_dialog_parent (GtkWidget *window);
void push_no_parent ();
void end_dialog_flow ();
void pop_dialog_parent ();
void print_dialog_stack ();
GtkWindow *get_dialog_parent ();

void ask_yes_no (const gchar *question,
		 void (*cont) (bool res, void *data), void *data);

void ask_custom (const gchar *question,
		 const gchar *ok_label, const gchar *cancel_label,
		 void (*cont) (bool res, void *data), void *data);

void ask_yes_no_with_details (const gchar *title,
			      const gchar *question,
			      package_info *pi, detail_kind kind,
			      void (*cont) (bool res, void *data), void *data);

void ask_yes_no_with_arbitrary_details (const gchar *title,
					const gchar *question,
					void (*cont) (bool res, void *data),
					void (*details) (void *data),
					void *data);

void annoy_user (const gchar *text);
void annoy_user_with_cont (const gchar *text, void (*cont) (void *data), void *data);
void annoy_user_with_details (const gchar *text,
			      package_info *pi, detail_kind kind);
void annoy_user_with_details_with_cont (const gchar *text,
					package_info *pi, detail_kind kind,
					void (*cont) (void *data), void *data);
void annoy_user_with_log (const gchar *text);
void annoy_user_with_errno (int err, const gchar *detail);
void annoy_user_with_gnome_vfs_result (GnomeVFSResult result,
				       const gchar *detail);

void irritate_user (const gchar *text);

void scare_user_with_legalese (bool sure,
			       void (*cont) (bool res, void *data),
			       void *data);

/** Progress indicator

  There is a single global progress indication dialog that can be
  shown or hidden.  The dialog is application modal and has a "Cancel"
  button.  The progress bar can either pulse or show a 'completed'
  fraction.

  The dialog will be shown when calling SHOW_PROGRESS but also
  automatically when a status report from the apt-worker comes, except
  when that status report is for op_updating_cache.  To be prepared
  for this automatic showing, you should use
  set_general_progress_title and reset_progress_was_cancelled at the
  right time.

  XXX - Yes, this logic is ugly and needs to be cleaned up.

  SHOW_PROGRESS shows the progress dialog, resets it to pulsing mode,
  and sets the current operation associated with the dialog to
  op_general (see apt-worker-proto.h).  The TITLE given in the call to
  show_progress is used whenever the current operation is op_general.

  SET_GENERAL_PROGRESS_TITLE can be use to set the title used for
  op_general without causing the dialog to show.

  SET_PROGRESS sets the displayed progress to relfect that ALREADY
  units out of TOTAL units have been completed.  When ALREADY is -1,
  however, the prgress bar starts pulsing.  The current operation is
  set to OP.  When it is op_general, the last title given to
  show_progress is used; otherwise, a title appropriate for OP is
  used.  When OP is op_downloading, the unit for TOTAL is taken to be
  bytes and this size is included in the title.

  When the current operation is op_downloading, clicking on the cancel
  button will call cancel_apt_worker (see apt-worker-client.h).
  Otherwise, only an information banner saying "Unableto cancel now.
  Please wait." is shown.

  HIDE_PROGRESS will hide the progress dialog.  Since the dialog is
  application modal, it is important to eventually hide it.
  Otherwise, the application will effectively freeze.

  PROGRESS_WAS_CANCELLED can be used to determine whether
  cancel_apt_worker was indeed called as the response to a click on
  the "Cancel" button (since the last call to show_progress or
  reset_progress_was_cancelled).

  RESET_PROGRESS_WAS_CANCELLED resets the flag to false that tracks
  whether the user has clicked on the "Cancel" button.
*/

void show_progress (const char *title);
void set_general_progress_title (const char *title);
void set_progress (apt_proto_operation op, int already, int total);
bool progress_was_cancelled ();
void reset_progress_was_cancelled ();
void hide_progress ();

/* SHOW_UPDATING and HIDE_UPDATING determine whether the "Updating"
   animation banner should be shown.  They maintain a counter;
   SHOW_UPDATING increases it and HIDE_UPDATING decreases it.  The
   banner is shown whenever that counter is positive.  The actual
   display of the banner is delayed by two seconds so that when the
   counter is positive for less than two seconds, no banner is shown.

   In addition ALLOW_UPDATING and PREVENT_UPDATING maintain a flag and
   the updating banner is only shown when that flag allows it.  The
   flag starts out in the 'allow' state.

   The label shown in the banner is determined by SHOW_UPDATING.
   Using NULL (the default) gives you the standard "Updating" label.
   The label specified with SHOW_LABEL will only be used when the
   banner is created.  Thus, when a banner is already active when you
   call SHOW_UPDATING again, the label is not changed.
*/
void show_updating (const char *label = NULL);
void hide_updating ();
void allow_updating ();
void prevent_updating ();


/* MAKE_SMALL_TEXT_VIEW constructs a widget that displays TEXT in a
   small font and with scrollbars if necessary.

   SET_SMALL_TEXT_VIEW_TEXT can be used to change the displayed text.

   MAKE_SMALL_LABEL constructs a GtkLabel that shows TEXT in a small
   font.
*/

GtkWidget *make_small_text_view (const char *text);
void set_small_text_view_text (GtkWidget *, const char *text);
GtkWidget *make_small_label (const char *text);


/* Global package list widget

  MAKE_GLOBAL_PACKAGE_LIST creates a widget that displays the given
  list of packages.  The nodes in PACKAGES must point to package_info
  structs.

  When INSTALLED is true, information about the installed version of a
  package is shown, otherwise the available version is used.

  EMPTY_LABEL is shown instead of a list when PACKAGES is NULL.

  OP_LABEL is the text used for the operation item in the context menu
  or a package.

  SELECTED and ACTIVATED are functions that are called when a row in
  the list is selected or activated, respectively.

  XXX - The state of the package list widget is partially stored in
  global variables (that's why the function is called
  make_GLOBAL_package_list).  Thus, you can only use one of these
  widgets at any one time.  This could clearly be improved.

  PACKAGES must remain valid until make_global_package_list is called
  again or until clear_global_package_list is called.

  CLEAR_GLOBAL_PACKAGE_LIST clears the list in the most recently
  constructed package list widget.

  If a package_info struct has been changed and the display should be
  udpated to reflect this, call GLOBAL_PACKAGE_INFO_CHANGED.  You can
  call this function on any package_info struct at any time,
  regardless of whether it is currently being displayed or not.
*/

typedef void package_info_callback (package_info *);

GtkWidget *make_global_package_list (GList *packages,
				     bool installed,
				     const char *empty_label,
				     const char *op_label,
				     package_info_callback *selected,
				     package_info_callback *activated);
void clear_global_package_list ();
void global_package_info_changed (package_info *pi);


/* Global section list widget

  MAKE_GLOBAL_SECTION_LIST creates a widget that displays the given
  list of sections.  The nodes in the SECTIONS list must point to
  section_info structs.

  ACT is called when a section has been activated.

  XXX - The state of the section list widget is partially stored in
  global variables (that's why the function is called
  make_GLOBAL_section_list).  Thus, you can only use one of these
  widgets at any one time.  This could clearly be improved.

  SECTIONS must remain valid until make_global_section_list is called
  again or until clear_global_section_list is called.
  
  CLEAR_GLOBAL_SECTION_LIST clears the list in the most recently
  constructed section list widget.
*/

typedef void section_activated (section_info *);

GtkWidget *make_global_section_list (GList *sections, section_activated *act);
void clear_global_section_list ();

/* Select packages to install dialog

   SELECT_PACKAGES_LIST shows a dialog containing the list of 
   packages to install. The title is TITLE, and it will show QUESTION

   CONT is called with DATA as DATA parameter when the dialog is closed. 
   If Ok is pressed, RES is TRUE.
 */

void select_package_list (GList *package_list,
			  const gchar *title,
			  const gchar *question,
			  void (*cont) (gboolean res, GList * pl, void *data),
			  void *data);

/* Formatting sizes

  SIZE_STRING_GENERAL and SIZE_STRING_DETAILED put a string decribing
  a size of BYTES bytes into BUF, using at most N characters,
  according to the Hildon rules for displaying file sizes.

  SIZE_STRING_GENERAL uses less space than SIZE_STRING_DETAILED.
*/

void size_string_general (char *buf, size_t n, int bytes);
void size_string_detailed (char *buf, size_t n, int bytes);

/* SHOW_DEB_FILE_CHOOSER shows a file chooser dialog for choosing a
   .deb file.  CONT is called with the selected URI, or NULL when
   the dialog has been cancelled.

   FILENAME must be freed by CONT with g_free.
*/
void show_deb_file_chooser (void (*cont) (char *uri, void *data),
			    void *data);

/* SHOW_FILE_CHOOSER_FOR_SAVE shows a file chooser for saving a text
   file, using the given TITLE and DEFAULT_FILENAME.

   CONT is called with the selected URI, or NULL when the dialog has
   been cancelled.  FILENAME must be freed by CONT with g_free.
*/
void show_file_chooser_for_save (const char *title,
				 GtkWindow *parent,
				 const char *default_filename,
				 void (*cont) (char *uri, void *data),
				 void *data);

/* PIXBUF_FROM_BASE64 takes a base64 encoded binary blob and loads it
   into a new pixbuf as a image file.

   When BASE64 is NULL or when the image data is invalid, NULL is
   returned.
*/
GdkPixbuf *pixbuf_from_base64 (const char *base64);

/* LOCALIZE_FILE_AND_KEEP_IT_OPEN makes sure that the file identified
   by URI is accessible in the local filesystem.

   In addition, the original URI is opened and kept open until
   CLEANUP_TEMP_FILE is called.  Keeping the file open all the time
   will signal to the system that it is in use all the time (and will
   prevent the MMC from being unmounted, for example) even if the file
   will in fact be read multiple times with gaps in between.

   CONT is called with the local name of the file, or NULL when
   something went wrong.  In the latter case, an appropriate error
   message has already been shown and CONT can simply silently clean
   up.  CONT must free LOCAL with g_free.  CONT must cause
   cleanup_temp_file to be called eventually when it received a
   non-NULL filename.

   CLEANUP_TEMP_FILE cleans up after a file localization.  It must be
   called after LOCALIZE_FILE has called CONT with a non-NULL filename.
*/

void localize_file_and_keep_it_open (const char *uri,
				     void (*cont) (char *local, void *data),
				     void *data);

void cleanup_temp_file ();

/* RUN_CMD spawns a process that executes the command specified by
   ARGV and calls CONT with the termination status of the process (as
   returned by waitpid).  When the process can not be spawned, STATUS
   is -1 and an appropriate error message has been put into the log.

   stdout and stderr of the subprocess are redirected into the log.
*/
void run_cmd (char **argv,
	      void (*cont) (int status, void *data),
	      void *data);

/* Skip over the leading whitespace characters of STR and return a
   pointer to the first non-whitespace one.
*/
const char *skip_whitespace (const char *str);

/* Return true when STR contains only whitspace characters, as
   determined by isspace.
 */
bool all_whitespace (const char *str);

/* NULL and empty strings are considered equal.  Whitespace at the
   beginning and end is ignored.  Sequences of whitespaces are equal
   to each other.
*/
bool tokens_equal (const char *str1, const char *str2);

/* ENSURE_NETWORK requests an internet connection and calls CONT when
   it has been established or when the attempt failed.  SUCCESS
   reflects whether the connection could be established.

   When the connection is disconnected and the current progress
   operation is op_downloading (see set_progress above),
   cancel_apt_worker is called.  This does not count as a "cancel" as
   far as progress_was_cancelled is concerned.
*/
void ensure_network (void (*cont) (bool success, void *data),
		     void *data);

/* Return the current http proxy in a form suitable for the
   "http_proxy" environment variable, or NULL if no proxy has
   currently been configured.  You must free the return value with
   g_free.

   The current proxy is taken either from gconf or from the http_proxy
   environment variable.
*/
char *get_http_proxy ();

/* Return the current https proxy in a form suitable for the
   "https_proxy" environment variable, or NULL if no proxy has
   currently been configured.  You must free the return value with
   g_free.

   The current proxy is taken either from gconf or from the
   https_proxy environment variable.
*/
char *get_https_proxy ();

/* PUSH and POP treat the GSList starting at PTR as a stack,
   allocating and freeng as list nodes as needed.
 */
void push (GSList *&ptr, void *data);
void *pop (GSList *&ptr);

/* If there is a translation available for ID, return it.  Otherwise
   return ENGLISH.
*/
const char *gettext_alt (const char *id, const char *english);

/* Return the device name.
 */
const char *device_name ();
void setup_dbus();

/* Set up a handler that emits the given RESPONSE for DIALOG when
   the user hits Escape.
*/
void respond_on_escape (GtkDialog *dialog, int response);

/* Make it so that WIDGET grabs the focus when it is put on the
   screen.
*/
void grab_focus_on_map (GtkWidget *widget);

/* Mark as the application should keep running when the dialog flows end */
void mark_keep_running ();

#endif /* !UTIL_H */
