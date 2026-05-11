#!/bin/bash
# Wait for camera 0
echo "Waiting for /dev/webcam2..."
for i in {1..60}; do
  if [ -c /dev/webcam2 ]; then
    echo "Camera 2 found"
    break
  fi
  sleep 1
done

if [ ! -c /dev/webcam2 ]; then
  echo "ERROR: Camera 2 not available"
  exit 1
fi

# Additional delay for USB hub enumeration
sleep 3

# Start camera 0
exec mjpg_streamer -i "input_uvc.so -f 5 -r 640x480 -d /dev/webcam2" -o "output_http.so -p 8081"
