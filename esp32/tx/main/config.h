/* TX configuration. Plain defines, no Kconfig, no menuconfig.
 * The Wi-Fi block must match esp32/rx/main/config.h exactly. */
#pragma once

/* --- link to the RX SoftAP --- */
#define WIFI_SSID     "csi-rx"
#define WIFI_PASS     "csi-track-2026"

/* One fixed quiet channel (1, 6 or 11), not the home network's.
 * Setting it here skips a full scan, so the TX associates faster after a reset. */
#define WIFI_CHANNEL  6

/* --- UDP target --- */
/* 192.168.4.1 is the ESP32 SoftAP's own address. Nothing listens on the port;
 * we only care that the frame goes on the air. */
#define DEST_IP       "192.168.4.1"
#define DEST_PORT     5555
#define PAYLOAD_LEN   100

/* --- packet rate --- */
/* 40 ms -> 25 packets/s. Every packet gives the RX one CSI record.
 *
 * Was 20 ms / 50 pps. Halved because the Pi 2's ch341 USB-serial link cannot
 * carry the resulting byte rate intact: at 460800 baud every single line
 * arrived with 5-19 of its 128 values missing (0 of 1174 lines usable), and
 * the loss was steady rather than bursty, which points at the bridge not
 * sustaining the baud rather than a buffer overflow. Dropping to 25 pps lets
 * the link run at 230400 baud, where 25 pps x ~500 B = ~12.5 KB/s against
 * ~23 KB/s of capacity.
 *
 * 25 pps is still ample for the model: a 2.0 s window (docs/CSI_PROCESSING_V1.md)
 * holds 50 samples, plenty for per-subcarrier mean and std. */
#define TX_PERIOD_MS  40

/* Identifies this board in the boot log. The TX writes no CSV. */
#define NODE_ID       "TX1"
