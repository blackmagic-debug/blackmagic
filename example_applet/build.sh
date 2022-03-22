#!/bin/bash

# Example build script for the app extension.
# Tested on Blue Pill board using STLink V2 pinout

set -x
APPLET_DIR=$(readlink -f $(dirname $0))
cd $(dirname $0)/..

make -j12 \
     V=1 \
     APPLET_SRC=$APPLET_DIR/logger_applet.c \
     APPLET_CFLAGS="-I${APPLET_DIR}" \
     PROBE_HOST=stlink \


