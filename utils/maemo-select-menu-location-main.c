/*
 * This file is part of the hildon-application-manager.
 *
 * Copyright (C) 2007, 2008 Nokia Corporation.  All Rights reserved.
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

/*
 * This utility used to let the user move the menu entry for an
 * application to a new location.  The idea was that this would be
 * done during installation of a new appliction.
 *
 * This utility is now deprecated and does nothing.  We don't want
 * anything to interrupt the installation.  Selecting the default
 * location for a new entry should be done by following some FD.O
 * standard, not in a maemo specific way.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

int
main (int argc, char **argv)
{
  fprintf (stderr, "Calling %s is deprecated.  It does nothing now.\n",
	   argv[0]);
  exit (0);
}
