/*
 * This file is part of hildon-application-manager.
 *
 * Original version is part of osso-backup.
 *
 * Copyright (C) 2007, 2008 Nokia Corporation.
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


#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <libhildonwm/hd-wm.h>

#define RUN_TEST_BLURB                                                          \
        "\n"                                                                    \
        "IMPORTANT:\n"                                                          \
        "\n"                                                                    \
        "This test will kill applications currently open in your build\n"       \
        "environment, you should be aware of this before start this test.\n"    \
        "\n"                                                                    \
        "If you want to continue, you can run this test with -r or --run.\n"    \
        "\n"


static gboolean      run_tests = FALSE;
static gboolean      list_apps = FALSE;
static const gchar  *veto = NULL;
static GOptionEntry  entries[] = {
        { "run", 'r', 0, G_OPTION_ARG_NONE, &run_tests, "Actually start the test.", NULL },
        { "list-apps", 'l', 0, G_OPTION_ARG_NONE, &list_apps, "Just list applications.", NULL },
        { "veto", 'v', 0, G_OPTION_ARG_STRING, &veto, "Ignore a particular application when killing all.", NULL },
        { NULL }
};

static void
test_get_apps (HDWM *hdwm)
{
        GList *applications, *l;

        hd_wm_update_client_list (hdwm);
        applications = hd_wm_get_applications (hdwm);

        g_print ("Found %d applications\n", g_list_length (applications));
        
        for (l = applications; l; l = l->next) {
                HDWMEntryInfo *info;

                info = l->data;

                g_print ("\tApp:'%s', Title:'%s', has %d children, active:%s, hibernating:%s\n", 
                         hd_wm_entry_info_get_app_name (info),
                         hd_wm_entry_info_get_title (info),
                         hd_wm_entry_info_get_n_children (info),
                         hd_wm_entry_info_is_active (info) ? "Yes" : "No", 
                         hd_wm_entry_info_is_hibernating (info) ? "Yes" : "No");
        }
}

static void
test_kill_apps (HDWM *hdwm)
{
        GList *applications, *l;

        hd_wm_update_client_list (hdwm);
        applications = hd_wm_get_applications (hdwm);

        g_print ("Closing %d applications:\n", g_list_length (applications));

        for (l = applications; l; l = l->next) {
                HDWMEntryInfo *info;

                info = l->data;

		if (veto) {
			const gchar *name;
			
			name = hd_wm_entry_info_get_app_name (info);
			if (name && strcmp (veto, name) == 0) {
                		g_print ("\tApp:'%s', Title:'%s' (IGNORED)\n", 
		                         hd_wm_entry_info_get_app_name (info),
                		         hd_wm_entry_info_get_title (info));
				continue;
			}
		}	

                g_print ("\tApp:'%s', Title:'%s'\n", 
                         hd_wm_entry_info_get_app_name (info),
                         hd_wm_entry_info_get_title (info));

                hd_wm_close_application (hdwm, info);
        }
}

int
main (int argc, char **argv)
{
        HDWM           *hdwm;
        GOptionContext *context;

        gtk_init (&argc,&argv);

        context = g_option_context_new ("- test app killing hildon-desktop API");
	g_option_context_add_main_entries (context, entries, NULL);
        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        if (!run_tests && !list_apps) {
                g_printerr (RUN_TEST_BLURB);
                return EXIT_SUCCESS;
        }

        hdwm = hd_wm_get_singleton ();

        if (list_apps) {
                test_get_apps (hdwm);
                return EXIT_SUCCESS;
        }

        test_kill_apps (hdwm);

	return EXIT_SUCCESS;
}

