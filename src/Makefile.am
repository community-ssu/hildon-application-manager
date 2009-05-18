AM_CPPFLAGS = -I $(top_srcdir)/statusbar

bin_PROGRAMS = hildon-application-manager \
               hildon-application-manager-config
dist_bin_SCRIPTS = hildon-application-manager-util
noinst_PROGRAMS = hildon-application-manager.run mime-open mime-server test-app-killer
libexec_PROGRAMS = apt-worker ham-after-boot

hildon_application_manager_SOURCES = main.h			\
                                            main.cc			\
					    operations.h		\
					    operations.cc		\
					    package-info-cell-renderer.h \
					    package-info-cell-renderer.c \
					    ham-long-label.h		\
					    ham-long-label.c		\
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
	                                    dbus.h			\
	                                    dbus.cc			\
					    xexp.h			\
					    xexp.c			\
					    apt-worker-client.h		\
					    apt-worker-client.cc	\
					    apt-worker-proto.h		\
					    apt-worker-proto.cc		\
                                            confutils.h			\
                                            confutils.cc		\
                                            user_files.h                \
                                            user_files.c                \
                                            apt-utils.h                 \
                                            apt-utils.cc
hildon_application_manager_CFLAGS =   $(HAM_DEPS_CFLAGS)
hildon_application_manager_CXXFLAGS = $(HAM_DEPS_CFLAGS) \
					$(LAUNCHER_DEPS_CFLAGS) \
                                        -fno-rtti -fno-exceptions
hildon_application_manager_LDADD =    $(HAM_DEPS_LIBS) \
					$(LAUNCHER_DEPS_LIBS) \
					-lapt-pkg

hildon_application_manager_run_SOURCES =  $(hildon_application_manager_SOURCES)
hildon_application_manager_run_CFLAGS =   $(HAM_DEPS_CFLAGS)
hildon_application_manager_run_CXXFLAGS = $(HAM_DEPS_CFLAGS)
hildon_application_manager_run_LDADD =    $(HAM_DEPS_LIBS) -lapt-pkg

hildon_application_manager_config_SOURCES =		\
               hildon-application-manager-config.cc	\
               xexp.h					\
               xexp.c					\
               confutils.h				\
               confutils.cc

hildon_application_manager_config_CFLAGS = $(AW_DEPS_CFLAGS)
hildon_application_manager_config_CXXFLAGS = $(AW_DEPS_CFLAGS)
hildon_application_manager_config_LDADD = $(AW_DEPS_LIBS)

apt_worker_SOURCES = apt-worker.cc  	 \
		     xexp.h		 \
                     xexp.c		 \
                     apt-worker-proto.h  \
                     apt-worker-proto.cc \
                     confutils.h	 \
                     confutils.cc

apt_worker_CFLAGS = $(AW_DEPS_CFLAGS)
apt_worker_CXXFLAGS = $(AW_DEPS_CFLAGS)
apt_worker_LDADD = $(AW_DEPS_LIBS) -lapt-pkg

ham_after_boot_SOURCES = ham-after-boot.c \
				user_files.c \
	 			xexp.c
ham_after_boot_CFLAGS = $(HAM_DEPS_CFLAGS)
ham_after_boot_LDADD = $(HAM_DEPS_LIBS)

mime_open_SOURCES = mime-open.c
mime_open_CFLAGS =   $(HAM_DEPS_CFLAGS)
mime_open_CXXFLAGS = $(HAM_DEPS_CFLAGS)
mime_open_LDADD =    $(HAM_DEPS_LIBS)

mime_server_SOURCES = mime-server.c
mime_server_CFLAGS =   $(HAM_DEPS_CFLAGS)
mime_server_CXXFLAGS = $(HAM_DEPS_CFLAGS)
mime_server_LDADD =    $(HAM_DEPS_LIBS)

test_app_killer_SOURCES = test-app-killer.c
test_app_killer_CFLAGS = $(HAM_DEPS_CFLAGS)
test_app_killer_CXXFLAGS = $(HAM_DEPS_CFLAGS)
test_app_killer_LDADD = $(HAM_DEPS_LIBS)

EXTRA_DIST = export.map
