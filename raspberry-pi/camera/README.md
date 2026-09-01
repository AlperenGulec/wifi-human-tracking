# raspberry-pi/camera

- `record.sh <output_dir>` — the `rpicam-vid` invocation from
  `docs/RASPBERRY_PI_V1.md`, verbatim. Writes `video.h264` and `frames_raw.txt`.
  Runs until SIGINT. Meant to be launched by `session_start.sh`, not run alone
  in normal use.
- `pts_to_frames.sh <frames_raw.txt> <anchor_monotonic_ms>` — converts
  `frames_raw.txt` to `frames.csv` (`frame_id,pi_timestamp_ms`) on stdout.

## The PTS format, and why an anchor argument is required

`--save-pts` does **not** write absolute monotonic microseconds — an earlier
draft of `docs/RASPBERRY_PI_V1.md` claimed that, and it was wrong. Verified
against a real capture:

```
# timecode format v2
0.000
100.001
200.000
...
```

This is mkvmerge timecode format v2: milliseconds with 3 decimals, **relative
to frame 0**. Because it is relative, `pts_to_frames.sh` needs an absolute
`CLOCK_MONOTONIC` anchor to produce timestamps comparable to `csi.csv`.
`session_start.sh` captures that anchor by polling for the first real line to
appear in `frames_raw.txt` — not by using the process launch time, which would
reintroduce libcamera's (potentially multi-second) startup delay into the
alignment. Full reasoning: `docs/RASPBERRY_PI_V1.md#frame-timestamps`.

`pts_to_frames.sh` takes the anchor as a plain argument rather than reading
`session.json` itself, so it stays a one-purpose converter that `session_stop.sh`
(which does read `session.json`) calls into.
