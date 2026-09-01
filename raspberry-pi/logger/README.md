# csi-logger

Reads CSI CSV from the ESP32 RX over serial, prepends a `CLOCK_MONOTONIC`
arrival timestamp, appends to a file. Nothing else — no parsing of the CSI
payload, no maths. That all happens on the PC.

```
csi-logger -p /dev/esp32-rx -o csi.csv -n RX1
```

| Flag | Default | Meaning |
|---|---|---|
| `-p` | `/dev/esp32-rx` | serial device (udev symlink, see `99-esp32.rules`) |
| `-o` | `csi.csv` | output file, opened in append mode |
| `-n` | `RX1` | node id; lines not starting `<node_id>,` are dropped |

Build: `make`. Install: `make install` (or through the Yocto recipe, which is
how it gets onto the image — `core-image-minimal` ships no toolchain, so this
cannot be compiled on the Pi itself).

Stop it with **SIGINT** (`kill -INT`), not `SIGKILL` — the handler flushes and
closes the file. `session_stop.sh` relies on this.

## Output

The ESP32 line with `pi_timestamp_ms` prepended, giving the 11-field form in
`docs/DATA_FORMAT.md`:

```
pi_timestamp_ms,node_id,seq,t_us,rssi,noise_floor,sig_mode,len,first_word_invalid,dropped,csi_raw
```

`pc/csi/parse.py` accepts this directly — verified against 638 real captured
lines.

## Design notes

Details and reasoning: `docs/RASPBERRY_PI_V1.md`, "Serial logger".

- **One `clock_gettime()` per `read()` burst**, applied to every complete line
  in that burst. Not per byte, and not per line — the point is to stamp arrival,
  and the syscall cost per line would be wasted.
- **Raw, non-canonical** (`cfmakeraw`, `VMIN=1`, `VTIME=0`). Canonical mode
  buffers inside the tty layer, which would destroy the arrival timestamp.
- **Partial-line carry buffer.** USB delivers arbitrary chunk boundaries, so a
  read can end mid-line, mid-number, or exactly on the `\n`.
- **Flush on a ~1 s timer**, never per line. At ~44 KB/s a crash costs at most
  ~44 KB, and per-line flushing would stall the read loop.
- **Reconnect loop.** If the ESP32 resets, the USB device re-enumerates and
  `read()` returns 0 or `EIO`/`ENXIO`. The logger closes, **drops the partial
  buffer**, and reopens.

## Known, accepted behaviours

- **The first CSI line after an ESP32 reset is usually lost.** The bootloader
  prints at a different baud, so its bytes arrive as garbage with no newline and
  get prepended to the first real line. The `node_id` prefix check then drops
  that merged line. Losing one line per reset is cheaper than trying to
  resynchronise mid-line.
- **A line longer than 8 KB is dropped** and the logger resynchronises at the
  next newline. A real line is ~870 B, so this only triggers on a wedged port.
- The `node_id` check requires the following character to be a comma, so `RX1`
  does not accidentally match a hypothetical `RX10`.
