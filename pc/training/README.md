# pc/training

Two-stage Random Forest baseline (see `docs/CSI_PROCESSING_V1.md` for the
full design and hyperparameters):

1. Stage 1 — presence: empty vs occupied
2. Stage 2 — 4-zone classification, trained only on occupied windows

A single 5-class Random Forest is also trained as a cross-check against the
two-stage approach.

Starting hyperparameters: `n_estimators=300`, `max_depth=None`,
`min_samples_leaf=5`, `max_features="sqrt"`, `class_weight="balanced"`,
`oob_score=True`.

No neural network until this baseline proves the information is present.
See `docs/ROADMAP.md` Stage 4 for the go/no-go gates, and
`docs/CSI_PROCESSING_V1.md` for the specific trigger condition to consider
a neural network.
