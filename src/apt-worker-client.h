/*
 * This file is part of osso-application-installer
 *
 * Copyright (C) 2005, 2006 Nokia Corporation.  All Right reserved.
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

#ifndef APT_WORKER_CLIENT_H
#define APT_WORKER_CLIENT_H

#include <glib/gtypes.h>

#include "util.h"
#include "apt-worker-proto.h"

extern int apt_worker_in_fd, apt_worker_out_fd;

bool start_apt_worker (gchar *prog);
void cancel_apt_worker ();

typedef void apt_worker_callback (int cmd,
				  apt_proto_decoder *dec,
				  void *callback_data);

/* There can be only one non-completed request per command.  If you
   try tp queue another request before the pending one has completed,
   you get an error and the DONE callback will be called with a NULL
   response data.
*/
void call_apt_worker (int cmd, char *data, int len,
		      apt_worker_callback *done,
		      void *done_data);

bool apt_worker_is_running ();
void send_apt_request (int cmd, int seq, char *data, int len);
void handle_one_apt_worker_response ();

/* Specific commands.
 */

void apt_worker_set_status_callback (apt_worker_callback *callback,
				     void *data);

void apt_worker_get_package_list (bool only_user,
				  bool only_installed,
				  bool only_available,
				  const char *pattern,
				  bool show_magic_sys,
				  apt_worker_callback *callback,
				  void *data);

void apt_worker_update_cache (apt_worker_callback *callback,
			      void *data);

void apt_worker_get_sources_list (apt_worker_callback *callback,
				  void *data);

void apt_worker_set_sources_list (void (*encoder) (apt_proto_encoder *,
						   void *),
				  void *encoder_data,
				  apt_worker_callback *callback, void *data);

void apt_worker_get_package_info (const char *package,
				  bool only_installable_info,
				  apt_worker_callback *callback,
				  void *data);

void apt_worker_get_package_details (const char *package,
				     const char *version,
				     int summary_kind,
				     apt_worker_callback *callback,
				     void *data);

void apt_worker_install_check (const char *package,
			       apt_worker_callback *callback,
			       void *data);

void apt_worker_install_package (const char *package,
				 bool updating,
				 apt_worker_callback *callback,
				 void *data);

void apt_worker_get_packages_to_remove (const char *package,
					apt_worker_callback *callback,
					void *data);

void apt_worker_remove_package (const char *package,
				apt_worker_callback *callback,
				void *data);

void apt_worker_clean (apt_worker_callback *callback,
		       void *data);

void apt_worker_install_file (const char *filename,
			      apt_worker_callback *callback,
			      void *data);

void apt_worker_get_file_details (bool only_user, const char *filename,
				  apt_worker_callback *callback,
				  void *data);

#endif /* !APT_WORKER_CLIENT_H */
