SUBDIRS = src utils catpo statusbar

desktopdir = $(datadir)/applications/hildon
desktop_DATA = hildon-application-manager.desktop

servicefiledir=$(datadir)/dbus-1/services
servicefile_DATA=com.nokia.hildon_application_manager.service

mimedir = $(datadir)/mime/packages
mime_DATA = hildon-application-manager.xml

backupdir = $(sysconfdir)/osso-backup/applications/
backup_DATA = hildon-application-manager.conf

restoredir = $(sysconfdir)/osso-backup/restore.d/always/
restore_SCRIPTS = hildon-application-manager.sh

rescuedir = $(libexecdir)
rescue_SCRIPTS = ham-rescue.sh

sudoersdir = $(sysconfdir)/sudoers.d/
sudoers_DATA = hildon-application-manager.sudoers

etcdir = $(sysconfdir)/hildon-application-manager/

pkgcatdir = $(datadir)/hildon-application-manager/catalogues

vardir = /var/lib/hildon-application-manager/

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(etcdir)
	if [ ! -f $(DESTDIR)$(etcdir)/catalogues ]; then \
          echo "<catalogues/>" >$(DESTDIR)$(etcdir)/catalogues; \
        fi
	if [ ! -f $(DESTDIR)$(etcdir)/notifier ]; then \
          echo "<notifier/>" >$(DESTDIR)$(etcdir)/notifier; \
        fi
	if [ ! -f $(DESTDIR)$(etcdir)/settings ]; then \
          echo "<settings/>" >$(DESTDIR)$(etcdir)/settings; \
        fi
	$(mkinstalldirs) $(DESTDIR)$(vardir)
	if [ ! -f $(DESTDIR)$(vardir)/available-updates ]; then \
          echo "<updates/>" >$(DESTDIR)$(vardir)/available-updates; \
        fi
	$(mkinstalldirs) $(DESTDIR)$(pkgcatdir)

EXTRA_DIST = COPYING.LIB                                        \
             hildon-application-manager.desktop	                \
             com.nokia.hildon_application_manager.service	\
             hildon-application-manager.xml			\
             hildon-application-manager.conf			\
             hildon-application-manager.sh			\
             hildon-application-manager.sudoers 		\
             ham-rescue.sh

deb:
	./make-package $(PACKAGE) $(VERSION) .
