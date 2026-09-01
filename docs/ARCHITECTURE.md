# Architecture

## Data flow

```
ESP32 TX (our firmware, STA)
   │  associates to the RX SoftAP
   │  sends UDP packets, ~50 packets/s, HT20
   ▼
ESP32 RX (our firmware, SoftAP)
   │  CSI callback → ring buffer → main loop → UART
   │  CSV lines, raw int8 CSI, 921600 baud
   ▼
Raspberry Pi 2
   │  reads serial, prepends its own timestamp, appends to file
   │  records camera video with frame timestamps
   ▼
files on disk  ──── copied to PC ────►
                                        PC
                                        │ person detection on video
                                        │ homography → floor (x, y)
                                        │ (x, y) → zone label
                                        │ join CSI and labels by timestamp
                                        │ train model
                                        ▼
                                   trained model
```

## What runs where

| Layer | Responsibility |
|---|---|
| **ESP32 TX** | Generate packets at a stable rate. Nothing else. |
| **ESP32 RX** | Capture CSI, add sequence number and local timestamp, print CSV. No filtering, no math. |
| **Raspberry Pi** | Read serial, timestamp on arrival, write to disk. Record video. Nothing else. |
| **PC** | Person detection, homography, label generation, dataset build, training, evaluation. |

Implemented as one Python package: `pc/csi`, `pc/labeling`, `pc/dataset`,
`pc/training`, `pc/eval`. See [../README.md](../README.md) for the repository layout.

## Why this split

- The ESP32 CSI callback runs inside the Wi-Fi task. It must stay cheap, so it only
  copies data into a ring buffer. All processing happens later, off-device.
- The Pi cannot do vision in real time, so video is recorded and processed later.
- Keeping amplitude computation on the PC means we keep the raw values and can change
  the rescaling method without reflashing anything.

## Firmware structure

Both firmwares are written by us. ESP-IDF is used as a command-line SDK because the
CSI API lives in Espressif's closed Wi-Fi driver — there is no way around it — but no
IDE and no third-party CSI code is involved.

The RX is a single task plus a lock-free ring buffer, not a producer task and a
consumer task talking through a FreeRTOS queue. FreeRTOS is still there because the
Wi-Fi driver requires it, but it barely shows up in our code.

Full reasoning, including why bare-metal would make timing worse rather than better:
[ESP32_V1.md](ESP32_V1.md).

## Synchronization

**One clock: the Raspberry Pi.**

- Every CSI line gets a Pi timestamp the moment it is read from serial.
- Every camera frame gets a Pi timestamp when captured.
- CSI and labels are joined later by nearest timestamp, tolerance ±50 ms.

No NTP between devices, no PTP, no ESP32 clock sync. A person walks about 1 m/s, so
50 ms of error is about 5 cm. Zone cells are far larger than that.

The ESP32's own microsecond timestamp travels in the CSV, but it is used only for
diagnosing gaps and jitter — never for alignment.

Implementation note: read the serial port in a tight loop and timestamp immediately
on read. Do not let lines pile up in a buffer and then stamp a whole batch.

## Transport choice

USB serial from RX to Pi.

- Simple, no extra Wi-Fi traffic competing with the CSI link
- Gives clean arrival timestamps
- 50 pps of CSV is ~44 KB/s, about half a 921600 baud link

Wi-Fi UDP or MQTT were rejected for V1: they add congestion on the same band we are
measuring, and the Pi's Ethernet shares bandwidth with USB.

## Topology options

**V1 (current): dedicated pair.** TX transmits, RX receives. One link. Fully under our
control, no router involved. This is the simplest way to prove CSI works.

**V2 option: router as transmitter.** Both ESP32s become receivers listening to the
home router. That gives two links from the same two boards, which improves zone
separation. Costs a second USB cable and depends on router position.

One link is basically a line through the room, so a person left of the line and right
of it can look similar. Adding the second link is the first upgrade if zone accuracy
stalls.

## Camera ground truth

1. Record video on the Pi with per-frame timestamps.
2. On the PC, run a person detector (YOLO or MobileNet-SSD) on the recorded frames.
3. Take the bottom-center of the bounding box as the foot point.
4. Apply a homography to map that pixel to floor (x, y).
5. Convert (x, y) to a zone id.

The homography is computed once per camera setup: mark 4 or more known floor points,
click them in one frame, and use `cv2.findHomography`. Recompute if the camera moves.

5-15 fps of ground truth is enough. The person does not move faster than CSI can follow.
