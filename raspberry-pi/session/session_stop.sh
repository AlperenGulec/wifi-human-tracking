#!/bin/sh
# session_stop.sh <session_dir>
#
# Stops a session started by session_start.sh: SIGINT (not SIGKILL) so csi-logger
# and rpicam-vid flush and close their files, waits for both to exit, converts
# frames_raw.txt to frames.csv, then runs session_verify.sh.
set -eu

session_dir="${1:?usage: $0 <session_dir>}"

# Poll for a PID to exit rather than `wait` - the logger and camera are not
# children of this shell (they were backgrounded by session_start.sh, a
# different process), so `wait` cannot be used on them here.
wait_for_exit() {
    pid="$1"
    tries=0
    while kill -0 "$pid" 2>/dev/null; do
        tries=$((tries + 1))
        [ "$tries" -ge 50 ] && break   # ~5s at 100ms
        sleep 0.1 2>/dev/null || sleep 1
    done
}

logger_pid=$(cat "$session_dir/.logger.pid" 2>/dev/null || true)
camera_pid=$(cat "$session_dir/.camera.pid" 2>/dev/null || true)

[ -n "${logger_pid:-}" ] && kill -INT "$logger_pid" 2>/dev/null || true
[ -n "${camera_pid:-}" ] && kill -INT "$camera_pid" 2>/dev/null || true

[ -n "${logger_pid:-}" ] && wait_for_exit "$logger_pid"
[ -n "${camera_pid:-}" ] && wait_for_exit "$camera_pid"

anchor=$(awk -F': *' '/camera_pts_anchor_monotonic_ms/ { gsub(/[, ]/, "", $2); print $2 }' \
    "$session_dir/session.json" 2>/dev/null || true)

if [ -s "$session_dir/frames_raw.txt" ] && [ -n "${anchor:-}" ]; then
    pts_to_frames.sh "$session_dir/frames_raw.txt" "$anchor" > "$session_dir/frames.csv"
else
    echo "session_stop: WARNING: no frames_raw.txt or no anchor in session.json," \
         "frames.csv not written" >&2
fi

session_verify.sh "$session_dir"
