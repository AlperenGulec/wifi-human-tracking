# ESP32 Firmware - Version 1

## Base project

Fork **ESP32-CSI-Tool** (Steven Hernandez).

- `active_ap` → our RX firmware
- `active_sta` → our TX firmware

It already outputs CSV over serial in almost the format we want. Do not write CSI
capture from scratch.

**Pin ESP-IDF v4.4.x.** The tool targets v4.3 and does not build cleanly on v5.x.
Do not auto-update the IDF.

Espressif's `esp-csi` repo is a useful reference for rate forcing and its Python
parser, but its examples are increasingly tuned for newer chips.

## Roles

### TX (`active_sta`)

- Associates to the RX SoftAP
- Sends UDP packets in a loop, `vTaskDelay(~20 ms)` → about 50 packets/s
- FreeRTOS tick rate set to 1000 Hz for 1 ms delay granularity

**Why associated mode matters:** an unassociated ESP32 transmits at 1 Mbps DSSS, which
carries no training fields and produces no usable CSI. Being associated forces an
OFDM/HT rate automatically, so no rate hacks are needed.

Avoid `esp_wifi_internal_set_rate()`. It is undocumented and changes between IDF
versions. We do not need it with this topology.

### RX (`active_ap`)

- SoftAP that the TX connects to
- CSI callback copies the raw buffer plus metadata into a struct and pushes it to a
  FreeRTOS queue. **No printf, no float math, no UART writes in the callback.**
- A separate lower-priority UART task formats the CSV line and writes it out

Queue depth 32, drop and count on overflow. UART task stack ~4 KB.

## CSI configuration

```c
wifi_csi_config_t cfg = {
    .lltf_en           = 1,
    .htltf_en          = 1,
    .stbc_htltf2_en    = 1,
    .ltf_merge_en      = 1,   // averages LLTF and HT-LTF, less noise
    .channel_filter_en = 0,   // OFF: keep subcarriers independent for sensing
    .manu_scale        = 0,   // auto scale
};
esp_wifi_set_csi_config(&cfg);
esp_wifi_set_csi_rx_cb(&csi_rx_cb, NULL);
esp_wifi_set_csi(true);
```

## RF settings

| Setting | Value | Reason |
|---|---|---|
| Power save | `esp_wifi_set_ps(WIFI_PS_NONE)` | Mandatory. Otherwise CSI is intermittent and timestamps lose precision. |
| Bandwidth | HT20 (`WIFI_BW_HT20`) | Clean 256-byte layout, half the data. Default is HT40, must be set explicitly. |
| Channel | one fixed quiet channel (1, 6 or 11), not the home network's | Less interference and contention |
| TX power | default | Do not max it out. Close range can slam the RX AGC. |

## Data format on the wire

The ESP32 sends **raw signed int8 pairs**, not amplitudes. Amplitude is computed on
the PC so we keep the raw values and can change the rescaling method later.

Byte layout: each subcarrier is 2 bytes, **imaginary first, then real**.
Field order: LLTF, HT-LTF, STBC-HT-LTF.

Expected lengths:

| `len` | Meaning |
|---|---|
| 256 | HT20. What we want. |
| 128 | Legacy/non-HT frame. **Rate fell back to 1 Mbps. Fix this.** |
| 384 / 612 | HT40 or STBC variants. Not expected here. |

If `first_word_invalid` is set, discard the first 4 bytes (a known ESP32 hardware
limitation).

## Subcarriers to keep

Keep the **52 valid LLTF subcarriers**. Drop guard bands, null subcarriers, and DC.
Ignore the HT-LTF block in V1 - 52 features are plenty for a 5-class problem.

Selection happens on the PC, so it can be retuned without reflashing.

## Serial output

- **Baud: 921600.** Reliable on both CP2102 and CH340 bridges. Higher rates
  (1.5M, 2M) work on good hardware but are flaky.
- **CSV, not binary.** At 50 pps we are far from saturation and CSV is debuggable.
  Switch to binary only if we push past ~100 pps.
- Include a **sequence counter** and the **local microsecond timestamp** in every
  record, even though the Pi timestamps on arrival. Gaps in the sequence tell us
  where packets are being lost.

Line emitted by the ESP32 (the Pi prepends its own timestamp):

```
node_id,seq,rssi,noise_floor,sig_mode,len,<raw int8 values...>
```

## Configurable parameters

Expose via menuconfig or a small `config.h`: Wi-Fi channel, packet rate, `node_id`,
baud rate, and a flag for LLTF-only vs full CSI.

## Bring-up checklist

1. Build and flash both boards on IDF v4.4. Confirm the TX associates to the RX SoftAP.
2. Confirm CSI lines appear on the RX serial at 921600.
3. Check metadata: `sig_mode == 1`, bandwidth == 0, `len == 256`.
   **If `len == 128`, stop and fix the rate fallback first.**
4. Check rate and loss: about 50 lines/s, no gaps in the sequence counter, stable RSSI.
5. Plot the 52 amplitudes with an empty room. Expect a smooth, structured curve.
6. Walk through each zone. The curve should deform clearly and repeatably.

## Go / no-go signal

**Go:** a steady ~50 lines/s of `sig_mode==1, len==256` records whose amplitude curve
is smooth and roughly stable in an empty room, and visibly changes when you walk.

**No-go:** flat zeros, identical records every time, or `len == 128`.

## Failure modes

| Symptom | Cause | Fix |
|---|---|---|
| `len == 128` | Rate fell back to 1 Mbps | Confirm the TX is actually associated |
| All-zero or constant CSI | TX not transmitting, or wrong channel | Check channel match and association |
| Low packet rate, gaps in sequence | Serial saturation | Fewer subcarriers, or binary output |
| Low rate, no sequence gaps | Losses on the air | Check placement, distance, interference |
| Amplitude jumps with a still room | Automatic gain control stepping | Expected. Rescale using RSSI on the PC. |
| Build errors | Wrong IDF version | Pin v4.4.x |

## Deferred to V2

- Router as transmitter, both ESP32s receiving
- Promiscuous / sniffer mode
- Binary serial framing and rates above 50 pps
- HT-LTF subcarriers and any use of phase
- On-device filtering or inference
- External antennas
