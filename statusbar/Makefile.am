AM_CPPFLAGS = -I $(top_srcdir)/src

AM_CFLAGS = \
	-Wall -Wmissing-prototypes -Wmissing-declarations -Werror \
	-ansi -std=gnu99

EXTRA_DIST = \
	ham-updates.desktop

hildonstatusmenudesktopentry_DATA = \
	ham-updates.desktop

hildondesktoplib_LTLIBRARIES = \
	ham-updates-status-menu-item.la

ham_updates_status_menu_item_la_CFLAGS = \
	$(NOTIFIER_DEPS_CFLAGS)

ham_updates_status_menu_item_la_LIBADD = \
	$(NOTIFIER_DEPS_LIBS)

ham_updates_status_menu_item_la_LDFLAGS = \
	-module -avoid-version -no-undefined

ham_updates_status_menu_item_la_SOURCES = 	\
	util.h				\
	util.c				\
	ham-updates-status-menu-item.h	\
	ham-updates-status-menu-item.c	\
	update-notifier-conf.h		\
	hn-app-pixbuf-anim-blinker.h	\
	hn-app-pixbuf-anim-blinker.c	\
	$(top_srcdir)/src/xexp.h	\
	$(top_srcdir)/src/xexp.c	\
	$(top_srcdir)/src/user_files.h	\
	$(top_srcdir)/src/user_files.c
