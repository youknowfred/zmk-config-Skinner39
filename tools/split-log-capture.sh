#!/bin/sh
# Skinner39 LEFT-half USB log capture (for the skinner39_left_USB_LOGGING build).
# Tails the keyboard's CDC log console, timestamps every line, writes one file per
# connect-session under ~/Documents/skinner39/split-logs/, and survives
# replug/reset/power-cycle by re-attaching when the device reappears.
#
# Start:  nohup ~/Documents/skinner39/split-log-capture.sh >/dev/null 2>&1 &
# Stop:   pkill -f split-log-capture.sh
# Watch:  tail -f ~/Documents/skinner39/split-logs/<newest>.log

LOGDIR="$HOME/Documents/skinner39/split-logs"
mkdir -p "$LOGDIR"

echo "[capture] started $(date '+%Y-%m-%d %H:%M:%S') pid $$" >> "$LOGDIR/capture-rig.log"

while true; do
    DEV=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
    if [ -n "$DEV" ]; then
        F="$LOGDIR/$(date +%Y%m%d-%H%M%S).log"
        echo "[capture] attaching $DEV -> $F" >> "$LOGDIR/capture-rig.log"
        stty -f "$DEV" raw 115200 2>/dev/null
        # cat exits when the device vanishes (reset/replug); loop re-attaches.
        cat "$DEV" 2>/dev/null | perl -pe 'use POSIX qw(strftime); use Time::HiRes qw(time); my $t=time; print strftime("[%H:%M:%S", localtime($t)) . sprintf(".%03d] ", ($t-int($t))*1000);' >> "$F"
        echo "[capture] detached from $DEV at $(date '+%Y-%m-%d %H:%M:%S')" >> "$LOGDIR/capture-rig.log"
    fi
    sleep 0.25
done
