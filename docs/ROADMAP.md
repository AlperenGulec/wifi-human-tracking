# Roadmap

Each stage has a gate. Do not start the next stage until the gate passes.

## Stage 1 - Prove CSI is real *(current)*

Write TX and RX firmware ourselves, flash both, get CSI onto the serial port.

**See [ESP32_V1.md](ESP32_V1.md) for the full firmware design.**

### Toolchain setup

- [ ] ESP-IDF `release/v5.5` cloned and `install.sh esp32` run
- [ ] `export.sh` works in a fresh shell; no IDE involved
- [ ] `sdkconfig.defaults` committed for both projects
- [ ] Generated `sdkconfig` confirmed to contain `CONFIG_ESP_WIFI_CSI_ENABLED=y`
- [ ] Exact IDF `git describe` recorded

### Firmware

- [ ] TX written: STA mode, connects to RX SoftAP, `WIFI_PS_NONE`, UDP send loop
      driven by `vTaskDelayUntil` at 20 ms
- [ ] RX written: SoftAP, CSI config set, callback does memcpy + barrier + index bump
      into the ring buffer only
- [ ] RX consumer loop in `app_main`, pinned to core 1, `uart_write_bytes` with a
      TX ring buffer, hand-rolled int8-to-ASCII
- [ ] IDF logging silenced on the RX so it cannot corrupt the CSV stream
- [ ] `dropped` counter emitted so ring overflow is visible, not silent

### Bring-up

- [ ] Both boards build and flash
- [ ] TX associates to the RX SoftAP
- [ ] CSI lines appear at 921600 baud
- [ ] `sig_mode == 1`, `len == 256`
- [ ] ~50 lines/s, no sequence gaps, `dropped` stays at 0
- [ ] Amplitude curve deforms when you walk between the boards

**Gate:** the walk test produces a clear, repeatable change. If the signal looks like
noise after fixing distance, channel, and rate, stop here and fix it. Nothing later
works without this.

## Stage 2 - Recording rig and ground-truth labeling

Pi-side logging and camera recording; PC-side video labeling and ground-truth generation.

**See [LABELING_V1.md](LABELING_V1.md) for the complete PC-side labeling pipeline design.**

### Pi side (logging and recording)

- [ ] Pi reads serial, timestamps on arrival with `CLOCK_MONOTONIC`, writes `csi.csv`
- [ ] Logger drops any line not starting with the configured `node_id`
- [ ] Pi records 1280x720 H.264 video at 10 fps with hardware encoding via rpicam-vid
- [ ] rpicam-vid `--save-pts` produces microsecond timestamps; converted to `frames.csv` with frame_id
- [ ] Yocto image built successfully with core-image-minimal + libcamera + rpicam-apps
- [ ] Session start/stop scripts work: clock set, `session.json` written with monotonic<->realtime offset, both loggers launched
- [ ] Session verification passes: CSI line rate ~50 pps, frame count ~10 fps, no mid-stream dropout

### PC side (labeling)

**Calibration (once per camera placement):**
- [ ] Camera position and height locked and noted in `session.json`
- [ ] 6-10 floor markers taped down, measured to +/-1-2 cm in room coordinates (origin at a corner)
- [ ] Reference calibration frame captured; click-to-pick helper computes homography via `cv2.findHomography`
- [ ] Reprojection error RMS < 5 px; held-out floor point projects within ~10 cm
- [ ] Zone-grid overlay visually lands on real floor tape
- [ ] Homography matrix + point correspondences + reference frame filename stored in `config/rooms/*.yaml`

**Per-session labeling:**
- [ ] PyAV decodes `.h264` -> frame sequence, counts frames, reconciles against `frames.csv` (abort on mid-stream mismatch)
- [ ] YOLO11n (COCO-pretrained person class, confidence >= 0.25) detects person per frame on PC
- [ ] Bounding-box bottom-centre -> floor metres via homography
- [ ] Median-3 smooth (x,y); apply boundary margin (0.15-0.20 m) + 3-frame hysteresis; set `boundary_flag` column
- [ ] Operator declares "empty room" time segments in `session.json` (`empty_segments_ms`); frame labeled `empty` only inside those segments
- [ ] Interpolate smoothed (x,y) to each CSI timestamp; convert to zone_id; carry `boundary_flag`; skip gaps > 50 ms
- [ ] Produce `labels.csv`: pi_timestamp_ms, frame_id, x, y, zone_id, boundary_flag
- [ ] Generate QC overlay (detection box + foot point + zone grid) burned onto video frames

**QC checks (all required before Stage 3):**
- [ ] Overlay video spot-checked: box + foot point + grid look correct during walk and during "stand still" steps
- [ ] x,y-vs-time plot is piecewise-smooth (no teleports or sudden jumps)
- [ ] Per-zone sample histogram shows expected dwell times (2 min per zone if protocol followed)
- [ ] `no_detection` fraction inside occupied segments < ~5% (otherwise detector/lighting issue)
- [ ] All `empty` labels fall inside operator-declared empty segments; no false empty from detector silence
- [ ] Boundary-flagged frame fraction reported and < ~15% of walking frames
- [ ] Reprojection RMS confirmed < 5 px; known-point floor distance < ~10 cm

**Gate:** one full session from start to finish (empty -> 4 zones -> walk) with CSI and labels that:
- align by timestamp to within +/-50 ms (hard count-check of decoded frames passed)
- have zero mid-stream frame misalignment (PyAV count reconciliation successful)
- show CSI line rate ~50 pps and label rate interpolated to 10 fps, both unbroken
- pass all four QC visualizations (overlay looks right, trajectory plot is smooth, zone histogram matches protocol, no detector failures inside occupied segments)
- have `boundary_flag` column populated and < ~15% of walking samples flagged
- are ready to join into a dataset (Stage 3)

## Stage 3 - Dataset

**See [CSI_PROCESSING_V1.md](CSI_PROCESSING_V1.md) for the complete parsing, rescaling, and feature spec.**

### CSI parsing and preprocessing

- [ ] Parse `csi.csv`: extract 52 valid LLTF subcarriers (indices 1-26, 38-63), reorder to -26..+26
- [ ] Handle `first_word_invalid` (drop/interpolate subcarrier +1 when set)
- [ ] Plot amplitude curve from an empty-room capture; confirm smooth and stable, no parsing-bug signatures
- [ ] Drop malformed packets (`len != 256`, `sig_mode != 1`, RSSI outliers)
- [ ] Hampel filter (window 5-7, 3-sigma) per subcarrier along time
- [ ] RSSI-based amplitude rescaling applied per packet, before windowing

### Windowing, features, and join

- [ ] Window CSI at 2.0 s, stride 0.5 s (75% overlap)
- [ ] Build the 107-feature vector per window (52 mean + 52 std + mean RSSI + std RSSI + raw global amplitude mean)
- [ ] Interpolate label (x,y) to CSI timestamps, convert to zone_id, join to windows by center timestamp
- [ ] Drop windows spanning a zone transition or containing a `boundary_flag` frame
- [ ] Sanity-check alignment by eye (plot a window's timestamp range against the label trajectory)
- [ ] At least 2 sessions on different days present in the combined dataset (3+ preferred)
- [ ] Dataset stored as `data/processed/dataset.parquet`, with `session_id` column for splitting

**Gate:** a dataset that can be loaded and inspected without surprises — feature distributions look sane per class, per-zone sample counts roughly match the collection protocol, and no `session_id`/timestamp columns leak into the feature matrix.

## Stage 4 - Basic model

**See [CSI_PROCESSING_V1.md](CSI_PROCESSING_V1.md) for hyperparameters, evaluation, and drift mitigation detail.**

- [ ] Split strictly by session: leave-one-session-out cross-validation (LOSO-CV), each direction reported separately
- [ ] Within-session number reported separately, using a time-based split (not random rows)
- [ ] Stage 1 model: presence classification (empty vs occupied), Random Forest with `class_weight="balanced"`
- [ ] Stage 2 model: 4-zone classification, Random Forest, trained only on occupied windows
- [ ] Single 5-class Random Forest trained as a cross-check against the two-stage approach
- [ ] Confusion matrix (raw + row-normalized) for both stages, every split
- [ ] Per-class precision/recall/F1, macro-F1, and weighted-F1 reported
- [ ] Majority-class, random, and RSSI-only baselines computed for comparison
- [ ] Leakage sanity check: shuffle labels and confirm accuracy collapses to majority-class baseline
- [ ] Permutation importance run on held-out session to identify which subcarriers matter

**Gates:**

- Presence above 85% within-session, and clearly beats the RSSI-only baseline. If not, the data or the sync is broken, not the model.
- 4-zone above 75% within-session, with confusion matrix errors concentrated between physically adjacent zones (not scattered). If not, add the second link before touching the model.
- Cross-session (LOSO-CV) zone accuracy beats majority-class and RSSI-only baselines. Holding near the within-session number is the stretch goal, not the minimum bar.

A Random Forest baseline tells us whether the information is there at all. If it gets
near chance, a neural network will not save it. Only consider a neural network if
within-session accuracy is good (>80%) but cross-session collapses (<60%) with 6+
sessions available — see CSI_PROCESSING_V1.md for the specific trigger and next step.

## Stage 5 - Real-time tracking

- [ ] Stream live CSI from the Pi to the PC
- [ ] Run inference on the PC
- [ ] Display predicted zone next to the camera view

## Stage 6 - Improvements

Only after the earlier stages hold up.

- Second link (router as TX, both ESP32s receiving)
- More zones, finer grid
- Continuous (x, y) regression
- Better handling of day-to-day drift (per-session calibration is the V1 mitigation; domain adaptation is a V2+ research topic)
- Small model pushed toward the Pi (inference only — training always stays on the PC)

## Fallbacks

| If this happens | Do this |
|---|---|
| CSI unusable after tuning | Fall back to RSSI-only zone classification |
| Zone accuracy stuck below 70% | Fewer zones, or add the second link |
| Cross-session accuracy collapses | Collect more varied data, revisit features, lean on std-based (drift-stable) features |
| Yocto camera integration blocks progress | Record encoded video only, do all vision on the PC |
| ESP32 CSI quality is the ceiling | Consider ESP32-C6 boards (note: different CSI config struct, firmware needs porting) |
| Model does not beat RSSI-only baseline | Recheck CSI parsing and AGC rescaling before touching the model |
