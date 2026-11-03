#!/bin/sh
# Builds everything: native/build/libtoolscreen.so, then dist/Toolscreen-<version>-linux-double-click-me.jar
set -e
cd "$(dirname "$0")"
native/build.sh
installer/build.sh
