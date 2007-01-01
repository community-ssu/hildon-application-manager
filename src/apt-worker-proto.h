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

#ifndef APT_WORKER_PROTO_H
#define APT_WORKER_PROTO_H

#include <stdlib.h>

/* No, this is not DBUS. */

enum apt_command {
  APTCMD_NOOP,

  APTCMD_STATUS,

  APTCMD_GET_PACKAGE_LIST,
  APTCMD_GET_PACKAGE_INFO,
  APTCMD_GET_PACKAGE_DETAILS,

  APTCMD_UPDATE_PACKAGE_CACHE,        // needs network
  APTCMD_GET_SOURCES_LIST,
  APTCMD_SET_SOURCES_LIST,

  APTCMD_INSTALL_CHECK,
  APTCMD_INSTALL_PACKAGE,             // needs network
  APTCMD_GET_PACKAGES_TO_REMOVE,
  APTCMD_REMOVE_PACKAGE,

  APTCMD_GET_FILE_DETAILS,
  APTCMD_INSTALL_FILE,

  APTCMD_CLEAN,

  APTCMD_MAX
};

struct apt_request_header {
  int cmd;
  int seq;
  int len;
};

struct apt_response_header {
  int cmd;
  int seq;
  int len;
};

enum apt_proto_result_code {
  rescode_success,              // (success)
  rescode_failure,              // Operation failed
  rescode_download_failed,      // Download failed
  rescode_package_corrupted,    // Package corrupted
  rescode_packages_not_found,   // Unable to download.  The
                                // package was not found.
  rescode_out_of_space          // Not enough memory in target location
};

// Encoding and decoding of data types
//
// All strings are in UTF-8.

struct apt_proto_encoder {

  apt_proto_encoder ();
  ~apt_proto_encoder ();
  
  void reset ();

  void encode_mem (const void *, int);
  void encode_int (int);
  void encode_string (const char *);
  void encode_stringn (const char *, int len);

  char *get_buf ();
  int get_len ();

private:
  char *buf;
  int buf_len;
  int len;

  void grow (int delta);
  void encode_mem_plus_zeros (const void *, int, int);
};

struct apt_proto_decoder {

  apt_proto_decoder ();
  apt_proto_decoder (const char *data, int len);
  ~apt_proto_decoder ();
  
  void reset (const char *data, int len);

  void decode_mem (void *, int);
  int decode_int ();
  const char *decode_string_in_place ();
  char *decode_string_dup ();

  bool at_end ();
  bool corrupted ();

private:
  const char *buf, *ptr;
  int len;
  bool corrupted_flag, at_end_flag;
};

// NOOP - do nothing, no parameters, no results

// STATUS - status report
//
// This command is special: you never send a request for it but
// instead you will receive STATUS responses whenever apt-worker has
// some progress to announce.
//
// The response always has seq == -1.  It contains:
//
// - operation (int).  See enum below.
// - already (int).    Amount of work already done.
// - total (int).      Total amount of work to do.

enum apt_proto_operation {
  op_updating_cache,
  op_downloading,
  op_general
};

// GET_PACKAGE_LIST - get a list of packages with their names,
//                    versions, and assorted information
//
// Parameters:
//
// - only_user (int).      Whether to return only packages from sections
//                         starting with "user/".
// - only_installed (int). Include only packages that are installed.
// - only_available (int). Include only packages that are available.
// - pattern (string).     Include only packages that match pattern.
// - show_magic_sys (int). Include the artificial "magic:sys" package.
//
// The response starts with an int that tells whether the request
// succeeded.  When that int is 0, no data follows.  When it is 1 then
// the response contains for each interesting package:
//
// - name (string) 
// - broken (int)
// - installed_version or null (string) 
// - installed_size (int)
// - installed_section or null (string)
// - installed_short_description or null (string).
// - installed_icon or null (string).
// - available_version or null (string) 
// - available_section (string)
// - available_short_description or null (string).
// - available_icon or null (string).
//
// When the available_short_description would be identical to the
// installed_short_description, it is set to null.  Likewise for the
// icon.

// UPDATE_PACKAGE_CACHE - recreate package cache
//
// Parameters:
//
// - http_proxy (string).    The value of the http_proxy envvar to use.
// - https_proxy (string).   The value of the https_proxy envvar to use.
//
// Response contains:
//
// - result_code (int)
//
// Error messages appear on stdout/stderr of the apt-worker process.


// GET_SOURCES_LIST - read the main sources.list files, unparsed.
//
// No parameters.
//
// Response contains:
//
// - source_lines (string)*,(null).
// - success (int).

// SET_SOURCES_LIST - write the main sources.list files, unparsed.
//
// Parameters:
//
// - source_lines (string)*,(null).
//
// Response:
//
// - success (int).

// GET_PACKAGE_INFO - get some more information about a specific
//                    package.  This information is used to augment
//                    the displayed list of packages but it is
//                    sufficiently expensive to calculate so that we
//                    dont want to include it in the GET_PACKAGE_LIST
//                    response.
//
// Parameters:
//
// - name (string).                Name of the package.
// - only_installable_info (int).
//
// Response:
//
// - info (apt_proto_package_info).

enum apt_proto_able_status {
  status_unknown,
  status_able,
  status_unable,                 // unknown reason
  status_conflicting,
  status_missing,
  status_needed,
  status_corrupted,
  status_incompatible,           // incompatible in general
  status_incompatible_current    // incompatible with current OS
};

struct apt_proto_package_info {
  int installable_status;
  int download_size;
  int install_user_size_delta;

  int removable_status;
  int remove_user_size_delta;
};

// GET_PACKAGE_DETAILS - get a lot of details about a specific
//                       package.  This is intended for the "Details"
//                       dialog, of course.
//
// Parameters:
//
// - name (string).
// - version (string).
// - summary_kind (int).  0 == none, 1 == install, 2 == remove
//
// Response:
//
// - maintainer (string).
// - description (string).
// - dependencies (deptype,string)*,(deptype_end).
// - summary (sumtype,string)*,(sumtype_end).

enum apt_proto_deptype { 
  deptype_end,
  deptype_depends,
  deptype_conflicts
};

enum apt_proto_sumtype {
  sumtype_end,
  sumtype_installing,
  sumtype_upgrading,
  sumtype_removing,
  sumtype_needed_by,
  sumtype_missing,
  sumtype_conflicting,
  sumtype_max
};

// INSTALL_CHECK - Check for non-authenticated and non-certified
//                 packages.
//
// This will setup the download operation and figure out whether there
// are any not-authenticated or not-certified packages.  It will also
// report information about which packages will be upgraded to which
// version.
//
// Parameters:
//
// - name (string).  The package to be installed.
//
// Response:
//
// - summary (preptype,string)*,(preptype_end)
// - upgrades (string,string)*,(null)   First string is package name,
//                                      second is version.
// - success (int).

enum apt_proto_preptype {
  preptype_end,
  preptype_notauth,
  preptype_notcert
};

// INSTALL_PACKAGE - Do the actual installation of a package
//
// Parameters:
//
// - name (string).         The package to be installed.
// - http_proxy (string).   The value of the http_proxy envvar to use.
// - https_proxy (string).  The value of the https_proxy envvar to use.
//
// Response:
//
// - result_code (int).


// GET_PACKAGES_TO_REMOVE - Return the names of packages that would be
//                          removed if the given package would be
//                          removed with REMOVE_PACKAGE.  If the given
//                          package can not be removed, the returned
//                          list is empty.
//
// Parameters:
//
// - name (string).
//
// Response:
//
// - names (string)*,(null).


// REMOVE_PACKAGE - remove one package
//
// Parameters:
//
// - name (string).
//
// Response:
//
// - success (int).


// CLEAN - empty the cache of downloaded archives
//
// No parameters.
//
// Response:
//
// - success (int).


// GET_FILE_DETAILS - Get details about a package in a .deb file.
//                    This is more or less the union of the
//                    information provided by GET_PACKAGE_LIST,
//                    GET_PACKAGE_INFO and GET_PACKAGE_DETAILS for a
//                    package.
//
// Parameters:
//
// - only_user (int).    - if true, declare all non-user packages incompatible
// - filename (string).
//
// Response:
//
// - name (string).
// - installed_version (string).
// - installed_size (int).
// - available_version (string).
// - maintainer (string).
// - available_section (string).
// - installable_status (int).
// - install_user_size_delta (int).
// - description (string).
// - available_icon (string).
// - summary (symtype,string)*,(sumtype_end).


// INSTALL_FILE - install a package from a .deb file
//
// Parameters:
//
// - filename (string).
//
// Response:
//
// - success (int).
//
// This command is not smart about what is going on.  It just call
// "dpkg --install" and reports whether it worked or not.  If "dpkg
// --install" fails, "dpkg --purge" is called automatically as an
// attempt to clean up.

#endif /* !APT_WORKER_PROTO_H */
