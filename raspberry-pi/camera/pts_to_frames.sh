#!/bin/sh
# pts_to_frames.sh <frames_raw.txt> <anchor_monotonic_ms>
#
# Converts rpicam-vid's --save-pts output into frames.csv (frame_id,pi_timestamp_ms)
# on stdout.
#
# --save-pts writes mkvmerge timecode format v2: a "# timecode format v2"
# header, then one value per line in MILLISECONDS WITH 3 DECIMALS, relative to
# frame 0. It is not absolute and not microseconds - see
# docs/RASPBERRY_PI_V1.md#frame-timestamps for how that was confirmed.
#
# <anchor_monotonic_ms> is CLOCK_MONOTONIC at the moment frame 0 was actually
# captured (session_start.sh polls for it - see that script for why launch
# time is not used instead). Adding it to each relative value puts frame
# timestamps on the same monotonic axis as csi.csv.
set -eu

raw="${1:?usage: $0 <frames_raw.txt> <anchor_monotonic_ms>}"
anchor="${2:?usage: $0 <frames_raw.txt> <anchor_monotonic_ms>}"

frame_id=0
while IFS= read -r line; do
    case "$line" in
        \#*) continue ;;    # "# timecode format v2" header
        "")  continue ;;
    esac
    # line is e.g. "1234.567". Round to the nearest integer ms; no bc/python
    # on the image, so do it in awk.
    rel_ms=$(awk -v v="$line" 'BEGIN { printf "%d", (v < 0 ? v - 0.5 : v + 0.5) }')
    printf '%d,%d\n' "$frame_id" "$((anchor + rel_ms))"
    frame_id=$((frame_id + 1))
done < "$raw"
