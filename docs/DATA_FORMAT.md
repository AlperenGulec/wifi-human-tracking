# Data Format

## Session layout

Each recording session gets its own folder:

```
data/raw/session_YYYYMMDD_HHMM/
├── csi.csv          written by the Pi
├── video.h264       written by the Pi
├── frames_raw.txt   raw PTS from rpicam-vid, microseconds (intermediate)
├── frames.csv       frame_id, pi_timestamp_ms
└── session.json     room, layout, notes, clock mapping
```

Processed output:

```
data/processed/session_YYYYMMDD_HHMM/
├── labels.csv       ground truth from the camera
└── dataset.npz      joined features and labels
```

## csi.csv

Written by the Pi. The Pi prepends `pi_timestamp_ms`, the rest comes from the ESP32.

```
pi_timestamp_ms,node_id,seq,rssi,noise_floor,sig_mode,len,csi_raw
```

`csi_raw` is a space-separated list of signed int8 values, imaginary then real per
subcarrier. Amplitude is **not** computed here.

Example:

```
1734203994512,RX1,10482,-52,-94,1,256,12 -7 9 -3 14 -2 ...
```

## labels.csv

Produced on the PC from the recorded video.

```
pi_timestamp_ms,frame_id,x,y,zone_id
```

- `x`, `y` are floor coordinates in metres, from the homography
- `zone_id` is 0-3 for the four zones, or `empty` when no person is detected

## Joining

Join CSI rows to labels by **nearest timestamp**, tolerance ±50 ms. Drop CSI rows with
no label inside that window.

## Feature extraction

- Window: 1 second of CSI
- Per window, per subcarrier: mean and standard deviation of amplitude
- Amplitude = `sqrt(real^2 + imag^2)`
- Rescale using RSSI before computing features, to compensate for automatic gain control
- 52 subcarriers → 104 features per window

## Session metadata (session.json)

Record anything that could change the signal, plus the clock mapping.

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
  "monotonic_to_realtime_offset_ms": 1764999876544
}
```

**The clock fields are required.** All `pi_timestamp_ms` values in `csi.csv` and
`frames.csv` are monotonic, not wall clock, because the Pi 2 has no RTC. Without
this offset the absolute time of a session cannot be recovered afterwards.
See [RASPBERRY_PI_V1.md](RASPBERRY_PI_V1.md) for why.

## Collection protocol

Per session:

1. Empty room, 2 minutes
2. Person standing still in the centre of each zone, 2 minutes per zone
3. Person walking normally, 5 minutes, camera provides the labels

Repeat the whole thing on a **different day**, ideally after moving a chair.

## Train/test splitting

**Split by session, never randomly by row.**

Random row splits put almost-identical neighbouring samples in both train and test.
That produces 95%+ accuracy that means nothing. Holding out a whole session is the
only honest measure of whether the model learned anything transferable.

Report both numbers:

- within-session accuracy (optimistic, useful for debugging)
- cross-session accuracy (the real number)
