#!/bin/bash
# Using a single image for Blue Pill and STLink-V2 boards.
set -x
APP_DIR=$(readlink -f $(dirname $0))
cd $(dirname $0)/..
make \
     V=1 \
     APP_SRC=$APP_DIR/app_log_buf.c \
     APP_CFLAGS="-I${APP_DIR}" \
     PROBE_HOST=stlink \


