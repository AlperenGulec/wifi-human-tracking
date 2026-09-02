# Data Format

## Session layout

Each recording session gets its own folder:

```
data/raw/session_YYYYMMDD_HHMM/
├── csi.csv          written by the Pi
├── video.h264       written by the Pi
├── frames_raw.txt   raw PTS from rpicam-vid, mkvmerge timecode v2, relative ms (intermediate)
├── frames.csv       frame_id, pi_timestamp_ms
└── session.json     room, layout, notes, clock mapping, empty-room segments
```

**Session names can collide, and a colliding name used to corrupt data.** The
Pi 2 has no RTC, so its wall clock restarts at an arbitrary value on every boot
and `session_YYYYMMDD_HHMM` repeats. `csi-logger` opens `csi.csv` in append
mode, so reusing a directory silently concatenated two unrelated recordings
into one file that still passed verification. `session_start.sh` now refuses to
reuse a directory and adds a `_2`, `_3`, ... suffix instead.

Sanity check for any session recorded before that fix: **`wc -l csi.csv` must
match the line count `csi-logger` printed on exit.** If the file has more, it
contains more than one run — look for `seq` restarting mid-file and discard it.

Processed output:

```
data/processed/session_YYYYMMDD_HHMM/
├── labels.csv       ground truth from the camera, at frame rate
└── dataset.npz      joined features and labels, at CSI rate
```

Dataset build output (all sessions combined):

```
data/processed/dataset.parquet   windowed features + labels + session_id
```

## csi.csv

Written by the Pi. The Pi prepends `pi_timestamp_ms`, the rest comes from the ESP32.

```
pi_timestamp_ms,node_id,seq,t_us,rssi,noise_floor,sig_mode,len,first_word_invalid,dropped,csi_raw
```

`csi_raw` is a space-separated list of signed int8 values, imaginary then real per
subcarrier. Amplitude is **not** computed here.

| Column | Meaning |
|---|---|
| `pi_timestamp_ms` | `CLOCK_MONOTONIC`, stamped by the Pi on arrival. **The only column used for alignment.** |
| `t_us` | The ESP32's own microsecond clock. Diagnosing gaps and jitter only — never alignment. |
| `sig_mode` | 1 = HT (11n), what we want. 0 means the rate fell back to 1 Mbps. |
| `len` | Number of int8 values in `csi_raw`. **128 on every line** with `LLTF_ONLY` (the default — see `docs/ESP32_V1.md#serial-output`); no longer a rate-fallback signal on its own, check `sig_mode` for that. |
| `first_word_invalid` | 1 when the first 4 bytes are hardware-invalid. Handled on the PC. |
| `dropped` | Running count of RX ring-buffer overflows. Should stay at 0. |

Example:

```
1734203994512,RX1,10482,44351200,-52,-94,1,256,0,0,12 -7 9 -3 14 -2 ...
```

`dropped` is a column rather than a periodic status line on purpose: a status
line would also start with `node_id`, so the Pi's prefix filter would pass it
into `csi.csv` and break the parser.

Full parsing rules (subcarrier selection, `first_word_invalid` handling,
reordering) are in [CSI_PROCESSING_V1.md](CSI_PROCESSING_V1.md).

## labels.csv

Produced on the PC from the recorded video, at frame rate (10 fps).

```
pi_timestamp_ms,frame_id,x,y,zone_id,boundary_flag
```

- `x`, `y` are floor coordinates in metres, from the homography
- `zone_id` is 0-3 for the four zones, `empty` inside an operator-declared
  empty segment, or `no_detection` if no person was found during an
  occupied segment (excluded from training)
- `boundary_flag` is 1 when the point is within the boundary margin
  (0.15-0.20 m) of a zone edge — excluded from training, kept for a
  separate boundary evaluation

Full labeling pipeline: [LABELING_V1.md](LABELING_V1.md)

## Joining CSI to labels

Two join steps, not one:

1. `labels.csv` (10 fps) is produced first, for QC and visualization.
2. The (x, y) trajectory is **interpolated to each CSI timestamp**, then
   converted to zone_id. Do not nearest-neighbour-copy frame labels onto CSI
   rows — see [LABELING_V1.md](LABELING_V1.md#interpolation-to-csi-rate) for
   why. Skip (leave unlabeled) any CSI packet whose nearest video frame is
   more than 50 ms away.

## Feature extraction

Full detail in [CSI_PROCESSING_V1.md](CSI_PROCESSING_V1.md). Summary:

- Keep the 52 valid LLTF subcarriers (drop DC, guard bands, HT-LTF block)
- Amplitude = `sqrt(real^2 + imag^2)`, rescaled per-packet using RSSI to
  compensate for automatic gain control
- Preprocessing: drop malformed packets, Hampel filter (window 5-7, 3-sigma),
  RSSI rescale
- **Window: 2.0 s, stride 0.5 s (75% overlap)**
- **107 features per window:** 52 per-subcarrier mean amplitude + 52
  per-subcarrier std amplitude + mean RSSI + std RSSI + raw (un-rescaled)
  global amplitude mean
- Windows that span a zone transition, or contain a `boundary_flag` frame,
  are dropped from the training set

## Session metadata (session.json)

Record anything that could change the signal, the clock mapping, and the
operator-declared empty-room segments.

```json
{
  "room": "office",
  "date": "2026-08-14",
  "tx_position": "north-east corner, 1.8 m",
  "rx_position": "south-west corner, 1.8 m",
  "camera_position": "north wall, 2.2 m, looking south",
  "channel": 6,
  "furniture_changed": false,
  "notes": "door closed, no other people in the room",

  "clock": "CLOCK_MONOTONIC",
  "t0_monotonic_ms": 123456,
  "t0_realtime_ms": 1765000000000,
  "monotonic_to_realtime_offset_ms": 1764999876544,
  "camera_pts_anchor_monotonic_ms": 124890,

  "empty_segments_ms": [
    [123456, 243456]
  ]
}
```

**The clock fields are required.** All `pi_timestamp_ms` values in `csi.csv` and
`frames.csv` are monotonic, not wall clock, because the Pi 2 has no RTC. Without
this offset the absolute time of a session cannot be recovered afterwards.
See [RASPBERRY_PI_V1.md](RASPBERRY_PI_V1.md) for why.

**`camera_pts_anchor_monotonic_ms`** is separate from `t0_monotonic_ms`.
`t0_monotonic_ms` is when the session itself started; `camera_pts_anchor_monotonic_ms`
is `CLOCK_MONOTONIC` at the instant `rpicam-vid` produced its first frame (frame 0
of `frames_raw.txt`), which is normally a little later. `rpicam-vid`'s own PTS
values are relative to that first frame, not absolute, so this anchor is what
`pts_to_frames.sh` adds back to make `frames.csv` timestamps comparable to
`csi.csv` timestamps. See [RASPBERRY_PI_V1.md](RASPBERRY_PI_V1.md#frame-timestamps).

**`empty_segments_ms` is required for the empty class.** The labeling
pipeline only ever assigns `zone_id = "empty"` inside these operator-declared
ranges — never from the detector finding nobody. See
[LABELING_V1.md](LABELING_V1.md#empty-room-class).

## Collection protocol

Per session:

1. Empty room, 2 minutes — record this range in `empty_segments_ms`
2. Person standing still in the centre of each zone, 2 minutes per zone
3. Person walking normally, 5 minutes, camera provides the labels

Repeat the whole thing on a **different day**, ideally after moving a chair.
Aim for **at least 3 sessions across at least 2 different days** — 2 sessions
is the bare minimum for cross-session evaluation and gives a noisy estimate.

## Train/test splitting

**Split by session, never randomly by row.**

Random row splits put almost-identical neighbouring samples (heavily
overlapping windows) in both train and test. That produces 95%+ accuracy
that means nothing. Holding out a whole session is the only honest measure
of whether the model learned anything transferable.

With 2-4 sessions, use **leave-one-session-out cross-validation (LOSO-CV)**
and report each direction separately, not just the average. Also report a
**within-session** number using a time-based split inside one session
(train on the first ~70% chronologically, test on the last ~30%) — never a
random row split, even within one session.

Report all of:

- within-session accuracy (optimistic, useful for debugging)
- cross-session accuracy, each LOSO-CV direction (the real number)

Full model and evaluation detail: [CSI_PROCESSING_V1.md](CSI_PROCESSING_V1.md)
