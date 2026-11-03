#!/bin/bash
# Runs the harness under libtoolscreen.so with a local test config and takes screenshots.
# Usage: run_harness.sh <rundir> <step>...   where step = key:<xdotool key> | shot:<name> | sleep:<sec>
R=$1; shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LIB=$ROOT/build/libtoolscreen.so
HAR=$ROOT/build/harness
mkdir -p "$R/toolscreen"
cd "$R" || exit 1
HARNESS_FRAMES=${FRAMES:-1200} LD_PRELOAD=$LIB "$HAR" > harness.log 2>&1 &
PID=$!
sleep 2.5
WID=$(xdotool search --name ts-harness | head -1)
xdotool windowactivate --sync "$WID" 2>/dev/null
for step in "$@"; do
    case $step in
        key:*) xdotool key --window "$WID" "${step#key:}" ;;
        shot:*) import -window root -crop 1920x1080+0+0 "${step#shot:}.png" ;;
        sleep:*) sleep "${step#sleep:}" ;;
        cmd:*) eval "${step#cmd:}" ;;
    esac
done
kill $PID 2>/dev/null
wait $PID 2>/dev/null
grep -v 'harness: cursor' harness.log | head -60
