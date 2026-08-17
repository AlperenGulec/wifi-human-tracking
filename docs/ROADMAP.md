# Roadmap

Each stage has a gate. Do not start the next stage until the gate passes.

## Stage 1 - Prove CSI is real *(current)*

Fork ESP32-CSI-Tool, flash TX and RX, get CSI onto the serial port.

- [ ] Both boards build and flash on IDF v4.4
- [ ] TX associates to the RX SoftAP
- [ ] CSI lines appear at 921600 baud
- [ ] `sig_mode == 1`, `len == 256`
- [ ] ~50 lines/s, no sequence gaps
- [ ] Amplitude curve deforms when you walk between the boards

**Gate:** the walk test produces a clear, repeatable change. If the signal looks like
noise after fixing distance, channel, and rate, stop here and fix it. Nothing later
works without this.

## Stage 2 - Recording rig

Pi-side logging and camera recording, PC-side labeling.

- [ ] Pi reads serial, timestamps on arrival, writes `csi.csv`
- [ ] Pi records 720p H.264 video with frame timestamps
- [ ] Yocto image includes what is needed for camera capture
- [ ] PC runs person detection on recorded video
- [ ] Homography maps pixels to floor coordinates
- [ ] `labels.csv` produced end to end

**Gate:** one full session recorded, with CSI and labels that line up in time.

## Stage 3 - Dataset

- [ ] Time-join CSI and labels
- [ ] Sanity-check alignment by eye
- [ ] At least 2 sessions on different days
- [ ] Split by session, not by row

**Gate:** a dataset that can be loaded and inspected without surprises.

## Stage 4 - Basic model

Start with presence, then zones. Random Forest first, no neural network.

- [ ] Presence classification (empty vs occupied)
- [ ] 4-zone classification
- [ ] Confusion matrix for both
- [ ] Within-session and cross-session numbers reported

**Gates:**

- Presence above 85%. If not, the data or the sync is broken, not the model.
- 4-zone above 60% within a session. If not, add the second link before touching
  the model.

A Random Forest baseline tells us whether the information is there at all. If it gets
near chance, a neural network will not save it.

## Stage 5 - Real-time tracking

- [ ] Stream live CSI from the Pi to the PC
- [ ] Run inference on the PC
- [ ] Display predicted zone next to the camera view

## Stage 6 - Improvements

Only after the earlier stages hold up.

- Second link (router as TX, both ESP32s receiving)
- More zones, finer grid
- Continuous (x, y) regression
- Better handling of day-to-day drift
- Small model pushed toward the Pi

## Fallbacks

| If this happens | Do this |
|---|---|
| CSI unusable after tuning | Fall back to RSSI-only zone classification |
| Zone accuracy stuck below 70% | Fewer zones, or add the second link |
| Cross-session accuracy collapses | Collect more varied data, revisit features |
| Yocto camera integration blocks progress | Record encoded video only, do all vision on the PC |
| ESP32 CSI quality is the ceiling | Consider ESP32-C6 boards |
