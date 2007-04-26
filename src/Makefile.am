bin_PROGRAMS = hildon-application-manager.launch \
               hildon-application-manager-config
noinst_PROGRAMS = hildon-application-manager.run mime-open mime-server
libexec_PROGRAMS = apt-worker

hildon_application_manager_launch_SOURCES = main.h			\
                                            main.cc			\
					    operations.h		\
					    operations.cc		\
					    util.h			\
					    util.cc			\
					    details.h			\
					    details.cc			\
					    menu.h			\
					    menu.cc			\
					    log.h			\
					    log.cc			\
					    settings.h			\
					    settings.cc			\
					    search.h			\
					    search.cc			\
					    repo.h			\
					    repo.cc			\
                                            instr.h			\
                                            instr.cc			\
					    hildonbreadcrumbtrail.h	\
					    hildonbreadcrumbtrail.c	\
					    xexp.h			\
					    xexp.c			\
					    apt-worker-client.h		\
					    apt-worker-client.cc	\
					    apt-worker-proto.h		\
					    apt-worker-proto.cc

hildon_application_manager_launch_CFLAGS =   $(HAM_DEPS_CFLAGS) -shared
hildon_application_manager_launch_CXXFLAGS = $(HAM_DEPS_CFLAGS) -shared \
                                             -fno-rtti -fno-exceptions
hildon_application_manager_launch_LDFLAGS =  -shared \
                                             -Wl,--version-script=export.map
hildon_application_manager_launch_LDADD =    $(HAM_DEPS_LIBS)

hildon_application_manager_run_SOURCES =  $(hildon_application_manager_launch_SOURCES)
hildon_application_manager_run_CFLAGS =   $(HAM_DEPS_CFLAGS)
hildon_application_manager_run_CXXFLAGS = $(HAM_DEPS_CFLAGS)
hildon_application_manager_run_LDADD =    $(HAM_DEPS_LIBS)

hildon_application_manager_config_SOURCES =		\
               hildon-application-manager-config.cc	\
               xexp.h					\
               xexp.c					\
               confutils.h				\
               confutils.cc
hildon_application_manager_config_CFLAGS = $(AW_DEPS_CFLAGS)
hildon_application_manager_config_CXXFLAGS = $(AW_DEPS_CFLAGS)
hildon_application_manager_config_LDADD = $(AW_DEPS_LIBS)

apt_worker_SOURCES = apt-worker.cc \
		     xexp.h \
                     xexp.c \
                     apt-worker-proto.h \
                     apt-worker-proto.cc

apt_worker_CFLAGS = $(AW_DEPS_CFLAGS)
apt_worker_CXXFLAGS = $(AW_DEPS_CFLAGS)
apt_worker_LDADD = $(AW_DEPS_LIBS) -lapt-pkg

mime_open_SOURCES = mime-open.c
mime_open_CFLAGS =   $(HAM_DEPS_CFLAGS)
mime_open_CXXFLAGS = $(HAM_DEPS_CFLAGS)
mime_open_LDADD =    $(HAM_DEPS_LIBS)

mime_server_SOURCES = mime-server.c
mime_server_CFLAGS =   $(HAM_DEPS_CFLAGS)
mime_server_CXXFLAGS = $(HAM_DEPS_CFLAGS)
mime_server_LDADD =    $(HAM_DEPS_LIBS)

EXTRA_DIST = export.map

install-exec-local:
	rm -f $(DESTDIR)$(bindir)/hildon-application-manager
	ln -s /usr/bin/maemo-invoker $(DESTDIR)$(bindir)/hildon-application-manager
