#!/bin/sh
#
# ham-rescue script.
#
# If there is an unfinished operation after the boot, this script is executed.
# It enables an operative environment for the rescue command in  apt-worker.

if [ ! -s /var/lib/hildon-application-manager/current-operation ]; 
then
    exit 0
fi

# Source necessary environment variables
source /etc/osso-af-init/af-defines.sh

# Force USER state in DSME (ACT_DEAD isn't good for rescuing...)
export BOOTSTATE="USER"
touch /tmp/$BOOTSTATE
echo $BOOTSTATE > /tmp/STATE
source /etc/resource_limits.conf

# Start DSME to avoid device hanging/restarting...
exec /sbin/dsme -p /usr/lib/dsme/libstartup.so

# wait for dsme (like in dsme upstart script)
until waitfordsme; do
    sleep 1
done

# Now we can start the rescue process
/usr/libexec/apt-worker rescue
