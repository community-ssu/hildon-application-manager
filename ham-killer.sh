#!/bin/sh

# hang rtcom-messaging-ui
/usr/bin/killall -SIGHUP rtcom-messaging-ui

# stop dsme services
/usr/sbin/dsmetool -k "/usr/bin/camera-ui"
/usr/sbin/dsmetool -k "/usr/sbin/browserd -d"
/usr/sbin/dsmetool -k "/usr/bin/hildon-home"
/usr/sbin/dsmetool -k "/usr/bin/hildon-input-method"
/usr/sbin/dsmetool -k "/usr/bin/intellisyncd"
/usr/sbin/dsmetool -k "/usr/bin/clipboard-manager"
/usr/sbin/dsmetool -k "/usr/bin/syncd"
/usr/sbin/dsmetool -k "/usr/bin/hildon-desktop"
/usr/sbin/dsmetool -k "/usr/bin/osso-connectivity-ui-conndlgs"

# kill the rest of processes. No mercy!
/usr/bin/killall -9 \
    syncd \
    trackerd \
    intellisyncd \
    syncd osso-abook-home-applet \
    hildon-thumbnailerd \
    maemo-xinput-sounds

exit 0

