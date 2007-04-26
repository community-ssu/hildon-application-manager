#!/bin/sh
#
# osso-backup restore.d script.
#
# We merge the restored catalogues right here and copy the list of
# packages to a place where the AM can find them.

if [ ! $1 ]
then
  exit 0;
fi

if [ ! -e $1 ]
then
  exit 0
fi

cats="/var/lib/hildon-application-manager/catalogues.backup"
pkgs="/var/lib/hildon-application-manager/packages.backup"
upkgs="$HOME/.hildon-application-manager.backup"

if grep -q "$cats" "$1"
then
  hildon-application-manager-config add "$cats"
fi

if grep -q "$pkgs" "$1"
then
  cp "$pkgs" "$upkgs"
fi

exit 0
