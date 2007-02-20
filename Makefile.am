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

aptconfdir = $(sysconfdir)/apt/apt.conf.d/
aptconf_DATA = 00-smallcache

etcdir = $(sysconfdir)/hildon-application-manager/

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(etcdir)
	if [ ! -f $(DESTDIR)$(etcdir)/catalogues ]; then \
          echo "<catalogues/>" >$(DESTDIR)$(etcdir)/catalogues; \
        fi

EXTRA_DIST = hildon-application-manager.desktop	                \
             com.nokia.hildon_application_manager.service	\
             hildon-application-manager.xml			\
             hildon-application-manager.conf		        \
             00-smallcache

deb:
	./make-package $(PACKAGE) $(VERSION) .
