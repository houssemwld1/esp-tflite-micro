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
#include "wps.h"
// #include "main_functions.h"
#include "wifi_test_code.h"
esp_err_t ret;

esp_err_t ret;

static int rssi;
TaskHandle_t myTaskHandle = NULL;
nvs_handle_t nvs;
#define DEFAULT_SCAN_LIST_SIZE 20
volatile char wifi_ssids[1024] = "";
static EventGroupHandle_t s_wifi_event_group;
#define RECV_ESPNOW_CSI
#define CONFIG_LESS_INTERFERENCE_CHANNEL 11
#define RADER_EVALUATE_SERVER_PORT 3232
// static led_strip_t *g_strip_handle = NULL;
static xQueueHandle g_csi_info_queue = NULL;
static bool s_reconnect = true;

static const char *TAG = "app_main";
csiStruct csi;

int test;
int cpt = 0;

int calib_test;
char *args_init[] = {"radar", "--csi_output_type", "LLFT", "--csi_output_format", "base64", NULL};
char *args_stop[] = {"radar", "--train_stop", NULL};
char *args_start[] = {"radar", "--train_start", NULL};
char *args_someone_threshold[] = {"radar", "--predict_someone_threshold", "0.001", NULL};
char *args_move_threshold[] = {"radar", "--predict_move_threshold", "0.001", NULL};
char *predict_buff_size[] = {"radar", "--predict_buff_size", "5", NULL};
char *predict_outliers_number[] = {"radar", "--predict_outliers_number", "2", NULL};
static size_t before_free_32bit;
static size_t free_32bit_in_clb;

static size_t after_free_32bit;

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
    char *buffer = malloc(8 * 1024);
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

        /* sprintf(csi.csi_data, "CSI_DATA,%d,%u,%u,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,",
                 count++, esp_log_timestamp(), g_console_input_config.collect_number, g_console_input_config.collect_taget,
                 MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
                 rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
                 rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
                 rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
                 rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state, info->valid_len, 0);*/
        // ESP_LOGI(TAG, "csi_output_format: %s", g_console_input_config.csi_output_format);

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
        printf("%s", buffer);
        sprintf(csi.csi_data, buffer);

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

    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    ret = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
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
    g_csi_info_queue = xQueueCreate(64, sizeof(void *));
    xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 7 * 1024, NULL, 7, NULL, 0);
}

//***************************MAIN **********************************//
void App_main_wifi(void)
{

    // led_init();

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    repl_config.prompt = "csi>";
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
    /**< Fix serial port garbled code due to high baud rate */
    uart_ll_set_sclk(UART_LL_GET_HW(CONFIG_ESP_CONSOLE_UART_NUM), UART_SCLK_APB);
    uart_ll_set_baudrate(UART_LL_GET_HW(CONFIG_ESP_CONSOLE_UART_NUM), CONFIG_ESP_CONSOLE_UART_BAUDRATE);
#endif

    // cmd_register_system();
    // cmd_register_ping();
    // cmd_register_wifi_config();
    // cmd_register_wifi_scan();
    // cmd_register_radar();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

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

    esp_err_t errr = nvs_open("storage", NVS_READWRITE, &nvs);
    // nvs_get_u8(nvs, "mac_parse0", &prise.mac_parse[0]);
    // nvs_get_u8(nvs, "mac_parse1", &prise.mac_parse[1]);
    // nvs_get_u8(nvs, "mac_parse2", &prise.mac_parse[2]);
    // nvs_get_u8(nvs, "mac_parse3", &prise.mac_parse[3]);
    // nvs_get_u8(nvs, "mac_parse4", &prise.mac_parse[4]);
    // nvs_get_u8(nvs, "mac_parse5", &prise.mac_parse[5]);
    // printf(" MAC adress IN NVS:: %2X %2X %2X %2X %2X %2X \n", prise.mac_parse[0], prise.mac_parse[1], prise.mac_parse[2], prise.mac_parse[3], prise.mac_parse[4], prise.mac_parse[5]);
    // /* if adress mac different to 0*/
    // if (prise.mac_parse[0] || prise.mac_parse[1] || prise.mac_parse[2] || prise.mac_parse[3] || prise.mac_parse[4] || prise.mac_parse[5])
    // {
    //     test = 1;
    //     printf("test initial true  %d \n", test);
    // }
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
    radar_config();
    // setup();
}

// /* Wi-Fi CSI console Example

//    This example code is in the Public Domain (or CC0 licensed, at your option.)

//    Unless required by applicable law or agreed to in writing, this
//    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
//    CONDITIONS OF ANY KIND, either express or implied.
// */
// #include <errno.h>
// #include <string.h>
// #include <stdio.h>
// #include <string.h>
// #include "nvs_flash.h"
// #include "argtable3/argtable3.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/event_groups.h"
// #include "esp_event.h"
// #include "esp_log.h"
// #include "esp_err.h"
// #include "esp_wifi.h"
// #include "esp_console.h"

// #include "lwip/inet.h"
// #include "lwip/netdb.h"
// #include "lwip/sockets.h"

// #define WIFI_CONNECTED_BIT BIT0
// #define WIFI_DISCONNECTED_BIT BIT1
// #define QUEUE_SIZE 25
// static bool s_reconnect = true;

// static EventGroupHandle_t s_wifi_event_group;
// //
// #include <errno.h>
// #include <string.h>
// #include <stdio.h>

// #include "freertos/FreeRTOS.h"
// #include "freertos/event_groups.h"
// #include "esp_event.h"
// #include "esp_log.h"
// #include "nvs_flash.h"
// #include "esp_err.h"
// #include "esp_console.h"

// #include "esp_wifi.h"
// #include "lwip/inet.h"
// #include "lwip/netdb.h"
// #include "lwip/sockets.h"

// #include "driver/gpio.h"
// #include "driver/rmt.h"
// #include "hal/uart_ll.h"
// // #include "led_strip.h"

// #include "esp_radar.h"
// // #include "csi_commands.h"

// #include "esp_ota_ops.h"
// #include "esp_netif.h"
// #include "esp_chip_info.h"

// #include "mbedtls/base64.h"
// #include "mqtt.h"
// #include "wifi_test_code.h"
// // #include "wps.h"
// #include <esp_event.h>
// #include "esp_wifi_types.h"
// // #include "ota.h"
// #include "config.h"
// // #include "main_functions.h"
// #include "wifi_test_code.h"
// esp_err_t ret;

// static int rssi;
// TaskHandle_t myTaskHandle = NULL;
// nvs_handle_t nvs;
// #define DEFAULT_SCAN_LIST_SIZE 20
// volatile char wifi_ssids[1024] = "";

// #define RECV_ESPNOW_CSI
// #define CONFIG_LESS_INTERFERENCE_CHANNEL 11
// #define RADER_EVALUATE_SERVER_PORT 3232
// // static led_strip_t *g_strip_handle = NULL;
// static xQueueHandle g_csi_info_queue = NULL;

// static const char *TAG = "app_main";
// csiStruct csi;
// // priseStruct prise;
// int test;
// int cpt = 0;

// int calib_test;
// char *args_init[] = {"radar", "--csi_output_type", "LLFT", "--csi_output_format", "base64", NULL};
// char *args_stop[] = {"radar", "--train_stop", NULL};
// char *args_start[] = {"radar", "--train_start", NULL};
// char *args_someone_threshold[] = {"radar", "--predict_someone_threshold", "0.001", NULL};
// char *args_move_threshold[] = {"radar", "--predict_move_threshold", "0.001", NULL};
// char *predict_buff_size[] = {"radar", "--predict_buff_size", "5", NULL};
// char *predict_outliers_number[] = {"radar", "--predict_outliers_number", "2", NULL};
// static size_t before_free_32bit;
// static size_t free_32bit_in_clb;

// static size_t after_free_32bit;

// void wifi_init(void)
// {
//     ESP_LOGI(TAG, "  wifi init ");

//     esp_err_t ret = nvs_flash_init();

//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
//     {
//         printf("eraaase flash in wifi int \n");
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }

//     ESP_ERROR_CHECK(ret);

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//     ESP_ERROR_CHECK(esp_event_loop_create_default());

//     ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
//     ESP_ERROR_CHECK(esp_wifi_start());
//     ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
//     ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
// #ifdef RECV_ESPNOW_CSI

// #endif
// }

// static TimerHandle_t g_collect_timer_handele = NULL;

// void csi_data_print_task(void *arg)
// {
//     wifi_csi_filtered_info_t *info = NULL;
//     char *buffer = malloc(5 * 1024);
//     static uint32_t count = 0;

//     while (xQueueReceive(g_csi_info_queue, &info, portMAX_DELAY))
//     {
//         size_t len = 0;
//         wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

//         if (!count)
//         {
//             ESP_LOGI(TAG, "================ CSI RECV1 ================");
//             len += sprintf(buffer + len, "type,sequence,timestamp,taget_seq,taget,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
//         }

//         if (!strcasecmp(g_console_input_config.csi_output_type, "LLFT"))
//         {
//             info->valid_len = info->valid_llft_len;
//         }
//         else if (!strcasecmp(g_console_input_config.csi_output_type, "HT-LFT"))
//         {
//             info->valid_len = info->valid_llft_len + info->valid_ht_lft_len;
//         }
//         else if (!strcasecmp(g_console_input_config.csi_output_type, "STBC-HT-LTF"))
//         {
//             info->valid_len = info->valid_llft_len + info->valid_ht_lft_len + info->valid_stbc_ht_lft_len;
//         }
//         info->valid_len = 112;
//         len += sprintf(buffer + len, "CSI_DATA,%d,%u,%u,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,",
//                        count++, esp_log_timestamp(), g_console_input_config.collect_number, g_console_input_config.collect_taget,
//                        MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
//                        rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
//                        rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
//                        rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
//                        rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state, info->valid_len, 0);

//         /* sprintf(csi.csi_data, "CSI_DATA,%d,%u,%u,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,",
//                  count++, esp_log_timestamp(), g_console_input_config.collect_number, g_console_input_config.collect_taget,
//                  MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
//                  rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
//                  rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
//                  rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
//                  rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state, info->valid_len, 0);*/
//         // ESP_LOGI(TAG, "csi_output_format: %s", g_console_input_config.csi_output_format);

//         if (!strcasecmp(g_console_input_config.csi_output_format, "base64"))
//         {
//             size_t size = 0;
//             mbedtls_base64_encode((uint8_t *)buffer + len, sizeof(buffer) - len, &size, (uint8_t *)info->valid_data, info->valid_len);
//             len += size;
//             len += sprintf(buffer + len, "\n");
//         }
//         else
//         {
//             len += sprintf(buffer + len, "\"[%d", info->valid_data[0]);

//             for (int i = 1; i < info->valid_len; i++)
//             {
//                 len += sprintf(buffer + len, ",%d", info->valid_data[i]);
//             }

//             len += sprintf(buffer + len, "]\"\n");
//         }
//         printf("%s", buffer);
//         // sprintf(csi.csi_data, buffer);

//         free(info);
//     }

//     free(buffer);
//     vTaskDelete(NULL);
// }

// void wifi_csi_raw_cb(const wifi_csi_filtered_info_t *info, void *ctx)
// {
//     wifi_csi_filtered_info_t *q_data = malloc(sizeof(wifi_csi_filtered_info_t) + info->valid_len);
//     *q_data = *info;
//     memcpy(q_data->valid_data, info->valid_data, info->valid_len);

//     if (!g_csi_info_queue || xQueueSend(g_csi_info_queue, &q_data, 0) == pdFALSE)
//     {
//         ESP_LOGW(TAG, "g_csi_info_queue full");
//         free(q_data);
//     }
// }

// static void wifi_event_handler(void *arg, esp_event_base_t event_base,
//                                int32_t event_id, void *event_data)
// {
//     if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
//     {
//         xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
//         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
//     {
//         if (s_reconnect)
//         {
//             ESP_LOGI(TAG, "sta disconnect, s_reconnect...");
//             esp_wifi_connect();
//         }
//         else
//         {
//             // ESP_LOGI(TAG, "sta disconnect");
//         }

//         xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//         xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
//     {
//         wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
//         ESP_LOGI(TAG, "Connected to %s (bssid: " MACSTR ", channel: %d)", event->ssid,
//                  MAC2STR(event->bssid), event->channel);
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
//     {
//         ESP_LOGI(TAG, "STA Connecting to the AP again...");
//     }
// }
// int wifi_initialize(void)
// {
//     uint16_t ap_count = 0;

//     ESP_LOGD(TAG, "Initializing WiFi station");

//     esp_err_t ret = esp_netif_init();
//     ret = esp_event_loop_create_default();
//     esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
//     assert(sta_netif);
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ret = esp_wifi_init(&cfg);
//     strcpy(wifi_ssids, "");
//     ret = esp_wifi_set_mode(WIFI_MODE_STA);

//     ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
//     ret = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
//     ret = esp_wifi_start();
//     ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
//     ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));

// #ifdef RECV_ESPNOW_CSI

// #endif

//     return 0;
// }

// // void str2mac(priseStruct *prise)
// // {
// //     sscanf(prise->mac_address, "%2hhX%2hhX%2hhX%2hhX%2hhX%2hhX", &prise->mac_parse[0], &prise->mac_parse[1], &prise->mac_parse[2], &prise->mac_parse[3], &prise->mac_parse[4], &prise->mac_parse[5]);
// //     printf(" MAC adress SET %2hhX%2hhX%2hhX%2hhX%2hhX%2hhX \n", prise->mac_parse[0], prise->mac_parse[1], prise->mac_parse[2], prise->mac_parse[3], prise->mac_parse[4], prise->mac_parse[5]);
// //     nvs_set_u8(nvs, "mac_parse0", prise->mac_parse[0]);
// //     nvs_set_u8(nvs, "mac_parse1", prise->mac_parse[1]);
// //     nvs_set_u8(nvs, "mac_parse2", prise->mac_parse[2]);
// //     nvs_set_u8(nvs, "mac_parse3", prise->mac_parse[3]);
// //     nvs_set_u8(nvs, "mac_parse4", prise->mac_parse[4]);
// //     nvs_set_u8(nvs, "mac_parse5", prise->mac_parse[5]);
// // }

// void radar_config()
// {
//     printf("radar config \n");
//     wifi_radar_config_t radar_config = {
//         // .wifi_radar_cb = wifi_radar_cb,
//         .wifi_csi_filtered_cb = wifi_csi_raw_cb,
//         .filter_mac = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4}, //{0x40, 0x4C, 0xCA, 0x88, 0xB6, 0x18},

//     };

//     esp_radar_init();
//     esp_radar_set_config(&radar_config);
//     esp_radar_start();
//     g_csi_info_queue = xQueueCreate(QUEUE_SIZE, sizeof(wifi_csi_filtered_info_t *));
//     // create a task pinned to core
//     xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 7 * 1024, NULL, 7, NULL, 1);
//     // xTaskCreate(csi_data_print_task, "csi_data_print", 7 * 1024, NULL, 0, NULL);
// }

// //***************************MAIN **********************************//
// void App_main_wifi()
// {

//     // led_init();

//     esp_console_repl_t *repl = NULL;
//     esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
//     esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
//     repl_config.prompt = "csi>";
//     ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

// #if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
//     /**< Fix serial port garbled code due to high baud rate */
//     uart_ll_set_sclk(UART_LL_GET_HW(CONFIG_ESP_CONSOLE_UART_NUM), UART_SCLK_APB);
//     uart_ll_set_baudrate(UART_LL_GET_HW(CONFIG_ESP_CONSOLE_UART_NUM), CONFIG_ESP_CONSOLE_UART_BAUDRATE);
// #endif

//     // cmd_register_system();
//     // cmd_register_ping();
//     // cmd_register_wifi_config();
//     // cmd_register_wifi_scan();
//     // cmd_register_radar();
//     ESP_ERROR_CHECK(esp_console_start_repl(repl));

//     esp_err_t ret = nvs_flash_init();

//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
//     {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         printf("Erase flash in main \n");
//         ret = nvs_flash_init();
//     }

//     // LED_setDuty(&prise.leds[0], 10, 5);
//     // LED_setOffset(&prise.leds[0], 5);
//     // LED_setDuty(&prise.leds[1], 10, 5);
//     // LED_setOffset(&prise.leds[1], 0);
//     vTaskDelay(1000 / portTICK_PERIOD_MS);
//     wifi_initialize();
//     // PRISE_Init(&prise);

//     ESP_LOGI(TAG, " VERSION : %s", PRISE_VESION);
//     ESP_LOGI(TAG, " houssem modifications\n");
//     ESP_LOGI(TAG, " \n");

//     esp_err_t errr = nvs_open("storage", NVS_READWRITE, &nvs);
//     // nvs_get_u8(nvs, "mac_parse0", &prise.mac_parse[0]);
//     // nvs_get_u8(nvs, "mac_parse1", &prise.mac_parse[1]);
//     // nvs_get_u8(nvs, "mac_parse2", &prise.mac_parse[2]);
//     // nvs_get_u8(nvs, "mac_parse3", &prise.mac_parse[3]);
//     // nvs_get_u8(nvs, "mac_parse4", &prise.mac_parse[4]);
//     // nvs_get_u8(nvs, "mac_parse5", &prise.mac_parse[5]);
//     // printf(" MAC adress IN NVS:: %2X %2X %2X %2X %2X %2X \n", prise.mac_parse[0], prise.mac_parse[1], prise.mac_parse[2], prise.mac_parse[3], prise.mac_parse[4], prise.mac_parse[5]);
//     // /* if adress mac different to 0*/
//     // if (prise.mac_parse[0] || prise.mac_parse[1] || prise.mac_parse[2] || prise.mac_parse[3] || prise.mac_parse[4] || prise.mac_parse[5])
//     // {
//     //     test = 1;
//     //     printf("test initial true  %d \n", test);
//     // }
//     wifi_config_t wifi_config;
//     esp_err_t err = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
//     ESP_LOGI(TAG, "Wifi configuration %d", err);
//     if (strlen((char *)wifi_config.sta.ssid) == 0 || strlen((char *)wifi_config.sta.password) == 0)
//     {
//         ESP_LOGI(TAG, "Wifi configuration not found in flash partition called NVS");
//         // start_wps();
//     }
//     else
//     {
//         ESP_LOGI(TAG, "Wifi configuration already stored in flash partition called NVS");
//         // LED_setDuty(&prise.leds[0], 10, 0);
//         // LED_setDuty(&prise.leds[1], 10, 5);

//         // ESP_LOGI(TAG, "ssid :%s", wifi_config.sta.ssid);
//         // size_t ssidLen = strlen((char *)wifi_config.sta.ssid);
//         // // ESP_LOGI(TAG, "ssid_len :%d", ssidLen);

//         // ESP_LOGI(TAG, "pass:%s", wifi_config.sta.password);
//         // size_t passLen = strlen((char *)wifi_config.sta.password);
//         // ESP_LOGI(TAG, "pass :%d", passLen);

//         esp_wifi_connect();
//     }
//     radar_config();
//     // setup();
// }
