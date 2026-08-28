# ESP32 Firmware

Written from scratch. No fork, no third-party CSI project.

```
esp32/
├── tx/    STA - associates to the RX SoftAP, sends UDP at ~50 pps  (~120 lines)
└── rx/    SoftAP - captures CSI, prints CSV over serial            (~200 lines)
```

## Toolchain

ESP-IDF `release/v5.5`, used as a **command-line SDK only**. No IDE, no Arduino
core, no PlatformIO, no menuconfig.

IDF itself is unavoidable: the CSI API (`esp_wifi_set_csi_rx_cb`) lives inside
Espressif's closed-source Wi-Fi driver, and that driver drags in FreeRTOS, NVS,
esp_event and LwIP. Everything above it is ours.

```bash
. ~/esp/esp-idf/export.sh
cd rx && idf.py set-target esp32 && idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Config lives in a committed `sdkconfig.defaults` plus a small `config.h` per
project, so a fresh clone builds identically without any interactive step.
`sdkconfig` is generated and gitignored — run `idf.py fullclean` after editing
`sdkconfig.defaults`.

See [../docs/ESP32_V1.md](../docs/ESP32_V1.md) for the full design, the ring
buffer and memory barrier, CSI and RF settings, bring-up steps, and failure modes.
