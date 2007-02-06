#!/bin/sh
#
# osso-backup restore.d script. This script restores the installed applications
# from the backed up hildon-application-manager applications.install file.
# It uses the dbus mime_open interface to start the application.

if [ ! $1 ]
then
    exit 0;
fi

if [ ! -e $1 ]
then
    exit 0
fi

if cat $1 | grep "/var/lib/hildon-application-manager/applications.install"
then

# Call app manager through osso_mime_open dbus call
    dbus-send --print-reply --type=method_call \
	--dest=com.nokia.hildon_application_manager \
	/com/nokia/hildon_application_manager \
	com.nokia.hildon_application_manager.mime_open \
	string:file:///var/lib/hildon-application-manager/applications.install

# at this point the application should be running. We can try to poll when the app manager
# uninstall process ends.
    
    RESULT="true"
    
    while [ "$RESULT" = "true" ]
    do sleep 1
	RESULT=`run-standalone.sh dbus-send --session --dest=org.freedesktop.DBus --type=method_call --print-reply / org.freedesktop.DBus.NameHasOwner string:com.nokia.hildon_application_manager |grep boolean|sed s/.*boolean\ //`
    done
    
fi

exit 0
