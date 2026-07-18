#!/bin/bash

until=$(date -d 'today 15:00' +%s)
now=$(date +%s)

if [ "$until" -gt "$now" ]; then
     printf "open 0\nclose %s\n" "$until" > house_notify_notification.lock
else
     printf "open 0\nclose 0\n" > house_notify_notification.lock
fi

cat house_notify_notification.lock
