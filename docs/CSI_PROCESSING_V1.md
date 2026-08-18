# CSI Processing and Model — Version 1

Everything in this document runs on the PC. The Pi 2 only writes `csi.csv` —
it does no parsing, no math.

```
pc/csi/         parse csi.csv, amplitude, subcarrier selection, RSSI rescale
pc/dataset/     windowing, features, join to labels, session split
pc/training/    two-stage Random Forest
pc/eval/        confusion matrix, baselines, metrics
```

## CSI parsing

Each CSI line is 256 bytes = 128 signed int8 = 128 complex values
(imaginary first, then real). Layout: **LLTF = complex indices 0-63,
HT-LTF = complex indices 64-127.** V1 uses LLTF only.

Within the 64-value LLTF block, the ESP32 orders subcarriers **0..+31, then
-32..-1** (not natural -26..+26 order). Reorder before use.

### The 52 valid subcarriers

| array index | subcarrier | action |
|---|---|---|
| 0 | 0 (DC) | drop |
| 1-26 | +1..+26 | keep |
| 27-31 | +27..+31 | drop (guard) |
| 32-37 | -32..-27 | drop (guard) |
| 38-63 | -26..-1 | keep |

Natural order = concatenate array indices `[38..63]` then `[1..26]`.
**Keep all 52, including the 4 pilot tones (+/-7, +/-21).** They carry the
same amplitude information as data tones for sensing; stripping them adds
bookkeeping for no measurable gain.

### `first_word_invalid`

When set, the first 4 bytes (array indices 0 and 1) are hardware-invalid.
Index 0 is DC, already dropped. **Index 1 is subcarrier +1, one of the 52
kept subcarriers — drop or interpolate it when the flag is set.** Some
firmware builds report the flag incorrectly, so also sanity-check DC and +1
regardless of the flag value.

### Parsing validation

In an empty room, amplitude across the 52 reordered subcarriers should be a
smooth, stable, gently-varying curve, tracking RSSI level packet to packet.

| Symptom | Likely cause |
|---|---|
| Zigzag every other sample | real/imag swapped, wrong stride |
| Curve mirror-flipped or split at the middle | 0..31 / -32..-1 halves not reassembled |
| Spike at center or edge | DC or guard bands not dropped |
| Huge value at subcarrier +1 | `first_word_invalid` not handled |
| Sign-flipped, oddly quantized values | int8 sign/endianness error |

## AGC / amplitude rescaling

The original ESP32 (unlike the S3) does not expose its AGC gain value on
IDF v4.4. **Rescale per-packet using RSSI, before windowing:**

```
amplitude = sqrt(real^2 + imag^2)              # per subcarrier
scale     = sqrt( 10^(RSSI/10) / sum(amplitude^2) )
amplitude_calibrated = amplitude * scale
```

This anchors each packet's total power to the RSSI-implied linear power,
cancelling most of the AGC step while keeping the relative subcarrier shape.
It is not perfect — AGC noise on ESP32 is known to be hard to fully remove —
but it is the standard, low-cost fix.

Per-packet self-normalization (dividing by the packet's own mean/L2 norm) was
considered and rejected as the *only* step: it throws away the absolute
amplitude level, which is itself informative for presence detection. Keep one
raw (un-rescaled) global amplitude mean as an extra feature (see below) so
presence information survives.

## Preprocessing chain (locked, minimal)

1. Drop packets where `len != 256`, `sig_mode != 1`, or RSSI is an outlier
   (> ~3 MAD from the session median, or outside ~-20 to -90 dBm).
2. Hampel filter along time, per subcarrier, window ~5-7 packets, 3-sigma —
   kills spike outliers.
3. RSSI rescale (above).

**Not used in V1** (overengineering at this stage): Butterworth low-pass
filtering, PCA/SVD denoising, upfront variance-based subcarrier pruning.
Revisit only if plots still look spiky after the Hampel filter, or if Random
Forest importance later flags specific dead subcarriers.

## Windowing and features

- **Window: 2.0 seconds. Stride: 0.5 seconds (75% overlap).**
  ~100 packets per window at 50 pkt/s — enough for stable statistics without
  smearing zone transitions too badly. 1 s windows were the original plan;
  2 s gives a steadier spatial fingerprint for a person standing still.
- If within-session accuracy looks suspiciously perfect, re-run with
  non-overlapping windows (stride = window) to sanity-check.

### Feature vector — 107 features (locked)

| Group | Count | Purpose |
|---|---|---|
| Per-subcarrier mean amplitude | 52 | static spatial fingerprint (standing-still zone ID) |
| Per-subcarrier std amplitude | 52 | motion energy, more drift-stable than mean |
| Mean RSSI in window | 1 | coarse level/presence cue |
| Std RSSI in window | 1 | coarse motion cue |
| Mean raw (un-rescaled) global amplitude | 1 | presence anchor, in case rescaling mutes level |

Skewness, kurtosis, min/max/range, and inter-subcarrier correlation are
**not included in V1** — they rarely add much for Random Forest and increase
the cross-session drift surface. Add a 52-feature motion-energy block
(mean abs difference between consecutive packets) only if walking-class
performance underperforms.

No z-scoring needed for Random Forest. If a scaled model (SVM/k-NN/logistic)
is tried later, fit the scaler on training sessions only — never across the
train/test session boundary.

## Dataset building

- **Window-to-label:** label each window by the label at its center
  timestamp, and **drop windows that span a zone transition** (contain more
  than one zone_id) or contain any `boundary_flag` frame.
- **`boundary_flag` windows:** excluded from training, kept as a separate
  held-out set to evaluate boundary behavior specifically.
- **Class imbalance:** use `class_weight="balanced"` in the Random Forest.
  No undersampling, no synthetic oversampling.
- **Split: strictly by session, never by row.** With 2-4 sessions, use
  leave-one-session-out cross-validation (LOSO-CV) — report both directions
  separately, never just the average. Also report a within-session number
  using a time-based split inside one session (train first ~70%
  chronologically, test last ~30%), never a random row split.
- **Minimum data:** aim for >= 500-1000 windows per class; below ~200/class
  results get unstable with ~107 features.
- **Storage: Parquet** (pandas + pyarrow) as the primary dataset file —
  handles the mixed feature/label/session_id/timestamp columns cleanly.
  `dataset.npz` kept as an optional fast-load cache of the raw feature matrix.

## Model

**Two-stage Random Forest:**
1. Stage 1 — presence: empty vs occupied.
2. Stage 2 — 4-zone classification, trained only on occupied windows.

This matches the project's success criteria (presence and zone are graded
separately) and keeps the large "empty" class from distorting the zone
decision boundary. Also train one single 5-class RF as a cross-check.

**Starting hyperparameters** (`sklearn.ensemble.RandomForestClassifier`),
for ~10k-50k windows x 107 features:

```
n_estimators   = 300
max_depth      = None
min_samples_leaf = 5
max_features   = "sqrt"
class_weight   = "balanced"
n_jobs         = -1
random_state   = 42
oob_score      = True
```

**When to consider a neural network:** only if within-session zone accuracy
is good (>80%) but cross-session collapses (<60%), AND there are >= 6-8
sessions across multiple days. First step would be LightGBM, then a small
1D-CNN over the 52 subcarriers (spatial fingerprint) or a GRU/1D-CNN over
time (for the walking/dynamic case). Not needed for V1.

**Feature importance:** use Random Forest impurity importance for a quick
look, but treat **permutation importance on the held-out test session** as
the real answer — impurity importance is biased and reflects training-set
statistics, not generalization.

## Evaluation

Report per split (within-session and each LOSO-CV direction):

- Confusion matrix (raw + row-normalized)
- Per-class precision, recall, F1
- Macro-F1 (fair across classes) and weighted-F1
- Presence: precision/recall specifically on "occupied"

**Baselines to compute every time:** majority-class, random, and
**RSSI-only** (same Random Forest, but trained only on the RSSI features).
If CSI does not clearly beat RSSI-only, something upstream is broken —
check parsing and rescaling before touching the model.

**Reading the confusion matrix:**
- Learned nothing transferable: rows collapse toward one/two columns, errors
  scattered with no geometric pattern, macro-F1 near majority-class baseline.
- Expected, healthy failure mode: strong diagonal, errors concentrated
  between physically **adjacent** zones only.

**Leakage / broken-pipeline sanity checks:**
- Cross-session accuracy ~= within-session and both near-perfect -> suspect
  a session/row split bug or windows straddling the split boundary.
- Shuffle the labels and retrain — accuracy should collapse to the
  majority-class baseline. If it doesn't, something (e.g. session_id or
  timestamp) is leaking into the feature matrix.

## Day-to-day drift

Cheapest-first mitigations for V1:

1. Lean on std-based (motion) features over mean-based (absolute level)
   features — they transfer across days better.
2. Record ~30-120 s of empty room at the start of every session and use it
   as a per-session baseline (subtract/divide the empty-room per-subcarrier
   mean). Cheapest effective fix available.
3. Collect more sessions across more days — the single most reliable fix.
4. Accept a real accuracy drop cross-day in V1. It is a known, partially
   open problem. Domain adaptation techniques (DANN, MMD alignment, GAN
   augmentation) are research-paper territory — not for this prototype.

## What runs where

**PC only.** Do not attempt parsing at scale, filtering, feature extraction,
or model training on the Raspberry Pi 2 (Cortex-A7 900 MHz, 1 GB RAM,
ARMv7) — RAM and CPU are both too tight, and Python ML wheels are painful
on that architecture.

**Looking ahead to Stage 5 (real-time):** windowing becomes a sliding
buffer, per-session calibration runs once at stream start, and a trained
Random Forest is small/cheap enough that **inference** (not training) could
eventually run on the Pi 2. Training always stays on the PC.

## Bring-up checklist

1. Parse one empty-room capture, reorder to -26..+26, plot the 52-subcarrier
   amplitude curve — confirm it is smooth and stable, RSSI tracks level.
2. Build the 107-feature dataset, train Stage-1 (presence) RF, evaluate with
   a within-session time-based split.
3. Train Stage-2 (4-zone) RF on occupied windows, same within-session split.
4. Collect >= 3 sessions across >= 2 days, run LOSO-CV both directions, add
   per-session empty-room calibration.

## Go / no-go gates

| Stage | Gate |
|---|---|
| A — parsing | amplitude curve smooth/stable, no failure signatures above |
| B — presence | within-session > 85%, and beats RSSI-only baseline |
| C — zones (within-session) | > 75%, confusion errors are adjacency-structured |
| D — cross-day | beats majority-class and RSSI-only; stretch goal is staying near the within-session number |

## Failure modes

| Symptom | Cause | Fix |
|---|---|---|
| Cross-session ~= within-session, both near-perfect | leakage: row split, or window straddling session boundary | enforce strict session split, drop straddling windows |
| High accuracy survives label shuffle | session_id/timestamp leaked into features | remove ID/timestamp columns from the feature matrix |
| Cross-session collapses to one class | drift, features not transferable | per-session calibration, lean on std features, more sessions |
| Model barely beats RSSI-only | CSI not used correctly | recheck parsing, subcarrier selection, rescaling |
| Presence detection unreliable | rescaling muted the absolute level | confirm the raw global-amplitude anchor feature is present |

## Deferred to later

- Phase-based features (linear-fit detrend to remove SFO/delay term) —
  experimental, single-antenna CFO still not removable
- Domain adaptation for cross-day drift
- 1D-CNN / GRU models
- Streaming / real-time inference (Stage 5)
- Any of this running on the Raspberry Pi 2
