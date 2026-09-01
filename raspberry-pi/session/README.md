# raspberry-pi/session

One shell script for start, one for stop, one for verify — no systemd.
`core-image-minimal` is SysVinit/BusyBox and V1 does not need auto-start.

```
session_start.sh [-p port] [-r room] [-c channel] <data_root>
session_stop.sh <session_dir>
session_verify.sh <session_dir>
```

`session_start.sh`:

1. Reads `CLOCK_MONOTONIC` and wall clock together, once, via `monoms` +
   `date +%s%3N` — this pair is `session.json`'s clock mapping.
2. Launches `csi-logger` and `record.sh` in the background.
3. Polls `frames_raw.txt` for the camera's first real frame and reads
   `CLOCK_MONOTONIC` at that instant — the `camera_pts_anchor_monotonic_ms`
   `pts_to_frames.sh` needs. See `raspberry-pi/camera/README.md` for why this
   is a poll, not the launch time.
4. Writes `session.json`, records both PIDs, prints the session directory.

`tx_position`, `rx_position`, `camera_position`, `notes` and
`furniture_changed` are left blank — a script cannot know where boards were
physically placed. Hand-edit `session.json` after starting.

`session_stop.sh`:

1. **SIGINT**, not SIGKILL, to both processes — required for `csi-logger`'s
   flush-on-exit and for `rpicam-vid` to close `video.h264` cleanly.
2. Polls `kill -0` until both exit. This is **not** shell `wait`: the logger
   and camera are children of `session_start.sh`'s process, not this one, and
   `wait` only works on your own children.
3. Converts `frames_raw.txt` → `frames.csv` via `pts_to_frames.sh`, using the
   anchor stored in `session.json`.
4. Runs `session_verify.sh`.

`session_verify.sh` checks all four files are present and non-empty, the CSI
line rate (~50 pps) and sequence-gap count, and the frame count. It does not
use `ffprobe` — that binary is not in the Pi image; video sanity-checking
happens on the PC instead.
