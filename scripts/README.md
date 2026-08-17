# scripts

Thin command-line entry points. No logic here — each script parses args and
calls into `pc/`. Keeping logic out of scripts makes it testable and reusable.

Expected scripts (add as implemented):

- `build_dataset.py` — join a session's CSI + labels, write `dataset.npz`
- `train.py` — train a model on one or more sessions
- `evaluate.py` — within-session and cross-session evaluation
- `label_session.py` — run detection + homography on a recorded session
