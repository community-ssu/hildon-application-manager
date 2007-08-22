#!/bin/sh
#
# osso-backup restore.d script.
#
# We merge the restored catalogues right here and copy the list of
# packages to a place where the AM can find them.

if [ ! -e $1 ]
then
  exit 0
fi

cats="/var/lib/hildon-application-manager/catalogues.backup"
cats2="/var/lib/hildon-application-manager/catalogues2.backup"
pkgs="/var/lib/hildon-application-manager/packages.backup"
upkgs="$HOME/.hildon-application-manager.backup"

if grep -q "$cats" "$1"
then
  sudo /usr/bin/hildon-application-manager-util restore-catalogues
elif grep -q "$cats2" "$1"
then
  sudo /usr/bin/hildon-application-manager-util restore-catalogues2
fi

if grep -q "$pkgs" "$1"
then
  cp "$pkgs" "$upkgs"
fi

exit 0
