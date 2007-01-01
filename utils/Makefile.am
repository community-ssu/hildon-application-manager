CXXFLAGS = $(CFLAGS)

bin_PROGRAMS = maemo-application-running \
               maemo-confirm-text.launch

bin_SCRIPTS = maemo-confirm-text maemo-list-user-packages

maemo_confirm_text_launch_SOURCES = maemo-confirm-text-user.c
maemo_confirm_text_launch_CFLAGS = $(AI_DEPS_CFLAGS) -shared
maemo_confirm_text_launch_LDFLAGS = -shared -Wl,--version-script=export.map
maemo_confirm_text_launch_LDADD = $(AI_DEPS_LIBS)

EXTRA_DIST = maemo-confirm-text maemo-list-user-packages export.map
