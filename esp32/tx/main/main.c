/* CSI transmitter.
 *
 * Job: put packets in the air at a steady rate. Nothing else.
 *
 * Associates to the RX SoftAP as a station and sends a small UDP datagram every
 * TX_PERIOD_MS. Being associated is what matters: an unassociated ESP32 sends at
 * 1 Mbps DSSS, which carries no training fields and produces no usable CSI.
 * Association forces an OFDM/HT rate on its own, so no rate hacks are needed.
 *
 * See docs/ESP32_V1.md, "TX design".
 */

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "config.h"

static const char *TAG = NODE_ID;

#define GOT_IP_BIT BIT0
static EventGroupHandle_t s_events;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        /* The RX may not be powered yet, or may have been reset. Retry forever.
         * No delay here: this runs on the system event task and must not block.
         * The scan inside esp_wifi_connect() paces the retries on its own. */
        xEventGroupClearBits(s_events, GOT_IP_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

static void wifi_start_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, WIFI_PASS, sizeof(cfg.sta.password));
    cfg.sta.channel = WIFI_CHANNEL;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Both mandatory. Power save makes CSI intermittent and blurs timestamps;
     * HT40 changes the CSI byte layout we parse on the PC. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
}

static int udp_socket_open(struct sockaddr_in *dest)
{
    memset(dest, 0, sizeof(*dest));
    dest->sin_family = AF_INET;
    dest->sin_port = htons(DEST_PORT);
    dest->sin_addr.s_addr = inet_addr(DEST_IP);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed, errno %d", errno);
    }
    return sock;
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_events = xEventGroupCreate();
    wifi_start_sta();

    ESP_LOGI(TAG, "waiting to associate with \"%s\" on channel %d", WIFI_SSID, WIFI_CHANNEL);
    xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "associated, sending %d B every %d ms", PAYLOAD_LEN, TX_PERIOD_MS);

    struct sockaddr_in dest;
    int sock = udp_socket_open(&dest);
    ESP_ERROR_CHECK(sock < 0 ? ESP_FAIL : ESP_OK);

    static uint8_t payload[PAYLOAD_LEN];
    memset(payload, 0xA5, sizeof(payload));

    uint32_t sent = 0;
    uint32_t failed = 0;
    TickType_t last = xTaskGetTickCount();

    while (1) {
        /* A counter in the first four bytes, so a capture on the air can be
         * matched against the RX sequence numbers if we ever need to. */
        payload[0] = (uint8_t)(sent);
        payload[1] = (uint8_t)(sent >> 8);
        payload[2] = (uint8_t)(sent >> 16);
        payload[3] = (uint8_t)(sent >> 24);

        if (sendto(sock, payload, sizeof(payload), 0,
                   (struct sockaddr *)&dest, sizeof(dest)) < 0) {
            /* Link dropped. Wait for the reconnect, then carry on. */
            if (++failed % 50 == 1) {
                ESP_LOGW(TAG, "sendto failed, errno %d (%" PRIu32 " total)", errno, failed);
            }
            xEventGroupWaitBits(s_events, GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
            last = xTaskGetTickCount();
        }
        sent++;

        /* vTaskDelayUntil, not vTaskDelay: vTaskDelay counts from after sendto()
         * returns, so the period drifts with send latency. This holds 50 pps. */
        vTaskDelayUntil(&last, pdMS_TO_TICKS(TX_PERIOD_MS));
    }
}
