#!/bin/bash

./turbofreq --csv -sandybridge -inteldoc &

BUSYLOOP=/media/usb0/Programs/UNIX/busyloop/busyloop

sleep 5

"$BUSYLOOP" -c 0 -N 1 &
sleep 40
kill -s SIGTERM `cat busyloop.pid`

#"$BUSYLOOP" -c 1 -N 1 &
#sleep 10
#kill -s SIGTERM `cat busyloop.pid`

#"$BUSYLOOP" -c 2 -N 1 &
#sleep 10
#kill -s SIGTERM `cat busyloop.pid`

#"$BUSYLOOP" -c 3 -N 1 &
#sleep 10
#kill -s SIGTERM `cat busyloop.pid`

