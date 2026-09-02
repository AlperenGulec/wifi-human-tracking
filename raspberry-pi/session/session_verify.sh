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

    # A well-formed line has exactly 11 comma-separated fields. Lines that
    # merged (a dropped '\n' on the USB-serial link) have many more, and their
    # seq field holds a t_us value. Counting gaps naively across those injects
    # spurious "gaps" of tens of millions, which says nothing useful - so only
    # compare seq between consecutive WELL-FORMED lines, and ignore jumps too
    # large to be real (the ESP32 sends 50 packets/s; a 10000-packet jump would
    # be 200 s of silence, which the duration check would already have caught).
    # NF==11 alone is NOT enough: bytes get dropped mid-payload on the
    # USB-serial link, giving a line with 11 valid fields but fewer values
    # than `len` claims. pc/csi/parse.py rejects those, so verification must
    # apply the same test or it reports "well-formed" for data the PC will
    # throw away. Seen for real: 1174 lines all passing NF==11, every one of
    # them short (len=128, 109-123 values), 0 usable.
    stats=$(awk -F, -v maxjump=10000 '
        NF == 11 && split($11, _v, " ") == $8 + 0 {
            good++
            if (have_prev) {
                d = $3 - prev - 1
                if (d > 0 && d <= maxjump) gaps += d
                else if (d > maxjump) suspect++
            }
            prev = $3; have_prev = 1
            next
        }
        { malformed++ }
        END { printf "%d %d %d %d", good+0, malformed+0, gaps+0, suspect+0 }
    ' "$dir/csi.csv")
    n_good=$(echo "$stats" | cut -d' ' -f1)
    n_bad=$(echo "$stats" | cut -d' ' -f2)
    gaps=$(echo "$stats" | cut -d' ' -f3)
    n_suspect=$(echo "$stats" | cut -d' ' -f4)

    t_first=$(head -n1 "$dir/csi.csv" | cut -d, -f1)
    t_last=$(tail -n1 "$dir/csi.csv" | cut -d, -f1)
    dur_s=$(( (t_last - t_first) / 1000 ))
    [ "$dur_s" -gt 0 ] || dur_s=1
    rate=$((n_good / dur_s))

    echo "info  csi.csv: $n_csi lines over ${dur_s}s (~$rate pps from well-formed lines)"
    echo "info  well-formed: $n_good, malformed: $n_bad, sequence gaps: $gaps"
    [ "$n_suspect" -gt 0 ] && \
        echo "info  $n_suspect implausible seq jumps ignored (corrupted seq field)"

    if [ "$n_bad" -gt 0 ]; then
        pct=$((100 * n_bad / n_csi))
        echo "WARN  ${pct}% of lines are malformed - records merging on the serial link" >&2
    fi
    [ "$gaps" -gt $((n_good / 10)) ] && \
        echo "WARN  more than 10% of expected packets missing on the air" >&2
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
