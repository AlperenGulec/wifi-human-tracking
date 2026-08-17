# pc/csi

Parses `csi.csv` (see `docs/DATA_FORMAT.md`), converts raw int8 pairs to
amplitude, and rescales using RSSI to compensate for ESP32 automatic gain
control. Keeps the 52 valid LLTF subcarriers.

This is the single source of truth for CSI parsing. Everything else imports
from here.
