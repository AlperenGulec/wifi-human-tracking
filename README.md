# Wi-Fi Indoor Human Tracking

Track a person inside a room using only Wi-Fi signals.

A camera is used during data collection to label where the person actually is.
A model learns the link between Wi-Fi CSI and that position. After training, the
camera is removed and tracking runs on Wi-Fi alone.

## How it works

```
ESP32 TX  ──── Wi-Fi packets ────►  ESP32 RX
                                       │ CSI over USB serial
                                       ▼
                             Raspberry Pi 2 (logger)
                             + Pi Camera v2.1 (video)
                                       │ files
                                       ▼
                                  PC (pc/)
                          person detection → floor position
                          → zone labels → dataset → model → eval
```

The Pi only records and timestamps. All vision and machine learning run on a PC.

## Hardware

- 2x ESP32 (WROOM class)
- Raspberry Pi 2 running a Yocto-built image
- Raspberry Pi Camera v2.1
- A PC for training

## Repository layout

| Path | What's there |
|---|---|
| `esp32/` | TX and RX firmware (ESP-IDF) |
| `raspberry-pi/` | Serial logger, camera recorder, session scripts, Yocto notes |
| `pc/` | CSI parsing, labeling, dataset building, training, evaluation |
| `config/` | Per-room configuration (zones, homography) |
| `scripts/` | Command-line entry points |
| `data/` | Generated sessions, datasets, models — not committed |
| `docs/` | Design documents |

## Status

| Stage | State |
|---|---|
| Feasibility research | Done |
| ESP32 firmware bring-up | **Done — Stage 1 gate passed** |
| Pi logging + camera recording | Working end to end |
| Dataset | Not started |
| Model | Not started |
| Real-time tracking | Not started |

## Documentation

| File | What it covers |
|---|---|
| [docs/PROJECT_OVERVIEW.md](docs/PROJECT_OVERVIEW.md) | Goal, scope, what is out of scope |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Boards, roles, placement |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System design and data flow |
| [docs/ESP32_V1.md](docs/ESP32_V1.md) | ESP32 firmware decisions and bring-up |
| [docs/RASPBERRY_PI_V1.md](docs/RASPBERRY_PI_V1.md) | Pi logger, camera, clock model, Yocto config |
| [docs/DATA_FORMAT.md](docs/DATA_FORMAT.md) | File formats and labeling |
| [docs/LABELING_V1.md](docs/LABELING_V1.md) | PC-side ground-truth labeling (video -> x,y -> zone) |
| [docs/CSI_PROCESSING_V1.md](docs/CSI_PROCESSING_V1.md) | CSI parsing, features, model, and evaluation design |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Stages and go/no-go gates |
| [docs/YOCTO_BUILD.md](docs/YOCTO_BUILD.md) | For step-by-step host setup and build commands |

## Expected results

Roughly 75-90% accuracy on 4 zones within one recording session. Accuracy drops
on a different day. Meter-level continuous position at best. Camera-quality pose
from Wi-Fi is not possible on this hardware.
