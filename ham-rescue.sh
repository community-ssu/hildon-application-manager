#!/bin/sh
#
# ham-rescue script.
#
# If there is an unfinished operation after the boot, this script is executed.
# It enables an operative environment for the rescue command in  apt-worker.

#######################################################
# 1.2 modifications by
#            Gary Birkett, gary.birkett@collabora.co.uk
#            Urho Konttori, urho.konttori@nokia.com
#######################################################
# the modificaitons for ssu 1.2 indicate we must have this updated script in place
# before actually downloading the new HAM package which could contain an updated version
# hence, the script is placed inside optify and copied over by its postinst
# optify is the only package actively installed prior to the 1.2 SSU operation
#######################################################

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
/sbin/dsme -p /usr/lib/dsme/libstartup.so &

# wait for dsme (like in dsme upstart script)
until /usr/sbin/waitfordsme; do
    sleep 1
done

######################################################
# create the /home and /home/user/MyDocs mountpoints
/sbin/modprobe ext3
/bin/mount -t ext3 -o rw,noatime,errors=continue,commit=1,data=writeback /dev/mmcblk0p2 /home
/usr/sbin/mmc-mount /dev/mmcblk0p1 /home/user/MyDocs

######################################################
# now, we call the new maemo-optify version of mountbind
# this caters for everything necessary to upgrade /opt symlink
/usr/sbin/maemo-optify-make-mountbind.sh

# Now we can start the rescue process
/usr/libexec/apt-worker rescue
