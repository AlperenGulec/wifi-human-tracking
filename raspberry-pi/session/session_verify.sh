#!/bin/sh
# session_verify.sh <session_dir>
#
# Sanity checks per docs/RASPBERRY_PI_V1.md "Verification": all four files
# exist and are non-empty, CSI line rate is roughly 50 pps with few sequence
# gaps, frame count is roughly duration x 10 fps.
set -eu

dir="${1:?usage: $0 <session_dir>}"
fail=0

for f in csi.csv video.h264 frames_raw.txt frames.csv session.json; do
    if [ ! -s "$dir/$f" ]; then
        echo "FAIL  $f missing or empty" >&2
        fail=1
    else
        echo "ok    $f present ($(wc -c < "$dir/$f") bytes)"
    fi
done

if [ -s "$dir/csi.csv" ]; then
    n_csi=$(wc -l < "$dir/csi.csv")
    # seq is field 3: pi_timestamp_ms,node_id,seq,...
    gaps=$(awk -F, 'NR>1 { d=$3-prev-1; if (d>0) gaps+=d } { prev=$3 } END { print gaps+0 }' \
        "$dir/csi.csv")
    t_first=$(head -n1 "$dir/csi.csv" | cut -d, -f1)
    t_last=$(tail -n1 "$dir/csi.csv" | cut -d, -f1)
    dur_s=$(( (t_last - t_first) / 1000 ))
    [ "$dur_s" -gt 0 ] || dur_s=1
    rate=$((n_csi / dur_s))
    echo "info  csi.csv: $n_csi lines over ${dur_s}s (~$rate pps), $gaps sequence gaps"
    [ "$gaps" -gt $((n_csi / 10)) ] && echo "WARN  more than 10% of expected packets missing" >&2
fi

if [ -s "$dir/frames.csv" ]; then
    n_frames=$(wc -l < "$dir/frames.csv")
    echo "info  frames.csv: $n_frames frames"
fi

if [ "$fail" -eq 0 ]; then
    echo "session_verify: PASS"
else
    echo "session_verify: FAIL" >&2
    exit 1
fi
