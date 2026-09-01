#!/bin/sh
# record.sh <output_dir>
#
# The rpicam-vid invocation from docs/RASPBERRY_PI_V1.md, verbatim. Writes
# video.h264 and frames_raw.txt into <output_dir>. Runs until SIGINT.
#
# -n            no preview (headless)
# --inline      SPS/PPS before each keyframe, so a truncated file still decodes
# -t 0          run until stopped
set -eu

out="${1:?usage: $0 <output_dir>}"

exec rpicam-vid -t 0 -n \
    --width 1280 --height 720 --framerate 10 \
    --codec h264 --inline \
    --bitrate 4000000 \
    --save-pts "$out/frames_raw.txt" \
    -o "$out/video.h264"
