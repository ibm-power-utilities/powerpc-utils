#! /bin/bash

# IBM "ibmvscsis.sh": ibmvscsis init script
#
# Copyright (c) 2004, 2005 International Business Machines.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# Author: Ryan Arnold <rsa@us.ibm.com>
#
# This file is tasked with testing for the existence of the ibmvscsis driver
# and configuring the ibmvscsi server properly as indicated by the config file
# located at /etc/ibmvscsis.conf
#
# For further details please reference man page ibmvscsis.sh.8

### BEGIN INIT INFO
# Provides:		ibmvscsis
# Required-Start:	$syslog $remote_fs
# Should-Start:		iprinit iprupdate
# Required-Stop:	$syslog $remote_fs
# Should-Stop:
# Default-Start:	3 5
# Default-Stop:
# Short-Description: configure this partition as virtual scsi server
# Description:	Based on /etc/ibmvscsis.conf, this partition
#	will export configured drives, partitions or loop mounted
#	files as SCSI drives to other partitions on this host.
#	Read the ibmvscsis.conf man page for further details.
### END INIT INFO

DRIVER=ibmvscsis
SYSFS=/sys/bus/vio/drivers/ibmvscsis
CONFIG_FILE=/etc/ibmvscsis.conf
APP=vscsisadmin

# The existence of $SYSFS indicates that the module has been loaded or that
# the driver is at least built into the kernel.

if ! test -e $CONFIG_FILE ; then
     echo "$CONFIG_FILE file does not exist.";
     exit 6
fi

# $APP is required to be in the path in order for this to work properly.  It
# is specified this way because the location may not be consistent across
# distributions.
app=`which $APP`
if [ ! "$app" ]; then
     echo "$APP not found on \$PATH"
     exit 5
fi

case "$1" in
     start)
          if ! test -e $SYSFS ; then
               echo "$DRIVER is not loaded, loading it for you."
               ret=`/sbin/modprobe $DRIVER  2>&1`
               if [ "$ret" == "FATAL: Module $DRIVER not found." ] ; then
                    echo "$ret";
                    exit 5
               fi
          fi
          $APP -start
     ;;
     stop)
          # If the module isn't loaded then exit
          if ! test -e $SYSFS ; then
               echo "$DRIVER module not loaded."
               exit 5
          fi
          $APP -stop
     ;;
     status)
          # If the module isn't loaded then exit
          if ! test -e $SYSFS ; then
               echo "$DRIVER module not loaded."
               exit 5
          fi
          $APP -status
     ;;
     restart)
          echo "Attempting to restart ibmvscsis configuration."
          # If the module isn't loaded then exit
          if ! test -e $SYSFS ; then
               echo "$DRIVER module not loaded."
               exit 5
          fi
          $APP -stop
          $APP -start
     ;;
     *)
          echo "Usage: $0 {start|stop|status|restart}"
          exit 1
     ;;
esac
