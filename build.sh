#!/bin/bash
set -e

check_command() {
PROGRAM=$1
command -v $PROGRAM >/dev/null 2>&1 || { echo "ERROR, this script requires $PROGRAM but it's not installed.  Aborting." >&2; exit 1; }
}

check_command docker

PROJECT="blackmagic"
echo "==== Building ... ===="
docker build -t $PROJECT .
echo "==== Copying firmwares ... ===="
docker run -v $PWD:/mnt $PROJECT bash -c "cp -v /home/$PROJECT/code/src/*.bin /mnt"
