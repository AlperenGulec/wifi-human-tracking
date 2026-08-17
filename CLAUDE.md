# CLAUDE.md

Context for Claude Code. Keep this short. Details live in `docs/`.

## What this project is

Wi-Fi based indoor human tracking. Two ESP32 boards measure Wi-Fi CSI. A Raspberry Pi
camera records the same room and provides ground truth. A model is trained on a PC to
predict where a person is from CSI alone. The camera is only used for training data.

## Fixed decisions (do not re-open without a reason)

- **CSI amplitude only.** Raw phase is unusable on single-antenna ESP32.
- **2 ESP32 boards in V1**: one TX, one RX. Original ESP32 (WROOM), not S3/C3/C6.
- **ESP-IDF v4.4.x**, pinned. Base firmware forked from ESP32-CSI-Tool.
- **Raspberry Pi 2 is a logger only.** No ML, no live person detection on it.
- **Pi runs Yocto** `core-image-minimal` + `meta-raspberrypi`. Keep Yocto. Add packages
  through recipes, not by switching to Raspberry Pi OS.
- **PC does all vision and ML**: person detection, homography, training, evaluation.
- **Clock: CLOCK_MONOTONIC.** The Pi timestamps every CSI line on arrival and every
  camera frame. No cross-device time sync. The Pi 2 has no RTC, so wall clock is
  recorded once per session as an offset in `session.json`.
- **First ML task**: classify 4 floor zones + 1 "empty room" class. Not (x,y) regression.
- **Train/test split by session, never by row.**

## Layout

| Path | Contents |
|---|---|
| `esp32/tx`, `esp32/rx` | ESP-IDF firmware projects |
| `raspberry-pi/` | Serial logger, camera recorder, session scripts, Yocto notes |
| `pc/` | One Python package: CSI parsing, labeling, dataset, training, eval |
| `config/rooms/` | Per-room zone grid, homography, node positions (YAML) |
| `scripts/` | Thin CLI entry points. No logic here — call into `pc/`. |
| `data/` | Generated only: sessions, datasets, models, results. Gitignored. |
| `docs/` | All design documents |

`pc/` is one installable package (see `pyproject.toml`). Import as `pc.csi`,
`pc.dataset`, etc. Do not duplicate CSI parsing logic across folders.

## Working rules

- Keep it simple. This is a prototype, not a product.
- Respect the hardware limits: Pi 2 is slow, ESP32 serial is the bottleneck.
- Do not add dependencies to the Pi image unless they are really needed.
- Prefer one clear recommendation over several options.
- Write simple English in code comments and docs.
- Never commit anything under `data/` except `.gitkeep` files.

## Current state

ESP32 firmware bring-up is the active task. Raspberry Pi recording rig is designed
but not implemented. No code written yet. See `docs/ROADMAP.md` for next steps.
