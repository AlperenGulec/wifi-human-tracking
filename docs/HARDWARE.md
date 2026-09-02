# Hardware

## Devices and roles

| Device | Role | Notes |
|---|---|---|
| ESP32 #1 (TX) | Sends Wi-Fi packets at a fixed rate | Runs `active_sta`. Only needs power. |
| ESP32 #2 (RX) | Captures CSI, prints CSV over USB serial | Runs `active_ap`. USB to the Pi. |
| Raspberry Pi 2 | Serial logger + camera recorder | Yocto image. Logger only. |
| Pi Camera v2.1 | Ground truth video | 720p, ~10 fps, hardware H.264 |
| PC | Vision, labeling, training | All heavy work happens here |

## Board choice

Original ESP32 (WROOM class), single antenna. Newer chips (C6, C5) give cleaner CSI
and more subcarriers, but the original ESP32 works and is what we have. If CSI quality
becomes the blocker, upgrading the boards is the first hardware change to consider.

## Placement rules

- Keep TX and RX **more than 1 m apart**. The PCB antenna has poor directivity and is
  easily disturbed by the board itself at short range.
- Put TX and RX at **opposite corners** of the room, at similar height (1.5-2 m).
  The link then crosses the whole walkable area.
- Keep both boards **physically fixed**. If a board moves, the CSI change looks exactly
  like a person moving. Tape or mount them.
- Keep antennas away from metal, walls, and the Pi itself.
- Both boards must be USB powered. CSI needs the radio fully active, so power draw is
  constant and the boards run warm. No batteries.
- **Watch for brownouts at radio power-up.** At the IDF default 20 dBm TX power, both
  of our boards reset repeatedly on `E BOD: Brownout detector was triggered`
  immediately after `phy_init` — the current spike when the radio comes up exceeds
  what the onboard regulator can hold. Swapping USB supplies and cables did not fix
  it; the limit is on the board, downstream of USB. The fix is capping TX power at
  10 dBm in `sdkconfig.defaults` (see `docs/ESP32_V1.md`). A bad cable *can* add to
  the problem on top of this, so use a short, decent one.
- A brownout-looping board never finishes bringing its radio up, so an RX in this
  state has no visible SoftAP at all and TX just reports `NO_AP_FOUND` forever.
  When a link "won't associate", scan for the SSID from a phone or PC first — that
  tells you which board to look at.

## Raspberry Pi 2 limits

- Quad-core Cortex-A7 at 900 MHz, 1 GB RAM, ARMv7
- 100 Mbit Ethernet shared with USB
- Video H.264 encoding is hardware accelerated, so recording is fine
- **Cannot** run person detection in real time. Even a Pi 4 manages only a few fps.
- **Cannot** run model inference at a useful rate

The Pi is a logger. Treat any plan that puts ML on it as a mistake.

## Room setup

One room, roughly 3x4 m. Mark 4 zones on the floor with tape. Also record an empty room
class. Keep the layout fixed between sessions and note any changes.
