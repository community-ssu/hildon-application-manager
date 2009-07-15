AM_CPPFLAGS = -I $(top_srcdir)/src

AM_CFLAGS = \
	-Wall -Wmissing-prototypes -Wmissing-declarations -Werror \
	-ansi -std=gnu99

EXTRA_DIST = \
	ham-updates.desktop 	\
	ham-notifier.desktop

hildonstatusmenudesktopentry_DATA = \
	ham-updates.desktop	\
	ham-notifier.desktop

hildondesktoplib_LTLIBRARIES = \
	ham-updates-status-menu-item.la	\
	ham-notifier-status-menu-item.la

ham_updates_status_menu_item_la_CFLAGS = \
	$(NOTIFIER_DEPS_CFLAGS)

ham_updates_status_menu_item_la_LIBADD = \
	$(NOTIFIER_DEPS_LIBS)

ham_updates_status_menu_item_la_LDFLAGS = \
	-export-symbols ham-updates-status-menu-item.sym \
	-module -avoid-version -no-undefined

ham_updates_status_menu_item_la_SOURCES = 	\
	util.h				\
	util.c				\
	ham-updates.h			\
	ham-updates.c			\
	ham-notifier.h			\
	ham-notifier.c			\
	ham-updates-status-menu-item.h	\
	ham-updates-status-menu-item.c	\
	update-notifier-conf.h		\
	$(top_srcdir)/src/xexp.h	\
	$(top_srcdir)/src/xexp.c	\
	$(top_srcdir)/src/user_files.h	\
	$(top_srcdir)/src/user_files.c

ham_notifier_status_menu_item_la_CFLAGS = \
	$(NOTIFIER_DEPS_CFLAGS)

ham_notifier_status_menu_item_la_LIBADD = \
	$(NOTIFIER_DEPS_LIBS)

ham_notifier_status_menu_item_la_LDFLAGS = \
	-export-symbols ham-notifier-status-menu-item.sym \
	-module -avoid-version -no-undefined

ham_notifier_status_menu_item_la_SOURCES = 	\
	util.h				\
	util.c				\
	ham-updates.h			\
	ham-updates.c			\
	ham-notifier.h			\
	ham-notifier.c			\
	ham-notifier-status-menu-item.h	\
	ham-notifier-status-menu-item.c	\
	update-notifier-conf.h		\
	$(top_srcdir)/src/xexp.h	\
	$(top_srcdir)/src/xexp.c	\
	$(top_srcdir)/src/user_files.h	\
	$(top_srcdir)/src/user_files.c
