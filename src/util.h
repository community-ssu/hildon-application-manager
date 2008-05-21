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
#include <gdk/gdkx.h>
#include <libgnomevfs/gnome-vfs.h>

#include "main.h"

/** Interaction flows

  The Application Manager can run one interaction flow at any single
  time.  A local flow is started with START_INTERACTION_FLOW and ended
  with END_INTERACTION_FLOW.  A flow that should run on top of a
  foreign window is started with START_FOREIGN_INTERACTION_FLOW and
  ended with END_INTERACTION_FLOW.

  When a new interaction flow is to be started while there is already
  one active, START_INTERACTION_FLOW displays an appropriate error
  message and returns false.  START_FOREIGN_INTERACTION_FLOW does not
  display this message.

  PUSH_DIALOG and POP_DIALOG are used to maintain the stack of dialogs
  that are currently active.  These calls make sure that the dialogs
  are properly transient for each other.  When creating a dialog, you
  should use NULL as the parent.

  You must take care to correctly balance calls to
  START_INTERACTION_FLOW (or START_FOREIGN_INTERACTION_FLOW) and
  END_INTERACTION_FLOW, as well as calls to PUSH_DIALOG and
  POP_DIALOG.

  The function IS_IDLE can be used to check whether there is currently
  a active interaction flow.  When it returns FALSE, a call to
  start_interaction_flow would fail.

  See "Exiting" for how interaction flows influence the life
  time of the application.

  
  The function START_INTERACTION_FLOW_WHEN_IDLE will start an
  interaction flow when the program has been idle for at least one
  minute.  When that condition is true or when it becomes true in the
  future, CONT will be called with DATA.  There can only be one
  pending idle interaction flow.  START_INTERACTION_FLOW_WHEN_IDLE
  returns false when one is already pending.

  Calling RESET_IDLE_TIMER will inform the idle interaction flow
  machinery that the user performed some action that should delay the
  start of a pending interaction flow.  You don't need to do this for
  interaction flows themselves, but for example when the user switches
  the views.
*/

bool start_interaction_flow ();
bool start_foreign_interaction_flow (Window parent);
void end_interaction_flow ();
bool is_interaction_flow_active ();

void reset_idle_timer ();
bool start_interaction_flow_when_idle (void (*cont) (void *), void *data);

void push_dialog (GtkWidget *dialog);
void pop_dialog (GtkWidget *dialog);

bool is_idle ();

/** The main window

  PRESENT_MAIN_WINDOW and HIDE_MAIN_WINDOW will show and hide the main
  window of the application, respectively.  When it is shown, the main
  window is also pushed to the front of the window stack.

  See "Exiting" for how the main window influence the life time of the
  application.
*/

void present_main_window ();
void hide_main_window ();

/** Exiting

  The Application Manager can run interaction flows on top of foreign
  windows and can also show its own window at the same time.  Thus the
  program should exit when the main window is not shown and no
  interaction flow is active.  This doesn't happen automatically, but
  only when MAYBE_EXIT is called.  This way, necessary cleanup actions
  such as flushing the D-Bus message queue can be performed.
 */

void maybe_exit ();

/** General dialog helpers

  The following functions construct and show different kinds of
  dialogs.  They do not wait for the user to answer these dialog;
  instead, you need to provide a callback that will be called with the
  result.  This callback is called the 'continuation'.  Continuations
  have a void* parameter, usually called DATA, that can be used in the
  usual fashion to pass arbitrary additional information to it.

  The dialogs are application modal.

  ASK_YES_NO shows QUESTION in a confirmation note.  The result RES
  passed to the continuation CONT is true when the user clicks "Ok",
  of course, and false otherwise.

  ASK_YES_NO_WITH_TITLE is like ask_yes_no but it constructs a real
  dialog with title TITLE.

  ASK_CUSTOM is like ask_yes_no but allows the texts in the buttons to
  be specified.

  ASK_YES_NO_WITH_DETAILS is like ask_yes_no but it constructs a real
  dialog with title TITLE and adds a third button, "Details".
  Clicking this button opens the "Details" dialog with the given
  package info PI and hint INSTALLED.  See show_package_details for
  the meaning of PI and INSTALLED.

  ASK_YES_NO_WITH_ARBITRARY_DETAILS is like ask_yes_no_with_details
  but instead of showing a package details dialog, it invokes a
  callback.

  ANNOY_USER display TEXT in a information note.  Clicking "Ok"
  removes that information note.  No continuation can be specified.

  ANNOY_USER_WITH_DETAILS is like annoy_user but adds a "Details"
  button like ask_yes_no_with_details.

  ANNOY_USER_WITH_ERRNO shows a notification that is appropriate for
  the given errno value ERR.  The DETAIL string will be put into the
  Log together with a detailed error message.

  ANNOY_USER_WITH_GNOME_VFS_RESULT does the same for a GnomeVFSResult
  code instead of a errno code.

  IRRITATE_USER shows TEXT in a information banner which goes away
  automatically after a certain time.

  WHAT_THE_FOCK_P irritates the user with a general "Operation failed"
  message.  Details should appear in the log.  It should be used when
  a situation has occured that can not reasonably explained to the
  user, such as the apt-worker returning out-of-sequence replies.

  SCARE_USER_WITH_LEGALESE shows one of two legal disclaimers,
  depending on the SURE parameter.  When SURE is true, the disclaimer
  reflects the fact that you know for sure that the user is about to
  install uncertified software.  Setting SURE to false means that the
  software might or might not be certified.  CONT is called with RES
  set true when the user agrees to take the risk of installing
  uncertified software.

*/

void ask_yes_no (const gchar *question,
		 void (*cont) (bool res, void *data), void *data);

void ask_yes_no_with_title (const gchar *title,
			    const gchar *question,
			    void (*cont) (bool res, void *data),
			    void *data);

void ask_custom (const gchar *question,
		 const gchar *ok_label, const gchar *cancel_label,
		 void (*cont) (bool res, void *data), void *data);

void ask_yes_no_with_details (const gchar *title,
			      const gchar *question,
			      package_info *pi, detail_kind kind,
			      const char *help_topic,
			      void (*cont) (bool res, void *data), void *data);

void ask_yes_no_with_arbitrary_details (const gchar *title,
					const gchar *question,
					void (*cont) (bool res, void *data),
					void (*details) (void *data),
					void *data);

void annoy_user (const gchar *text, void (*cont) (void *data), void *data);
void annoy_user_with_details (const gchar *text,
			      package_info *pi, detail_kind kind,
			      void (*cont) (void *data), void *data);
void annoy_user_with_arbitrary_details (const gchar *text,
					void (*details) (void *data),
					void (*cont) (void *data),
					void *data);
void annoy_user_with_arbitrary_details_2 (const gchar *text,
					  void (*details) (void *data),
					  void (*cont) (void *data),
					  void *data);

void annoy_user_with_errno (int err, const gchar *detail,
			    void (*cont) (void *), void *data);
void annoy_user_with_gnome_vfs_result (GnomeVFSResult result,
				       const gchar *detail,
				       void (*cont) (void *), void *data);

void irritate_user (const gchar *text);
void what_the_fock_p ();

void scare_user_with_legalese (bool sure,
			       void (*cont) (bool res, void *data),
			       void *data);

/* Progress dialog.
   
   START_ENTERTAINING_USER shows a progress bar dialog with or without
   a cancel button (depending on the value for its only parameter) and
   STOP_ENTERTAINING_USER removes it.  These two functions must be
   called in pairs and must be properly nested with respect to
   push_dialog_parent and pop_dialog_parent.  There will only be at
   most one progress bar dialog active at any given time, and nesting
   start_entertaining_user/stop_entertaining_user will just maintain a
   counter of the nesting depth.

   SET_ENTERTAINMENT_FUN sets the state of the progress bar dialog.
   You can and should call this function before calling
   start_entertaining_user to setup the initial state of the dialog.
   TITLE is the text displayed in the dialog and ALREADY and TOTAL
   determine the content of the progress bar.  When ALREADY is
   negative, the progress bar pulses.

   SET_ENTERTAINMENT_DOWNLOAD_FUN is a specialization of
   set_entertainment_fun: it automatically provides an appropriate
   sub-title that includes the total download size.

   SET_ENTERTAINMENT_CANCEL associates a callback with the "Cancel"
   button in the dialog.  When CALLBACK is NULL, the button is
   insensitive.

   SET_ENTERTAINMENT_CANCEL associates a callback with the "Cancel"
   button in the dialog.  When CALLBACK is NULL, the button is
   insensitive.

   SET_ENTERTAINMENT_SYSTEM_MODAL converts the progress bar dialog
   into a system modal dialog. This operation is not reversible, so
   the dialog will be system modal until it gets destroyed.

   CANCEL_ENTERTAINMENT calls the callback associated with
   set_entertainment_cancel, if there is one.

   ENTERTAINMENT_WAS_CANCELLED returns true when cancel_entertainment
   has been called since the last call to start_entertaining_user.
   Clicking on the "Cancel" button will also call
   cancel_entertainment.

   The entertainment can be divided into a sequence of 'games'.  Each
   game is allocated its own segment of the progress bar.  For
   example, you can specify that the first half of the progress bar
   should be used for the first game, and the second half for the
   second game.  Then, when setting the amount of fun with
   set_entertainment_fun, the 100% percent mark for the first game is
   in the middle of the progress bar.
   
   Games are played in sequence.  The next game starts when its ID is
   first passed to set_entertainment_fun.  Games might be skipped.
 */

void start_entertaining_user (gboolean with_button);
void stop_entertaining_user ();

void set_entertainment_main_title (const char *main_title,
				   bool strong = false);
void set_entertainment_sub_title (const char *sub_title);

struct entertainment_game {
  int id;
  double fraction;
};

void set_entertainment_games (int n, entertainment_game *games);

void set_entertainment_fun (const char *sub_title,
			    int game, int64_t alreday, int64_t total);
void set_entertainment_download_fun (int game, int64_t already, int64_t total);

void set_entertainment_cancel (void (*callback)(void *), void *data);

void set_entertainment_system_modal (void);

void cancel_entertainment ();
bool entertainment_was_cancelled ();

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
void reset_global_target_path ();

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
			  int state,
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

void size_string_general (char *buf, size_t n, int64_t bytes);
void size_string_detailed (char *buf, size_t n, int64_t bytes);

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
				 const char *default_folder,
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
	      bool ignore_nonexisting,
	      void (*cont) (int status, void *data),
	      void *data);

/* CLOSE_APPS kills all the user applications currently running in the
   device except the hildon-application-manager */
void close_apps (void);

/* Skip over the leading whitespace characters of STR and return a
   pointer to the first non-whitespace one.
*/
const char *skip_whitespace (const char *str);

/* Return true when STR contains only whitspace characters, as
   determined by isspace.
 */
bool all_whitespace (const char *str);

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

/* Set up a handler that emits the given RESPONSE for DIALOG when
   the user hits Escape.
*/
void respond_on_escape (GtkDialog *dialog, int response);

/* Make it so that WIDGET grabs the focus when it is put on the
   screen.
*/
void grab_focus_on_map (GtkWidget *widget);

/* Get the number of free bytes in the root filesystem.
 */
int64_t get_free_space ();

/* Get the number of free bytes at the specified path.
 */
int64_t get_free_space_at_path (const char *path);

/* Checks if there's some volume mounted in the specified path */
gboolean volume_path_is_mounted (const gchar *path);

/* Set a DBUS message to reboot the device */
void send_reboot_message (void);

/* Save the LAST_UPDATE timstamp from disk */
void save_last_update_time (time_t t);

/* Load the LAST_UPDATE timstamp from disk */
int load_last_update_time ();

/* Check whether the package cache is up-to-date or not */
gboolean is_package_cache_updated ();

#endif /* !UTIL_H */

