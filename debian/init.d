#! /bin/sh

### BEGIN INIT INFO
# Provides:          nginx
# Required-Start:    $local_fs $remote_fs $network $syslog
# Required-Stop:     $local_fs $remote_fs $network $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: starts the nginx web server
# Description:       starts nginx using start-stop-daemon
### END INIT INFO

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/usr/sbin/nginx
NAME=nginx
DESC=nginx

test -x $DAEMON || exit 0

# Include nginx defaults if available
if [ -f /etc/default/nginx ] ; then
	. /etc/default/nginx
fi

set -e

. /lib/lsb/init-functions

test_nginx_config() {
  if nginx -t $DAEMON_OPTS
  then
    return 0
  else
    return $?
  fi
}

case "$1" in
  start)
	echo -n "Starting $DESC: "
        test_nginx_config
	start-stop-daemon --start --quiet --pidfile /var/run/$NAME.pid \
		--exec $DAEMON -- $DAEMON_OPTS || true
	echo "$NAME."
	;;
  stop)
	echo -n "Stopping $DESC: "
	start-stop-daemon --stop --quiet --pidfile /var/run/$NAME.pid \
		--exec $DAEMON || true
	echo "$NAME."
	;;
  restart|force-reload)
	echo -n "Restarting $DESC: "
	start-stop-daemon --stop --quiet --pidfile \
		/var/run/$NAME.pid --exec $DAEMON || true
	sleep 1
        test_nginx_config
	start-stop-daemon --start --quiet --pidfile \
		/var/run/$NAME.pid --exec $DAEMON -- $DAEMON_OPTS || true
	echo "$NAME."
	;;
  reload)
        echo -n "Reloading $DESC configuration: "
        test_nginx_config
        start-stop-daemon --stop --signal HUP --quiet --pidfile /var/run/$NAME.pid \
            --exec $DAEMON || true
        echo "$NAME."
        ;;
  graceful_restart)
        echo -n "Graceful restart $DESC : "
        test_nginx_config
	NGX_PID=`cat /var/run/$NAME.pid`
    	if kill -s USR2 $NGX_PID 2>/dev/null
    	then
      		while [ ! -f /var/run/$NAME.pid.oldbin ]
      		do
        		cnt=`expr $cnt + 1`
        		if [ $cnt -gt 10 ]
        		then
          			echo "Nginx 'soft' update failed, doing restart"
          			kill -s KILL $NGX_PID
            			start-stop-daemon --start --quiet --pidfile /var/run/$NAME.pid --exec $DAEMON -- $DAEMON_OPTS || true
          			exit 0
        		fi
        		sleep 1
      		done
      		NGX_OLD_PID=`cat /var/run/$NAME.pid.oldbin`
      		kill -s QUIT $NGX_OLD_PID
	else
		echo "No nginx found, doing a normal start"
            	start-stop-daemon --start --quiet --pidfile /var/run/$NAME.pid --exec $DAEMON -- $DAEMON_OPTS || true
	fi
        echo "$NAME."
	;;
  configtest)
        echo -n "Testing $DESC configuration: "
        if test_nginx_config
        then
          echo "$NAME."
        else
         exit $?
        fi
        ;;
  status)
	status_of_proc -p /var/run/$NAME.pid "$DAEMON" nginx && exit 0 || exit $?
	;;
  *)
	echo "Usage: $NAME {start|stop|restart|reload|force-reload|graceful_restart|status|configtest}" >&2
	exit 1
	;;
esac

exit 0
