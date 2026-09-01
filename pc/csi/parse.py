"""Parse RX CSI lines and turn raw int8 pairs into LLTF amplitudes.

This is the only place in the project that parses raw CSI. Everything else
imports from here. See docs/DATA_FORMAT.md for the line format and
docs/CSI_PROCESSING_V1.md for the subcarrier layout.

V1 scope: enough to plot an amplitude curve during ESP32 bring-up. The Hampel
filter, packet drop rules, windowing and features arrive in Stage 3.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

__all__ = [
    "Packet",
    "SUBCARRIERS",
    "parse_line",
    "lltf_amplitude",
    "rssi_rescale",
]

# The LLTF block is the first 64 complex values (128 bytes) of the record.
LLTF_COMPLEX = 64

# The ESP32 orders LLTF subcarriers 0..+31 then -32..-1, not natural order.
# Natural -26..+26 order = array indices [38..63] then [1..26]. This drops DC
# (index 0) and both guard bands (27..31 and 32..37), leaving 52.
_KEEP = np.concatenate([np.arange(38, 64), np.arange(1, 27)])

# What each of the 52 kept values means, in the same order.
SUBCARRIERS = np.concatenate([np.arange(-26, 0), np.arange(1, 27)])

# Position of subcarrier +1 in the reordered array. Array index 1 is the second
# half of the first 4 bytes, so this is the one kept value that
# first_word_invalid corrupts.
_SC_PLUS1 = 26

# node_id,seq,t_us,rssi,noise_floor,sig_mode,len,first_word_invalid,dropped,csi_raw
_N_FIELDS = 10


@dataclass
class Packet:
    """One CSI record. Field order matches the CSV."""

    node_id: str
    seq: int
    t_us: int
    rssi: int
    noise_floor: int
    sig_mode: int
    length: int
    first_word_invalid: int
    dropped: int
    raw: np.ndarray
    pi_timestamp_ms: int | None = None


def parse_line(line: str, node_id: str | None = None) -> Packet | None:
    """Parse one CSV line into a Packet, or return None if it is not one.

    Accepts both shapes: the raw ESP32 line (10 fields) and a csi.csv line
    written by the Pi (11 fields, pi_timestamp_ms prepended).

    Returns None rather than raising, because the serial stream legitimately
    contains boot messages, partial lines after a reset, and line noise. Pass
    node_id to also reject records from an unexpected board.
    """
    fields = line.strip().split(",")

    pi_ms = None
    if len(fields) == _N_FIELDS + 1:
        try:
            pi_ms = int(fields[0])
        except ValueError:
            return None
        fields = fields[1:]
    elif len(fields) != _N_FIELDS:
        return None

    if node_id is not None and fields[0] != node_id:
        return None

    try:
        seq, t_us, rssi, noise_floor, sig_mode, length, fwi, drop = (
            int(f) for f in fields[1:9]
        )
        values = np.fromiter((int(v) for v in fields[9].split()), dtype=np.int32)
    except ValueError:
        return None

    # len is the number of values in the last field, by construction on the
    # ESP32 side. A mismatch means a truncated or spliced line.
    if values.size != length:
        return None
    if values.size and (values.min() < -128 or values.max() > 127):
        return None
    raw = values.astype(np.int8)

    return Packet(
        node_id=fields[0],
        seq=seq,
        t_us=t_us,
        rssi=rssi,
        noise_floor=noise_floor,
        sig_mode=sig_mode,
        length=length,
        first_word_invalid=fwi,
        dropped=drop,
        raw=raw,
        pi_timestamp_ms=pi_ms,
    )


def lltf_amplitude(raw: np.ndarray, first_word_invalid: int = 0) -> np.ndarray:
    """Amplitude of the 52 valid LLTF subcarriers, in natural -26..+26 order.

    raw is the int8 array straight off the wire: two bytes per subcarrier,
    imaginary first then real.
    """
    if raw.size < 2 * LLTF_COMPLEX:
        raise ValueError(f"need {2 * LLTF_COMPLEX} bytes of LLTF, got {raw.size}")

    v = raw[: 2 * LLTF_COMPLEX].astype(np.float64)
    imag = v[0::2]
    real = v[1::2]
    amp = np.hypot(real, imag)[_KEEP]

    if first_word_invalid:
        # The first 4 bytes are hardware-invalid. Index 0 is DC and already
        # dropped; index 1 is subcarrier +1, which we keep. Interpolate it from
        # its neighbours (-1 and +2) rather than letting a garbage value through.
        amp[_SC_PLUS1] = 0.5 * (amp[_SC_PLUS1 - 1] + amp[_SC_PLUS1 + 1])

    return amp


def rssi_rescale(amp: np.ndarray, rssi: float) -> np.ndarray:
    """Undo most of the AGC step by anchoring packet power to the RSSI level.

    The original ESP32 does not expose its AGC gain, so total amplitude jumps by
    an unknown factor even in a still room. Scaling each packet to the linear
    power its RSSI implies cancels most of that while keeping the relative
    subcarrier shape, which is the part that carries position information.
    """
    power = float(np.sum(amp ** 2))
    if power <= 0.0:
        return amp.copy()
    return amp * math.sqrt(10.0 ** (rssi / 10.0) / power)
