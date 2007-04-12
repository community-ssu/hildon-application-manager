#!/bin/sh
#
# osso-backup restore.d script. This script rescues the restored
# backup data to a place where the Application Manager can find them.

if [ ! $1 ]
then
  exit 0;
fi

if [ ! -e $1 ]
then
  exit 0
fi

if grep "/var/lib/hildon-application-manager/backup" "$1"
then
  cp /var/lib/hildon-application-manager/backup $HOME/.hildon-application-manager.backup
fi

exit 0
