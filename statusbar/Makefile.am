AM_CPPFLAGS = -I $(top_srcdir)/src

AM_CFLAGS = \
	-Wall -Wmissing-prototypes -Wmissing-declarations -Werror \
	-ansi -std=gnu99

EXTRA_DIST = \
	hildon-update-notifier.desktop

hildonstatusmenudesktopentry_DATA = \
	hildon-update-notifier.desktop

hildondesktoplib_LTLIBRARIES = \
	libhildon-update-notifier.la

libhildon_update_notifier_la_CFLAGS = \
	$(NOTIFIER_DEPS_CFLAGS)

libhildon_update_notifier_la_LIBADD = \
	$(NOTIFIER_DEPS_LIBS)

libhildon_update_notifier_la_LDFLAGS = \
	-module -avoid-version -no-undefined

libhildon_update_notifier_la_SOURCES = 	\
	update-notifier.h		\
	update-notifier.c		\
	update-notifier-conf.h		\
	hn-app-pixbuf-anim-blinker.h	\
	hn-app-pixbuf-anim-blinker.c	\
	$(top_srcdir)/src/xexp.h	\
	$(top_srcdir)/src/xexp.c	\
	$(top_srcdir)/src/user_files.h	\
	$(top_srcdir)/src/user_files.c
