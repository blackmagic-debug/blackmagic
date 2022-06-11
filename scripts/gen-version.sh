#!/bin/sh

if ! command -v git &>/dev/null; then
	echo "Git not found, assuming up to date include/version.h"
	exit 0
fi

if ! git rev-parse HEAD &>/dev/null; then
	echo "No git repository found, not updating include/version.h"
	exit 0
fi

version=`git describe --always --dirty --tags`
header="#define FIRMWARE_VERSION \"$version\""

if [ "`cat include/version.h 2>/dev/null`" != "$header" ]; then
	echo $header > include/version.h
fi
