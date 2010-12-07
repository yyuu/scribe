#! /bin/sh
#
# /etc/init.d/scribe -- startup script for the scribe
#
# Written by Ben Standefer <ben@simplegeo.com>
# Modified by Yamashita, Yuu <yamashita@geishatokyo.com>
#
### BEGIN INIT INFO
# Provides:          scribe
# Required-Start:    $local_fs $remote_fs $network
# Required-Stop:     $local_fs $remote_fs $network
# Should-Start:      $named
# Should-Stop:       $named
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start scribe.
# Description:       Start scribe.
### END INIT INFO

export PYTHONPATH=$PYTHONPATH:/usr/lib/python2.6/site-packages

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/usr/bin/scribed
NAME=scribe
DESC=scribe
DAEMON_OPTS="-c /etc/scribe/scribe.conf"
SCRIBE_CTRL=/usr/bin/scribe_ctrl
JAVA_HOME=/usr/lib/jvm/java-6-sun
ARCH=`uname -m | sed -e 's/^x86_64$/amd64/'`
LD_LIBRARY_PATH=/usr/local/lib:/usr/lib:/lib:$JAVA_HOME/jre/lib/$ARCH/server
export JAVA_HOME LD_LIBRARY_PATH

test -x $DAEMON || exit 0

# Include scribe defaults if available
if [ -f /etc/default/scribe ] ; then
	. /etc/default/scribe
fi

set -e

case "$1" in
  start)
	echo -n "Starting $DESC: "
	start-stop-daemon --start --quiet --background --exec $DAEMON -- $DAEMON_OPTS
	echo "$NAME."
	;;
  stop)
	echo -n "Stopping $DESC: "
	$SCRIBE_CTRL stop
	echo "$NAME."
	;;
  reload)
	echo "Reloading $DESC configuration files."
	$SCRIBE_CTRL reload
  	;;
  restart)
    echo -n "Restarting $DESC: "
	$SCRIBE_CTRL stop
	sleep 1
	start-stop-daemon --start --quiet --background --exec $DAEMON -- $DAEMON_OPTS
	echo "$NAME."
	;;
  *)
	N=/etc/init.d/$NAME
	echo "Usage: $N {start|stop|restart|reload}" >&2
	exit 1
	;;
esac

exit 0
