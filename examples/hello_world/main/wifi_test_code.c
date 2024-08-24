/* Wi-Fi CSI console Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_console.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

//
#include "freertos/semphr.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_console.h"

#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "driver/gpio.h"
#include "driver/rmt.h"
#include "hal/uart_ll.h"
// #include "led_strip.h"

#include "esp_radar.h"
// #include "csi_commands.h"

#include "esp_ota_ops.h"
#include "esp_netif.h"
#include "esp_chip_info.h"

#include "mbedtls/base64.h"
#include "mqtt.h"
#include "wifi_test_code.h"
// #include "wps.h"
#include <esp_event.h>
#include "esp_wifi_types.h"
// #include "ota.h"
#include "config.h"
// #include "wps.h"
// #include "main_functions.h"
#include "wifi_test_code.h"
esp_err_t ret;

esp_err_t ret;

static int rssi;
TaskHandle_t prediction_task_handle;

#define DEFAULT_SCAN_LIST_SIZE 20
volatile char wifi_ssids[1024] = "";

#define RECV_ESPNOW_CSI
#define CONFIG_LESS_INTERFERENCE_CHANNEL 11
#define RADER_EVALUATE_SERVER_PORT 3232

// static led_strip_t *g_strip_handle = NULL;
static xQueueHandle g_csi_info_queue = NULL;
// static bool s_reconnect = true;

int mutexPredict = 0;
int msg_id;
float Amp[50][55];
// char tmpString[700];
int cmpt_pred;
int k = 0;
static const char *TAG = "app_main";
csiStruct csi;

// circular  buffer

CircularBuffer csiBuffer;

void init_circular_buffer()
{
    csiBuffer.head = 0;
    csiBuffer.tail = 0;
    csiBuffer.count = 0;
    csiBuffer.mutex = xSemaphoreCreateMutex();
    if (csiBuffer.mutex == NULL)
    {
        ESP_LOGE("TAG", "Failed to create mutex");
    }
}

void wifi_init(void)
{
    ESP_LOGI(TAG, "  wifi init ");

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        printf("eraaase flash in wifi int \n");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
#ifdef RECV_ESPNOW_CSI

#endif
}
static TimerHandle_t g_collect_timer_handele = NULL;

void csi_data_print_task(void *arg)
{
    wifi_csi_filtered_info_t *info = NULL;
    char *buffer = malloc(2 * 1024);
    static uint32_t count = 0;

    while (xQueueReceive(g_csi_info_queue, &info, portMAX_DELAY))
    {
        size_t len = 0;
        wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

        if (!count)
        {
            ESP_LOGI(TAG, "================ CSI RECV1 ================");
            len += sprintf(buffer + len, "type,sequence,timestamp,taget_seq,taget,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
        }

        if (!strcasecmp(g_console_input_config.csi_output_type, "LLFT"))
        {
            info->valid_len = info->valid_llft_len;
        }
        else if (!strcasecmp(g_console_input_config.csi_output_type, "HT-LFT"))
        {
            info->valid_len = info->valid_llft_len + info->valid_ht_lft_len;
        }
        else if (!strcasecmp(g_console_input_config.csi_output_type, "STBC-HT-LTF"))
        {
            info->valid_len = info->valid_llft_len + info->valid_ht_lft_len + info->valid_stbc_ht_lft_len;
        }
        info->valid_len = 112;
        len += sprintf(buffer + len, "CSI_DATA,%d,%u,%u,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,",
                       count++, esp_log_timestamp(), g_console_input_config.collect_number, g_console_input_config.collect_taget,
                       MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
                       rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
                       rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
                       rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
                       rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state, info->valid_len, 0);

        if (!strcasecmp(g_console_input_config.csi_output_format, "base64"))
        {
            size_t size = 0;
            mbedtls_base64_encode((uint8_t *)buffer + len, sizeof(buffer) - len, &size, (uint8_t *)info->valid_data, info->valid_len);
            len += size;
            len += sprintf(buffer + len, "\n");
        }
        else
        {
            len += sprintf(buffer + len, "\"[%d", info->valid_data[0]);

            for (int i = 1; i < info->valid_len; i++)
            {
                len += sprintf(buffer + len, ",%d", info->valid_data[i]);
            }

            len += sprintf(buffer + len, "]\"\n");
        }
        uint8_t count_amp = 0;
        for (int i = 56; i < info->valid_len; i += 2)
        {

            {
                Amp[k][count_amp] = sqrt(info->valid_data[i] * info->valid_data[i] + info->valid_data[i + 1] * info->valid_data[i + 1]);
                count_amp++;
            }
        }
        for (int i = 2; i < 56; i += 2)
        {

            {
                Amp[k][count_amp] = sqrt(info->valid_data[i] * info->valid_data[i] + info->valid_data[i + 1] * info->valid_data[i + 1]);
                count_amp++;
            }
        }

        k++;

        if (k > 50)
        {
            if (xSemaphoreTake(csiBuffer.mutex, portMAX_DELAY))
            {
                printf("Amp was added %f", Amp[0][54]);
                // Copy data to buffer
                memcpy(csiBuffer.buffer[csiBuffer.head], Amp, sizeof(Amp));

                if (csiBuffer.count < BUFFER_SIZE)
                {
                    csiBuffer.count++;
                }
                else
                {
                    // Buffer is full, update tail
                    csiBuffer.tail = (csiBuffer.tail + 1) % BUFFER_SIZE;
                }
                csiBuffer.head = (csiBuffer.head + 1) % BUFFER_SIZE;
                printf("head was added ");
                xSemaphoreGive(csiBuffer.mutex);
                k = 0;
                // Notify the consumer task
                xTaskNotifyGive(prediction_task_handle);
            }
        }

        // printf("%s", buffer);
        // sprintf(csi.csi_data, buffer);

        free(info);
    }

    free(buffer);
    vTaskDelete(NULL);
}
void wifi_csi_raw_cb(const wifi_csi_filtered_info_t *info, void *ctx)
{
    // Allocate memory for q_data with a size large enough to hold the structure and valid data
    wifi_csi_filtered_info_t *q_data = malloc(sizeof(wifi_csi_filtered_info_t) + info->valid_len);
    if (q_data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for CSI data");
        return;
    }

    // Copy the CSI information into the newly allocated memory
    *q_data = *info;
    memcpy(q_data->valid_data, info->valid_data, info->valid_len);

    // Send the data to the queue, but wait for a small timeout if the queue is full
    if (!g_csi_info_queue || xQueueSend(g_csi_info_queue, &q_data, pdMS_TO_TICKS(10)) == pdFALSE)
    {
        ESP_LOGW(TAG, "g_csi_info_queue full, freeing memory");
        free(q_data);
    }
}

int wifi_initialize(void)
{
    uint16_t ap_count = 0;

    ESP_LOGD(TAG, "Initializing WiFi station");

    esp_err_t ret = esp_netif_init();
    ret = esp_event_loop_create_default();
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    strcpy(wifi_ssids, "");
    ret = esp_wifi_set_mode(WIFI_MODE_STA);

    // ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    // ret = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    ret = esp_wifi_start();
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));

#ifdef RECV_ESPNOW_CSI

#endif

    return 0;
}

void radar_config()
{
    printf("radar config \n");
    wifi_radar_config_t radar_config = {
        // .wifi_radar_cb = wifi_radar_cb,
        .wifi_csi_filtered_cb = wifi_csi_raw_cb,
        .filter_mac = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4}, //{0x40, 0x4C, 0xCA, 0x88, 0xB6, 0x18},

    };
    esp_radar_init();
    esp_radar_set_config(&radar_config);
    esp_radar_start();
    g_csi_info_queue = xQueueCreate(50, sizeof(void *));
    xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 3 * 1024, NULL, 2, NULL, 0);
}

//***************************MAIN **********************************//
void App_main_wifi(void)
{

    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        printf("Erase flash in main \n");
        ret = nvs_flash_init();
    }

    // LED_setDuty(&prise.leds[0], 10, 5);
    // LED_setOffset(&prise.leds[0], 5);
    // LED_setDuty(&prise.leds[1], 10, 5);
    // LED_setOffset(&prise.leds[1], 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    wifi_initialize();
    // PRISE_Init(&prise);

    ESP_LOGI(TAG, " VERSION : %s", PRISE_VESION);
    ESP_LOGI(TAG, " houssem modifications\n");
    ESP_LOGI(TAG, " \n");

    // esp_err_t errr = nvs_open("storage", NVS_READWRITE, &nvs);

    wifi_config_t wifi_config;
    esp_err_t err = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
    ESP_LOGI(TAG, "Wifi configuration %d", err);
    if (strlen((char *)wifi_config.sta.ssid) == 0 || strlen((char *)wifi_config.sta.password) == 0)
    {
        ESP_LOGI(TAG, "Wifi configuration not found in flash partition called NVS");
        // start_wps();
    }
    else
    {
        ESP_LOGI(TAG, "Wifi configuration already stored in flash partition called NVS");
        // LED_setDuty(&prise.leds[0], 10, 0);
        // LED_setDuty(&prise.leds[1], 10, 5);

        ESP_LOGI(TAG, "ssid :%s", wifi_config.sta.ssid);
        size_t ssidLen = strlen((char *)wifi_config.sta.ssid);
        // ESP_LOGI(TAG, "ssid_len :%d", ssidLen);

        ESP_LOGI(TAG, "pass:%s", wifi_config.sta.password);
        size_t passLen = strlen((char *)wifi_config.sta.password);
        // ESP_LOGI(TAG, "pass :%d", passLen);

        esp_wifi_connect();
    }
    init_circular_buffer();
    radar_config();
    // setup();
}
