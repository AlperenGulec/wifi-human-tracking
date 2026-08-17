# pc

One Python package. All PC-side work: parsing, labeling, dataset building,
training, evaluation. Import as `pc.csi`, `pc.dataset`, etc. — installed via the
root `pyproject.toml` (`pip install -e .`).

```
pc/
├── csi/         parse CSI CSV, compute amplitude, RSSI rescaling
├── labeling/    person detection, homography, floor coords, zone ids
├── dataset/     time join CSI + labels, windowing, features, session splits
├── training/    model training
└── eval/        metrics, confusion matrices, cross-session evaluation
```

Rules:

- CSI parsing lives in `pc/csi` only. Every other module imports it — never
  re-parses raw CSV.
- Split train/test by session, never by row. See `docs/DATA_FORMAT.md`.
- Report within-session and cross-session accuracy separately.
