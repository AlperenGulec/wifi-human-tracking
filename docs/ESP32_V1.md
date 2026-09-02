# ESP32 Firmware - Version 1

We write both firmwares ourselves. No fork, no third-party CSI project, no IDE.

## Toolchain policy

**ESP-IDF is used strictly as a command-line SDK. All application code is ours.**

This is a deliberate choice. The goal of the project is to understand every line,
so we do not want a large donated codebase we did not write.

| Thing | Decision |
|---|---|
| ESP-IDF | **Required.** See below. Used via `export.sh` + `idf.py` only. |
| IDE (VS Code plugin, Eclipse, Espressif-IDE) | **Not used.** Terminal and a text editor. |
| ESP32-CSI-Tool or any other CSI project | **Not used.** We write TX and RX from scratch. |
| Arduino core / PlatformIO | **Not used.** They hide `sdkconfig`, so we lose control of the CSI and FreeRTOS options we need. |
| `menuconfig` as a workflow | **Not used.** Config lives in a committed `sdkconfig.defaults`. |

### Why ESP-IDF cannot be dropped

Wi-Fi CSI on the ESP32 comes out of Espressif's closed-source Wi-Fi driver
(`libnet80211.a`, `libpp.a`, `libphy.a`). The only way to reach it is
`esp_wifi_set_csi_rx_cb()`. There is no open-source Wi-Fi MAC/PHY for this chip.

That blob does not stand alone. It calls into FreeRTOS through an adapter table,
needs NVS for radio calibration data, needs `esp_event`, and the TX needs
`esp_netif` + LwIP for its UDP socket. It also pulls in the bootloader, the
partition table and the linker scripts. You cannot take one library out of IDF.

So "no IDF" is not reachable on this hardware. "No IDE, no borrowed code, config
in git" is, and that is what we do.

### Why FreeRTOS stays

`app_main()` is already a FreeRTOS task — there is no bare-metal entry point once
Wi-Fi is in the picture. `esp_wifi_init()` fails if the scheduler is not running.

Removing it would also make timing *worse*, not better:

| Jitter source | Size |
|---|---|
| FreeRTOS context switch | ~2 us |
| Wi-Fi task holding the CPU (PHY work, retries, calibration) | 100s of us |
| Flash cache miss stall | ~10 us |
| CH340 / CP2102 USB batching | 1-2 ms |
| **Our join tolerance** | **+/-50 ms** |

Deleting the scheduler removes the 2 us and none of the rest. The Wi-Fi blob is
the dominant perturbation and it cannot be scheduled around.

What we *do* control is how much RTOS appears in **our** code, and the answer is
almost none: no extra tasks, no queues, no priorities. See the RX design below.

## ESP-IDF version

**Use the `release/v5.5` branch.**

The old v4.4 pin only existed because ESP32-CSI-Tool does not build on v5.x. That
reason is gone, and v4.4 is end-of-life.

- v5.5 is supported into 2028 and the classic-ESP32 CSI API is unchanged there.
- v6.x is newer but brings breaking changes for no benefit here, and classic
  ESP32 CSI is the least-exercised path in it.
- Newer chips (C5/C6) use a different struct (`wifi_csi_acquire_config_t`).
  Ignore C6-targeted example code — it will not compile for us.

Record the exact `git describe` output of your IDF checkout in `session.json`
notes for the first real capture, so a result can always be traced to a toolchain.

### v4 -> v5 renames that matter

- `CONFIG_ESP32_WIFI_*` is now `CONFIG_ESP_WIFI_*`
  (so it is `CONFIG_ESP_WIFI_CSI_ENABLED`).
- Several Wi-Fi calls return `ESP_ERR_INVALID_ARG` instead of `ESP_ERR_WIFI_ARG`.

## Install and build

Once, on the Ubuntu host:

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32
```

In every new shell:

```bash
. ~/esp/esp-idf/export.sh
```

On Windows the same install is driven from PowerShell — clone to a drive with
5-8 GB free, run `install.ps1 esp32`, and use `. $HOME\esp\esp-idf\export.ps1`
(or the "ESP-IDF PowerShell" start-menu shortcut) in place of `export.sh`.
Everything below is identical apart from the port name (`COM5`, not
`/dev/ttyUSB0`).

Build and flash:

```bash
cd esp32/rx
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor    # ctrl-] to quit
```

**After changing `sdkconfig.defaults`, delete `sdkconfig` — `idf.py fullclean`
alone is not enough.**

```bash
rm sdkconfig && idf.py build
```

`fullclean` wipes the build directory but leaves `sdkconfig` in place, and
`sdkconfig.defaults` is only consulted when `sdkconfig` does not exist. So a
fullclean-and-rebuild silently produces a binary with the *old* settings. This
was hit for real when adding `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER`: fullclean +
build reported success, and the generated `sdkconfig` still read `=20`.
`sdkconfig` is generated and gitignored, so deleting it is safe.

Always confirm the setting actually landed rather than trusting the build:

```bash
grep CONFIG_ESP_WIFI_CSI_ENABLED sdkconfig
```

## Project layout

Both projects are plain IDF apps, four files each:

```
esp32/tx/
├── CMakeLists.txt          3 lines, project boilerplate
├── sdkconfig.defaults      committed - this replaces menuconfig
└── main/
    ├── CMakeLists.txt      idf_component_register(...)
    ├── config.h            channel, SSID, rate, node_id
    └── main.c              ~120 lines

esp32/rx/
├── CMakeLists.txt
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── config.h
    └── main.c              ~200 lines
```

`sdkconfig` itself stays gitignored (it is generated). `sdkconfig.defaults` is
committed, so a fresh clone builds identically with no interactive step.

## sdkconfig.defaults

Shared by both projects, plus notes on why each line exists. One exception:
`CONFIG_ESP_CONSOLE_UART_BAUDRATE` is `460800` on RX and `921600` on TX — see
below.

```
CONFIG_IDF_TARGET="esp32"

# 1 ms tick, so vTaskDelayUntil(20 ms) is exact
CONFIG_FREERTOS_HZ=1000

# the whole point
CONFIG_ESP_WIFI_CSI_ENABLED=y

# keep our loop off core 0, which the Wi-Fi driver owns
CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=6144

CONFIG_ESP_CONSOLE_UART_BAUDRATE=921600   # RX: 460800 - see "Serial output"
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
```

After the first build, check the generated `sdkconfig` to confirm
`CONFIG_ESP_WIFI_CSI_ENABLED=y` really took. If a symbol was renamed between
versions it is silently ignored, and you get a firmware with no CSI at all.

## TX design (`esp32/tx`)

Job: put packets in the air at a steady rate. Nothing else.

1. `nvs_flash_init()`, `esp_netif_init()`, `esp_event_loop_create_default()`
2. Wi-Fi in STA mode, connect to the RX SoftAP
3. `esp_wifi_set_ps(WIFI_PS_NONE)`
4. Wait for the got-IP event
5. UDP socket, then a loop

```c
TickType_t last = xTaskGetTickCount();
while (1) {
    sendto(sock, payload, sizeof(payload), 0,
           (struct sockaddr *)&dest, sizeof(dest));
    vTaskDelayUntil(&last, pdMS_TO_TICKS(TX_PERIOD_MS));   // 20 ms
}
```

**`vTaskDelayUntil`, not `vTaskDelay`.** `vTaskDelay(20)` counts from *after*
`sendto()` returns, so the period drifts with send latency. `vTaskDelayUntil`
holds a stable 50 pps.

Do not try to drive this from a hardware timer ISR. The Wi-Fi and LwIP stacks
cannot be called from interrupt context.

**Why associated mode matters:** an unassociated ESP32 transmits at 1 Mbps DSSS,
which carries no training fields and produces no usable CSI. Being associated
forces an OFDM/HT rate automatically, so no rate hacks are needed.

Avoid `esp_wifi_internal_set_rate()`. It is undocumented and changes between IDF
versions. We do not need it with this topology.

## RX design (`esp32/rx`)

Job: capture CSI, print CSV. Also nothing else.

Structure is a **single task plus a lock-free ring buffer**. No second task, no
`xQueueSend`, no priorities to reason about. The CSI callback is the producer
(it runs inside the Wi-Fi task on core 0), `app_main` is the consumer (core 1).

### Producer - the CSI callback

Runs inside the Wi-Fi task, so it must stay cheap. Copy and leave.

```c
#define RING_SZ 32          /* power of two, so & works instead of % */

typedef struct {
    uint32_t seq;
    uint32_t dropped;
    int64_t  t_us;
    int8_t   rssi;
    int8_t   noise_floor;
    uint8_t  sig_mode;
    uint8_t  first_word_invalid;
    uint16_t len;
    int8_t   buf[CSI_BUF_MAX];
} csi_record_t;

static csi_record_t ring[RING_SZ];
static volatile uint32_t head = 0, tail = 0;
static volatile uint32_t dropped = 0;
static uint32_t seq = 0;

static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    uint32_t next = (head + 1) & (RING_SZ - 1);
    if (next == tail) { dropped++; return; }      /* full: drop, count it */

    uint16_t len = info->len;
    if (len > CSI_BUF_MAX) len = CSI_BUF_MAX;     /* clamp before copying */

    csi_record_t *r = &ring[head];
    r->seq   = ++seq;
    r->dropped = dropped;
    r->t_us  = esp_timer_get_time();
    r->rssi  = info->rx_ctrl.rssi;
    r->noise_floor = info->rx_ctrl.noise_floor;
    r->sig_mode = info->rx_ctrl.sig_mode;
    r->len   = len;
    r->first_word_invalid = info->first_word_invalid;
    memcpy(r->buf, info->buf, len);

    __sync_synchronize();     /* publish the data BEFORE the index */
    head = next;
}
```

**Clamp `info->len` before the `memcpy`.** `CSI_BUF_MAX` is 384, not 256, and an
HT40 or STBC frame can report more than that. Copying an unclamped length into a
fixed buffer is an overflow, so an unexpected length must truncate instead. The
`len` field then always equals the number of values on the line, which is what
the PC parser checks against.

**The memory barrier is not optional.** The producer is on core 0 and the
consumer on core 1. Without it, the compiler or the store buffer can make the new
`head` visible before the payload, and the consumer reads a half-written record.
This is exactly the thing `xQueueSend` was hiding from us — using a raw ring
buffer means we own it. One line, and now we understand it.

`info->buf` is freed as soon as the callback returns, so the `memcpy` is
mandatory.

### Consumer - `app_main`

```c
while (1) {
    while (tail != head) {
        int n = format_csv_line(&ring[tail], line, sizeof(line));
        uart_write_bytes(UART_NUM_0, line, n);
        tail = (tail + 1) & (RING_SZ - 1);
    }
    vTaskDelay(1);            /* 1 ms at 1000 Hz tick */
}
```

Three rules that actually protect timing:

- **`uart_write_bytes()` with an installed driver and a >= 2 KB TX ring buffer.**
  Never `printf`. The default stdout path busy-waits on the FIFO and will stall
  the loop.
- **Hand-rolled int8-to-ASCII**, not `snprintf` per value. 128 values through
  `snprintf` is roughly 10x slower for no gain.
- **Main task pinned to core 1** (in `sdkconfig.defaults`), so CSV formatting
  never competes with the radio.

Ring cost: 32 x ~280 B is about 9 KB of static DRAM. Fine on 320 KB.

### Logging

Call `esp_log_level_set("*", ESP_LOG_NONE)` right after Wi-Fi start. Otherwise IDF
log lines land in the middle of the CSV stream on the same UART.

Belt and braces: the Pi-side logger drops any line that does not start with the
configured `node_id`. Cheap, and it also survives boot messages after a reset.

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
| Power save | `esp_wifi_set_ps(WIFI_PS_NONE)` | Mandatory on both boards. Otherwise CSI is intermittent and timestamps lose precision. |
| Bandwidth | HT20 (`WIFI_BW_HT20`) | Clean 256-byte layout, half the data. Default is HT40, must be set explicitly. |
| Channel | one fixed quiet channel (1, 6 or 11), not the home network's | Less interference and contention |
| TX power | **10 dBm**, via `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=10` | **Not the default.** IDF defaults to 20 dBm (max), whose radio power-up current spike brownout-resets these boards before `phy_init` finishes — see failure modes. 10 dBm is the Kconfig minimum and is ample for one room. Also avoids slamming the RX AGC at close range. |

## Data format on the wire

The ESP32 sends **raw signed int8 pairs**, not amplitudes. Amplitude is computed on
the PC so we keep the raw values and can change the rescaling method later.

Byte layout: each subcarrier is 2 bytes, **imaginary first, then real**.
Field order: LLTF, HT-LTF, STBC-HT-LTF.

Expected lengths, as the **driver** reports them (`info->len`, before any RX-side
truncation):

| `info->len` | Meaning |
|---|---|
| 256 | HT20. What we want. |
| 128 | Legacy/non-HT frame. **Rate fell back to 1 Mbps. Fix this.** |
| 384 / 612 | HT40 or STBC variants. Not expected here. |

**This table is about `info->len`, not the `len` column you actually see in the
CSV.** With `LLTF_ONLY` on (the current default — see "Serial output"), RX
truncates every record to 128 bytes before it ever reaches the wire, so the CSV's
`len` column reads **128 on every single line**, always, regardless of what
`info->len` actually was. The CSV `len` field can no longer distinguish "normal
LLTF-only truncation" from "rate genuinely fell back" — only `sig_mode` can:
`1` means HT (good), `0` means the rate actually fell back (bad). See the
bring-up checklist and failure modes below.

`first_word_invalid` is forwarded in the CSV and handled on the PC, not here.

## Subcarriers to keep

Keep the **52 valid LLTF subcarriers**. Drop guard bands, null subcarriers, and DC.
Ignore the HT-LTF block in V1 - 52 features are plenty for a 5-class problem.

Selection happens on the PC, so it can be retuned without reflashing.

## Serial output

- **Baud: 460800 to the Pi, RX only.** Not 921600 — that was the original
  choice and it **is** reliable from a PC (Milestone 1 ran extended CH340
  captures over it with zero unparsable lines), but the Raspberry Pi 2's Linux
  `ch341` driver drops bytes at 921600, specifically sometimes the line's own
  `\n`, at up to a ~60% rate in testing. A raw `stty`+`head` capture that
  bypasses this project's own code entirely reproduced the same loss, and
  removing other USB traffic (disconnecting Ethernet, which shares the Pi 2's
  internal USB hub with its onboard "Ethernet") made no difference — so it is
  not contention, and not our code. Dropping to 460800 cut the loss to ~10%.
  See the failure modes table. TX's own console baud is unaffected — TX has no
  CSV data path, its serial only carries IDF debug logs to a PC.
- **`LLTF_ONLY` is on, permanently, alongside the baud change.** Not because it
  fixed the corruption on its own (tested in isolation at 921600, it did not —
  loss got worse, not better) but because it halves an already-tight link and
  V1 uses nothing but LLTF on the PC anyway. A 128-value line runs ~450 B, so
  50 pps is ~22.5 KB/s of a ~46 KB/s link. Both changes shipped together as the
  tested combination, not attributed individually.
- **CSV, not binary.** Switch to binary only past ~100 pps.
- Include a **sequence counter** and the **local microsecond timestamp** in every
  record, even though the Pi timestamps on arrival. Gaps in the sequence tell us
  where packets are being lost.

Line emitted by the ESP32 (the Pi prepends its own timestamp):

```
node_id,seq,t_us,rssi,noise_floor,sig_mode,len,first_word_invalid,dropped,<raw int8 values...>
```

`len` is the number of int8 values in the last field, so a truncated or spliced
line is caught by counting.

**`dropped` is a column, not a separate status line.** A status line would start
with `node_id` too, so the Pi's prefix filter would let it through into
`csi.csv` and break the parser. One line shape, one parser.

`first_word_invalid` is forwarded here and handled on the PC, not on the ESP32.

## Configurable parameters

A small committed `config.h` per project: Wi-Fi channel, SSID and password,
packet rate, `node_id`, UART baud, and an LLTF-only vs full-CSI flag.

Plain `#define`s. No Kconfig, no menuconfig.

## Bring-up checklist

1. `. export.sh`, build and flash both boards on v5.5. Confirm the TX associates
   to the RX SoftAP.
2. Confirm `CONFIG_ESP_WIFI_CSI_ENABLED=y` in the generated `sdkconfig`.
3. Confirm CSI lines appear on the RX serial at 460800 (from a PC directly,
   921600 also works — see "Serial output" for why the Pi is different).
4. Check metadata: `sig_mode == 1`, bandwidth == 0. **`len` is always 128 now**
   (`LLTF_ONLY`, on by default) — that is expected, not a fault. If `sig_mode`
   reads `0`, the rate genuinely fell back; stop and fix that.
5. Check rate and loss: about 50 lines/s, no gaps in the sequence counter,
   `dropped` staying at 0, stable RSSI. On the Pi specifically, also expect a
   small (~10%) rate of malformed/merged lines that `pc/csi/parse.py` rejects
   cleanly — see the failure modes table. That is a known, accepted residual,
   not something to chase further.
6. Plot the 52 amplitudes with an empty room. Expect a smooth, structured curve.
7. Walk through each zone. The curve should deform clearly and repeatably.

## Go / no-go signal

**Go:** a steady ~50 lines/s of `sig_mode==1` records (via a PC directly, or
via the Pi with the expected ~10% clean-rejected residual — see failure modes)
whose amplitude curve is smooth and roughly stable in an empty room, and
visibly changes when you walk.

**No-go:** flat zeros, identical records every time, or `sig_mode == 0`.

## Failure modes

| Symptom | Cause | Fix |
|---|---|---|
| No CSI lines at all, everything else fine | `CONFIG_ESP_WIFI_CSI_ENABLED` renamed and silently ignored | check the generated `sdkconfig`; `rm sdkconfig && idf.py build` (fullclean alone will not re-read `sdkconfig.defaults`) |
| A new `sdkconfig.defaults` setting has no effect, build reports success | `sdkconfig` already existed, so `sdkconfig.defaults` was never consulted | `rm sdkconfig && idf.py build`, then `grep` the generated `sdkconfig` to confirm |
| `E BOD: Brownout detector was triggered`, repeating right after `phy_init` | Radio power-up current spike at the default 20 dBm TX power exceeds what the board's regulator can hold. Not fixable by changing USB supply or cable - the limit is on the board | `CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=10` and `CONFIG_ESP_PHY_REDUCE_TX_POWER=y` in `sdkconfig.defaults`. Must be Kconfig, not a runtime `esp_wifi_set_max_tx_power()` call - the brownout happens inside `esp_wifi_start()` |
| TX logs `reason=201` (`NO_AP_FOUND`) with `rssi=-128` forever | The RX SoftAP is not actually broadcasting - often because RX itself is brownout-resetting before it finishes `phy_init` | Scan for the SSID from a phone or PC (`netsh wlan show networks`). If absent, fix RX before looking at TX |
| `sig_mode == 0` | Rate fell back to 1 Mbps | Confirm the TX is actually associated |
| All-zero or constant CSI | TX not transmitting, or wrong channel | Check channel match and association |
| Some lines on the Pi merge two records into one (field count much higher than 11, e.g. 20, 29...) at ~10% rate | Raspberry Pi 2's Linux `ch341` USB-serial driver drops bytes at high baud — confirmed via a raw `stty`+`head` capture bypassing all of this project's code; not fixed by removing other USB traffic or by data volume alone | Already mitigated: 460800 baud (was 921600) + `LLTF_ONLY`. Residual ~10% is cleanly rejected by `pc/csi/parse.py`'s field-count/length check, same as any other malformed packet — not a data-quality problem, just a modest rate loss. From a PC directly (not the Pi), 921600 has never shown this. |
| Occasional garbled / half-written record | missing memory barrier in the callback | `__sync_synchronize()` before publishing `head` |
| `dropped` climbing | consumer too slow, or `printf` used instead of `uart_write_bytes` | fix the write path before enlarging the ring |
| Log text mixed into CSV | IDF logging left on | `esp_log_level_set("*", ESP_LOG_NONE)`, and filter by node_id on the Pi |
| Packet period drifting | `vTaskDelay` instead of `vTaskDelayUntil` on TX | use `vTaskDelayUntil` |
| Low packet rate, gaps in sequence | Serial saturation | Fewer subcarriers, or binary output |
| Low rate, no sequence gaps | Losses on the air | Check placement, distance, interference |
| Amplitude jumps with a still room | Automatic gain control stepping | Expected. Rescale using RSSI on the PC. |
| Build errors after pulling IDF | version drift | pin `release/v5.5`, record `git describe` |

## Deferred to V2

- Router as transmitter, both ESP32s receiving
- Promiscuous / sniffer mode
- Binary serial framing and rates above 50 pps
- HT-LTF subcarriers and any use of phase
- On-device filtering or inference
- External antennas
