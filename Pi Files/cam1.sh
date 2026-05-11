#!/bin/bash
# Wait for camera 1
echo "Waiting for /dev/webcam1..."
for i in {1..60}; do
  if [ -c /dev/webcam1 ]; then
    echo "Camera 1 found"
    break
  fi
  sleep 1
done

if [ ! -c /dev/webcam1 ]; then
  echo "ERROR: Camera 1 not available"
  exit 1
fi

# Additional delay for USB hub enumeration
sleep 3

# Start camera 1
exec mjpg_streamer -i "input_uvc.so -f 10 -r 800x600 -d /dev/webcam1" -o "output_http.so -p 8080"
