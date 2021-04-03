#!/bin/bash

# Example build script for the app extension.
# Tested on Blue Pill board using STLink V2 pinout

set -x
APP_DIR=$(readlink -f $(dirname $0))
cd $(dirname $0)/..

make \
     V=1 \
     APP_SRC=$APP_DIR/app_log_buf.c \
     APP_CFLAGS="-I${APP_DIR}" \
     PROBE_HOST=stlink \


