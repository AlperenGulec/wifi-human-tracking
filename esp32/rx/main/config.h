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

/* 230400. This has come down twice, both times driven by real measurements on
 * the Pi 2's ch341 USB-serial link (921600 is fine from a PC - Milestone 1 ran
 * extended CH340 captures with zero unparsable lines - so this is specific to
 * that bridge on that host):
 *
 *   921600 - severe corruption, ~41-60% of lines losing their terminating
 *            '\n' and merging. Confirmed via a raw `stty`+`head` capture
 *            bypassing csi-logger.c entirely, so not our code. Unaffected by
 *            disconnecting Ethernet (not USB contention) and made *worse*, not
 *            better, by halving the line size with LLTF_ONLY.
 *   460800 - looked like "~10% malformed" by line count, but measuring the
 *            payload properly showed EVERY line short: 0 of 1174 carried its
 *            declared 128 values, each missing 5-19 of them. Unusable. The
 *            loss was steady rather than bursty, and identical with the camera
 *            stopped, so it is the bridge not sustaining the baud - not CPU
 *            contention and not a buffer overflow.
 *   230400 - current. Requires TX_PERIOD_MS=40 (25 pps) so the byte rate fits:
 *            ~12.5 KB/s against ~23 KB/s of capacity.
 *
 * Pi-side must match: csi-logger -b 230400 (the default, as of this change).
 * See docs/ESP32_V1.md's failure modes table. */
#define UART_BAUD      230400
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
 * Halves the serial load; V1 only ever uses LLTF on the PC anyway. Kept on
 * permanently alongside the 460800 baud change above - both together are the
 * tested, working configuration (~10% residual dropped-line rate, cleanly
 * rejected by pc/csi/parse.py like any other malformed packet, not shipped
 * as corrupt data). Data volume alone did not fix the corruption (tested at
 * 921600: made the loss rate worse, not better) - this is kept for the
 * bandwidth headroom it genuinely gives, not as the fix.
 *
 * IMPORTANT: with this on, `len` reads 128 for EVERY packet. Before this
 * change, len==128 meant "rate fell back to 1 Mbps, stop and fix it" - see
 * docs/ESP32_V1.md. That check is no longer len==128; check sig_mode instead:
 * 1 means HT (good, and expected here now), 0 means the rate actually fell
 * back (bad, unrelated to this flag). */
#define LLTF_ONLY      1
#define LLTF_BYTES     128
