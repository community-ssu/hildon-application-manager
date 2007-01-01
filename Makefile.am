SUBDIRS = src utils test-data

desktopdir = $(datadir)/applications/hildon
desktop_DATA = hildon-application-manager.desktop

servicefiledir=$(datadir)/dbus-1/services
servicefile_DATA=com.nokia.hildon-application-manager.service

mimedir = $(datadir)/mime/packages
mime_DATA = hildon-application-manager.xml

etcdir = $(sysconfdir)/hildon-application-manager/
etc_DATA = certified.list

backupdir = $(sysconfdir)/osso-backup/applications/
backup_DATA = hildon-application-manager.conf

aptconfdir = $(sysconfdir)/apt/apt.conf.d/
aptconf_DATA = 00-smallcache

EXTRA_DIST = hildon-application-manager.desktop	                \
             com.nokia.hildon-application-manager.service	\
             hildon-application-manager.xml			\
             certified.list			                \
             hildon-application-manager.conf		        \
             00-smallcache

deb:
	./make-package $(PACKAGE) $(VERSION) .
