# pc/csi

Parses `csi.csv` (see `docs/DATA_FORMAT.md` and `docs/CSI_PROCESSING_V1.md`).

- Extracts the 52 valid LLTF subcarriers (array indices 1-26 and 38-63),
  reorders to natural -26..+26 order
- Handles `first_word_invalid` (drops/interpolates subcarrier +1 when set)
- Converts raw int8 pairs to amplitude: `sqrt(real^2 + imag^2)`
- Drops malformed packets (`len != 256`, `sig_mode != 1`, RSSI outliers)
- Hampel filter (window 5-7, 3-sigma) per subcarrier along time
- Rescales amplitude per-packet using RSSI to compensate for automatic
  gain control (formula in `docs/CSI_PROCESSING_V1.md`)

This is the single source of truth for CSI parsing. Everything else imports
from here — never re-parse raw CSV elsewhere.
