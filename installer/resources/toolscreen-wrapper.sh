#!/bin/sh
# Toolscreen for Linux launch wrapper (set as the instance's Wrapper command by the installer).
# Loads libtoolscreen.so into the game through LD_PRELOAD, then runs the rest of the command line,
# which is the launcher's Java command (or another wrapper such as gamemoderun / prime-run).
# Set TOOLSCREEN_DISABLE=1 in the environment to launch without it.
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LIB="$DIR/libtoolscreen.so"
if [ -z "$TOOLSCREEN_DISABLE" ] && [ -f "$LIB" ]; then
    if [ -n "$LD_PRELOAD" ]; then
        export LD_PRELOAD="$LIB:$LD_PRELOAD"
    else
        export LD_PRELOAD="$LIB"
    fi
fi
exec "$@"
