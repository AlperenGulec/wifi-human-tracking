# Raspberry Pi - Version 1

The Pi records two streams and timestamps both. Nothing else.

```
raspberry-pi/
├── logger/    C serial logger (csi-logger)
├── camera/    rpicam-vid wrapper + PTS conversion
├── session/   start / stop / verify scripts
└── yocto/     recipes and layer notes
```

## Clock model (read this first)

**Everything uses `CLOCK_MONOTONIC`.**

The Pi 2 has no hardware RTC, so wall-clock time is wrong at boot until it is set.
If NTP steps the clock mid-session, every timestamp after the step jumps and the
CSI-to-frame alignment breaks silently. Monotonic never jumps.

Camera frame timestamps from libcamera are already on the monotonic clock, so both
streams share one axis with no offset maths.

At session start, set the clock once (NTP or manual), then read both clocks together
and store the mapping in `session.json`. That lets the PC recover wall-clock time
later without ever using it for alignment.

Never let NTP step the clock during a recording.

## Serial logger

**A small C program.** Not Python, not `cat`/`stdbuf`.

`cat` cannot attach a per-line timestamp at all. Python would work at 50 lines/s, but
C keeps the image lean (no python3 + pyserial recipes) and gives a predictable
`clock_gettime()` immediately after `read()` returns.

Port setup:

- `cfmakeraw()` - raw, non-canonical. Canonical mode buffers inside the tty layer.
- `VMIN = 1`, `VTIME = 0` - `read()` returns as soon as a byte is available.
- **460800 baud, not 921600.** Confirmed on real hardware: 921600 works cleanly
  from a PC, but the Pi 2's Linux `ch341` driver drops bytes at that rate -
  dropping to 460800 (plus `LLTF_ONLY` in the RX firmware) cut it from a
  ~41-60% corrupted-line rate down to ~10%. See `csi-logger`'s `-b` flag and
  `docs/ESP32_V1.md`'s failure modes table.
- Take **one timestamp per `read()` burst** and apply it to every complete line in
  that burst. Do not call `clock_gettime()` per byte.
- Keep a **partial-line carry buffer**. USB delivers arbitrary chunk boundaries, so a
  read can end mid-line.

Writing to disk: normal `FILE*`, `fflush()` on a ~1 s timer, not per line. At ~22.5 KB/s
a crash costs at most ~22.5 KB. A separate writer thread is over-engineering at this rate.

Reconnection: if `read()` returns 0 or `EIO`/`ENXIO` (the ESP32 reset and the USB
device re-enumerated), close, drop the partial buffer, and reopen in a retry loop.

**Bridge latency:** CP2102 and CH340 batch bytes in hardware before sending them over
USB - roughly 1-2 ms of jitter. There is no `latency_timer` knob like FTDI has. This
is ~40x smaller than the ±50 ms join tolerance, so ignore it. It does mean the Pi
timestamp is *time of arrival at the Pi*, not time on air, which is exactly the
synchronization model we chose.

**Stable device name** via udev (`/etc/udev/rules.d/99-esp32.rules`):

```
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", SYMLINK+="esp32-rx"
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", SYMLINK+="esp32-rx"
```

core-image-minimal already ships udev, so no extra package is needed. The symlink also
survives the ESP32 re-enumerating after a reset.

## Camera

**Modern stack: libcamera + libcamera-apps** (the Yocto package name; the
binaries it ships are `rpicam-vid`, `rpicam-hello`, etc. — see
`docs/YOCTO_BUILD.md`). The legacy MMAL / `raspivid` / `start_x=1`
path is deprecated by Raspberry Pi and is a dead end for a new build.

Hardware H.264 works on the Pi 2. The BCM2836 keeps the BCM2835's VideoCore IV
encoder, and rpicam-vid uses it by default. 720p at 10 fps is about 15% of its
1080p30 ceiling, so the ARM cores stay idle. (The Pi 5 lost this encoder - that
limitation does not apply here.)

```
rpicam-vid -t 0 -n \
  --width 1280 --height 720 --framerate 10 \
  --codec h264 --inline \
  --bitrate 4000000 \
  --save-pts frames_raw.txt \
  -o video.h264
```

- `-n` no preview (headless)
- `--inline` puts SPS/PPS headers before each keyframe, so a truncated file still decodes
- `-t 0` runs until stopped by SIGINT

Do **not** set `start_x`, `camera_auto_detect`, or raise `gpu_mem`. Under libcamera
they are unnecessary, and raising `gpu_mem` is actively harmful.

## Frame timestamps

**Corrected from an earlier draft of this document, which claimed `--save-pts`
writes absolute monotonic microseconds. It does not. Verified against a real
capture from the user's Pi.**

`--save-pts` writes **mkvmerge timecode format v2**: a literal
`# timecode format v2` header line, then one value per line in **milliseconds
with 3 decimals**, measured **relative to frame 0** (the first line is always
`0.000`). It is not absolute, and it is not microseconds.

```
# timecode format v2
0.000
100.001
200.000
300.002
...
```

Because it is relative, an absolute anchor is still needed to put frame
timestamps on the same `CLOCK_MONOTONIC` axis as `csi.csv`. The start script
records `CLOCK_MONOTONIC` at the moment the **first** line past the header
appears in `frames_raw.txt` (polled, not the process-launch time), and stores it
in `session.json` as `camera_pts_anchor_monotonic_ms`. Frame timestamps are then:

```
pi_timestamp_ms(frame_i) = camera_pts_anchor_monotonic_ms + round(relative_ms(frame_i))
```

Using the *process launch* time instead was rejected for exactly the reason the
old text of this section warned about: libcamera can take seconds to produce its
first frame, and that whole delay would land in the alignment. Polling for the
first real line removes the startup delay from the anchor; what is left is
whatever latency exists between `rpicam-vid` writing that line and our poll loop
observing it. That residual has not been measured on real hardware yet. If it
turns out `rpicam-vid` buffers `frames_raw.txt` internally rather than flushing
early, the poll will not see anything until the file closes and
`session_start.sh` falls back to launch-time with a printed warning — see
`raspberry-pi/session/session_start.sh`.

The stop script converts `frames_raw.txt` into `frames.csv` with a sequential
`frame_id` starting at 0, using `raspberry-pi/camera/pts_to_frames.sh`.

GStreamer probes were rejected: more packages in the image, fiddly timestamp
behaviour, no benefit over `--save-pts`.

## Session management

**One shell script** for start, one for stop. Not systemd.

core-image-minimal uses SysVinit and BusyBox. Switching the init manager to systemd
just to wrap two recorders is not worth it in V1.

Start script:
1. Set the clock (NTP if networked, otherwise manual).
2. Read both clocks, create `data/raw/session_YYYYMMDD_HHMM/`.
3. Write `session.json` including the clock mapping.
4. Launch `csi-logger` and `rpicam-vid`, record their start times.

Stop script:
1. Send **SIGINT** (not SIGKILL) to both, so files flush and close cleanly.
2. Convert `frames_raw.txt` to `frames.csv`.
3. Run verification.

Verification (`session_verify.sh`, on the Pi):

- CSI line count / duration should be ~50 pps
- count gaps in `seq` to estimate dropped CSI packets
- `frames.csv` line count should be ~ duration x 10
- all four files exist and are non-empty

`ffprobe video.h264` returning a valid stream is a useful check too, but
`ffprobe` is not in the Pi image (and shouldn't be, for a logger-only device) -
that check happens on the PC after copying the session over, not in
`session_verify.sh`.

## Storage

**Write to the SD card.** No USB stick, no network streaming - both add failure modes
to a component whose only job is reliable local capture.

| Stream | Rate | Per hour |
|---|---|---|
| csi.csv (~450 B/line at 50 pps, `LLTF_ONLY`) | ~22.5 KB/s | ~81 MB |
| video.h264 (4 Mbit/s cap) | ~0.5 MB/s | ~1.8 GB |
| **Combined** | **~0.52 MB/s** | **~1.9 GB** |

A class A1 card sustains at least 10 MB/s sequential write, so there is roughly 20x
headroom. Both files are append-only streams, which is the SD card's best case.

The real risk is truncation on unclean shutdown, not wear. Use a quality card and
always stop with the script.

The 100 Mbit Ethernet sharing bandwidth with USB does not matter here - nothing is
being streamed.

## Yocto configuration

**See [YOCTO_BUILD.md](YOCTO_BUILD.md) for step-by-step host setup and build commands.**

This section covers the high-level configuration needed. Key settings:

- `MACHINE = "raspberrypi2"`
- `RASPBERRYPI_CAMERA_V2 = "1"`
- Layers: poky, meta-raspberrypi, meta-oe, meta-python, meta-multimedia

## Bring-up checklist

1. Image boots. Plug in the ESP32 RX. `dmesg` shows cp210x or ch341, and
   `/dev/esp32-rx` resolves. Also check `dmesg` for `Undervoltage detected` -
   if present, the power supply is inadequate even before the ESP32 is
   attached; fix that first (see failure modes).
2. `stty -F /dev/esp32-rx 460800 raw -echo; cat /dev/esp32-rx` shows CSI CSV
   lines. Confirms baud and wiring. **460800, not 921600** — see "Serial
   logger" for why the Pi specifically needs the lower rate.
3. Run `csi-logger`. `pi_timestamp_ms` increases monotonically at ~50 lines/s.
4. `rpicam-hello --list-cameras` finds the imx219.
5. Record 10 s. Copy `video.h264` to the PC and play it with **ffmpeg or mpv, not
   VLC** (recent VLC mishandles raw .h264). `frames_raw.txt` should have ~100 lines.
   CPU staying low here confirms hardware encoding.
6. Start a session. `session.json` has both clock values and the folder name has a
   sensible date.
7. Record ~2 minutes with someone moving, then run verification.

## Failure modes

| Symptom | Cause | Fix |
|---|---|---|
| No `/dev/esp32-rx` | driver not loaded, or udev VID/PID wrong | check dmesg, check the rule |
| Garbage bytes | wrong baud | 460800 both ends (not 921600 — see "Serial logger") |
| Dropped CSI lines | read loop blocked by per-line flush | flush on a timer instead |
| ~10-60% of lines corrupted (two records merged, one missing `\n`), even in a raw `stty`+`head` capture with `csi-logger` out of the picture entirely | Pi 2's Linux `ch341` driver drops bytes at 921600 baud. Confirmed not caused by our code, not fixed by removing other USB traffic (Ethernet, which shares the Pi 2's internal USB hub, made no difference), not fixed by data volume alone (halving the line size at 921600 made it worse) | 460800 baud + `LLTF_ONLY` cuts it to ~10%, which `pc/csi/parse.py` already rejects cleanly as malformed. Accepted as a residual for V1 rather than chased further. |
| `Undervoltage detected` in `dmesg` | Power supply/cable inadequate for the Pi 2 plus whatever's on its USB ports (the ESP32 board draws current too) | Official-spec 5V/2.5A supply, short thick-gauge cable. If it still trips with the ESP32 attached, power it from a separately-powered USB hub instead of the Pi's own port. |
| "No cameras available" | overlay missing, legacy stack enabled | fix config.txt, reseat ribbon |
| Video unplayable | killed hard, or player issue | use SIGINT, `--inline`, play with ffmpeg |
| CSI and frames misaligned | mixed clocks | both must be CLOCK_MONOTONIC; check µs to ms |
| Timestamps jump mid-session | NTP stepped the clock | set time only at session start |
| Camera app crashes instantly | ARMv8 NEON build flag | current meta-raspberrypi, or bbappend |
| Writes fail late in a run | disk full | check free space at start, budget 2 GB/h |

## Fallbacks if the camera blocks progress

The CSI path and the clock model do not change in any of these:

1. Lower resolution or frame rate (still hardware encoded).
2. `--codec mjpeg` - hardware JPEG encode, simpler to decode frame by frame on the PC.
3. Periodic stills with `rpicam-still` at ~100 ms intervals.

## Deferred to later

- systemd services and auto-start on boot
- MP4 muxing on the Pi (keep raw .h264 + PTS, mux on the PC)
- Writer thread and ring buffer for CSI (only needed well above 50 pps)
- Streaming to the PC over the network
- Any vision, labeling, or ML on the Pi
