# scripts

Thin command-line entry points. No logic here — each script parses args and
calls into `pc/`. Keeping logic out of scripts makes it testable and reusable.

Implemented:

- `csi_monitor.py` — ESP32 bring-up. Reads the RX serial stream (or replays a
  saved capture), plots the 52-subcarrier amplitude curve, and prints a
  once-per-second line rate / `len` / `sig_mode` / sequence-gap / `dropped`
  summary. This is what closes the Stage 1 gate in `docs/ROADMAP.md`.

  ```
  python scripts/csi_monitor.py --port COM5 --save empty_room.csv
  python scripts/csi_monitor.py --file empty_room.csv --rescale
  python scripts/csi_monitor.py --port COM5 --no-plot
  ```

Expected scripts (add as implemented):

- `build_dataset.py` — join a session's CSI + labels, write `dataset.npz`
- `train.py` — train a model on one or more sessions
- `evaluate.py` — within-session and cross-session evaluation
- `label_session.py` — run detection + homography on a recorded session
