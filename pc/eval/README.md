# pc/eval

Metrics and confusion matrices. Always reports within-session accuracy
(optimistic) and cross-session accuracy via leave-one-session-out
cross-validation (the real number) separately — each LOSO-CV direction
reported on its own, not averaged.

Reports per split: confusion matrix (raw + row-normalized), per-class
precision/recall/F1, macro-F1, weighted-F1.

Always computes three baselines for comparison: majority-class, random, and
RSSI-only (same model, RSSI features only, no CSI). If the full model does
not clearly beat RSSI-only, the CSI pipeline is not adding value and needs
to be re-checked (parsing, rescaling) before the model is trusted.

Includes a leakage sanity check: shuffle labels and retrain — accuracy
should collapse to the majority-class baseline.

See `docs/CSI_PROCESSING_V1.md` for full detail on reading confusion
matrices (adjacency-structured errors are expected and healthy; collapse
toward one class means the model learned nothing transferable).
