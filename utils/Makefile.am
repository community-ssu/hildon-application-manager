CXXFLAGS = $(CFLAGS)

bin_PROGRAMS = maemo-application-running		\
               maemo-confirm-text.launch

dist_bin_SCRIPTS = maemo-confirm-text		\
                   maemo-list-user-packages
noinst_PROGRAMS = maemo-confirm-text.run

maemo_confirm_text_launch_SOURCES = maemo-confirm-text-user.c
maemo_confirm_text_launch_CFLAGS = $(HAM_DEPS_CFLAGS) -shared
maemo_confirm_text_launch_LDFLAGS = -shared -Wl,--version-script=$(srcdir)/export.map
maemo_confirm_text_launch_LDADD = $(HAM_DEPS_LIBS)

maemo_confirm_text_run_SOURCES =  $(maemo_confirm_text_launch_SOURCES)
maemo_confirm_text_run_CFLAGS =   $(HAM_DEPS_CFLAGS)
maemo_confirm_text_run_CXXFLAGS = $(HAM_DEPS_CFLAGS)
maemo_confirm_text_run_LDADD =    $(HAM_DEPS_LIBS)

maemo_select_menu_location_SOURCES = maemo-select-menu-location-main.c

EXTRA_DIST = export.map
