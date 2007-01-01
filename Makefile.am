SUBDIRS = src utils test-data

desktopdir = $(datadir)/applications/hildon
desktop_DATA = osso-application-installer.desktop

servicefiledir=$(datadir)/dbus-1/services
servicefile_DATA=com.nokia.osso_application_installer.service

mimedir = $(datadir)/mime/packages
mime_DATA = osso-application-installer.xml

etcdir = $(sysconfdir)/osso-application-installer/
etc_DATA = certified.list

backupdir = $(sysconfdir)/osso-backup/applications/
backup_DATA = osso-application-installer.conf

aptconfdir = $(sysconfdir)/apt/apt.conf.d/
aptconf_DATA = 00-smallcache

modir = $(datadir)/locale/en_GB/LC_MESSAGES/osso-application-installer/
noinst_DATA = osso-application-installer.mo

EXTRA_DIST = osso-application-installer.desktop			\
             com.nokia.osso_application_installer.service	\
             osso-application-installer.xml			\
             certified.list					\
             osso-application-installer.mo			\
             sources.list					\
             osso-application-installer.conf			\
             00-smallcache

deb:
	./make-package $(PACKAGE) $(VERSION) .
