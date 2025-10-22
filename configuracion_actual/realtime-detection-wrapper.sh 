#!/bin/sh
# Wrapper para Realtime_detection_http
echo "$(date) - Starting Realtime Detection with args: $@" >> /var/log/realtime-detection.log

# Convertir argumento simple a formato --mode
if [ $# -eq 1 ] && { [ "$1" = "http" ] || [ "$1" = "mqtt" ]; }; then
    exec /usr/local/bin/Realtime_detection_http --mode "$1" >> /var/log/realtime-detection.log 2>&1
else
    exec /usr/local/bin/Realtime_detection_http "$@" >> /var/log/realtime-detection.log 2>&1
fi
