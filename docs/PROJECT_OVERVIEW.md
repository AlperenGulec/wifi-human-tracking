# Project Overview

## Goal

Build a system that tracks one person in a room using only Wi-Fi signals.

During data collection, a camera records the same room and gives the true position
of the person. A model learns the mapping from Wi-Fi CSI to that position. After
training, the camera is removed.

## Why this works

A human body absorbs and reflects 2.4 GHz signals. When a person moves, the signal
arriving at a receiver changes in a way that depends on where they are. CSI exposes
that change per subcarrier, which is much richer than a single RSSI value.

## Scope of Version 1

In scope:

- One room, roughly 3x4 m
- One person
- 4 floor zones plus an "empty room" class
- Offline training on recorded data
- Amplitude-only CSI

Out of scope for now:

- Continuous (x, y) coordinates
- Multiple people
- Real-time inference
- Working in rooms the model was not trained in

## Out of scope permanently (hardware limits)

- **Pose estimation or body images from Wi-Fi.** Published work doing this used
  multi-antenna MIMO cards, USRP radios, or FMCW radar. A single-antenna ESP32
  does not have the bandwidth, the antennas, or usable phase.
- **Sub-meter continuous position.** Realistic ESP32 ranging error is 1-3 m.
- **Reliable raw phase.** Corrupted by CFO, SFO, and packet detection delay.

## Success criteria

| Level | Target |
|---|---|
| Minimum | Presence detection (empty vs occupied) above 85% |
| Good | 4-zone classification above 75% within a session |
| Stretch | Zone accuracy holds up on a different day |

## Known hard problems

- CSI drifts when furniture moves, doors open, or the environment changes. Models
  trained one day may not work the next. This is the real research challenge here.
- ESP32 automatic gain control scales CSI amplitude by an unknown factor. Handled
  by rescaling on the PC using RSSI.
