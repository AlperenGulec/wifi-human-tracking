# pc/dataset

Windows CSI, builds features, joins to labels, and produces session splits.
See `docs/CSI_PROCESSING_V1.md` for full detail.

- Windowing: 2.0 s window, 0.5 s stride (75% overlap)
- 107 features per window: 52 per-subcarrier mean amplitude, 52 per-subcarrier
  std amplitude, mean RSSI, std RSSI, raw (un-rescaled) global amplitude mean
- Labels interpolated to CSI timestamps (not nearest-neighbour copied), then
  converted to zone_id and joined to windows by center timestamp
- Windows spanning a zone transition, or containing a `boundary_flag` frame,
  are dropped from training
- Class imbalance handled via `class_weight="balanced"` at training time —
  no resampling here
- Splits by `session_id` only, never by row

Output: `data/processed/dataset.parquet` (primary), with an optional
`dataset.npz` cache of the raw feature matrix.
