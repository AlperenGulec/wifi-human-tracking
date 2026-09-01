/* RX configuration. Plain defines, no Kconfig, no menuconfig.
 * The Wi-Fi block must match esp32/tx/main/config.h exactly. */
#pragma once

/* --- SoftAP the TX associates to --- */
#define WIFI_SSID      "csi-rx"
#define WIFI_PASS      "csi-track-2026"   /* >= 8 chars, WPA2 requires it */
#define WIFI_CHANNEL   6                  /* one fixed quiet channel: 1, 6 or 11 */
#define MAX_STA_CONN   1                  /* only the TX may join */

/* --- serial output --- */
/* Prefixes every CSV line. The Pi logger drops anything not starting with it,
 * which also throws away boot messages after an ESP32 reset. */
#define NODE_ID        "RX1"

/* 921600 is reliable on both CP2102 and CH340. Higher rates are flaky.
 * Keep this equal to CONFIG_ESP_CONSOLE_UART_BAUDRATE in sdkconfig.defaults. */
#define UART_BAUD      921600
#define UART_TX_BUF    4096   /* >= 2 KB, so uart_write_bytes does not busy-wait */
#define UART_RX_BUF    256    /* unused, but the driver requires > 128 */

/* --- CSI capture --- */
/* Power of two, so & works instead of %. 32 slots hold ~0.6 s at 50 pps,
 * far more headroom than the consumer needs. */
#define RING_SZ        32

/* The driver reports 256 for HT20, which is what we expect, but can return 384
 * or 612 for HT40 / STBC frames. The callback clamps to this before memcpy, so
 * an unexpected length truncates instead of overflowing the record. */
#define CSI_BUF_MAX    384

/* Emit only the first 128 bytes (the 64-value LLTF block) instead of all 256.
 * Halves the serial load; V1 only ever uses LLTF on the PC anyway.
 *
 * Keep this 0 during bring-up. With it on, `len` reads 128 and looks exactly
 * like the 1 Mbps rate-fallback failure signature. Tell them apart with
 * sig_mode: 1 means HT (good), 0 means the rate fell back (bad). */
#define LLTF_ONLY      0
#define LLTF_BYTES     128
