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

#ifndef APT_WORKER_PROTO_H
#define APT_WORKER_PROTO_H

#include <stdlib.h>

extern "C" {
#include "xexp.h"
}

/* No, this is not D-Bus. */

enum apt_command {
  APTCMD_NOOP,

  APTCMD_STATUS,

  APTCMD_GET_PACKAGE_LIST,
  APTCMD_GET_PACKAGE_INFO,
  APTCMD_GET_PACKAGE_DETAILS,

  APTCMD_CHECK_UPDATES,        // needs network
  APTCMD_GET_CATALOGUES,
  APTCMD_SET_CATALOGUES,
  APTCMD_ADD_TEMP_CATALOGUES,
  APTCMD_RM_TEMP_CATALOGUES,

  APTCMD_INSTALL_CHECK,
  APTCMD_DOWNLOAD_PACKAGE,      // needs network
  APTCMD_INSTALL_PACKAGE,      // needs network

  APTCMD_REMOVE_CHECK,
  APTCMD_REMOVE_PACKAGE,

  APTCMD_GET_FILE_DETAILS,
  APTCMD_INSTALL_FILE,

  APTCMD_CLEAN,

  APTCMD_SAVE_BACKUP_DATA,

  APTCMD_GET_SYSTEM_UPDATE_PACKAGES,
  APTCMD_REBOOT,
  APTCMD_SET_OPTIONS,
  APTCMD_SET_ENV,
  APTCMD_THIRD_PARTY_POLICY_CHECK,

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
  rescode_partial_success,
  rescode_cancelled,
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
  void encode_int64 (int64_t);
  void encode_string (const char *);
  void encode_stringn (const char *, int len);
  void encode_xexp (xexp *x);

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
  int64_t decode_int64 ();
  const char *decode_string_in_place ();
  char *decode_string_dup ();
  xexp *decode_xexp ();

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
// - installed_size (int64)
// - installed_section or null (string)
// - installed_pretty_name or null (string)
// - installed_short_description or null (string)
// - installed_icon or null (string).
// - available_version or null (string) 
// - available_section (string)
// - available_pretty_name or null (string)
// - available_short_description or null (string)
// - available_icon or null (string)
// - flags (int)
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
// - catalogue_report (xexp).  The current catalogue configuration with 
//                             error messages attached.
// - result_code (int).      
//
// Error messages appear in the catalogue_report as well as on
// stdout/stderr of the apt-worker process.


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

// GET_CATALOGUES - read the system wide catalogue configuration
//
// No parameters.
//
// Response contains:
//
// - catalogues (xexp).
//
// The xexp is NULL on errors.

// SET_CATALOGUES - write the system wide catalogue configuration
//
// Parameters.
//
// - catalogues (xexp).
//
// Response contains:
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
  status_unable,                   // unknown reason
  status_conflicting,
  status_missing,
  status_needed,
  status_corrupted,
  status_incompatible,             // incompatible in general
  status_incompatible_current,     // incompatible with current OS
  status_system_update_unremovable,// could be removed but it's a bad idea
  status_not_found,                // there is no such package
  status_incompatible_thirdparty   // package that breaks the SSU policy
};

enum apt_proto_install_flags {
  pkgflag_close_apps       =  1,
  pkgflag_suggest_backup   =  2,
  pkgflag_reboot           =  4,
  pkgflag_system_update    =  8,
  pkgflag_flash_and_reboot = 16
};

struct apt_proto_package_info {
  int installable_status;
  int64_t download_size;
  int64_t install_user_size_delta;
  int64_t required_free_space;
  int install_flags;

  int removable_status;
  int64_t remove_user_size_delta;
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
// - repository (string).
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
//                 packages and gather information about the
//                 installation.
//
// This will setup the download operation and figure out the kind of
// trust we have in the packages that will be installed.  It will also
// report information about which packages will be upgraded to which
// version.
//
// Parameters:
//
// - name (string).  The package to be installed.
//
// Response:
//
// - summary (pkgtrust,string)*,(pktrust_end)
// - upgrades (string,string)*,(null)   First string is package name,
//                                      second is version.
// - success (int).

enum apt_proto_pkgtrust {
  pkgtrust_end,
  pkgtrust_not_certified,
  pkgtrust_domains_violated
};

// INSTALL_PACKAGE - Do the actual installation of a package
//
// Parameters:
//
// - name (string).              The package to be installed.
// - alt_download_root (string). Alternative download root filesystem.
// - http_proxy (string).        The value of the http_proxy envvar to use.
// - https_proxy (string).       The value of the https_proxy envvar to use.
// - check_free_space (int).     Whether or not to check the
//                               "Required-Free-Space" field of the packages
// Response:
//
// - result_code (int).


// REMOVE_CHECK - Return the names of packages that would be removed
//                if the given package would be removed with
//                REMOVE_PACKAGE.  Also, the union of all the flags of
//                the to-be-removed packages is returned.
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
// - pretty_name (string).
// - installed_version (string).
// - installed_size (int64_t).
// - available_version (string).
// - maintainer (string).
// - available_section (string).
// - installable_status (int).
// - install_user_size_delta (int64_t).
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

// REBOOT - Run /sbin/reboot.
//
// Parameter: none.
// Response: empty.

// SET_OPTIONS - set the backend options.
//
// Parameters:
//
// - options (string).  Same as when invoking apt-worker.
//
// Response: empty.

// SET_ENV - set the environment variables for the backend.
//
// Parameters:
//
// - http_proxy (string).  Defines an HTTP proxy.
// - https_proxy (string).  Defines an HTTPS proxy.
// - internal_mmc (string).  Defines mountpoint for the internal MMC
// - removable_mmc (string).  Defines mountpoint for the external MMC
//
// Response: empty.

// SET_THIRD_PARTY_POLICY_CHECK - check the 3rd party policy for a package.
//
// Parameters:
//
// - package (string).
// - version (string).
//
// Response:
//
// - third_party_policy_status (int).

enum third_party_policy_status {
  third_party_unknown,
  third_party_compatible,
  third_party_incompatible
};

#endif /* !APT_WORKER_PROTO_H */
