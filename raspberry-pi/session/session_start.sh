#!/bin/sh
# session_start.sh [-p port] [-r room] [-c channel] [-b baud] <data_root>
#
# Starts one recording session: creates data_root/session_YYYYMMDD_HHMM/,
# launches csi-logger and the camera in the background, and writes
# session.json with the clock mapping. Run session_stop.sh to end it cleanly.
#
# Descriptive fields (tx_position, rx_position, camera_position, notes,
# furniture_changed) are left blank in session.json - a script cannot know
# where boards were physically placed. Hand-edit them after starting, or
# before archiving the session.
set -eu

port="/dev/esp32-rx"
room="office"
channel="6"
# Passed to csi-logger explicitly rather than relying on its compiled-in
# default. The two live in different places (this script is shipped as text,
# csi-logger's default is baked in at build time) and have already drifted
# apart once when the firmware's UART_BAUD changed - a mismatch produces a
# stream of garbage that still looks like "lines", so make it explicit.
# Must equal UART_BAUD in esp32/rx/main/config.h.
baud="230400"

while getopts "p:r:c:b:" opt; do
    case "$opt" in
        p) port="$OPTARG" ;;
        r) room="$OPTARG" ;;
        c) channel="$OPTARG" ;;
        b) baud="$OPTARG" ;;
        *) echo "usage: $0 [-p port] [-r room] [-c channel] [-b baud] <data_root>" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
data_root="${1:?usage: $0 [-p port] [-r room] [-c channel] [-b baud] <data_root>}"

# --- pick a session directory that does not already exist ---
# The Pi 2 has no RTC, so the wall clock restarts at some arbitrary value on
# every boot and session names REPEAT across reboots. Combined with csi-logger
# opening csi.csv in append mode, reusing a directory silently concatenates two
# unrelated recordings into one file that still looks valid: seq restarts
# mid-file, the line count exceeds what the logger reported writing, and the
# gap count explodes. That happened for real (seq 1805 -> 1 at line 938), so
# never mkdir -p onto an existing session.
session_base="session_$(date +%Y%m%d_%H%M)"
session_name="$session_base"
n=2
while [ -e "$data_root/$session_name" ]; do
    echo "session_start: WARNING: $data_root/$session_name already exists" \
         "(the Pi's clock has no RTC and repeats across reboots) - using a" \
         "suffix so this recording cannot be appended onto the old one" >&2
    session_name="${session_base}_$n"
    n=$((n + 1))
done
session_dir="$data_root/$session_name"
mkdir "$session_dir"

# Warn if the wall clock is obviously unset. This does not affect CSI/frame
# alignment (that is all CLOCK_MONOTONIC) but it does make session.json's
# absolute time meaningless and it is what makes names collide above.
year=$(date +%Y)
if [ "$year" -lt 2020 ]; then
    echo "session_start: WARNING: wall clock reads $year - it has not been set." \
         "session.json's date and t0_realtime_ms will be wrong, and session" \
         "names will keep colliding. Set it first, e.g.:" \
         "date -s '2026-09-02 14:30:00'" >&2
fi

# --- clock mapping: read both clocks together, once, at session start ---
# monoms reads the exact clock csi-logger stamps every line with.
t0_monotonic_ms=$(monoms)
t0_realtime_ms=$(date +%s%3N)
offset_ms=$((t0_realtime_ms - t0_monotonic_ms))

# --- start the CSI logger ---
csi-logger -p "$port" -o "$session_dir/csi.csv" -n RX1 -b "$baud" &
logger_pid=$!

# --- start the camera ---
record.sh "$session_dir" &
camera_pid=$!

# --- anchor the camera's PTS clock to when frame 0 actually arrives ---
# See docs/RASPBERRY_PI_V1.md#frame-timestamps: --save-pts values are relative
# to frame 0, not absolute, so this anchor is what makes frames.csv timestamps
# comparable to csi.csv timestamps. Polling for the real first line (not using
# launch time) removes libcamera's startup delay from the error budget - what
# remains is whatever latency exists between rpicam-vid writing that line and
# this loop observing it.
#
# Confirmed on real hardware: frames_raw.txt IS written incrementally (not
# buffered until the process exits), so polling for it works. The window is
# 20s, not the original 5s - a Pi 2 test showed libcamera's own startup
# (camera_manager init, sensor mode negotiation) visibly taking longer than
# 5s on this CPU, which starved the poll every time before frame 0 ever
# arrived. 20s covers that comfortably; a real failure (no camera, wrong
# overlay) still falls back within a bounded time instead of hanging.
frames_raw="$session_dir/frames_raw.txt"
camera_anchor_ms=""
i=0
while [ "$i" -lt 200 ]; do    # ~20s at 100ms, if fractional sleep works
    if [ -s "$frames_raw" ] && grep -qv '^#' "$frames_raw" 2>/dev/null; then
        camera_anchor_ms=$(monoms)
        break
    fi
    i=$((i + 1))
    sleep 0.1 2>/dev/null || sleep 1
done
if [ -z "$camera_anchor_ms" ]; then
    echo "session_start: WARNING: no frame seen from the camera within the poll" \
         "window, falling back to launch time as the PTS anchor - residual error" \
         "equals however long the camera took to produce its first frame" >&2
    camera_anchor_ms=$t0_monotonic_ms
fi

cat > "$session_dir/session.json" <<EOF
{
  "room": "$room",
  "date": "$(date -u +%Y-%m-%d)",
  "tx_position": "",
  "rx_position": "",
  "camera_position": "",
  "channel": $channel,
  "furniture_changed": false,
  "notes": "",

  "clock": "CLOCK_MONOTONIC",
  "t0_monotonic_ms": $t0_monotonic_ms,
  "t0_realtime_ms": $t0_realtime_ms,
  "monotonic_to_realtime_offset_ms": $offset_ms,
  "camera_pts_anchor_monotonic_ms": $camera_anchor_ms,

  "empty_segments_ms": []
}
EOF

echo "$logger_pid" > "$session_dir/.logger.pid"
echo "$camera_pid"  > "$session_dir/.camera.pid"
echo "$session_dir" > "$data_root/.current_session"

echo "session started: $session_dir"
echo "fill in tx_position / rx_position / camera_position / notes by hand"
echo "stop with: session_stop.sh $session_dir"
