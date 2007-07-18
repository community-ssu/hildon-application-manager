/*
 * This file is part of the hildon-application-manager.
 *
 * Parts of this file are derived from apt.  Apt is copyright 1997,
 * 1998, 1999 Jason Gunthorpe and others.
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

/* This is the process that runs as root and does all the work.

   It is started from a separate program (as opposed to being forked
   directly from the GUI process) since that allows us to use sudo for
   starting it.

   This process communicates with the GUI process via some named pipes
   that are created by that process.  You can't really use it from the
   command line.

   It will output stuff to stdin and stderr, which the GUI process is
   supposed to catch and put into its log.

   The program tries hard not to exit prematurely.  Once the
   connection between the GUI process and this process has been
   established, the apt-worker is supposed to stick around until that
   connection is broken, even if it has to fail every request send to
   it.  This allows the user to try and fix the system after something
   went wrong, although the options are limited, of course.  The best
   example is a corrupted /etc/apt/sources.list: even tho you can't do
   anything related to packages, you still need the apt-worker to
   correct /etc/apt/sources.list itself in the UI.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <dirent.h>

#include <fstream>

#include <apt-pkg/init.h>
#include <apt-pkg/error.h>
#include <apt-pkg/tagfile.h>
#include <apt-pkg/pkgcache.h>
#include <apt-pkg/pkgcachegen.h>
#include <apt-pkg/sourcelist.h>
#include <apt-pkg/acquire.h>
#include <apt-pkg/acquire-item.h>
#include <apt-pkg/cachefile.h>
#include <apt-pkg/strutl.h>
#include <apt-pkg/sptr.h>
#include <apt-pkg/packagemanager.h>
#include <apt-pkg/deblistparser.h>
#include <apt-pkg/debversion.h>
#include <apt-pkg/dpkgpm.h>
#include <apt-pkg/debsystem.h>
#include <apt-pkg/orderlist.h>
#include <apt-pkg/algorithms.h>
#include <apt-pkg/metaindex.h>
#include <apt-pkg/debmetaindex.h>

#include <glib/glist.h>
#include <glib/gstring.h>
#include <glib/gstrfuncs.h>
#include <glib/gmem.h>
#include <glib/gfileutils.h>
#include <glib/gslist.h>
#include <glib/gkeyfile.h>

#include "apt-worker-proto.h"
#include "confutils.h"

using namespace std;

/* Table of contents.
 
   COMPILE-TIME CONFIGURATION
   
   RUN-TIME CONFIGURATION

   GENERAL UTILITIES

   COMMUNICATING WITH THE FRONTEND

   STARTUP AND COMMAND DISPATCHER

   CACHE HANDLING
*/


/** COMPILE-TIME CONFIGURATION
 */

/* Defining this to non-zero will also recognize packages in the
   "maemo" section as user packages.  There are still packages
   floating around that follow this old rule.
*/
#define ENABLE_OLD_MAEMO_SECTION_TEST 1

/* Requests up to this size are put into a stack allocated buffer.
 */
#define FIXED_REQUEST_BUF_SIZE 4096

/* Temporary repositories APT cache and status directories */
#define TEMP_APT_CACHE "/var/cache/hildon-application-manager/temp-cache"
#define TEMP_APT_STATE "/var/lib/hildon-application-manager/temp-state"

/* Temporary catalogues and temporary sources.list */
#define TEMP_CATALOGUE_CONF "/var/lib/hildon-application-manager/catalogues.temp"
#define TEMP_APT_SOURCE_LIST "/var/lib/hildon-application-manager/sources.list.temp"

/* The file where we store our backup data.
 */
#define BACKUP_CATALOGUES "/var/lib/hildon-application-manager/catalogues.backup"
#define BACKUP_PACKAGES "/var/lib/hildon-application-manager/packages.backup"


/* You know what this means.
 */
#define DEBUG


/** RUN-TIME CONFIGURATION
 */

/* Setting this to true will break the lock of the dpkg status
   database if necessary.
*/
bool flag_break_locks = false;


/** GENERAL UTILITIES
 */

void
log_stderr (const char *fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  fprintf (stderr, "apt-worker: ");
  vfprintf (stderr, fmt, args);
  va_end (args);
  fprintf (stderr, "\n");
}

/** APT WORKER MULTI STATE MANAGEMENT
 */

/* Inside this process, domains are identified with small integers.
 */

typedef signed char domain_t;

struct domain_info {
  const char *name;
  xexp *conf;
  int trust_level;
  bool is_certified;
};

xexp *domain_conf = NULL;
domain_info *domains = NULL;
int default_domain = -1;

#define DOMAIN_INVALID  -1
#define DOMAIN_UNSIGNED  0
#define DOMAIN_SIGNED    1 

static void
read_domain_conf ()
{
  delete[] domains;
  xexp_free (domain_conf);

  domain_conf = xexp_read_file (DOMAIN_CONF);

  int n_domains = 2;
  if (domain_conf)
    n_domains += xexp_length (domain_conf);

  domains = new domain_info[n_domains+1];

  /* Setup the two implicit domains.
   */
  domains[0].name = "unsigned";
  domains[0].conf = NULL;
  domains[0].trust_level = 0;
  domains[0].is_certified = false;
    
  domains[1].name = "signed";
  domains[1].conf = NULL;
  domains[1].trust_level = 1;
  domains[1].is_certified = false;

  default_domain = 1;

  int i = 2;
  if (domain_conf)
    {
      for (xexp *d = xexp_first (domain_conf); d; d = xexp_rest (d))
	{
	  if (!xexp_is (d, "domain"))
	    continue;

	  domains[i].name = xexp_aref_text (d, "name");
	  if (domains[i].name == NULL)
	    continue;

	  domains[i].trust_level = xexp_aref_int (d, "trust-level", 2);
	  domains[i].is_certified = xexp_aref_bool (d, "certified");
	  domains[i].conf = d;
	  if (xexp_aref_bool (d, "default"))
	    default_domain = i;

	  i += 1;
	}
    }
  domains[i].name = NULL;
}

static domain_t
find_domain_by_tag (const char *tag, const char *val)
{
  for (int i = 0; domains[i].name; i++)
    {
      if (domains[i].conf == NULL)
	continue;
      
      for (xexp *x = xexp_aref (domains[i].conf, tag);
	   x;
	   x = xexp_aref_rest (x, tag))
	{
	  if (xexp_is_text (x) && strcmp (xexp_text (x), val) == 0)
	    return i;
	}
    }

  return DOMAIN_SIGNED;
}

static domain_t
find_domain_by_key (const char *key)
{
  return find_domain_by_tag ("key", key);
}

static domain_t
find_domain_by_uri (const char *uri)
{
  return find_domain_by_tag ("uri", uri);
}

/* This struct describes some status flags for specific packages.
 * AptWorkerState includes an array of these, with an entry per
 * package.
 */
typedef struct extra_info_struct
{
  bool autoinst : 1;
  bool related : 1;
  domain_t cur_domain, new_domain;
};

/* This class implements state and cache switching of apt-worker. With
 * this we can switch between the standard apt configuration and a
 * temporary one, used to install a specific set of packages
 */
class AptWorkerState
{
public:
  AptWorkerState (bool cache_generate, string dir_cache, 
		  string dir_state, string source_list, string _source_parts);
  AptWorkerState ();
  static void SetDefault ();
  static void SetTemp ();
  static bool IsTemp ();
  void SetAsCurrent ();
  static AptWorkerState * GetCurrent ();
  static void Initialize ();
  string dir_cache;
  string dir_state;
  string source_list;
  string source_parts;
  pkgCacheFile *cache;
  unsigned int package_count;
  bool cache_generate;
  bool init_cache_after_request;
  extra_info_struct *extra_info;
  void InitializeValues ();
  static AptWorkerState *current_state;
  static AptWorkerState *default_state;
  static AptWorkerState *temp_state;
  static bool global_initialized;
};

/* It's a class attribute.
 * Currently selected state. It should be 0 (empty), or be
 * equal to default_state (standard apt configuration selected)
 * or to temp_state (temporary configuration of apt selected).
 */
AptWorkerState *AptWorkerState::current_state = 0;

/* Class attribute pointing to a state with default
 * APT configuration.
 */
AptWorkerState *AptWorkerState::default_state = 0;

/* Class attribute pointing to a state with temporary
 * APT configurations.
 */
AptWorkerState *AptWorkerState::temp_state = 0;

/* Class attribute. If it's enabled, then initialization
 * of the apt library and AptWorkerState default and temp
 * states has been done.
 */
bool AptWorkerState::global_initialized = false;

AptWorkerState::AptWorkerState (bool _cache_generate, string _dir_cache, string _dir_state, string _source_list, string _source_parts)
{
  cache_generate = _cache_generate;
  dir_cache = _dir_cache;
  dir_state = _dir_state;
  source_list = _source_list;
  source_parts = _source_parts;
  InitializeValues ();
}

/* Default instance. It gets the information from the current
 * APT configuration. To get the default configuration, it should
 * be obtained before setting any other config.
 */
AptWorkerState::AptWorkerState ()
{
  cache_generate = _config->FindB ("Apt::Cache::generate");
  dir_cache = _config->Find ("Dir::Cache");
  dir_state = _config->Find ("Dir::State");
  source_list = _config->Find ("Dir::Etc::SourceList");
  source_parts = _config->Find ("Dir::Etc::SourceParts");
  InitializeValues ();
}

void 
AptWorkerState::InitializeValues (void)
{
  this->cache = NULL;
  this->init_cache_after_request = false;
  this->package_count = 0;
}

void 
AptWorkerState::SetDefault (void)
{
  default_state->SetAsCurrent ();;
}

void 
AptWorkerState::SetTemp (void)
{
  temp_state->SetAsCurrent ();
}

bool
AptWorkerState::IsTemp (void)
{
  return current_state == temp_state;
}

AptWorkerState *
AptWorkerState::GetCurrent (void)
{
  if (current_state == 0)
    {
      current_state = default_state;
    }
  
  return current_state;
}

/* Initialization of apt worker state. It initializes the
 * APT subsystem, and then gets the Default and Temp state
 * instances. */
void
AptWorkerState::Initialize (void)
{
  if (!global_initialized)
    {
      if (pkgInitConfig(*_config) == false ||
	  pkgInitSystem(*_config,_system) == false)
	{
	  _error->DumpErrors ();
	  return;
	}
      global_initialized = true;
    }
  
  default_state = new AptWorkerState ();
  temp_state = new AptWorkerState (false, TEMP_APT_CACHE, TEMP_APT_STATE, TEMP_APT_SOURCE_LIST, "");
  
  current_state = default_state;
}

void AptWorkerState::SetAsCurrent ()
{
  if (current_state != this)
    {
      _config->Set ("Apt::Cache::Generate", cache_generate);
      _config->Set ("Dir::Cache", dir_cache);
      _config->Set ("Dir::State", dir_state);
      _config->Set ("Dir::Etc::SourceList", source_list);
      _config->Set ("Dir::Etc::SourceParts", source_parts);
      current_state = this;
    }
}

/* ALLOC_BUF and FREE_BUF can be used to manage a temporary buffer of
   arbitrary size without having to allocate memory from the heap when
   the buffer is small.
   
   The way to use them is to allocate a buffer of 'normal' but fixed
   size statically or on the stack and the use ALLOC_BUF when the
   actual size of the needed buffer is known.  If the actual size is
   small enough, ALLOC_BUF will use the fixed size buffer, otherwise
   it will allocate a new one.  FREE_BUF will free that buffer.
*/

/* Return a pointer to LEN bytes of free storage.  When LEN is less
   than or equal to FIXED_BUF_LEN return FIXED_BUF, otherwise a newly
   allocated block of memory is returned.  ALLOC_BUF never return
   NULL.
*/
char *
alloc_buf (int len, char *fixed_buf, int fixed_buf_len)
{
  if (len <= fixed_buf_len)
    return fixed_buf;
  else
    return new char[len];
}

/* Free the block of memory pointed to by BUF if it is different from
   FIXED_BUF.
*/
void
free_buf (char *buf, char *fixed_buf)
{
  if (buf != fixed_buf)
    delete[] buf;
}

/* Open FILENAME with FLAGS, or die.
 */
static int
must_open (char *filename, int flags)
{
  int fd = open (filename, flags);
  if (fd < 0)
    {
      perror (filename);
      exit (1);
    }
  return fd;
}

static void
must_set_flags (int fd, int flags)
{
  if (fcntl (fd, F_SETFL, flags) < 0)
    {
      perror ("apt-worker fcntl");
      exit (1);
    }
}

static void
block_for_read (int fd)
{
  fd_set set;
  FD_ZERO (&set);
  FD_SET (fd, &set);

  if (select (fd+1, &set, NULL, NULL, NULL) < 0)
    {
      perror ("apt-worker select");
      exit (1);
    }
}

static int
read_byte (int fd)
{
  unsigned char byte;
  if (read (fd, &byte, 1) == 1)
    return byte;
  return -1;
}

/* DRAIN_FD reads all bytes from FD that are available.
*/
static void
drain_fd (int fd)
{
  while (read_byte (fd) >= 0)
    ;
}

/* Get a lock as with GetLock from libapt-pkg, breaking it if needed
   and allowed by flag_break_locks.

   We do this so that the users can not lock themselves out.  We break
   locks instead of not locking since noisily breaking a lock is
   better than silently corrupting stuff.
 */
int
ForceLock (string File, bool Errors = true)
{
  int lock_fd = GetLock (File, false);
  if (lock_fd >= 0)
    return lock_fd;

  if (flag_break_locks)
    {
      int res = unlink (File.c_str ());
      if (res < 0 && errno != ENOENT)
	log_stderr ("Can't remove %s: %m", File.c_str ());
      else if (res == 0)
	log_stderr ("Forcing %s", File.c_str ());
    }

  return GetLock (File, Errors);
}


/** COMMUNICATING WITH THE FRONTEND.
 
   The communication with the frontend happens over four
   unidirectional fifos: requests are read from INPUT_FD and responses
   are sent back via OUTPUT_FD.  No new request is read until the
   response to the current one has been completely sent.

   The data read from INPUT_FD must follow the request format
   specified in <apt-worker-proto.h>.  The data written to OUTPUT_FD
   follows the response format specified there.

   The CANCEL_FD is polled periodically and when something is
   available to be read, the current operation is aborted.  There is
   currently no meaning defined for the actual bytes that are sent,
   the mere arrival of a byte triggers the abort.

   When using the libapt-pkg PackageManager, it is configured in such
   a way that it sends it "pmstatus:" message lines to STATUS_FD.
   Other asynchronous status reports are sent as spontaneous
   APTCMD_STATUS responses via OUTPUT_FD.  'Spontaneous' should mean
   that no request is required to receive APTCMD_STATUS responses.  In
   fact, APTCMD_STATUS requests are treated as an error by the
   apt-worker.

   Logging and debug output, and output from dpkg and the maintainer
   scripts appears normally on stdout and stderr of the apt-worker
   process.
*/

int input_fd, output_fd, status_fd, cancel_fd;

#ifdef DEBUG
#define DBG log_stderr
#else
#define DBG(...)
#endif

/* MUST_READ and MUST_WRITE read and write blocks of raw bytes from
   INPUT_FD and to OUTPUT_FD.  If they return, they have succeeded and
   read or written the whole block.
*/

void
must_read (void *buf, size_t n)
{
  int r;

  while (n > 0)
    {
      r = read (input_fd, buf, n);
      if (r < 0)
	{
	  perror ("apt-worker read");
	  exit (1);
	}
      else if (r == 0)
	{
	  DBG ("exiting");
	  exit (0);
	}
      n -= r;
      buf = ((char *)buf) + r;
    }
}

static void
must_write (void *buf, ssize_t n)
{
  if (n > 0 && write (output_fd, buf, n) != n)
    {
      perror ("apt-worker write");
      exit (1);
    }
}

/* This function sends a response on OUTPUT_FD with the given CMD and
   SEQ.  It either succeeds or does not return.
*/
void
send_response_raw (int cmd, int seq, void *response, size_t len)
{
  apt_response_header res = { cmd, seq, len };
  must_write (&res, sizeof (res));
  must_write (response, len);
}

/* Fabricate and send a APTCMD_STATUS response.  Parameters OP,
   ALREADY, and TOTAL are as specified in apt-worker-proto.h.

   A status response is only sent when there is enough change since
   the last time.  The following counts as 'enough': ALREADY has
   decreased, it has increased by more than MIN_CHANGE, it is equal to
   -1, LAST_TOTAL has changed, or OP has changed.
*/
void
send_status (int op, int already, int total, int min_change)
{

  static apt_proto_encoder status_response;
  static int last_op;
  static int last_already;
  static int last_total;

  if (already == -1 
      || already < last_already 
      || already >= last_already + min_change
      || total != last_total
      || op != last_op)
    {
      last_already = already;
      last_total = total;
      last_op = op;
      
      status_response.reset ();
      status_response.encode_int (op);
      status_response.encode_int (already);
      status_response.encode_int (total);
      send_response_raw (APTCMD_STATUS, -1, 
			 status_response.get_buf (),
			 status_response.get_len ());
    }
}


/** STARTUP AND COMMAND DISPATCHER.
 */

/* Since the apt-worker only works on a single command at a time, we
   use two global encoder and decoder engines that manage the
   parameters of the request and the result values of the response.

   Handlers of specific commands will read the parameters from REQUEST
   and put the results into RESPONSE.  The command dispatcher will
   prepare REQUEST before calling the command handler and ship out
   RESPONSE after it returned.
*/
apt_proto_decoder request;
apt_proto_encoder response;

void cmd_get_package_list ();
void cmd_get_package_info ();
void cmd_get_package_details ();
void cmd_update_package_cache ();
void cmd_get_catalogues ();
void cmd_set_catalogues ();
void cmd_install_check ();
void cmd_install_package ();
void cmd_remove_check ();
void cmd_remove_package ();
void cmd_clean ();
void cmd_get_file_details ();
void cmd_install_file ();
void cmd_save_backup_data ();
void cmd_get_system_update_packages ();

/* Commands can request the package cache to be refreshed by calling
   NEED_CACHE_INIT before they return.  The cache will then be
   reconstructed after sending the response and before starting to
   handle the next command.  In this way, the cache reconstruction
   happens in the background.

   XXX - However, APTCMD_STATUS messages are still being sent when the
         cache is reconstructed in the background and the UI has some
         ugly logic to deal with that.
*/

void cache_init (bool with_status = true);

void
need_cache_init ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  state->init_cache_after_request = true;
}

/* Function called before each handle_request call. It selects the proper
 * apt worker state depending on the protocol state provided.
 */
static void
ensure_state (int state)
{
  switch (state)
    {
    case APTSTATE_CURRENT: break;
    case APTSTATE_DEFAULT: 
      AptWorkerState::SetDefault ();
      break;
    case APTSTATE_TEMP:
      AptWorkerState::SetTemp ();
      break;
    default: break;
    }

  AptWorkerState::GetCurrent ()->init_cache_after_request = false;
}

#ifdef DEBUG
static char *cmd_names[] = {
  "NOOP",
  "STATUS",
  "GET_PACKAGE_LIST",
  "GET_PACKAGE_INFO",
  "GET_PACKAGE_DETAILS",
  "UPDATE_PACKAGE_CACHE",
  "GET_CATALOGUES",
  "SET_CATALOGUES",
  "INSTALL_CHECK",
  "INSTALL_PACKAGE",
  "REMOVE_CHECK",
  "REMOVE_PACKAGE",
  "GET_FILE_DETAILS",
  "INSTALL_FILE",
  "CLEAN",
  "SAVE_BACKUP_DATA",
  "GET_SYSTEM_UPDATE_PACKAGES"
};
#endif

void
handle_request ()
{
  apt_request_header req;
  char stack_reqbuf[FIXED_REQUEST_BUF_SIZE];
  char *reqbuf;
  AptWorkerState * state = 0;

  must_read (&req, sizeof (req));
  DBG ("got req %s/%d/%d state %d",
       cmd_names[req.cmd], req.seq, req.len, req.state);

  reqbuf = alloc_buf (req.len, stack_reqbuf, FIXED_REQUEST_BUF_SIZE);
  must_read (reqbuf, req.len);

  drain_fd (cancel_fd);

  request.reset (reqbuf, req.len);
  response.reset ();

  ensure_state (req.state);
  state = AptWorkerState::GetCurrent ();

  switch (req.cmd)
    {

    case APTCMD_NOOP:
      // Nothing to do.
      break;

    case APTCMD_GET_PACKAGE_LIST:
      cmd_get_package_list ();
      break;

    case APTCMD_GET_PACKAGE_INFO:
      cmd_get_package_info ();
      break;

    case APTCMD_GET_PACKAGE_DETAILS:
      cmd_get_package_details ();
      break;

    case APTCMD_UPDATE_PACKAGE_CACHE:
      cmd_update_package_cache ();
      break;

    case APTCMD_GET_CATALOGUES:
      cmd_get_catalogues ();
      break;

    case APTCMD_SET_CATALOGUES:
      cmd_set_catalogues ();
      break;

    case APTCMD_INSTALL_CHECK:
      cmd_install_check ();
      break;

    case APTCMD_INSTALL_PACKAGE:
      cmd_install_package ();
      break;

    case APTCMD_REMOVE_CHECK:
      cmd_remove_check ();
      break;

    case APTCMD_REMOVE_PACKAGE:
      cmd_remove_package ();
      break;

    case APTCMD_CLEAN:
      cmd_clean ();
      break;

    case APTCMD_GET_FILE_DETAILS:
      cmd_get_file_details ();
      break;

    case APTCMD_INSTALL_FILE:
      cmd_install_file ();
      break;

    case APTCMD_SAVE_BACKUP_DATA:
      cmd_save_backup_data ();
      break;

    case APTCMD_GET_SYSTEM_UPDATE_PACKAGES:
      cmd_get_system_update_packages ();
      break;

    default:
      log_stderr ("unrecognized request: %d", req.cmd);
      break;
    }

  _error->DumpErrors ();

  send_response_raw (req.cmd, req.seq,
		     response.get_buf (), response.get_len ());

  DBG ("sent resp %s/%d/%d",
       cmd_names[req.cmd], req.seq, response.get_len ());

  free_buf (reqbuf, stack_reqbuf);

  if (state->init_cache_after_request)
    {
      cache_init (false);
      _error->DumpErrors ();
    }
}

static int index_trust_level_for_package (pkgIndexFile *index,
					  const pkgCache::VerIterator &ver);

static const char *lc_messages;

int
main (int argc, char **argv)
{
  const char *options;

  if (argc != 6)
    {
      log_stderr ("wrong invocation");
      exit (1);
    }

  DBG ("starting up");

  input_fd = must_open (argv[1], O_RDONLY | O_NONBLOCK);
  cancel_fd = must_open (argv[4], O_RDONLY | O_NONBLOCK);
  output_fd = must_open (argv[2], O_WRONLY);
  status_fd = must_open (argv[3], O_WRONLY);

  /* This tells the frontend that the fifos are open.
   */
  send_status (op_general, 0, 0, -1);

  /* This blocks until the frontend has opened our input fifo for
     writing.
  */
  block_for_read (input_fd);

  /* Reset the O_NONBLOCK flag for the input_fd since we want to block
     until a new request arrives.  The cancel_fd remains in
     non-blocking mode since we just poll it periodically.
  */
  must_set_flags (input_fd, O_RDONLY);

  options = argv[5];
  lc_messages = getenv ("LC_MESSAGES");

  DBG ("starting with pid %d, in %d, out %d, stat %d, cancel %d, options %s",
       getpid (), input_fd, output_fd, status_fd, cancel_fd,
       options);
  DBG ("LC_MESSAGES %s", lc_messages);

  if (strchr (options, 'B'))
    flag_break_locks = true;

  /* Don't let our heavy lifting starve the UI.
   */
  errno = 0;
  if (nice (20) == -1 && errno != 0)
    log_stderr ("nice: %m");

  read_domain_conf ();

  AptWorkerState::Initialize ();
  cache_init (false);

#ifdef HAVE_APT_TRUST_HOOK
  apt_set_index_trust_level_for_package_hook (index_trust_level_for_package);
#endif

  while (true)
    handle_request ();
}

/** CACHE HANDLING

    This section contains some general purpose functions to maintain
    the cache of the package database.

    The package cache can represent both the 'current' situation
    (i.e., the union of the information from /var/lib/dpkg/status and
    the various Packages files downloaded from repositories) and a
    'desired' situation.

    A operation such as installing a package is performed by modifying
    the 'desired' situation in the cache and if that leads to a
    consistent configuration, the 'current' situation is brought in
    line with the 'desired' one by downloading the needed archives and
    running dpkg in an approriate way.

    We have our own idea of what should happen when a new package (or
    a new version of a package) is installed, for example, and the
    functions in this section implement this idea.  These principal
    functions are available:

    - cache_init

    This function creates or recreates the cache from
    /var/lib/dpkg/status and the various Packages file from the
    repositories.

    - ensure_cache

    This function tries to make sure that there is a valid
    PACKAGE_CACHE to work with.  It returns true when it succeeds and
    PACKAGE_CACHE is non-NULL then.  The idea is that if the cache
    couldn't be created in the past because of some transient error,
    it might be able to create it now.  Thus, every command handler
    that needs a cache should call ensure_cache.  When ensure_cache
    actually does some work, it will send STATUS messages.

    - cache_reset ()

    This function resets the 'desired' state of the cache to be
    identical to the 'current' one.

    - mark_for_install ()

    This function modifies the 'desired' state of the cache to reflect
    the installation of the given package.  It will try to achieve a
    consistent 'desired' configuration by installing missing
    dependencies etc.  In general, it implements our own installation
    smartness.

    - mark_for_remove ()

    This function modifies the 'desired' state of the cache to reflect
    the removal of the given package.  As with mark_for_install,
    mark_for_removal implements our own removal smartness.
 */

/* We only report real progress information when reconstructing the
   cache and during downloads.  Only downloads can be cancelled.

   The following two classes allow us to hook into libapt-pkgs
   progress reporting mechanism.  Instances of UPDATE_PROGESS are used
   for cache related activities, and instances of DOWNLOAD_STATUS are
   used when 'acquiring' things.
*/

class UpdateProgress : public OpProgress
{
  bool with_status;

  virtual void
  Update ()
  {
    if (with_status)
      send_status (op_general, (int)Percent, 100, 5);
  }

public:
  UpdateProgress (bool ws) : with_status (ws) { }
};

class DownloadStatus : public pkgAcquireStatus
{
  virtual bool
  MediaChange (string Media, string Drive)
  {
    return false;
  }

  virtual bool
  Pulse (pkgAcquire *Owner)
  {
    pkgAcquireStatus::Pulse (Owner);

    send_status (op_downloading, (int)CurrentBytes, (int)TotalBytes, 1000);

    /* The cancel_fd is in non-blocking mode.
     */
    if (read_byte (cancel_fd) >= 0)
      return false;

    return true;
  }
};

bool
is_user_section (const char *section, const char *end)
{
  if (section == NULL)
    return false;

#if ENABLE_OLD_MAEMO_SECTION_TEST
  if (end-section > 6 && !strncmp (section, "maemo/", 6))
    return true;
#endif
  
  return end-section > 5 && !strncmp (section, "user/", 5);
}

bool
is_user_package (const pkgCache::VerIterator &ver)
{
  const char *section = ver.Section ();

  if (section == NULL)
    return false;

  return is_user_section (section, section + strlen (section));
}

/* Save the 'extra_info' of the cache.  We first make a copy of the
   Auto flags in our own extra_info storage so that CACHE_RESET
   will reset the Auto flags to the state last saved with this
   function.
*/

void
save_extra_info ()
{
  AptWorkerState * state = AptWorkerState::GetCurrent ();
  if (state == NULL)
    return;

  if (mkdir ("/var/lib/hildon-application-manager", 0777) < 0 && errno != EEXIST)
    {
      log_stderr ("/var/lib/hildon-application-manager: %m");
      return;
    }

  FILE *f = fopen ("/var/lib/hildon-application-manager/autoinst", "w");
  if (f)
    {
      pkgDepCache &cache = *(state->cache);

      for (pkgCache::PkgIterator pkg = cache.PkgBegin(); !pkg.end (); pkg++)
	{
	  if (cache[pkg].Flags & pkgCache::Flag::Auto)
	    {
	      state->extra_info[pkg->ID].autoinst = true;
	      fprintf (f, "%s\n", pkg.Name ());
	    }
	  else
	    state->extra_info[pkg->ID].autoinst = false;
	}
      fclose (f);
    }

  for (domain_t i = 0; domains[i].name != NULL; i++)
    {
      char *name =
	g_strdup_printf ("/var/lib/hildon-application-manager/domain.%s",
			 domains[i].name);
      
      FILE *f = fopen (name, "w");
      if (f)
	{
	  pkgDepCache &cache = *(state->cache);

	  for (pkgCache::PkgIterator pkg = cache.PkgBegin();
	       !pkg.end (); pkg++)
	    {
	      if (state->extra_info[pkg->ID].cur_domain == i)
		fprintf (f, "%s\n", pkg.Name ());
	    }
	}
      fclose (f);
    }
}

/* Load the 'extra_info'.  You need to call CACHE_RESET to transfer
   the auto flag into the actual cache.
*/

void
load_extra_info ()
{
  // XXX - log errors.
  AptWorkerState *state = NULL;

  state = AptWorkerState::GetCurrent ();
  if ((state == NULL)||(state->cache == NULL))
    return;

  for (unsigned int i = 0; i < state->package_count; i++)
    {
      state->extra_info[i].autoinst = false;
      state->extra_info[i].cur_domain = default_domain;
    }

  FILE *f = fopen ("/var/lib/hildon-application-manager/autoinst", "r");
  if (f)
    {
      pkgDepCache &cache = *(state->cache);

      char *line = NULL;
      size_t len = 0;
      ssize_t n;

      while ((n = getline (&line, &len, f)) != -1)
	{
	  if (n > 0 && line[n-1] == '\n')
	    line[n-1] = '\0';

	  pkgCache::PkgIterator pkg = cache.FindPkg (line);
	  if (!pkg.end ())
	    {
	      DBG ("auto: %s", pkg.Name ());
	      state->extra_info[pkg->ID].autoinst = true;
	    }
	}

      free (line);
      fclose (f);
    }

  for (domain_t i = 0; domains[i].name != NULL; i++)
    {
      char *name =
	g_strdup_printf ("/var/lib/hildon-application-manager/domain.%s",
			 domains[i].name);
      
      FILE *f = fopen (name, "r");
      if (f)
	{
	  pkgDepCache &cache = *(state->cache);

	  char *line = NULL;
	  size_t len = 0;
	  ssize_t n;
	  
	  while ((n = getline (&line, &len, f)) != -1)
	    {
	      if (n > 0 && line[n-1] == '\n')
		line[n-1] = '\0';

	      pkgCache::PkgIterator pkg = cache.FindPkg (line);
	      if (!pkg.end ())
		{
		  // DBG ("%s: %s", domains[i].name, pkg.Name ());
		  state->extra_info[pkg->ID].cur_domain = i;
		}
	    }

	  free (line);
	  fclose (f);
	}
    }
}

/* Our own version of debSystem.  We override the Lock member function
   to be able to break locks and to avoid failing when dpkg has left a
   journal.
*/

class mydebSystem : public debSystem
{
  // For locking support
  int LockFD;
  unsigned LockCount;

public:

  virtual signed Score (Configuration const &Cnf);
  virtual bool Lock ();
  virtual bool UnLock (bool NoErrors);
  
  mydebSystem ();
};

mydebSystem::mydebSystem ()
  : debSystem ()
{
   Label = "handsfree Debian dpkg interface";
}

signed
mydebSystem::Score (Configuration const &Cnf)
{
  return debSystem::Score (Cnf) + 10;  // Pick me, pick me, pick me!
}

bool
mydebSystem::Lock ()
{
  /* This is a modified copy of debSystem::Lock, which in turn is a
     copy of the behavior of dpkg.
  */

  // Disable file locking
  if (_config->FindB("Debug::NoLocking",false) == true || LockCount > 1)
    {
      LockCount++;
      return true;
    }

  // Create the lockfile, breaking the lock if requested.
  string AdminDir = flNotFile(_config->Find("Dir::State::status"));
  LockFD = ForceLock(AdminDir + "lock");
  if (LockFD == -1)
    {
      if (errno == EACCES || errno == EAGAIN)
	return _error->Error("Unable to lock the administration directory (%s), "
			     "is another process using it?",AdminDir.c_str());
      else
	return _error->Error("Unable to lock the administration directory (%s), "
			     "are you root?",AdminDir.c_str());
    }

  LockCount++;
      
  return true;
}

// System::UnLock - Drop a lock						/*{{{*/
// ---------------------------------------------------------------------
/* */
bool mydebSystem::UnLock(bool NoErrors)
{
   if (LockCount == 0 && NoErrors == true)
      return false;
   
   if (LockCount < 1)
      return _error->Error("Not locked");
   if (--LockCount == 0)
   {
      close(LockFD);
      LockCount = 0;
   }
   
   return true;
}
									/*}}}*/
mydebSystem mydebsystem;

static void
clear_dpkg_updates ()
{
  string File = flNotFile(_config->Find("Dir::State::status")) + "updates/";
  DIR *DirP = opendir(File.c_str());
  if (DirP == 0)
    return;
   
  /* We ignore any files that are not all digits, this skips .,.. and 
     some tmp files dpkg will leave behind.. */

  for (struct dirent *Ent = readdir(DirP); Ent != 0; Ent = readdir(DirP))
    {
      bool ignore = false;

      for (unsigned int I = 0; Ent->d_name[I] != 0; I++)
	{
	  // Check if its not a digit..
	  if (isdigit(Ent->d_name[I]) == 0)
	    {
	      ignore = true;
	      break;
	    }
	}

      if (!ignore)
	{
	  log_stderr ("Running 'dpkg --configure dpkg' "
		      "to clean up the journal.");
	  system ("dpkg --configure dpkg");
	  break;
	}
    }

   closedir(DirP);
}

void cache_reset ();

/* Initialize libapt-pkg if this has not been already and (re-)create
   PACKAGE_CACHE.  If the cache can not be created, PACKAGE_CACHE is
   set to NULL and an appropriate message is output.
*/
void
cache_init (bool with_status)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();

  /* Closes default and temp caches, to prevent getting blocked
   * by other locks in dpkg structures. If we don't do it, changing
   * the apt worker state does not remove the dpkg state lock and
   * then fails on trying to run dpkg */
  if (AptWorkerState::default_state->cache)
    {
      DBG ("closing");
      AptWorkerState::default_state->cache->Close ();
      delete AptWorkerState::default_state->cache;
      delete[] AptWorkerState::default_state->extra_info;
      AptWorkerState::default_state->cache = NULL;
      AptWorkerState::default_state->extra_info = NULL;
      DBG ("done");
    }

  if (AptWorkerState::temp_state->cache)
    {
      DBG ("closing");
      AptWorkerState::temp_state->cache->Close ();
      delete AptWorkerState::temp_state->cache;
      delete[] AptWorkerState::temp_state->extra_info;
      AptWorkerState::temp_state->cache = NULL;
      AptWorkerState::temp_state->extra_info = NULL;
      DBG ("done");
    }

  /* We need to dump the errors here since any pending errors will
     cause the following operations to fail.
  */
  _error->DumpErrors ();

  /* Clear out the dpkg journal before construction the cache.
   */
  clear_dpkg_updates ();
	  
  UpdateProgress progress (with_status);
  state->cache = new pkgCacheFile;

  DBG ("init.");
  if (!state->cache->Open (progress))
    {
      DBG ("failed.");
      _error->DumpErrors ();
      delete state->cache;
      state->cache = NULL;
    }

  if (state->cache)
    {
      pkgDepCache &cache = *state->cache;
      state->package_count = cache.Head ().PackageCount;
      state->extra_info = new extra_info_struct[state->package_count];
    }

  load_extra_info ();
  cache_reset ();
}

bool
ensure_cache ()
{
  AptWorkerState * state = NULL;

  state = AptWorkerState::GetCurrent ();
  if (state->cache == NULL)
    cache_init (true);

  return state->cache != NULL;
}

/* Determine whether a package was installed automatically to satisfy
   a dependency.
*/
bool
is_auto_package (pkgCache::PkgIterator &pkg)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);
  
  return (cache[pkg].Flags & pkgCache::Flag::Auto) != 0;
}

/* Determine whether a package is related to the current operation.
*/
bool
is_related (pkgCache::PkgIterator &pkg)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  return state->extra_info[pkg->ID].related;
}

void
mark_related (pkgCache::PkgIterator &pkg)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  if (state->extra_info[pkg->ID].related)
    return;

  state->extra_info[pkg->ID].related = true;

  pkgDepCache &cache = *state->cache;

  if (pkg.State() == pkgCache::PkgIterator::NeedsUnpack)
    cache.SetReInstall (pkg, true);
}

/* Revert the cache to its initial state.  More concretely, all
   packages are marked as 'keep' and 'unrelated'.

   The effect on the cache should be the same as calling
   pkgDebCache::Init, but much faster.
*/
void
cache_reset_package (pkgCache::PkgIterator &pkg)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  cache.MarkKeep (pkg);

  if (state->extra_info[pkg->ID].autoinst)
    cache[pkg].Flags |= pkgCache::Flag::Auto;
  else
    cache[pkg].Flags &= ~pkgCache::Flag::Auto;
  
  state->extra_info[pkg->ID].related = false;
}

void
cache_reset ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  if (state->cache == NULL)
    return;

  pkgDepCache &cache = *(state->cache);

  for (pkgCache::PkgIterator pkg = cache.PkgBegin(); !pkg.end (); pkg++)
    cache_reset_package (pkg);
}

/* Determine whether PKG replaces TARGET.
 */
static bool
package_replaces (pkgCache::PkgIterator &pkg,
		  pkgCache::PkgIterator &target)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  pkgCache::DepIterator Dep = cache[pkg].InstVerIter(cache).DependsList();
  for (; Dep.end() != true; Dep++)
    {
      if (Dep->Type == pkgCache::Dep::Replaces)
	{
	  SPtrArray<pkgCache::Version *> List = Dep.AllTargets();
	  for (pkgCache::Version **I = List; *I != 0; I++)
	    {
	      pkgCache::VerIterator Ver(cache,*I);
	      pkgCache::PkgIterator Pkg = Ver.ParentPkg();
	  
	      if (Pkg == target)
		return true;
	    }
	}
    }
  return false;
}

/* Determine whether PKG is a critical dependency of other packages
   thata re going to be installed.
 */
static bool
package_is_needed (pkgCache::PkgIterator &pkg)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  pkgCache::DepIterator Dep = pkg.RevDependsList();
  for (; Dep.end() != true; Dep++)
    {
      if (Dep->Type == pkgCache::Dep::PreDepends
	  || Dep->Type == pkgCache::Dep::Depends)
	{
	  pkgCache::PkgIterator other_pkg = Dep.ParentPkg();
	  pkgCache::VerIterator other_ver = Dep.ParentVer();
	  pkgCache::VerIterator inst_ver = cache[other_pkg].InstVerIter(cache);

	  if (other_ver == inst_ver)
	    return true;
	}
    }
  return false;
}

/* Mark a package for installation, using a 'no-surprises' approach
   suitable for the Application Manager.

   Concretely, installing a package will never automatically remove
   other packages.  Thus, we undo the removals scheduled by
   MarkInstall.  Doing this will break the original package, but that
   is what we want.
*/

static void mark_for_remove (pkgCache::PkgIterator &pkg,
			     bool only_maybe = false);

static void
mark_for_install (pkgCache::PkgIterator &pkg, int level = 0)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  /* This check is just to be extra robust against infinite
     recursions.  They shouldn't happen, but you never know...
  */
  if (level > 100)
    return;

  mark_related (pkg);

  /* Avoid recursion if package is already marked for installation.
   */
  if (cache[pkg].Mode == pkgDepCache::ModeInstall)
    return;

  /* Now mark it and return if that fails.
   */
  cache.MarkInstall (pkg, false);
  if (cache[pkg].Mode != pkgDepCache::ModeInstall)
    return;

  /* Try to satisfy dependencies.  We can't use MarkInstall with
     AutoInst == true since we don't like how it handles conflicts,
     and we have our own way of uninstalling packages.

     The code below is lifted from pkgDepCache::MarkInstall.  Sorry
     for introducing this mess here.
  */

  pkgCache::DepIterator Dep = cache[pkg].InstVerIter(cache).DependsList();
  for (; Dep.end() != true;)
    {
      // Grok or groups
      pkgCache::DepIterator Start = Dep;
      bool Result = true;
      unsigned Ors = 0;
      for (bool LastOR = true; Dep.end() == false && LastOR == true;
	   Dep++,Ors++)
	{
	  LastOR = (Dep->CompareOp & pkgCache::Dep::Or) == pkgCache::Dep::Or;

	  if ((cache[Dep] & pkgDepCache::DepInstall) == pkgDepCache::DepInstall)
	    Result = false;
	}
      
      // Dep is satisfied okay.
      if (Result == false)
	continue;

      /* Check if this dep should be consider for install. If it is a user
         defined important dep and we are installed a new package then 
	 it will be installed. Otherwise we only worry about critical deps */
      if (cache.IsImportantDep(Start) == false)
	continue;
      if (pkg->CurrentVer != 0 && Start.IsCritical() == false)
	continue;
      
      /* If we are in an or group locate the first or that can 
         succeed. We have already cached this.. */
      for (; Ors > 1 
	     && (cache[Start] & pkgDepCache::DepCVer) != pkgDepCache::DepCVer;
	   Ors--)
	Start++;

      /* This bit is for processing the possibilty of an install/upgrade
         fixing the problem */
      SPtrArray<pkgCache::Version *> List = Start.AllTargets();
      if ((cache[Start] & pkgDepCache::DepCVer) == pkgDepCache::DepCVer)
	{
	  // Right, find the best version to install..
	  pkgCache::Version **Cur = List;
	  pkgCache::PkgIterator P = Start.TargetPkg();
	  pkgCache::PkgIterator InstPkg(cache,0);
	 
	  // See if there are direct matches (at the start of the list)
	  for (; *Cur != 0 && (*Cur)->ParentPkg == P.Index(); Cur++)
	    {
	      pkgCache &pkgcache = cache.GetCache ();
	      pkgCache::PkgIterator Pkg(pkgcache,
					pkgcache.PkgP + (*Cur)->ParentPkg);
	      if (cache[Pkg].CandidateVer != *Cur)
		continue;
	      InstPkg = Pkg;
	      break;
	    }

	  // Select the highest priority providing package
	  if (InstPkg.end() == true)
	    {
	      pkgPrioSortList(cache,Cur);
	      for (; *Cur != 0; Cur++)
		{
		  pkgCache &pkgcache = cache.GetCache ();
		  pkgCache::PkgIterator
		    Pkg(pkgcache,pkgcache.PkgP + (*Cur)->ParentPkg);
		  if (cache[Pkg].CandidateVer != *Cur)
		    continue;
		  InstPkg = Pkg;
		  break;
		}
	    }
	  
	  if (InstPkg.end() == false)
	    {
	      mark_for_install (InstPkg, level + 1);

	      // Set the autoflag, after MarkInstall because
	      // MarkInstall unsets it
	      if (P->CurrentVer == 0)
		cache[InstPkg].Flags |= pkgCache::Flag::Auto;
	    }

	  continue;
	}

      /* For conflicts/replaces combinations we de-install the package
         with mark_for_remove, but only if it is a non-user package.
         (Conflicts and Replaces may not have or groups.)
      */
      if (Start->Type == pkgCache::Dep::Conflicts
	  || Start->Type == pkgCache::Dep::Obsoletes)
	{
	  for (pkgCache::Version **I = List; *I != 0; I++)
	    {
	      pkgCache::VerIterator Ver(cache,*I);
	      pkgCache::PkgIterator target = Ver.ParentPkg();

	      if (!is_user_package (Ver)
		  && package_replaces (pkg, target))
		mark_for_remove (target, true);
	    }
	  continue;
	}
    }
}

/* Mark every upgradeable non-user package for installation.
 */
static void
mark_sys_upgrades ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  for (pkgCache::PkgIterator p = cache.PkgBegin (); !p.end (); p++)
    {
      if (!p.CurrentVer().end()
	  && !is_user_package (p.CurrentVer())
	  && cache[p].Keep())
	mark_for_install (p);
    }
}

/* Mark the named package for installation.  This function also
   handles magic packages like "magic:sys".
*/

static bool
mark_named_package_for_install (const char *package)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  if (!strcmp (package, "magic:sys"))
    {
      mark_sys_upgrades ();
      return true;
    }
  else
    {
      pkgDepCache &cache = *(state->cache);
      pkgCache::PkgIterator pkg = cache.FindPkg (package);
      if (!pkg.end())
	{
	  mark_for_install (pkg);
	  return true;
	}
      else
	return false;
    }
}

/* Mark a package for removal and also remove as many of the packages
   that it depends on as possible.
*/
static void
mark_for_remove (pkgCache::PkgIterator &pkg, bool only_maybe)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  if (only_maybe && package_is_needed (pkg))
    return;

  cache.MarkDelete (pkg);
  cache[pkg].Flags &= ~pkgCache::Flag::Auto;

  if (!cache[pkg].Delete ())
    {
      if (only_maybe)
	cache_reset_package (pkg);
      return;
    }

  // Now try to remove all non-user, auto-installed dependencies of
  // this package.

  pkgCache::VerIterator cur = pkg.CurrentVer ();
  if (cur.end ())
    return;

  for (pkgCache::DepIterator dep = cur.DependsList(); dep.end() == false;
       dep++)
    {
      if (dep->Type == pkgCache::Dep::PreDepends ||
	  dep->Type == pkgCache::Dep::Depends)
	{
	  pkgCache::PkgIterator p = dep.TargetPkg ();
	  if (!p.end ()
	      && is_auto_package (p)
	      && !p.CurrentVer().end()
	      && !is_user_package (p.CurrentVer()))
	    mark_for_remove (p, true);
	}
    }
}

/* Getting the package record in a nicely parsable form.
 */

struct package_record {
  pkgRecords Recs;
  pkgRecords::Parser &P;
  pkgTagSection section;
  bool valid;

  package_record (pkgCache::VerIterator &ver);

  bool has (const char *tag);
  string get_string (const char *tag);
  char *get (const char *tag);
  int get_int (const char *tag, int def);

  string get_localized_string (const char *tag);
};

package_record::package_record (pkgCache::VerIterator &ver)
  : Recs (*(AptWorkerState::GetCurrent ()->cache)),
    P (Recs.Lookup (ver.FileList ()))
{
  const char *start, *stop;
  P.GetRec (start, stop);

  /* NOTE: pkTagSection::Scan only succeeds when the record ends in
           two newlines, but pkgRecords::Parser::GetRec does not
           include the second newline in its returned region.
           However, that second newline is always there, so we just
           pass one more character to Scan.
  */
  
  valid = section.Scan (start, stop-start+1);
}

bool
package_record::has (const char *tag)
{
  unsigned pos;
  return valid && section.Find (tag, pos);
}

string
package_record::get_string (const char *tag)
{
  if (valid)
    return section.FindS (tag);
  else
    return string ("");
}
 
int
all_white_space (const char *text)
{
  while (*text)
    if (!isspace (*text++))
      return 0;
  return 1;
}

char *
package_record::get (const char *tag)
{
  if (!valid)
    return NULL;

  string res = get_string (tag);
  if (all_white_space (res.c_str ()))
    return NULL;
  else
    return g_strdup (res.c_str());
}

int
package_record::get_int (const char *tag, int def)
{
  if (!valid)
    return def;

  return section.FindI (tag, def);
}

string
package_record::get_localized_string (const char *tag)
{
  if (lc_messages && *lc_messages)
    {
      char *locale_tag = g_strdup_printf ("%s-%s", tag, lc_messages);

      if (has (locale_tag))
	{
	  string res = get_string (locale_tag);
	  g_free (locale_tag);
	  return res;
	}
      g_free (locale_tag);
    }

  return get_string (tag);
}

/** COMMAND HANDLERS
 */

/* APTCMD_GET_PACKAGE_LIST 

   The get_package_list command can do some filtering and we have a
   few utility functions for implementing the necessary checks.  The
   check generally take cache iterators to identify a package or a
   version.
 */

bool
name_matches_pattern (pkgCache::PkgIterator &pkg,
		      const char *pattern)
{
  return strcasestr (pkg.Name(), pattern);
}

bool
description_matches_pattern (pkgCache::VerIterator &ver,
			     const char *pattern)
{
  package_record rec (ver);
  const char *desc = rec.P.LongDesc().c_str();

  // XXX - UTF8?
  return strcasestr (desc, pattern);
}

static string
get_description (int summary_kind,
		 pkgCache::PkgIterator &pkg,
		 package_record &rec)
{
  string res;

  if (summary_kind == 1 && !pkg.CurrentVer().end())
    res = rec.get_localized_string ("Maemo-Upgrade-Description");

  if (res.empty())
    {
      /* XXX - support apt's own method of localizing descriptions as
	       well.
      */
      res = rec.get_localized_string ("Description");
    }

  return res;
}
	 
static string
get_short_description (int summary_kind,
		       pkgCache::PkgIterator &pkg,
		       package_record &rec)
{
  string res = get_description (summary_kind, pkg, rec);
  string::size_type pos = res.find('\n');
  if (pos != string::npos)
    return string (res,0,pos);
  return res;
}

static string
get_long_description (int summary_kind,
		      pkgCache::PkgIterator &pkg,
		      package_record &rec)
{
  return get_description (summary_kind, pkg, rec);
}

static char *
get_icon (package_record &rec)
{
  return rec.get ("Maemo-Icon-26");
}

struct {
  const char *name;
  int flag;
} flag_names[] = {
  { "close-apps",     pkgflag_close_apps },
  { "suggest-backup", pkgflag_suggest_backup },
  { "reboot",         pkgflag_reboot },
  { "system-update",  pkgflag_system_update },
  NULL
};

static int
get_flags (package_record &rec)
{
  int flags = 0;
  char *flag_string = rec.get ("Maemo-Flags");
  char *ptr = flag_string, *tok;
  while (tok = strsep (&ptr, ","))
    {
      for (int i = 0; flag_names[i].name != NULL; i++)
	if (tokens_equal (flag_names[i].name, tok))
	  {
	    flags |= flag_names[i].flag;
	    break;
	  }
    }
  g_free (flag_string);

  return flags;
}

static string
get_pretty_name (package_record &rec)
{
  return rec.get_localized_string ("Maemo-Display-Name");
}

static int64_t
get_required_free_space (package_record &rec)
{
  return 1024 * (int64_t) rec.get_int ("Maemo-Required-Free-Space", 0);
}

static void
encode_version_info (int summary_kind,
		     pkgCache::VerIterator &ver, bool include_size)
{
  package_record rec (ver);
  char *icon;

  response.encode_string (ver.VerStr ());
  if (include_size)
    response.encode_int64 (ver->InstalledSize);
  response.encode_string (ver.Section ());
  string pretty = get_pretty_name (rec);
  response.encode_string (pretty.empty()? NULL : pretty.c_str());
  pkgCache::PkgIterator pkg = ver.ParentPkg();
  response.encode_string 
    (get_short_description (summary_kind, pkg, rec).c_str());
  icon = get_icon (rec);
  response.encode_string (icon);
  g_free (icon);
}

static void
encode_empty_version_info (bool include_size)
{
  response.encode_string (NULL);
  if (include_size)
    response.encode_int64 (0);
  response.encode_string (NULL);
  response.encode_string (NULL);
  response.encode_string (NULL);
  response.encode_string (NULL);
}

void
cmd_get_package_list ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  bool only_user = request.decode_int ();
  bool only_installed = request.decode_int ();
  bool only_available = request.decode_int ();
  const char *pattern = request.decode_string_in_place ();
  bool show_magic_sys = request.decode_int ();

  if (!ensure_cache ())
    {
      response.encode_int (0);
      return;
    }

  response.encode_int (1);
  pkgDepCache &cache = *(state->cache);

  for (pkgCache::PkgIterator pkg = cache.PkgBegin(); !pkg.end (); pkg++)
    {
      pkgCache::VerIterator installed = pkg.CurrentVer ();

      // skip non user packages if requested
      //
      if (only_user
	  && !installed.end ()
	  && !is_user_package (installed))
	continue;

      // skip not-installed packages if requested
      //
      if (only_installed
	  && installed.end ())
	continue;

      pkgCache::VerIterator candidate = cache.GetCandidateVer (pkg);

      // skip non user packages if requested
      //
      if (only_user
	  && !candidate.end ()
	  && !is_user_package (candidate))
	continue;

      // skip non-available packages if requested
      //
      if (only_available
	  && candidate.end ())
	continue;

      // skip packages that are not installed and not available
      //
      if (installed.end () && candidate.end ())
	continue;

      // skip system updates that are not installed
      //
      if (installed.end () && !candidate.end ())
	{
	  package_record rec (candidate);
	  int flags = get_flags (rec);
	  if (flags & pkgflag_system_update)
	    continue;
	}

      // skip packages that don't match the pattern if requested
      //
      if (pattern
	  && !(name_matches_pattern (pkg, pattern)
	       || (!installed.end ()
		   && description_matches_pattern (installed, pattern))
	       || (!candidate.end ()
		   && description_matches_pattern (candidate, pattern))))
	continue;

      // Name
      response.encode_string (pkg.Name ());

      // Broken.
      //
      // This check doesn't catch all kinds of broken packages, only
      // those that failed to unpack or configure, but not, for
      // example, those that have been forcfully installed despite
      // missing dependencies.  This is probably OK for now since only
      // the former kinds of brokenness should be producable with the
      // Application Manager anyway.
      //
      response.encode_int (pkg.State ()
			   != pkgCache::PkgIterator::NeedsNothing);

      // Installed version
      if (!installed.end())
	encode_version_info (2, installed, true);
      else
	encode_empty_version_info (true);

      // Available version 
      //
      // We only offer an available version if the package is not
      // installed at all, or if the available version is newer than
      // the installed one, or if the installed version needs to be
      // reinstalled and it is actually downloadable.

      if (!candidate.end ()
	  && (installed.end ()
	      || installed.CompareVer (candidate) < 0
	      || (pkg.State () == pkgCache::PkgIterator::NeedsUnpack
		  && candidate.Downloadable ())))
	encode_version_info (1, candidate, false);
      else
	encode_empty_version_info (false);
    }

  if (show_magic_sys)
    {
      // Append the "magic:sys" package that represents all system
      // packages.  This artificial package is identified by its name
      // and handled specially by MARK_NAMED_PACKAGE_FOR_INSTALL, etc.
      
      // Name
      response.encode_string ("magic:sys");
      
      // Broken?  XXX - give real information here
      response.encode_int (FALSE);

      // Installed version
      response.encode_string ("");
      response.encode_int64 (1000);
      response.encode_string ("system");
      response.encode_string ("Operating System");
      response.encode_string ("All system packages");
      response.encode_string (NULL);
      
      // Available version
      response.encode_string ("");
      response.encode_string ("system");
      response.encode_string ("Operating System");
      response.encode_string ("Updates to all system packages");
      response.encode_string (NULL);
    }
}

void
cmd_get_system_update_packages ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();

  if (!ensure_cache ())
    return;

  pkgDepCache &cache = *(state->cache);

  for (pkgCache::PkgIterator pkg = cache.PkgBegin(); !pkg.end (); pkg++)
    {
      pkgCache::VerIterator installed = pkg.CurrentVer ();
      pkgCache::VerIterator candidate = cache.GetCandidateVer (pkg);
      
      if (installed.end () || candidate.end ())
	continue;

      package_record rec (candidate);
      int flags = get_flags (rec);
      if (flags & pkgflag_system_update)
	response.encode_string (pkg.Name ());
    }

  response.encode_string (NULL);
}

/* APTCMD_GET_PACKAGE_INFO

   This command performs a simulated install and removal of the
   specified package to gather the requested information.
 */

static int
installable_status_1 (pkgCache::PkgIterator &pkg)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &Cache = *(state->cache);
  pkgCache::VerIterator Ver = Cache[pkg].InstVerIter(Cache);

  bool some_missing = false, some_conflicting = false;

  if (Ver.end() == true)
    return status_unable;
      
  for (pkgCache::DepIterator D = Ver.DependsList(); D.end() == false;)
    {
      // Compute a single dependency element (glob or)
      pkgCache::DepIterator Start;
      pkgCache::DepIterator End;
      D.GlobOr(Start,End); // advances D

      if ((Cache[End] & pkgDepCache::DepGInstall)
	  == pkgDepCache::DepGInstall)
	continue;

      if (Start->Type == pkgCache::Dep::PreDepends ||
	  Start->Type == pkgCache::Dep::Depends)
	some_missing = true;
      else if (Start->Type == pkgCache::Dep::Conflicts)
	some_conflicting = true;
    }

  if (some_missing)
    return status_missing;
  if (some_conflicting)
    return status_conflicting;

  return status_unable;
}

static int
combine_status (int s1, int s2)
{
  return max (s1, s2);
}

static int
installable_status ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);
  int installable_status = status_unable;
  
  for (pkgCache::PkgIterator pkg = cache.PkgBegin();
       pkg.end() != true;
       pkg++)
    {
      if (cache[pkg].InstBroken())
	installable_status =
	  combine_status (installable_status_1 (pkg), installable_status);
    }

  return installable_status;
}

static int
removable_status ()
{
  AptWorkerState * state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);
  for (pkgCache::PkgIterator pkg = cache.PkgBegin();
       pkg.end() != true;
       pkg++)
    {
      if (cache[pkg].InstBroken())
	return status_needed;
    }
  
  return status_unable;
}

void
cmd_get_package_info ()
{
  const char *package = request.decode_string_in_place ();
  bool only_installable_info = request.decode_int ();

  apt_proto_package_info info;

  info.installable_status = status_unknown;
  info.download_size = 0;
  info.install_user_size_delta = 0;
  info.required_free_space = 0;
  info.install_flags = 0;
  info.removable_status = status_unknown;
  info.remove_user_size_delta = 0;

  if (ensure_cache ())
    {
      AptWorkerState *state = AptWorkerState::GetCurrent ();
      pkgDepCache &cache = *(state->cache);
      pkgCache::PkgIterator pkg = cache.FindPkg (package);

      // simulate install
      
      unsigned int old_broken_count = cache.BrokenCount();
      mark_named_package_for_install (package);
      if (cache.BrokenCount() > old_broken_count)
	info.installable_status = installable_status ();
      else
	info.installable_status = status_able;
      info.download_size = (int64_t) cache.DebSize ();
      info.install_user_size_delta = (int64_t) cache.UsrSize ();

      for (pkgCache::PkgIterator pkg = cache.PkgBegin();
	   pkg.end() != true;
	   pkg++)
	{
	  if (cache[pkg].Upgrade())
	    {
	      pkgCache::VerIterator ver = cache.GetCandidateVer (pkg);
	      package_record rec (ver);
	      info.install_flags |= get_flags (rec);
	      info.required_free_space += get_required_free_space (rec);
	    }
	}

      cache_reset ();

      if (!only_installable_info)
	{
	  // simulate remove
	  
	  if (!strcmp (package, "magic:sys"))
	    {
	      info.removable_status = status_system_update_unremovable;
	    }
	  else
	    {
	      old_broken_count = cache.BrokenCount();
	      if (!pkg.end())
		mark_for_remove (pkg);

	      for (pkgCache::PkgIterator pkg = cache.PkgBegin();
		   pkg.end() != true;
		   pkg++)
		{
		  if (cache[pkg].Delete())
		    {
		      pkgCache::VerIterator ver = pkg.CurrentVer ();
		      package_record rec (ver);
		      int flags = get_flags (rec);
		      if (flags & pkgflag_system_update)
			{
			  info.removable_status =
			    status_system_update_unremovable;
			  break;
			}
		    }
		}

	      if (info.removable_status == status_unknown)
		{
		  if (cache.BrokenCount() > old_broken_count)
		    info.removable_status = removable_status ();
		  else
		    info.removable_status = status_able;
		}
	      info.remove_user_size_delta = (int64_t) cache.UsrSize ();
	      cache_reset ();
	    }
	}
    }
  
  response.encode_mem (&info, sizeof (apt_proto_package_info));
}

/* APTCMD_GET_PACKAGE_DETAILS
   
   Like APTCMD_GET_PACKAGE_INFO, this command performs a simulated
   install or removal (as requested), but it gathers a lot more
   information about the package and what is happening.
*/

static void
append_display_name (GString *str, const pkgCache::PkgIterator &pkg)
{
  pkgCache::VerIterator ver = pkg.CurrentVer();
  if (!ver.end())
    {
      package_record rec (ver);
      string pretty_name = get_pretty_name (rec);
      if (!pretty_name.empty())
	{
	  g_string_append (str, pretty_name.c_str());
	  return;
	}
    }
  
  g_string_append (str, pkg.Name());
}

void
encode_dependencies (pkgCache::VerIterator &ver)
{
  for (pkgCache::DepIterator dep = ver.DependsList(); !dep.end(); )
    {
      GString *str;
      apt_proto_deptype type;
      pkgCache::DepIterator start;
      pkgCache::DepIterator end;
      dep.GlobOr(start, end);

      if (start->Type == pkgCache::Dep::PreDepends ||
	  start->Type == pkgCache::Dep::Depends)
	type = deptype_depends;
      else if (start->Type == pkgCache::Dep::Conflicts)
	type = deptype_conflicts;
      else 
	continue;

      str = g_string_new ("");
      while (1)
	{
	  g_string_append_printf (str, " %s", start.TargetPkg().Name());
	  if (start.TargetVer() != 0)
	    g_string_append_printf (str, " (%s %s)",
				    start.CompType(), start.TargetVer());
	  
	  if (start == end)
	    break;
	  g_string_append_printf (str, " |");
	  start++;
	}

      response.encode_int (type);
      response.encode_string (str->str);
      g_string_free (str, 1);
    }

  response.encode_int (deptype_end);
}

void
encode_broken (pkgCache::PkgIterator &pkg,
	       const char *want)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &Cache = *(state->cache);
  pkgCache::VerIterator Ver = Cache[pkg].InstVerIter(Cache);
      
  if (Ver.end() == true)
    return;
      
  for (pkgCache::DepIterator D = Ver.DependsList(); D.end() == false;)
    {
      GString *str;
      apt_proto_sumtype type;

      // Compute a single dependency element (glob or)
      pkgCache::DepIterator Start;
      pkgCache::DepIterator End;
      D.GlobOr(Start,End); // advances D

      if ((Cache[End] & pkgDepCache::DepGInstall)
	  == pkgDepCache::DepGInstall)
	continue;

      if (Start->Type == pkgCache::Dep::PreDepends ||
	  Start->Type == pkgCache::Dep::Depends)
	type = sumtype_missing;
      else if (Start->Type == pkgCache::Dep::Conflicts)
	type = sumtype_conflicting;
      else
	continue;

      str = g_string_new ("");
      while (1)
	{
	  /* Show a summary of the target package if possible. In the case
	     of virtual packages we show nothing 
	  */
	  pkgCache::PkgIterator target = Start.TargetPkg ();

	  /* Never blame conflicts on the package that we want to
	     install.
	  */
	  if (strcmp (target.Name(), want) == 0
	      && Start->Type == pkgCache::Dep::Conflicts)
	    append_display_name (str, pkg);
	  else
	    {
	      append_display_name (str, target);
	      if (Start.TargetVer() != 0)
		g_string_append_printf (str, " (%s %s)",
					Start.CompType(), Start.TargetVer());
	    }

	  if (Start != End)
	    g_string_append_printf (str, " | ");
	  
	  if (Start == End)
	    break;
	  Start++;
	}

      response.encode_int (type);
      response.encode_string (str->str);
      g_string_free (str, 1);
    }
}

void
encode_package_and_version (pkgCache::VerIterator ver)
{
  package_record rec (ver);
  GString *str = g_string_new ("");
  string pretty = get_pretty_name (rec);
  if (!pretty.empty())
    g_string_append (str, pretty.c_str());
  else
    g_string_append (str, ver.ParentPkg().Name());
  g_string_append_printf (str, " (%s)", ver.VerStr());
  response.encode_string (str->str);
  g_string_free (str, 1);
}

void
encode_install_summary (const char *want)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  // XXX - the summary is not really correct when there are broken
  //       packages in the device.  The problems of those packages
  //       might be included in the report.

  if (cache.BrokenCount() > 0)
    fprintf (stderr, "[ Some installed packages are broken! ]\n");

  mark_named_package_for_install (want);

  for (pkgCache::PkgIterator pkg = cache.PkgBegin();
       pkg.end() != true;
       pkg++)
    {
      if (cache[pkg].NewInstall())
	{
	  response.encode_int (sumtype_installing);
	  encode_package_and_version (cache[pkg].CandidateVerIter(cache));
	}
      else if (cache[pkg].Upgrade())
	{
	  response.encode_int (sumtype_upgrading);
	  encode_package_and_version (cache[pkg].CandidateVerIter(cache));
	}
      else if (cache[pkg].Delete())
	{
	  response.encode_int (sumtype_removing);
	  encode_package_and_version (pkg.CurrentVer());
	}

      if (cache[pkg].InstBroken())
	encode_broken (pkg, want);
    }

  response.encode_int (sumtype_end);

  cache_reset ();
}

void
encode_remove_summary (pkgCache::PkgIterator &want)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  if (cache.BrokenCount() > 0)
    log_stderr ("[ Some installed packages are broken! ]\n");

  mark_for_remove (want);

  for (pkgCache::PkgIterator pkg = cache.PkgBegin();
       pkg.end() != true;
       pkg++)
    {
      if (cache[pkg].Delete())
	{
	  response.encode_int (sumtype_removing);
	  encode_package_and_version (pkg.CurrentVer());
	}

      if (cache[pkg].InstBroken())
	{
	  response.encode_int (sumtype_needed_by);
	  encode_package_and_version (pkg.CurrentVer());
	}
    }

  response.encode_int (sumtype_end);

  cache_reset ();
}

bool
find_package_version (pkgCacheFile *cache_file,
		      pkgCache::PkgIterator &pkg,
		      pkgCache::VerIterator &ver,
		      const char *package, const char *version)
{
  if (cache_file == NULL || package == NULL || version == NULL)
    return false;

  pkgDepCache &cache = *cache_file;
  pkg = cache.FindPkg (package);
  if (!pkg.end ())
    {
      for (ver = pkg.VersionList(); ver.end() != true; ver++)
	if (!strcmp (ver.VerStr (), version))
	  return true;
    }
  return false;
}

void
cmd_get_package_details ()
{
  const char *package = request.decode_string_in_place ();
  const char *version = request.decode_string_in_place ();
  int summary_kind = request.decode_int ();

  if (!strcmp (package, "magic:sys"))
    {
      response.encode_string ("");      // maintainer
      response.encode_string 
	("This is an artificial package that represents all\n"
	 "system packages that are installed on your device.");
      response.encode_int (deptype_end);  // dependencies
      if (summary_kind == 1)
	encode_install_summary (package);
      else
	response.encode_int (sumtype_end);
    }
  else
    {
      AptWorkerState *state = AptWorkerState::GetCurrent ();
      pkgCache::PkgIterator pkg;
      pkgCache::VerIterator ver;
      
      if (find_package_version (state->cache, pkg, ver, package, version))
	{
	  package_record rec (ver);
	  
	  response.encode_string (rec.P.Maintainer().c_str());
	  response.encode_string 
	    (get_long_description (summary_kind, pkg, rec).c_str());
	  encode_dependencies (ver);
	  if (summary_kind == 1)
	    encode_install_summary (package);
	  else if (summary_kind == 2)
	    encode_remove_summary (pkg);
	  else
	    response.encode_int (sumtype_end);
	}
      else
	{
	  // not found
	  response.encode_string (NULL);      // maintainer
	  response.encode_string (NULL);      // description
	  response.encode_int (deptype_end);  // dependencies
	  response.encode_int (sumtype_end);  // summary
	}
    }
}

/* APTCMD_UPDATE_PACKAGE_CACHE
*/

static xexp *
find_catalogue_for_item_desc (xexp *catalogues, string desc_uri)
{
  /* This is a hack to associate error messages produced during
     downloading with a specific catalogue so that a good error report
     can be shown to the user.

     DESC_URI is matched against all the catalogues and we return the
     first hit.

     DESC_URI matches a catalogue if it is of the form

        URI/dists/DIST/<no-more-slashes>

     or

        URI/dists/DIST/COMP/<rest-with-slashes>

     or

        URI/DIST<rest-with-slashes>

     where URI and DIST are the respective elements of the catalogue,
     and COMP is one of the components of the catalogue.

     XXX - This is not the right thing to do, of course.  Apt-pkg
           should offer a way to easily associate user level objects
           with acquire items.
  */

  if (catalogues == NULL)
    return NULL;

  const char *match_uri = desc_uri.c_str ();

  fprintf (stderr, "MATCHING %s\n", match_uri);

  for (xexp *cat = xexp_first (catalogues); cat; cat = xexp_rest (cat))
    {
      char *uri = g_strdup (xexp_aref_text (cat, "uri"));
      const char *dist = xexp_aref_text (cat, "dist");
      gchar **comps = g_strsplit_set (xexp_aref_text (cat, "components"),
				      " \t\n", 0);
      char *pfx = NULL;

      if (dist == NULL)
	dist = DEFAULT_DIST;

      while (uri[0] && uri[strlen(uri)-1] == '/')
	uri[strlen(uri)-1] = '\0';

      fprintf (stderr, "AGAINST %s %s\n", uri, dist);

      if (dist[0] && dist[strlen(dist)-1] == '/')
	{
	  /* A simple repository without components
	   */
	  
	  pfx = g_strconcat (uri, "/", dist, NULL);
	  if (g_str_has_prefix (match_uri, pfx))
	    goto found_it;
	}
      else
	{
	  /* A repository with components
	   */
	  
	  pfx = g_strconcat (uri, "/dists/", dist, "/", NULL);
	  if (!g_str_has_prefix (match_uri, pfx))
	    goto try_next;
	  
	  const char *rest = match_uri + strlen (pfx);

	  if (!strchr (rest, '/'))
	    goto found_it;

	  for (int i = 0; comps[i]; i++)
	    {
	      gchar *comp = comps[i];

	      if (comp[0] == '\0')
		continue;

	      fprintf (stderr, "COMPS %s\n", comp);

	      if (g_str_has_prefix (rest, comp)
		  && rest[strlen(comp)] == '/')
		goto found_it;
	    }
	}

    try_next:
      g_strfreev (comps);
      g_free (pfx);
      g_free (uri);
      continue;

    found_it:
      g_strfreev (comps);
      g_free (pfx);
      g_free (uri);
      return cat;
    }

  fprintf (stderr, "NO MATCH\n");

  return NULL;
}

int
update_package_cache (xexp *catalogues_for_report)
{
  // Get the source list
  pkgSourceList List;
  if (List.ReadMainList () == false)
    return rescode_failure;

  // Lock the list directory
  FileFd Lock;
  if (_config->FindB("Debug::NoLocking",false) == false)
    {
      Lock.Fd (ForceLock (_config->FindDir("Dir::State::Lists") + "lock"));
      if (_error->PendingError () == true)
	{
	  _error->Error ("Unable to lock the list directory");
	  return rescode_failure;
	}
    }
   
  // Create the download object
  DownloadStatus Stat;
  pkgAcquire Fetcher(&Stat);

  // Populate it with the source selection
  if (List.GetIndexes(&Fetcher) == false)
    return rescode_failure;
   
  // Run it
  if (Fetcher.Run() == pkgAcquire::Failed)
    return rescode_failure;

  bool some_failed = false;
  for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin();
       I != Fetcher.ItemsEnd(); I++)
    {
      if ((*I)->Status == pkgAcquire::Item::StatDone)
	{
	  fprintf (stderr, "SUCCESS: %s\n", (*I)->DescURI().c_str());
	  continue;
	}
      
      (*I)->Finished();
      
      fprintf (stderr, "FAILED: %s\n", (*I)->DescURI().c_str());

      xexp *cat = find_catalogue_for_item_desc (catalogues_for_report,
						(*I)->DescURI());
      if (cat)
	{
	  xexp *errors = xexp_aref (cat, "errors");
	  if (errors == NULL)
	    {
	      errors = xexp_list_new ("errors");
	      xexp_append_1 (cat, errors);
	    }

	  xexp *error = xexp_list_new ("error");
	  xexp_aset_text (error, "uri", (*I)->DescURI().c_str());
	  xexp_aset_text (error, "msg", (*I)->ErrorText.c_str());

	  xexp_append_1 (errors, error);
	}

      _error->Error ("Failed to fetch %s  %s", (*I)->DescURI().c_str(),
		     (*I)->ErrorText.c_str());
      some_failed = true;
    }

  // Clean out any old list files
  if (_config->FindB("APT::Get::List-Cleanup",true) == true)
    {
      Fetcher.Clean (_config->FindDir("Dir::State::lists"));
      Fetcher.Clean (_config->FindDir("Dir::State::lists") + "partial/");
    }

  /* When errors have been encountered, we init the cache in the
     background since it looks funny to do it visible and only
     afterwards display errors that are known before hand.
  */

  if (some_failed)
    {
      need_cache_init ();
      return rescode_partial_success;
    }
  else
    {
      cache_init ();
      return rescode_success;
    }
}

void
cmd_update_package_cache ()
{
  const char *http_proxy = request.decode_string_in_place ();
  const char *https_proxy = request.decode_string_in_place ();

  if (http_proxy)
    {
      setenv ("http_proxy", http_proxy, 1);
      DBG ("http_proxy: %s", http_proxy);
    }
  
  if (https_proxy)
    {
      setenv ("https_proxy", https_proxy, 1);
      DBG ("https_proxy: %s", https_proxy);
    }

  xexp *catalogues;
  if (AptWorkerState::IsTemp ())
    catalogues = xexp_read_file (TEMP_CATALOGUE_CONF);
  else
    catalogues = xexp_read_file (CATALOGUE_CONF);

  int result_code = update_package_cache (catalogues);

  response.encode_xexp (catalogues);
  response.encode_int (result_code);
}

/* APTCMD_GET_CATALOGUES
 *
 * We also return the non-comment lines from all sources.list files in
 * order to let the user at least know that there are sources in use
 * that are not controlled by us.  The code for this is copied from
 * apt-pkg.
 */

void
append_system_sources (xexp *catalogues, string File)
{
   // Open the stream for reading
   ifstream F(File.c_str(),ios::in /*| ios::nocreate*/);
   if (!F != 0)
     return;
   
   // CNC:2003-12-10 - 300 is too short.
   char Buffer[1024];

   int CurLine = 0;
   while (F.eof() == false)
     {
       F.getline(Buffer,sizeof(Buffer));
       CurLine++;
       _strtabexpand(Buffer,sizeof(Buffer));
       if (F.fail() && !F.eof())
	 return;

       char *I;
       // CNC:2003-02-20 - Do not break if '#' is inside [].
       for (I = Buffer; *I != 0 && *I != '#'; I++)
         if (*I == '[')
	   for (I++; *I != 0 && *I != ']'; I++);
       *I = 0;
       
       const char *C = _strstrip(Buffer);
      
       // Comment or blank
       if (C[0] == '#' || C[0] == 0)
	 continue;
      	    
       xexp_append_1 (catalogues, xexp_text_new ("source", C));
     }
}

void
append_system_source_dir (xexp *catalogues, string Dir)
{
   DIR *D = opendir(Dir.c_str());
   if (D == 0)
     return;

   vector<string> List;
   
   for (struct dirent *Ent = readdir(D); Ent != 0; Ent = readdir(D))
     {
       if (Ent->d_name[0] == '.')
	 continue;

       // CNC:2003-12-02 Only accept .list files as valid sourceparts
       if (flExtension(Ent->d_name) != "list")
	 continue;
      
       // Skip bad file names ala run-parts
       const char *C = Ent->d_name;
       for (; *C != 0; C++)
	 if (isalpha(*C) == 0 && isdigit(*C) == 0
             && *C != '_' && *C != '-' && *C != '.')
	   break;
       if (*C != 0)
	 continue;
      
       // Make sure it is a file and not something else
       string File = flCombine(Dir,Ent->d_name);
       struct stat St;
       if (stat(File.c_str(),&St) != 0 || S_ISREG(St.st_mode) == 0)
	 continue;

       // skip our own file
       if (File == CATALOGUE_APT_SOURCE)
	 continue;

       List.push_back(File);      
     }
   closedir(D);
   
   sort(List.begin(),List.end());

   // Read the files
   for (vector<string>::const_iterator I = List.begin(); I != List.end(); I++)
     append_system_sources (catalogues, *I);
}

void
cmd_get_catalogues ()
{
  xexp *catalogues;
  
  catalogues = xexp_read_file (CATALOGUE_CONF);
  
  string Main = _config->FindFile("Dir::Etc::sourcelist");
  if (FileExists(Main) == true)
    append_system_sources (catalogues, Main);   

  string Parts = _config->FindDir("Dir::Etc::sourceparts");
  if (FileExists(Parts) == true)
    append_system_source_dir (catalogues, Parts);   

  response.encode_xexp (catalogues);
  xexp_free (catalogues);
}

/* APTCMD_SET_CATALOGUES
 */

void
cmd_set_catalogues ()
{
  int success;

  xexp *catalogues = request.decode_xexp ();
  xexp_adel (catalogues, "source");

  if (AptWorkerState::IsTemp ())
    {
      success = 
	(xexp_write_file (TEMP_CATALOGUE_CONF, catalogues)
	 && write_sources_list (TEMP_APT_SOURCE_LIST, catalogues));
    }
  else
    {
      success =
	(xexp_write_file (CATALOGUE_CONF, catalogues)
	 && write_sources_list (CATALOGUE_APT_SOURCE, catalogues));
    }

  xexp_free (catalogues);
  response.encode_int (success);
}

int operation (bool only_check);

/* APTCMD_INSTALL_CHECK
 *
 * Check if a package can be installed. It uses the
 * common "operation ()" code, that runs or checks the
 * current operation queue (packages marked for install
 * or uninstall).
 */

void
cmd_install_check ()
{
  const char *package = request.decode_string_in_place ();
  bool found = false;
  int result_code = rescode_failure;
  
  if (ensure_cache ())
    {
      found = mark_named_package_for_install (package);
      result_code = operation (true);
      cache_reset ();
    }

  response.encode_int (found && result_code == rescode_success);
}

/* APTCMD_INSTALL_PACKAGE
 *
 * Install a package, using the common "operation ()" code, that
 * installs packages marked for install.
 */

void
cmd_install_package ()
{
  const char *package = request.decode_string_in_place ();
  const char *http_proxy = request.decode_string_in_place ();
  const char *https_proxy = request.decode_string_in_place ();
  int result_code = rescode_failure;

  if (http_proxy)
    {
      setenv ("http_proxy", http_proxy, 1);
      DBG ("http_proxy: %s", http_proxy);
    }

  if (https_proxy)
    {
      setenv ("https_proxy", https_proxy, 1);
      DBG ("https_proxy: %s", https_proxy);
    }

  if (ensure_cache ())
    {
      if (mark_named_package_for_install (package))
	result_code = operation (false);
      else
	result_code = rescode_packages_not_found;
    }

  need_cache_init ();
  response.encode_int (result_code);
}

void
cmd_remove_check ()
{
  const char *package = request.decode_string_in_place ();

  if (ensure_cache ())
    {
      AptWorkerState *state = AptWorkerState::GetCurrent ();
      pkgDepCache &cache = *(state->cache);
      pkgCache::PkgIterator pkg = cache.FindPkg (package);

      if (!pkg.end ())
	{
	  mark_for_remove (pkg);
	  
	  for (pkgCache::PkgIterator pkg = cache.PkgBegin();
	       pkg.end() != true;
	       pkg++)
	    {
	      if (cache[pkg].Delete())
		response.encode_string (pkg.Name());
	    }

	  cache_reset ();
	}
    }

  response.encode_string (NULL);
}

void
cmd_remove_package ()
{
  const char *package = request.decode_string_in_place ();
  int result_code = rescode_failure;

  if (ensure_cache ())
    {
      AptWorkerState *state = AptWorkerState::GetCurrent ();
      pkgDepCache &cache = *(state->cache);
      pkgCache::PkgIterator pkg = cache.FindPkg (package);

      if (!pkg.end ())
	{
	  mark_for_remove (pkg);
	  result_code = operation (false);
	}
    }

  need_cache_init ();
  response.encode_int (result_code == rescode_success);
}

/* Package source domains.

   Package sources are classified into domains, depending on which key
   was used to sign their Release file.  When upgrading a package, the
   update must come from the same domain as the installed package, or
   from one that 'dominates' it.

   Domains are identified by symbolic names.  The defining property of
   a domain is the list of keys.  Any source that is signed by one of
   these keys is associated with that domain.

   In addition to the explicitly defined domains (see below), there
   are two implicitly defined domains, "signed" and "unsigned".  The
   "signed" domain includes all sources that are signed but are not
   associated with any of the explicit domains.  The "unsigned" domain
   includes all the unsigned sources.

   The explicitly defined domains can be declared to be "certified".
   When a package is installed or upgraded from such a domain, the
   does not need to agree to a legal disclaimer.

   No explicit domain dominates any other explicit domain.  All trust
   domains dominate the "signed" and "unsigned" domains, and "signed"
   dominates "unsigned".  Thus, the dominance rules are hard coded.

   Domains are also used when installing a package for the first time:
   when a given package version is available from more than one
   source, sources belonging to any explicit domain or the "signed"
   domain are preferred over the "unsigned" domain.  You can influence
   which source is used via the normal apt source priorities.

   One domain is designated as the default domain (usually the Nokia
   certified domain).  When storing and reporting information about
   domains, only the non-default domains are explicitly mentioned.
   This saves some space and time when most of the packages are in the
   default domain, which they usually are.
*/

static pkgSourceList *cur_sources = NULL;

static debReleaseIndex *
find_deb_meta_index (pkgIndexFile *index)
{
  if (cur_sources == NULL)
    return NULL;

  for (pkgSourceList::const_iterator I = cur_sources->begin();
       I != cur_sources->end(); I++)
    {
      vector<pkgIndexFile *> *Indexes = (*I)->GetIndexFiles();
      for (vector<pkgIndexFile *>::const_iterator J = Indexes->begin();
	   J != Indexes->end(); J++)
	{
	  if ((*J) == index)
	    {
	      if (strcmp ((*I)->GetType(), "deb") == 0)
		return (debReleaseIndex *)(*I);
	      else
		return NULL;
	    }
	}
    }
  
  return NULL;
}

static char *
get_meta_info_key (debReleaseIndex *meta)
{
  string file = meta->MetaIndexFile ("Release.gpg.info");
  char *key = NULL;

  FILE *f = fopen (file.c_str(), "r");
  if (f)
    {
      char *line = NULL;
      size_t len = 0;
      ssize_t n;

      while ((n = getline (&line, &len, f)) != -1)
	{
	  if (n > 0 && line[n-1] == '\n')
	    line[n-1] = '\0';

	  if (g_str_has_prefix (line, "VALIDSIG"))
	    {
	      key = g_strchug (g_strdup (line + 8)); // *Cough*
	      break;
	    }
	}

      free (line);
      fclose (f);
    }

  return key;
}

static int
get_domain (pkgIndexFile *index)
{
  if (index->IsTrusted ())
    {
      debReleaseIndex *meta = find_deb_meta_index (index);
      if (meta)
	{
	  domain_t d = find_domain_by_uri (meta->GetURI().c_str());
	  if (d != DOMAIN_SIGNED)
	    return d;

	  char *key = get_meta_info_key (meta);
	  if (key)
	    {
	      domain_t d = find_domain_by_key (key);
	      g_free (key);
	      return d;
	    }
	}
      return DOMAIN_SIGNED;
    }
  else
    return DOMAIN_UNSIGNED;
}

static bool
domain_dominates_or_is_equal (int a, int b)
{
  return domains[a].trust_level >= domains[b].trust_level;
}

static void
reset_new_domains ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();

  for (unsigned int i = 0; i < state->package_count; i++)
    state->extra_info[i].new_domain = DOMAIN_UNSIGNED;
}

static void
collect_new_domains ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();

  for (unsigned int i = 0; i < state->package_count; i++)
    if (state->extra_info[i].related)
      state->extra_info[i].cur_domain = state->extra_info[i].new_domain;
}

static int
index_trust_level_for_package (pkgIndexFile *index,
			       const pkgCache::VerIterator &ver)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);
  pkgCache::PkgIterator pkg = ver.ParentPkg();

  int cur_level = domains[state->extra_info[pkg->ID].cur_domain].trust_level;

  int index_domain = get_domain (index);
  int index_level = domains[index_domain].trust_level;

  DBG ("trust_level: cur %d index %d (%s)",
       cur_level, index_level, domains[index_domain].name);

  /* If we have already found a good domain, accept this one only if
     it is the same domain, or strictly better.
   */
  if (state->extra_info[pkg->ID].new_domain != DOMAIN_UNSIGNED)
    {
      int new_level =
	domains[state->extra_info[pkg->ID].new_domain].trust_level;

      if (index_domain == state->extra_info[pkg->ID].new_domain
	  || index_level > new_level)
	{
	  state->extra_info[pkg->ID].new_domain = index_domain;
	  return index_level;
	}
      else
	return -1;
    }

  /* If this is a new install, accept the first domain that comes
     along.
   */
  if (cache[pkg].NewInstall())
    {
      DBG ("new: accept index");
      state->extra_info[pkg->ID].new_domain = index_domain;
      return index_level;
    }

  /* This is an upgrade, only accept the current domain, or ones that
     dominate it.
  */
  if (index_level >= cur_level)
    {
      DBG ("upgrade: accept better");
      state->extra_info[pkg->ID].new_domain = index_domain;
      return index_level;
    }

  /* This index is as good as any other.
   */
  DBG ("Hmm");
  return cur_level;
}

static void
encode_trust_summary ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  pkgDepCache &cache = *(state->cache);

  for (pkgCache::PkgIterator pkg = cache.PkgBegin();
       pkg.end() != true;
       pkg++)
    {
      domain_t cur_domain = state->extra_info[pkg->ID].cur_domain;
      domain_t new_domain = state->extra_info[pkg->ID].new_domain;

      if (cache[pkg].Upgrade() && !domains[new_domain].is_certified)
	{
	  DBG ("not certified: %s", pkg.Name());
	  response.encode_int (pkgtrust_not_certified);
	  response.encode_string (pkg.Name());
	}

      if (cache[pkg].Upgrade() && !cache[pkg].NewInstall()
	  && !domain_dominates_or_is_equal (new_domain, cur_domain))
	{
	  log_stderr ("domain change: %s (%s -> %s)",
		      pkg.Name(),
		      domains[cur_domain].name,
		      domains[new_domain].name);
	  response.encode_int (pkgtrust_domains_violated);
	  response.encode_string (pkg.Name());
	}
    }

  response.encode_int (pkgtrust_end);
}

static void
encode_upgrades ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  if (ensure_cache ())
    {
      pkgDepCache &cache = *(state->cache);

      for (pkgCache::PkgIterator pkg = cache.PkgBegin();
	   pkg.end() != true;
	   pkg++)
	{
	  if (cache[pkg].Upgrade() && !cache[pkg].NewInstall())
	    {
	      response.encode_string (pkg.Name());
	      response.encode_string (cache[pkg].CandVersion);
	    }
	}
    }

  response.encode_string (NULL);
}

/* We modify the pkgDPkgPM package manager so that we can provide our
   own method of constructing the 'order list', the ordered list of
   packages to handle.  We do this to ignore packages that should be
   kept.  Ordinarily, if a package should be kept but needs to be
   reinstalled or configured, libapt-pkg will try to do that with
   every operation.

   XXX - there might be a way to get the same effect by cleverly
         manipulating the pkgDepCache, but I would have to look
         harder...
*/

class myDPkgPM : public pkgDPkgPM
{
public:

  bool CreateOrderList ();

  myDPkgPM(pkgDepCache *Cache);
};

bool
myDPkgPM::CreateOrderList ()
{
  if (pkgPackageManager::List != 0)
    return true;
   
  delete pkgPackageManager::List;
  pkgPackageManager::List = new pkgOrderList(&Cache);
   
  // Generate the list of affected packages and sort it
  for (PkgIterator I = Cache.PkgBegin(); I.end() == false; I++)
    {
      // Ignore no-version packages
      if (I->VersionList == 0)
	continue;
      
      // Not interesting
      if ((Cache[I].Keep() == true || 
	   Cache[I].InstVerIter(Cache) == I.CurrentVer()) && 
	  I.State() == pkgCache::PkgIterator::NeedsNothing &&
	  (Cache[I].iFlags & pkgDepCache::ReInstall) != pkgDepCache::ReInstall &&
	  (I.Purge() != false || Cache[I].Mode != pkgDepCache::ModeDelete ||
	   (Cache[I].iFlags & pkgDepCache::Purge) != pkgDepCache::Purge))
	continue;
      
      // Ignore interesting but kept packages, except when they are
      // related to the current operation.

      if (Cache[I].Keep() == true && !is_related (I))
	{
	  log_stderr ("Not handling unrelated package %s.", I.Name());
	  continue;
	}
      
      DBG ("Handling interesting package %s.", I.Name());

      // Append it to the list
      pkgPackageManager::List->push_back(I);      
    }
   
  return true;
}

myDPkgPM::myDPkgPM (pkgDepCache *Cache)
  : pkgDPkgPM (Cache)
{
}

static int
combine_rescodes (int all, int one)
{
  if (all == rescode_success)
    return one;
  else if (all == one)
    return all;
  else
    return rescode_failure;
}

/* operation () is used to run pending apt operations
 * (removals or installations). If check_only parameter is
 * enabled, it will only check if the operation is doable.
 *
 * operation () is used from cmd_install_package,
 * cmd_install_check and cmd_remove_package
 */

int
operation (bool check_only)
{
   AptWorkerState *state = AptWorkerState::GetCurrent ();
   pkgCacheFile &Cache = *(state->cache);

   if (_config->FindB("APT::Get::Purge",false) == true)
     {
       pkgCache::PkgIterator I = Cache->PkgBegin();
       for (; I.end() == false; I++)
	 {
	   if (I.Purge() == false && Cache[I].Mode == pkgDepCache::ModeDelete)
	     Cache->MarkDelete(I,true);
	 }
     }

   if (Cache->DelCount() == 0 && Cache->InstCount() == 0 &&
       Cache->BadCount() == 0)
      return rescode_success;
   
   // Create the text record parser
   pkgRecords Recs (Cache);
   if (_error->PendingError() == true)
      return rescode_failure;
   
   // Lock the archive directory
   FileFd Lock;
   if (_config->FindB("Debug::NoLocking",false) == false)
   {
      Lock.Fd(ForceLock(_config->FindDir("Dir::Cache::Archives") + "lock"));
      if (_error->PendingError() == true)
	{
	  _error->Error("Unable to lock the download directory");
	  return rescode_failure;
	}
   }
   
   // Create the download object
   DownloadStatus Stat;
   pkgAcquire Fetcher (&Stat);

   // Read the source list
   pkgSourceList List;
   if (List.ReadMainList() == false)
     {
       _error->Error("The list of sources could not be read.");
       return rescode_failure;
     }
   
   cur_sources = &List;

   // Create the package manager
   //
   SPtr<myDPkgPM> Pm = new myDPkgPM (Cache);
   
   // Create the order list explicitely in a way that we like.  We
   // have to do it explicitely since CreateOrderList is not virtual.
   //
   if (!Pm->CreateOrderList ())
     return rescode_failure;

   // Prepare to download
   //
   reset_new_domains ();
   if (Pm->GetArchives(&Fetcher,&List,&Recs) == false || 
       _error->PendingError() == true)
     return rescode_failure;

   double FetchBytes = Fetcher.FetchNeeded();
   double FetchPBytes = Fetcher.PartialPresent();
   double DebBytes = Fetcher.TotalNeeded();

   if (_error->PendingError() == true)
      return rescode_failure;

   if (check_only)
     {
       encode_trust_summary ();
       encode_upgrades ();
       return rescode_success;
     }

   collect_new_domains ();

   /* Check for enough free space. */
   {
     struct statvfs Buf;
     string OutputDir = _config->FindDir("Dir::Cache::Archives");
     if (statvfs(OutputDir.c_str(),&Buf) != 0)
       {
	 _error->Errno("statvfs","Couldn't determine free space in %s",
		       OutputDir.c_str());
	 return rescode_failure;
       }

     if (unsigned(Buf.f_bfree) < (FetchBytes - FetchPBytes)/Buf.f_bsize)
       {
	 _error->Error("You don't have enough free space in %s.",
		       OutputDir.c_str());
	 return rescode_out_of_space;
       }
   }
   
   /* Send a status report now if we are going to download something.
      This makes sure that the progress dialog is shown even if the
      first pulse of the fetcher takes a long time to arrive.
   */
   if ((int)(FetchBytes - FetchPBytes) > 0)
     send_status (op_downloading, 0, (int)(FetchBytes - FetchPBytes), 0);

   if (Fetcher.Run() == pkgAcquire::Failed)
     return rescode_failure;
      
   /* Print out errors and distill the failure reasons into a
      apt_proto_rescode.
   */
   int result = rescode_success;
   for (pkgAcquire::ItemIterator I = Fetcher.ItemsBegin();
	I != Fetcher.ItemsEnd(); I++)
     {
       if ((*I)->Status == pkgAcquire::Item::StatDone &&
	   (*I)->Complete == true)
	 continue;
       
       if ((*I)->Status == pkgAcquire::Item::StatIdle)
	 continue;

       fprintf (stderr, 
		"Failed to fetch %s: %s\n",
		(*I)->DescURI().c_str(),
		(*I)->ErrorText.c_str());

       int this_result;

       if (g_str_has_prefix ((*I)->ErrorText.c_str(), "404"))
	 this_result = rescode_packages_not_found;
       else if (g_str_has_prefix ((*I)->ErrorText.c_str(), "Size mismatch"))
	 this_result = rescode_package_corrupted;
       else if (g_str_has_prefix ((*I)->ErrorText.c_str(), "MD5Sum mismatch"))
       	 this_result = rescode_package_corrupted;
       else
       	 this_result = rescode_failure;

       result = combine_rescodes (result, this_result);
     }

   if (result != rescode_success)
     return (result == rescode_failure)? rescode_download_failed : result;
      
   send_status (op_general, -1, 0, 0);
      
   _system->UnLock();
   pkgPackageManager::OrderResult Res = Pm->DoInstall (status_fd);
   _system->Lock();

   save_extra_info ();

   if (Res == pkgPackageManager::Failed || _error->PendingError() == true)
     return rescode_failure;

   if (Res == pkgPackageManager::Completed)
     return rescode_success;
     
   return rescode_failure;
}

/* APTCMD_CLEAN
 */

void
cmd_clean ()
{
  bool success = true;
  AptWorkerState *state = AptWorkerState::GetCurrent ();

  // Try to lock the archive directory.  If that fails because we are
  // out of space, continue anyway since it is critical to free flash
  // in that case.
  //
  // ForceLock has the same interface as GetLock: it returns -1 in
  // case of failure with errno set appropriately.  However, errnor is
  // always EPERM when the open syscall failed.  "Feh.."

  FileFd Lock;
  if (_config->FindB("Debug::NoLocking",false) == false)
    {
      int fd = ForceLock(_config->FindDir("Dir::Cache::Archives") + "lock");
      if (fd < 0)
	{
	  if (errno != EPERM && errno != ENOSPC)
	    {
	      success = false;
	      _error->Error("Unable to lock the download directory");
	    }
	  else
	    _error->Warning("Unable to lock the download directory, but cleaning it anyway.");
	}
      else
	Lock.Fd (fd);
    }
   
  if (success)
    {
      pkgAcquire Fetcher;
      Fetcher.Clean(_config->FindDir("Dir::Cache::archives"));
      Fetcher.Clean(_config->FindDir("Dir::Cache::archives") + "partial/");
    }

  response.encode_int (success);

  // As a special case, we try to init the cache again.  Chances are
  // good that it will now succeed because there might be more space
  // available now.  We don't use ensure_cache for this since we want
  // it to happen silently.

  if (state->cache == NULL)
    need_cache_init ();
}

static char *
escape_for_shell (const char *str)
{
  char buf[2000];
  char *p = buf;

  /* Enclose the string in single quotes and escape single quotes.
   */

  *p++ = '\'';
  while (*str)
    {
      if (p >= buf+sizeof(buf)-6)
	return NULL;

      if (*str == '\'')
	{
	  // Don't you love bourne shell syntax?
	  *p++ = '\'';
	  *p++ = '\\';
	  *p++ = '\'';
	  *p++ = '\'';
	  str++;
	}
      else
	*p++ = *str++;
    }
  *p++ = '\'';
  *p++ = '\0';
  return g_strdup (buf);
}

// XXX - interpret status codes

static char *
get_deb_record (const char *filename)
{
  char *esc_filename = escape_for_shell (filename);
  if (esc_filename == NULL)
    return NULL;

  char *cmd = g_strdup_printf ("/usr/bin/dpkg-deb -f %s", esc_filename);
  fprintf (stderr, "%s\n", cmd);
  FILE *f = popen (cmd, "r");

  g_free (cmd);
  g_free (esc_filename);

  if (f)
    {
      const size_t incr = 2000;
      char *record = NULL;
      size_t size = 0;

      do
	{
	  // increase buffer and try to fill it, leaving room for the
	  // trailing newlines and nul.
	  // XXX - do it properly.
	  
	  char *new_record = new char[size+incr+3];
	  if (record)
	    {
	      memcpy (new_record, record, size);
	      delete record;
	    }
	  record = new_record;
	  
	  size += fread (record+size, 1, incr, f);
	}
      while (!feof (f));

      int status = pclose (f);
      if (status != 0)
	{
	  delete (record);
	  return NULL;
	}

      record[size] = '\n';
      record[size+1] = '\n';
      record[size+2] = '\0';
      return record;
    }
  return NULL;
}

static bool
check_dependency (string &package, string &version, unsigned int op)
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();
  if (!ensure_cache ())
    return false;

  pkgDepCache &cache = (*(state->cache));
  pkgCache::PkgIterator pkg;
  pkgCache::VerIterator installed;

  pkg = cache.FindPkg (package);
  if (pkg.end ())
    return false;

  installed = pkg.CurrentVer ();
  if (installed.end ())
    {
      // might be a virtual package, check the provides list.

      pkgCache::PrvIterator P = pkg.ProvidesList();

      for (; P.end() != true; P++)
	{
	  // Check if the provides is a hit
	  if (P.OwnerPkg().CurrentVer() != P.OwnerVer())
	    continue;

	  // Compare the versions.
	  if (debVS.CheckDep (P.ProvideVersion(), op, version.c_str ()))
	    return true;
	}

      return false;
    }
  else
    return debVS.CheckDep (installed.VerStr (), op, version.c_str ());
}

static void
add_dep_string (string &str,
		string &package, string &version, unsigned int op)
{
  str += package;
  if (op != pkgCache::Dep::NoOp)
    {
      str += " (";
      str += pkgCache::CompType (op);
      str += " ";
      str += version;
      str += ")";
    }
}

static int
check_and_encode_missing_dependencies (const char *deps, const char *end,
				       bool only_check)
{
  const char *ptr;
  string package, version;
  unsigned int op;
  bool dep_ok = true;

  ptr = deps;
  while (true)
    {
      // check one 'or group'

      bool group_ok = false;
      string group_string = "";

      while (true)
	{
	  ptr = debListParser::ParseDepends (ptr, end,
					     package, version, op,
					     false);
	  if (ptr == NULL)
	    {
	      cerr << "Error parsing depends list\n";
	      return false;
	    }

	  if (only_check && package == "maemo")
	    return status_incompatible_current;

	  add_dep_string (group_string, package, version, op);

	  if (!group_ok)
	    group_ok = check_dependency (package, version,
					 op & ~pkgCache::Dep::Or);

	  if ((op & pkgCache::Dep::Or) == 0)
	    break;

	  group_string += " | ";
	}

      if (!group_ok)
	{
	  if (only_check)
	    cerr << "FAILED: " << group_string << "\n";
	  else
	    {
	      response.encode_int (sumtype_missing);
	      response.encode_string (group_string.c_str ());
	    }
	  dep_ok = false;
	}

      if (ptr == end)
	break;
    }

  return dep_ok? status_able : status_missing;
}

static bool
get_field (pkgTagSection *section, const char *field,
	   const char *&start, const char *&end)
{
  if (section->Find (field, start, end))
    {
      // fprintf (stderr, "%s = %.*s\n", field, end-start, start);
      return true;
    }
  else
    {
      // fprintf (stderr, "%s = <undefined>\n", field);
      return false;
    }
}

static int
get_field_int (pkgTagSection *section, const char *field, int def)
{
  const char *start, *end;
  if (get_field (section, field, start, end))
    return atoi (start);
  else
    return def;
}

static void
encode_field (pkgTagSection *section, const char *field,
	      const char *def = "")
{
  const char *start, *end;
  if (get_field (section, field, start, end))
    response.encode_stringn (start, end-start);
  else
    response.encode_string (def);
}

static void
encode_localized_field (pkgTagSection *section, const char *field,
			const char *def = "")
{
  const char *start, *end;

  if (lc_messages && *lc_messages)
    {
      char *locale_field = g_strdup_printf ("%s-%s", field, lc_messages);
      if (get_field (section, locale_field, start, end))
	{
	  response.encode_stringn (start, end-start);
	  g_free (locale_field);
	  return;
	}
      g_free (locale_field);
    }
  
  if (get_field (section, field, start, end))
    response.encode_stringn (start, end-start);
  else
    response.encode_string (def);
}

static bool
substreq (const char *start, const char *end, const char *str)
{
  return end-start == strlen (str) && !strncmp (start, str, end-start);
}

static int
check_installable (pkgTagSection &section, bool only_user)
{
  int installable_status = status_able;
  const char *start, *end;

  if (!get_field (&section, "Architecture", start, end)
      || !(substreq (start, end, DEB_HOST_ARCH)
	   || substreq (start, end, "all")))
    installable_status = status_incompatible;
    
  if (only_user
      && (!get_field (&section, "Section", start, end)
	  || !is_user_section (start, end)))
    {
      /* Put more information for developers into the log.  They will
	 likely be confused by the "incompatible" error message when
	 testing a package that has not been properly 'sectionized'.
       */
      fprintf (stderr,
	       "Package must have \"Section: user/FOO\" "
	       "to be considered compatible.\n");
      installable_status = status_incompatible;
    }

  if (get_field (&section, "Pre-Depends", start, end))
    installable_status =
      combine_status (check_and_encode_missing_dependencies (start, end, true),
		      installable_status);

  if (get_field (&section, "Depends", start, end))
    installable_status = 
      combine_status (check_and_encode_missing_dependencies (start, end, true),
		      installable_status);

  return installable_status;
}

static void
encode_missing_dependencies (pkgTagSection &section)
{
  const char *start, *end;

  if (get_field (&section, "Pre-Depends", start, end))
    check_and_encode_missing_dependencies (start, end, false);

  if (get_field (&section, "Depends", start, end))
    check_and_encode_missing_dependencies (start, end, false);
}

void
cmd_get_file_details ()
{
  bool only_user = request.decode_int ();
  const char *filename = request.decode_string_in_place ();

  char *record = get_deb_record (filename);
  pkgTagSection section;
  if (record == NULL || !section.Scan (record, strlen (record)))
    {
      response.encode_string (basename (filename));
      response.encode_string (basename (filename));
      response.encode_string (NULL);      // installed_version
      response.encode_int64 (0);          // installed_size
      response.encode_string ("");        // version
      response.encode_string ("");        // maintainer
      response.encode_string ("");        // section
      response.encode_int (status_corrupted);
      response.encode_int64 (0);          // installed size
      response.encode_string ("");        // description
      response.encode_string (NULL);      // icon
      response.encode_int (sumtype_end);
      return;
    }

  int installable_status = check_installable (section, only_user);

  const char *installed_version = NULL;
  int64_t installed_size = 0;

  if (ensure_cache ())
    {
      AptWorkerState *state = AptWorkerState::GetCurrent ();
      pkgDepCache &cache = *(state->cache);
      pkgCache::PkgIterator pkg = cache.FindPkg (section.FindS ("Package"));
      if (!pkg.end ())
	{
	  pkgCache::VerIterator cur = pkg.CurrentVer ();
	  if (!cur.end ())
	    {
	      installed_version = cur.VerStr ();
	      installed_size = cur->InstalledSize;
	    }
	}
    }

  encode_field (&section, "Package");
  encode_localized_field (&section, "Maemo-Display-Name", NULL);
  response.encode_string (installed_version);
  response.encode_int64 (installed_size);
  encode_field (&section, "Version");
  encode_field (&section, "Maintainer");
  encode_field (&section, "Section");
  response.encode_int (installable_status);
  response.encode_int64 (1024LL * get_field_int (&section, "Installed-Size", 0)
		       - installed_size);
  encode_localized_field (&section, "Description");
  encode_field (&section, "Maemo-Icon-26", NULL);

  if (installable_status != status_able)
    encode_missing_dependencies (section);
  response.encode_int (sumtype_end);

  delete[] record;
}

void
cmd_install_file ()
{
  const char *filename = request.decode_string_in_place ();
  char *esc_filename = escape_for_shell (filename);
  
  if (esc_filename == NULL)
    {
      response.encode_int (0);
      return;
    }

  _system->UnLock();

  char *cmd = g_strdup_printf ("/usr/bin/dpkg --install %s", esc_filename);
  fprintf (stderr, "%s\n", cmd);
  int res = system (cmd);
  g_free (cmd);

  if (res)
    {
#if 0
      /* We do not remove packages that failed to install since this
	 might have been an update attempt and removing the old
	 version is confusing.  Installations at this point do not
	 fail because of missing dependencies, so it is not that
	 important anymore to try to leave the system in a consistent
	 state.
      */
      char *cmd =
	g_strdup_printf ("/usr/bin/dpkg --purge "
			 "`/usr/bin/dpkg-deb -f %s Package`",
			 esc_filename);
      fprintf (stderr, "%s\n", cmd);
      system (cmd);
      g_free (cmd);
#endif
    }

  g_free (esc_filename);

  _system->Lock();

  need_cache_init ();
  response.encode_int (res == 0);
}

/* APTCMD_SAVE_BACKUP_DATA

   This method is used to store the list of installed packages. It's
   used in backup machinery to restore the installed applications
   from their repositories.
 */

static xexp *
get_backup_catalogues ()
{
  xexp *catalogues = xexp_read_file (CATALOGUE_CONF);
  if (catalogues)
    {
      xexp *c = xexp_first (catalogues);
      while(c)
	{
	  xexp *r = xexp_rest (c);
	  if (xexp_aref_bool (c, "nobackup"))
	    xexp_del (catalogues, c);
	  c = r;
	}
    }

  return catalogues;
}

static xexp *
get_backup_packages ()
{
  AptWorkerState *state = AptWorkerState::GetCurrent ();

  if (!ensure_cache ())
    return NULL;

  xexp *packages = xexp_list_new ("backup");

  pkgDepCache &cache = *(state->cache);

  for (pkgCache::PkgIterator pkg = cache.PkgBegin(); !pkg.end (); pkg++)
    {
      pkgCache::VerIterator installed = pkg.CurrentVer ();

      // skip not-installed packages
      //
      if (installed.end ())
	continue;

      // skip non user packages
      //
      if (!installed.end ()
	  && !is_user_package (installed))
	continue;

      xexp_cons (packages, xexp_text_new ("pkg", pkg.Name ()));
    }

  return packages;
}

void
cmd_save_backup_data ()
{
  xexp *catalogues = get_backup_catalogues ();
  if (catalogues)
    {
      xexp_write_file (BACKUP_CATALOGUES, catalogues);
      xexp_free (catalogues);
    }
  
  xexp *packages = get_backup_packages ();
  if (packages)
    {
      xexp_write_file (BACKUP_PACKAGES, packages);
      xexp_free (packages);
    }
}
