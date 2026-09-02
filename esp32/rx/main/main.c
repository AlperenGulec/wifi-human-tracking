/* CSI receiver.
 *
 * Job: capture CSI, print CSV. Nothing else. No filtering, no maths — raw int8
 * pairs go out over the wire and the PC computes amplitude, so the rescaling
 * method can change without reflashing.
 *
 * Structure is a single task plus a lock-free ring buffer. The CSI callback is
 * the producer and runs inside the Wi-Fi task on core 0; app_main is the
 * consumer and is pinned to core 1 by sdkconfig.defaults. No second task, no
 * queue, no priorities to reason about.
 *
 * Line format (the Pi prepends its own arrival timestamp):
 *
 *   node_id,seq,t_us,rssi,noise_floor,sig_mode,len,first_word_invalid,dropped,<int8 values>
 *
 * See docs/ESP32_V1.md, "RX design".
 */

#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "config.h"

static const char *TAG = NODE_ID;

/* ------------------------------------------------------------------ */
/* Ring buffer                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t seq;
    uint32_t dropped;           /* ring overflows so far, snapshot at capture */
    int64_t  t_us;              /* ESP32 local clock, for gap diagnosis only */
    int8_t   rssi;
    int8_t   noise_floor;
    uint8_t  sig_mode;          /* 1 = HT (11n). 0 means the rate fell back. */
    uint8_t  first_word_invalid;
    uint16_t len;               /* bytes actually stored in buf */
    int8_t   buf[CSI_BUF_MAX];
} csi_record_t;

/* 32 x ~400 B is about 13 KB of static DRAM. Fine on 320 KB. */
static csi_record_t ring[RING_SZ];
static volatile uint32_t head = 0, tail = 0;
static volatile uint32_t dropped = 0;
static uint32_t seq = 0;

static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;

    uint32_t next = (head + 1) & (RING_SZ - 1);
    if (next == tail) {         /* full: drop this record, but count it */
        dropped++;
        return;
    }

    uint16_t len = info->len;
    if (len > CSI_BUF_MAX) {
        len = CSI_BUF_MAX;
    }
#if LLTF_ONLY
    if (len > LLTF_BYTES) {
        len = LLTF_BYTES;       /* the LLTF block is the first 128 bytes */
    }
#endif

    csi_record_t *r = &ring[head];
    r->seq                = ++seq;
    r->dropped            = dropped;
    r->t_us               = esp_timer_get_time();
    r->rssi               = info->rx_ctrl.rssi;
    r->noise_floor        = info->rx_ctrl.noise_floor;
    r->sig_mode           = info->rx_ctrl.sig_mode;
    r->first_word_invalid = info->first_word_invalid;
    r->len                = len;
    /* info->buf is freed as soon as this callback returns, so the copy is
     * mandatory, not an optimisation choice. */
    memcpy(r->buf, info->buf, len);

    /* Publish the payload BEFORE the index. The producer is on core 0 and the
     * consumer on core 1; without this the store buffer can make the new head
     * visible first and the consumer reads a half-written record. */
    __sync_synchronize();
    head = next;
}

/* ------------------------------------------------------------------ */
/* CSV formatting                                                      */
/* ------------------------------------------------------------------ */

/* Hand-rolled, not snprintf. Up to 384 values per line at 50 lines/s makes
 * snprintf-per-value roughly 10x slower for no gain. */

static inline char *put_u32(char *p, uint32_t v)
{
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    while (n) {
        *p++ = tmp[--n];
    }
    return p;
}

static inline char *put_i64(char *p, int64_t v)
{
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    } while (v);
    while (n) {
        *p++ = tmp[--n];
    }
    return p;
}

static inline char *put_i8(char *p, int8_t v)
{
    int x = v;                  /* int, so -128 negates without overflowing */
    if (x < 0) {
        *p++ = '-';
        x = -x;
    }
    if (x >= 100) {
        *p++ = (char)('0' + x / 100);
        *p++ = (char)('0' + (x / 10) % 10);
    } else if (x >= 10) {
        *p++ = (char)('0' + x / 10);
    }
    *p++ = (char)('0' + x % 10);
    return p;
}

/* Worst case: 9 header fields (~70 B) + CSI_BUF_MAX values of "-128 " (5 B).
 * Not named LINE_MAX: POSIX already defines that in <limits.h>. */
#define CSV_LINE_MAX (96 + CSI_BUF_MAX * 5)

static int format_line(const csi_record_t *r, char *out)
{
    char *p = out;

    memcpy(p, NODE_ID, sizeof(NODE_ID) - 1);
    p += sizeof(NODE_ID) - 1;

    *p++ = ','; p = put_u32(p, r->seq);
    *p++ = ','; p = put_i64(p, r->t_us);
    *p++ = ','; p = put_i8(p, r->rssi);
    *p++ = ','; p = put_i8(p, r->noise_floor);
    *p++ = ','; p = put_u32(p, r->sig_mode);
    *p++ = ','; p = put_u32(p, r->len);
    *p++ = ','; p = put_u32(p, r->first_word_invalid);
    *p++ = ','; p = put_u32(p, r->dropped);
    *p++ = ',';

    for (uint16_t i = 0; i < r->len; i++) {
        if (i) {
            *p++ = ' ';
        }
        p = put_i8(p, r->buf[i]);
    }
    *p++ = '\n';

    return (int)(p - out);
}

/* ------------------------------------------------------------------ */
/* Setup                                                               */
/* ------------------------------------------------------------------ */

static void uart_start(void)
{
    /* A real TX ring buffer, so uart_write_bytes queues and returns instead of
     * busy-waiting on the FIFO the way the default printf path does. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, UART_RX_BUF, UART_TX_BUF, 0, NULL, 0));

    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &cfg));
}

/* No station join/leave logging here, deliberately. Anything logged after
 * app_main goes out on the same UART as the CSV stream and corrupts whatever
 * line is mid-transmission. A diagnostic build with that logging was flashed
 * once during debugging and did exactly that - it showed up as records merging
 * with a t_us value landing in the seq column. If you need those events again,
 * flash such a build for PC-side monitoring only, never for a real capture. */
static void wifi_start_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.ap.ssid, WIFI_SSID, sizeof(cfg.ap.ssid));
    strncpy((char *)cfg.ap.password, WIFI_PASS, sizeof(cfg.ap.password));
    cfg.ap.ssid_len       = strlen(WIFI_SSID);
    cfg.ap.channel        = WIFI_CHANNEL;
    cfg.ap.max_connection = MAX_STA_CONN;
    cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Both mandatory. Power save makes CSI intermittent; HT40 changes the byte
     * layout the PC parser expects and doubles the data for nothing. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));
}

static void csi_start(void)
{
    wifi_csi_config_t cfg = {
        .lltf_en           = 1,
        .htltf_en          = 1,
        .stbc_htltf2_en    = 1,
        .ltf_merge_en      = 1,   /* averages LLTF and HT-LTF, less noise */
        .channel_filter_en = 0,   /* OFF: keep subcarriers independent for sensing */
        .manu_scale        = 0,   /* auto scale */
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

/* ------------------------------------------------------------------ */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    uart_start();
    wifi_start_ap();
    csi_start();

    ESP_LOGI(TAG, "AP \"%s\" up on channel %d, CSI on, going quiet", WIFI_SSID, WIFI_CHANNEL);

    /* From here the UART carries CSV only. Any IDF log line would land in the
     * middle of a record on the same wire. */
    esp_log_level_set("*", ESP_LOG_NONE);

    static char line[CSV_LINE_MAX];

    while (1) {
        while (tail != head) {
            /* Read the index before the payload, mirroring the producer. */
            __sync_synchronize();
            int n = format_line(&ring[tail], line);
            uart_write_bytes(UART_NUM_0, line, n);
            tail = (tail + 1) & (RING_SZ - 1);
        }
        vTaskDelay(1);          /* 1 ms at a 1000 Hz tick */
    }
}
