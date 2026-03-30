#!/bin/bash
# Wrapper to force loading SDK libs first and run autocharge_service from build dir
SDK_LIB="/home/pi/DOLY-DIY/SDK/lib"
DEV_LIB="/home/pi/dolydev/libs/Doly/libs"
OPENCV_LIB="/.doly/libs/opencv/lib"
VL6180_LIB="/home/pi/dolydev/3rd/vl6180_pi"

export LD_LIBRARY_PATH="$SDK_LIB:$DEV_LIB:$VL6180_LIB:$OPENCV_LIB:$LD_LIBRARY_PATH"

echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

# If first argument is an absolute path to a binary, run that; otherwise assume current dir has autocharge_service
if [ -n "$1" ] && [ -x "$1" ]; then
    BIN="$1"
    shift
else
    BIN="$(pwd)/autocharge_service"
fi

if [ ! -x "$BIN" ]; then
    echo "ERROR: binary not found or not executable: $BIN" >&2
    exit 2
fi

exec "$BIN" "$@"
