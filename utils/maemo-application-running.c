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

/* This utility will check whether the application specified via a
   .desktop file is currently running.  If it is running, it exits 0;
   if it is not running, it exits 1; and if an error occurs, it exits
   2.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int
find_exec_pid (const char *file)
{
  char fullname[512];
  struct stat file_buf, exe_buf;

  if (stat (file, &file_buf) < 0)
    {
      perror (file);
      exit (2);
    }

  DIR *d = opendir ("/proc");
  if (d)
    {
      struct dirent *e;
      while ((e = readdir (d)))
	{
	  strcpy (fullname, "/proc/");
	  strcat (fullname, e->d_name);
	  strcat (fullname, "/exe");
	  if (stat (fullname, &exe_buf) >= 0)
	    {
	      if (file_buf.st_dev == exe_buf.st_dev
		  && file_buf.st_ino == exe_buf.st_ino)
		{
		  int pid = atoi (e->d_name);
		  closedir (d);
		  return pid;
		}
	    }
	}
      closedir (d);
    }

  return -1;
}

static void
usage ()
{
  fprintf (stderr, "usage: maemo-application-running -x executable-file\n");
  fprintf (stderr, "       maemo-application-running -d app.desktop\n");
}

int
main (int argc, char **argv)
{
  if (argc == 3 && !strcmp (argv[1], "-x"))
    {
      if (find_exec_pid (argv[2]) >= 0)
	return 0;
      else
	return 1;
    }
  else if (argc == 3 && !strcmp (argv[1], "-d"))
    {
      fprintf (stderr, "not yet, sorry\n");
      return 2;
    }
  else
    {
      usage ();
      return 2;
    }
}
