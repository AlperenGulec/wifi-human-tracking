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
- 921600 baud.
- Take **one timestamp per `read()` burst** and apply it to every complete line in
  that burst. Do not call `clock_gettime()` per byte.
- Keep a **partial-line carry buffer**. USB delivers arbitrary chunk boundaries, so a
  read can end mid-line.

Writing to disk: normal `FILE*`, `fflush()` on a ~1 s timer, not per line. At ~30 KB/s
a crash costs at most ~30 KB. A separate writer thread is over-engineering at this rate.

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

**Modern stack: libcamera + rpicam-apps.** The legacy MMAL / `raspivid` / `start_x=1`
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

`--save-pts` writes one timestamp per frame in **microseconds**, on the monotonic
clock. So:

```
pi_timestamp_ms = round(pts_us / 1000)
```

No start-offset guessing. Do not use the "wall clock at start plus PTS offset" trick -
libcamera can take seconds to produce its first frame, which puts that error straight
into the alignment.

The stop script converts `frames_raw.txt` into `frames.csv` with a sequential
`frame_id` starting at 0.

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

Verification:

- CSI line count / duration should be ~50 pps
- count gaps in `seq` to estimate dropped CSI packets
- `frames.csv` line count should be ~ duration x 10
- `ffprobe video.h264` returns a valid stream
- all four files exist and are non-empty

## Storage

**Write to the SD card.** No USB stick, no network streaming - both add failure modes
to a component whose only job is reliable local capture.

| Stream | Rate | Per hour |
|---|---|---|
| csi.csv (~600 B/line at 50 pps) | ~30 KB/s | ~110 MB |
| video.h264 (4 Mbit/s cap) | ~0.5 MB/s | ~1.8 GB |
| **Combined** | **~0.53 MB/s** | **~2 GB** |

A class A1 card sustains at least 10 MB/s sequential write, so there is roughly 20x
headroom. Both files are append-only streams, which is the SD card's best case.

The real risk is truncation on unclean shutdown, not wear. Use a quality card and
always stop with the script.

The 100 Mbit Ethernet sharing bandwidth with USB does not matter here - nothing is
being streamed.

## Yocto configuration

**Layers** (keep all on the same branch, e.g. scarthgap LTS):

```
poky/meta, meta-poky, meta-yocto-bsp
meta-raspberrypi
meta-openembedded/meta-oe
meta-openembedded/meta-python
meta-openembedded/meta-multimedia     <- required, see below
```

`meta-multimedia` is not optional. The camera recipe `rpi-libcamera-apps` lives in
meta-raspberrypi's `dynamic-layers/multimedia-layer/`, so it is invisible without it.
This is the usual reason people report "the libcamera-apps recipe does not exist".
Note the name is `rpi-libcamera-apps`, not `libcamera-apps`.

**local.conf:**

```
MACHINE = "raspberrypi2"
RASPBERRYPI_CAMERA_V2 = "1"        # adds dtoverlay=imx219 + CMA
IMAGE_INSTALL:append = " rpi-libcamera-apps kernel-modules csi-logger esp32-udev"
KERNEL_MODULE_AUTOLOAD:append = " cp210x ch341"
```

`csi-logger` and `esp32-udev` are our own recipes (the C logger plus scripts, and the
udev rule).

**Not needed** - keep them out: `userland`, `raspi-gpio`, `gstreamer1.0*`, `python3`,
`python3-pyserial`. Add `v4l-utils` temporarily for bring-up, then drop it.

Do not hand-edit `config.txt`. `RASPBERRYPI_CAMERA_V2` generates what is needed; use
`RPI_EXTRA_CONFIG` if anything else is ever required.

**Known Yocto traps:**

- **"Illegal Instruction" on ARMv7.** An older `rpi-libcamera-apps` recipe forced
  `LIBCAMERA_ARCH:arm = "armv8-neon"`, which emits ARMv8 NEON instructions that crash
  the Pi 2's Cortex-A7. Fixed upstream in 2023. If you pin an old layer, remove that
  line in a bbappend.
- "No cameras available" is almost always a wrong or missing overlay, or a half-enabled
  legacy stack. Check the generated config.txt for `dtoverlay=imx219` and no `start_x`.
- Some branches have had packaging QA failures for libcamera-apps. Confirm
  `rpi-libcamera-apps` builds cleanly *before* adding our own recipes on top.

## Bring-up checklist

1. Image boots. Plug in the ESP32 RX. `dmesg` shows cp210x or ch341, and
   `/dev/esp32-rx` resolves.
2. `cat /dev/esp32-rx` shows CSI CSV lines. Confirms baud and wiring.
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
| Garbage bytes | wrong baud | 921600 both ends |
| Dropped CSI lines | read loop blocked by per-line flush | flush on a timer instead |
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
