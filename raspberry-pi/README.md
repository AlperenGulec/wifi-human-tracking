# Raspberry Pi

Logger only. No ML, no live person detection.

```
raspberry-pi/
├── logger/    C serial logger, timestamps on arrival, writes csi.csv
├── camera/    rpicam-vid wrapper + PTS to frames.csv
├── session/   start / stop / verify scripts
└── yocto/     recipes, layer and config notes
```

Full design, Yocto package list, bring-up checklist and failure modes:
[../docs/RASPBERRY_PI_V1.md](../docs/RASPBERRY_PI_V1.md)

Two rules that matter most:

- Everything uses **CLOCK_MONOTONIC**. The Pi 2 has no RTC.
- Keep the image lean. Add packages through recipes, never by switching to Raspberry Pi OS.
