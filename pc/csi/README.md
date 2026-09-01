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

## Implemented so far

`parse.py` covers what ESP32 bring-up needs:

| Name | Does |
|---|---|
| `parse_line(line, node_id=None)` | One CSV line -> `Packet`, or `None`. Accepts both the raw 10-field ESP32 line and the 11-field `csi.csv` line the Pi writes. Returns `None` rather than raising, because the serial stream also carries boot messages and partial lines. |
| `lltf_amplitude(raw, first_word_invalid)` | The 52 amplitudes, reordered to -26..+26, with subcarrier +1 interpolated when the flag is set. |
| `rssi_rescale(amp, rssi)` | The per-packet AGC rescale. |
| `SUBCARRIERS` | The 52 subcarrier indices, matching the output order. |

Still to come, in Stage 3: malformed-packet dropping and the Hampel filter.
