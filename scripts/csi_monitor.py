#!/usr/bin/env python3
"""ESP32 bring-up tool: watch the RX serial stream and plot the CSI amplitude.

This exists to close the Stage 1 gate in docs/ROADMAP.md — "the amplitude curve
deforms clearly and repeatably when you walk between the boards". It is a
diagnostic, not part of the recording pipeline; the Pi writes csi.csv in Stage 2.

    # live, from the RX board
    python scripts/csi_monitor.py --port COM5

    # record while watching, then replay later
    python scripts/csi_monitor.py --port COM5 --save empty_room.csv
    python scripts/csi_monitor.py --file empty_room.csv --rescale

    # stats only, no window
    python scripts/csi_monitor.py --port COM5 --no-plot

Logic lives in pc.csi. This file only moves bytes and draws.
"""

from __future__ import annotations

import argparse
import collections
import sys
import time

import numpy as np

from pc.csi import SUBCARRIERS, lltf_amplitude, parse_line, rssi_rescale


def read_serial(port: str, baud: int):
    """Yield lines from the RX board."""
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is not installed. Run: pip install -e .")

    with serial.Serial(port, baud, timeout=1.0) as ser:
        while True:
            chunk = ser.readline()
            if chunk:
                yield chunk.decode("ascii", errors="replace")


def read_file(path: str):
    """Yield lines from a saved capture."""
    with open(path, "r", encoding="ascii", errors="replace") as fh:
        yield from fh


class Stats:
    """One status line per second. Covers bring-up checks 4 and 5."""

    def __init__(self, interval: float = 1.0) -> None:
        self.interval = interval
        self.t0 = time.monotonic()
        self.reset()
        self.rejected = 0
        self.last_seq: int | None = None
        self.gaps = 0
        self.total = 0

    def reset(self) -> None:
        self.t_mark = time.monotonic()
        self.n = 0
        self.lens: collections.Counter[int] = collections.Counter()
        self.sig_modes: collections.Counter[int] = collections.Counter()
        self.rssi: list[int] = []
        self.fwi = 0

    def add(self, pkt) -> None:
        self.n += 1
        self.total += 1
        self.lens[pkt.length] += 1
        self.sig_modes[pkt.sig_mode] += 1
        self.rssi.append(pkt.rssi)
        self.fwi += bool(pkt.first_word_invalid)

        if self.last_seq is not None and pkt.seq != self.last_seq + 1:
            # Backwards means the board reset; forwards means lost packets.
            if pkt.seq > self.last_seq:
                self.gaps += pkt.seq - self.last_seq - 1
        self.last_seq = pkt.seq

    def maybe_report(self, dropped: int, force: bool = False) -> None:
        elapsed = time.monotonic() - self.t_mark
        if elapsed < self.interval and not force:
            return
        if self.n == 0:
            return

        rate = self.n / elapsed if elapsed > 0 else float("nan")
        lens = " ".join(f"{k}x{v}" for k, v in sorted(self.lens.items()))
        modes = " ".join(f"{k}x{v}" for k, v in sorted(self.sig_modes.items()))
        rssi = f"{min(self.rssi)}..{max(self.rssi)}" if self.rssi else "-"

        print(
            f"[{time.monotonic() - self.t0:6.1f}s] "
            f"{rate:5.1f} lines/s  len={lens or '-'}  sig_mode={modes or '-'}  "
            f"rssi={rssi}  fwi={self.fwi}  seq_gaps={self.gaps}  "
            f"dropped={dropped}  bad_lines={self.rejected}",
            flush=True,
        )
        self.reset()


class Plot:
    """Live amplitude curve, plus a slow rolling mean to make drift visible."""

    def __init__(self, rescale: bool, history: int) -> None:
        try:
            import matplotlib.pyplot as plt
        except ImportError:
            sys.exit("matplotlib is not installed. Run: pip install -e . (or use --no-plot)")

        self.plt = plt
        self.rescale = rescale
        self.recent: collections.deque[np.ndarray] = collections.deque(maxlen=history)

        plt.ion()
        self.fig, self.ax = plt.subplots(figsize=(10, 5))
        (self.line,) = self.ax.plot(SUBCARRIERS, np.zeros(52), lw=1.5, label="now")
        (self.mean,) = self.ax.plot(
            SUBCARRIERS, np.zeros(52), lw=1.0, ls="--", alpha=0.7,
            label=f"mean of last {history}",
        )
        self.ax.set_xlabel("subcarrier")
        self.ax.set_ylabel("amplitude" + (" (RSSI rescaled)" if rescale else " (raw)"))
        self.ax.set_title("LLTF amplitude — walk between the boards")
        self.ax.grid(alpha=0.3)
        self.ax.legend(loc="upper right")
        self.fig.tight_layout()

    def update(self, pkt) -> None:
        amp = lltf_amplitude(pkt.raw, pkt.first_word_invalid)
        if self.rescale:
            amp = rssi_rescale(amp, pkt.rssi)
        self.recent.append(amp)

        self.line.set_ydata(amp)
        self.mean.set_ydata(np.mean(self.recent, axis=0))
        self.ax.relim()
        self.ax.autoscale_view(scalex=False)
        self.fig.canvas.draw_idle()
        self.fig.canvas.flush_events()

    def alive(self) -> bool:
        return bool(self.plt.get_fignums())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port of the RX board, e.g. COM5 or /dev/ttyUSB0")
    src.add_argument("--file", help="replay a saved capture instead of reading serial")
    ap.add_argument(
        "--baud", type=int, default=460800,
        help="default: 460800, matching esp32/rx/main/config.h's UART_BAUD. "
             "That default was 921600 until real-hardware testing on a "
             "Raspberry Pi 2 found it unreliable there (fine from a PC "
             "directly) - see docs/ESP32_V1.md's failure modes.",
    )
    ap.add_argument("--node-id", default="RX1", help="reject lines from other boards")
    ap.add_argument("--save", help="write every accepted line to this file")
    ap.add_argument("--rescale", action="store_true", help="apply the RSSI-based AGC rescale")
    ap.add_argument("--no-plot", action="store_true", help="status lines only, no window")
    ap.add_argument("--history", type=int, default=50, help="packets in the rolling mean")
    ap.add_argument(
        "--fps", type=float, default=10.0,
        help="redraw rate cap; lower it if the plot cannot keep up with the serial stream",
    )
    args = ap.parse_args()

    lines = read_file(args.file) if args.file else read_serial(args.port, args.baud)
    plot = None if args.no_plot else Plot(args.rescale, args.history)
    stats = Stats()
    out = open(args.save, "w", encoding="ascii", newline="") if args.save else None

    dropped = 0
    draw_period = 1.0 / args.fps if args.fps > 0 else 0.0
    next_draw = 0.0
    try:
        for line in lines:
            pkt = parse_line(line, node_id=args.node_id)
            if pkt is None:
                # Boot messages, partial lines after a reset, line noise.
                if line.strip():
                    stats.rejected += 1
                continue

            stats.add(pkt)
            dropped = pkt.dropped
            if out is not None:
                out.write(line if line.endswith("\n") else line + "\n")
            # Throttle by wall clock, not packet count: if the GUI backend is
            # slow, drawing must thin out on its own or the serial buffer backs
            # up and the curve on screen lags behind the room.
            now = time.monotonic()
            if plot is not None and now >= next_draw:
                next_draw = now + draw_period
                plot.update(pkt)
                if not plot.alive():
                    break
            stats.maybe_report(dropped)
    except KeyboardInterrupt:
        pass
    finally:
        if out is not None:
            out.close()

    # Covers the last interval, so a short file replay still reports its len
    # and sig_mode histograms — that is bring-up check 4.
    stats.maybe_report(dropped, force=True)
    print(f"\n{stats.total} packets, {stats.gaps} sequence gaps, "
          f"{stats.rejected} unparsable lines, RX reported {dropped} ring drops.")
    if args.save:
        print(f"saved to {args.save}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
