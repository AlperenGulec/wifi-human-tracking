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
/* 20 ms -> 50 packets/s. Every packet gives the RX one CSI record. */
#define TX_PERIOD_MS  20

/* Identifies this board in the boot log. The TX writes no CSV. */
#define NODE_ID       "TX1"
