# Ground-Truth Labeling — Version 1

Turns recorded video into `labels.csv`. Runs entirely on the PC.

```
pc/labeling/
├── calibrate.py     click-to-pick homography helper, run once per camera setup
├── detect.py        YOLO11n person detection per frame
├── project.py       bbox bottom-centre -> floor (x, y) via homography
├── smooth.py        median filter, boundary hysteresis, boundary_flag
├── segments.py      empty-room segment handling
└── label.py         ties it together, writes labels.csv
```

## Detection

**YOLO11n (Ultralytics), COCO-pretrained, person class only, confidence >= 0.25.**
Per-frame, no tracker. With one person in frame, a tracker (ByteTrack/SORT) adds
complexity for no benefit — keep the highest-confidence box, with a simple
nearest-to-previous-frame check to reject a stray second detection.

Background subtraction (MOG2/KNN) was considered and **rejected as the primary
method**: the protocol has the person standing still for 2 minutes per zone,
which gets absorbed into the background model and produces false "empty" frames
exactly where you want clean data. Background subtraction is used only as a
secondary "was there motion?" cross-check for the empty-room logic below.

**Licensing note:** Ultralytics YOLO is AGPL-3.0. Fine for a private,
non-distributed prototype. Revisit before publishing or shipping.

## Foot point and floor projection

Foot point = bounding-box bottom-centre: `x = (x1+x2)/2, y = y2` in pixels.
Projected to floor metres through a homography (`cv2.findHomography`).

Expected error: **~10–30 cm**, well inside the 1.5 x 2.0 m zone cells except
near boundaries. For boundary-flagged frames only, re-check with MediaPipe Pose
ankle midpoint instead of bbox-bottom — do not build the whole pipeline on pose.

## Homography calibration

Once per camera placement:

1. Tape down 6-10 floor markers spread across the walkable floor, measure to
   +/-1-2 cm in room coordinates (origin at one corner, matches `config/rooms/*.yaml`).
2. Capture one reference frame. Click each marker's pixel position with the
   calibration helper, enter its measured world coordinate.
3. `cv2.findHomography(image_pts, world_pts, cv2.RANSAC, 3.0)`.
4. Validate: reprojection RMS, a held-out point not used in the fit, and a
   visual overlay of the zone grid on the reference frame.

**Lens distortion is skipped.** The stock Pi Camera v2.1 (IMX219, 62.2 deg FOV)
has low enough distortion that intrinsic calibration is not worth it at
metre-level accuracy in a 3x4 m room. Only add chessboard calibration if the
lens is ever swapped for a wide-angle module.

Store in the room YAML: the 3x3 matrix, the raw point correspondences, the
reference frame filename, and the calibration date. **Recompute from a fresh
reference frame any time the camera moves. Never reuse an old matrix.**

## Empty room class

**Driven only by operator-declared segments, never by detector silence.**
The operator records "room empty" time ranges in `session.json` (or a
`segments.csv`). A frame is `empty` only inside a declared segment. If the
detector finds nobody during an *occupied* segment, the frame is `no_detection`
and excluded from training — it is never relabeled `empty`.

This is the single highest-risk item in the pipeline: a false `empty` label
poisons the 5-class dataset directly.

## Decoding and frame alignment

**Decode with PyAV, index frames in decode order, and hard-check the count
against `frames.csv`.** OpenCV `VideoCapture` on a raw `.h264` elementary
stream does not report frame count reliably — do not use it for indexing.

- Counts match -> map decoded frame *i* to `frames.csv` row *i*.
- Counts differ -> **abort**, do not guess an offset. A truncated file at the
  end can be handled by labeling only `min(decoded, csv)` frames and marking
  the session partial. A mismatch in the middle means the session is rejected.

Wrapping to MP4 (`ffmpeg -c copy`) is fine for QC playback but is not the
source of truth — always index against the original `.h264` decode.

## Smoothing and boundary handling

- **Median-3 filter** on (x, y) before zone assignment (kills single-frame
  outliers without lagging real motion).
- **0.15-0.20 m boundary margin** plus **3-frame hysteresis** before a zone
  label is allowed to switch.
- Every frame gets a **`boundary_flag`** column. Flagged frames are excluded
  from training later (dataset stage), not from `labels.csv` itself.

## Interpolation to CSI rate

Labels come out at 10 fps (100 ms), CSI at ~50 pkt/s (20 ms). **Interpolate
the smoothed (x, y) trajectory to each CSI timestamp, then convert to zone_id**
— do not copy the nearest frame's label. Nearest-neighbour copying smears the
zone boundary onto up to 5 CSI packets per frame. Skip (leave unlabeled) any
CSI packet whose nearest frame is more than 50 ms away.

## Bring-up checklist

1. Calibrate: reprojection RMS < 5 px, held-out point < ~10 cm, grid overlay
   sits on the real floor tape.
2. Decode a test session, confirm decoded frame count == `frames.csv` row count.
3. Run detection + projection on one session, eyeball the overlay video
   (box + foot point + zone grid) through a walk and a "stand still" segment.
4. Plot x,y vs time — should be piecewise-smooth, no teleports.
5. Histogram samples per zone — should roughly match protocol dwell time.
6. Check `no_detection` fraction inside occupied segments.
7. Confirm every `empty` label falls inside a declared empty segment.

## Go / no-go signal

**Go:** reprojection RMS < 5 px, held-out point < ~10 cm, `no_detection` inside
occupied segments < ~5%, boundary-flagged fraction < ~15% of walking frames,
overlay and trajectory plot look correct.

**No-go:** grid overlay visibly off the real floor, `no_detection` > 10% during
occupied segments, or any `empty` label outside a declared empty segment.

## Failure modes

| Symptom | Cause | Fix |
|---|---|---|
| Frames during a 2-min "stand still" step labeled empty | used detector silence instead of operator metadata | label empty only from declared segments |
| Foot point drifts with shadow | shadow inside the box | accept the bias; use pose ankle point near boundaries |
| Zone label flickers near a line | no debounce | boundary margin + hysteresis, drop ambiguous frames |
| Labels wrong after the first minute | frame/frame_id misalignment | PyAV count-check vs frames.csv, abort on mismatch |
| Frame count unreliable across machines | OpenCV on raw .h264 | decode with PyAV instead |
| Grid doesn't sit on the real floor | stale or clustered-point homography | recompute from a fresh reference frame, spread points |
| CSI packets near a crossing get a stale zone | nearest-neighbour label copy | interpolate (x,y) to CSI timestamps first |

## Deferred / not needed

- Multi-person tracking (protocol is one person only)
- Pose estimation as the primary foot point (only used on boundary frames)
- Camera intrinsic calibration (skipped for the stock lens)
- Any of this running on the Raspberry Pi 2 — it is PC-only work
