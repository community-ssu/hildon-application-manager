SUBDIRS = src utils

desktopdir = $(datadir)/applications/hildon
desktop_DATA = hildon-application-manager.desktop

servicefiledir=$(datadir)/dbus-1/services
servicefile_DATA=com.nokia.hildon_application_manager.service

mimedir = $(datadir)/mime/packages
mime_DATA = hildon-application-manager.xml

backupdir = $(sysconfdir)/osso-backup/applications/
backup_DATA = hildon-application-manager.conf

restoredir = $(sysconfdir)/osso-backup/restore.d/always/
restore_DATA = hildon-application-manager.sh

etcdir = $(sysconfdir)/hildon-application-manager/

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(etcdir)
	if [ ! -f $(DESTDIR)$(etcdir)/catalogues ]; then \
          echo "<catalogues/>" >$(DESTDIR)$(etcdir)/catalogues; \
        fi
	if [ ! -f $(DESTDIR)$(etcdir)/domains ]; then \
          echo "<domains/>" >$(DESTDIR)$(etcdir)/domains; \
        fi

EXTRA_DIST = hildon-application-manager.desktop			\
             com.nokia.hildon_application_manager.service	\
             hildon-application-manager.xml			\
             hildon-application-manager.conf			\
             hildon-application-manager.sh

deb:
	./make-package $(PACKAGE) $(VERSION) .
