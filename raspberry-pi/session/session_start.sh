#!/bin/sh
# session_start.sh [-p port] [-r room] [-c channel] <data_root>
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

while getopts "p:r:c:" opt; do
    case "$opt" in
        p) port="$OPTARG" ;;
        r) room="$OPTARG" ;;
        c) channel="$OPTARG" ;;
        *) echo "usage: $0 [-p port] [-r room] [-c channel] <data_root>" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
data_root="${1:?usage: $0 [-p port] [-r room] [-c channel] <data_root>}"

session_name="session_$(date +%Y%m%d_%H%M)"
session_dir="$data_root/$session_name"
mkdir -p "$session_dir"

# --- clock mapping: read both clocks together, once, at session start ---
# monoms reads the exact clock csi-logger stamps every line with.
t0_monotonic_ms=$(monoms)
t0_realtime_ms=$(date +%s%3N)
offset_ms=$((t0_realtime_ms - t0_monotonic_ms))

# --- start the CSI logger ---
csi-logger -p "$port" -o "$session_dir/csi.csv" -n RX1 &
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
# this loop observing it, which has not been measured on real hardware.
frames_raw="$session_dir/frames_raw.txt"
camera_anchor_ms=""
i=0
while [ "$i" -lt 50 ]; do    # ~5s at 100ms, if fractional sleep works
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
