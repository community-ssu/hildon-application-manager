#!/bin/sh

# Clear user defined catalogues
sudo /usr/bin/hildon-application-manager-util clear-user-catalogues

# Clear user files for HAM
HAM_UFILES_DIR=/home/user/.hildon-application-manager
rm -f "${HAM_UFILES_DIR}/packages.backup"
rm -f "${HAM_UFILES_DIR}/seen-updates"
rm -f "${HAM_UFILES_DIR}/tapped-updates"
rm -f "${HAM_UFILES_DIR}/seen-notifications"
rm -f "${HAM_UFILES_DIR}/tapped-notifications"
rm -f "${HAM_UFILES_DIR}/available-notifications"
rm -f "${HAM_UFILES_DIR}/boot"
rm -f "${HAM_UFILES_DIR}/update-notifier"
rm -f "${HAM_UFILES_DIR}/last-update"

exit 0
