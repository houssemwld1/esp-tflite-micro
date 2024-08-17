
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_radar.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#define CONFIG_LESS_INTERFERENCE_CHANNEL 11
#define CONFIG_SEND_FREQUENCY 100
#define QUEUE_SIZE 64

const char *TAG = "SENSING_C";
const char *WIFI_TAG = "wifi_station";
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;

int s_retry_num = 0;
EventGroupHandle_t s_wifi_event_group;
TaskHandle_t myTaskHandlecsi;
QueueHandle_t g_csi_info_queue = NULL;

static const uint8_t CONFIG_CSI_SEND_MAC[] = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4};

struct sensingStruct
{
    int radarStarted;
};

struct sensingStruct sensing;
char bufDataString_data[700];

struct console_input_config
{
    bool train_start;
    float predict_someone_threshold;
    float predict_move_threshold;
    uint32_t predict_buff_size;
    uint32_t predict_outliers_number;
    char collect_taget[16];
    uint32_t collect_number;
    char csi_output_type[16];
    char csi_output_format[16];
} g_console_input_config = {
    .predict_someone_threshold = 0.01,
    .predict_move_threshold = 0.01,
    .predict_buff_size = 5,
    .predict_outliers_number = 2,
    .train_start = false,
    .collect_taget = "unknown",
    .csi_output_type = "LLFT",
    .csi_output_format = "decimal"};

// CSI Data Print Function
void csi_print(const uint8_t *data, size_t len)
{
    printf("CSI Data: ");
    for (size_t i = 0; i < len; i++)
    {
        printf("%d ", data[i]);
    }
    printf("\n");
}

// CSI Data Collection Task
void csi_data_print_task(void *arg)
{
    printf("csi_data_print_task\n");
    wifi_csi_filtered_info_t *info = NULL;
    static uint32_t count = 0;

    while (xQueueReceive(g_csi_info_queue, &info, portMAX_DELAY))
    {
        printf("xQueueReceive\n");

        char *buffer = malloc(5 * 1024);
        if (buffer == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate buffer");
            vTaskDelete(NULL);
            return;
        }

        size_t len = 0;
        wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

        if (count == 0)
        {
            len += sprintf(buffer + len, "type,sequence,timestamp,target_seq,target,mac,rssi,rate,sig_mode,mcs,bandwidth,...,first_word,data\n");
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
            mbedtls_base64_encode((uint8_t *)buffer + len, 5 * 1024 - len, &size, (uint8_t *)info->valid_data, info->valid_len);
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

        ESP_LOGI(TAG, "Formatted data: %s", buffer);
        free(buffer);
    }
    vTaskDelete(NULL);
}

// WiFi Initialization
static void wifi_init()
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT40));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, WIFI_PHY_RATE_MCS0_SGI));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, CONFIG_CSI_SEND_MAC));
}

// CSI Initialization

// CSI Data Callback
void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    if (!info || !info->buf)
    {
        ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return;
    }

    if (memcmp(info->mac, CONFIG_CSI_SEND_MAC, 6))
    {
        return;
    }

    static int s_count = 0;
    const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

    if (!s_count)
    {
        ESP_LOGI(TAG, "================ CSI RECV ================");
        ets_printf("type,id,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
    }

    ets_printf("CSI_DATA,%d," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
               s_count++, MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
               rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
               rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
               rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
               rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state);

    ets_printf(",%d,%d,\"[%d", info->len, info->first_word_invalid, info->buf[0]);

    for (int i = 1; i < info->len; i++)
    {
        ets_printf(",%d", info->buf[i]);
    }

    ets_printf("]\"\n");
}

// ESP-NOW Send Callback
void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "ESP-NOW Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Success" : "Failure");
}

// ESP-NOW Init
static void espnow_init()
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_add_peer(CONFIG_CSI_SEND_MAC, ESP_NOW_ROLE_COM, 1, NULL, 0));
}

// Radar Configuration


// Radar Callback
void radar_rx_cb(const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "Radar data received");
    csi_print(data, len);
}
void radar_config()
{
    radar_config_t radar_config = {
        .enable_radar = true,
        .radar_trigger = RADAR_TRIGGER_TIMEOUT,
        .radar_timeout = 1000, // 1 second timeout
    };

    ESP_ERROR_CHECK(esp_radar_init(&radar_config));
    ESP_ERROR_CHECK(esp_radar_set_rx_cb(radar_rx_cb));
}
static void wifi_csi_init()
{
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

void app_main_wifi()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    s_wifi_event_group = xEventGroupCreate();
    g_csi_info_queue = xQueueCreate(QUEUE_SIZE, sizeof(wifi_csi_info_t *));

    wifi_init();
    espnow_init();
    wifi_csi_init();
    radar_config();

    xTaskCreate(csi_data_print_task, "csi_data_print_task", 4096, NULL, 5, &myTaskHandlecsi);
}

// #include <stdio.h>
// #include <stdint.h>
// #include <stddef.h>
// #include <string.h>
// #include "esp_system.h"
// #include "driver/gpio.h"
// #include "driver/uart.h"
// #include "nvs_flash.h"
// #include "esp_event.h"
// #include "esp_netif.h"
// #include "esp_log.h"
// #include "mqtt_client.h"
// #include "esp_rom_gpio.h"
// #include <sys/param.h>
// #include "cJSON.h"
// #include "mqtt.h"
// #include <esp_wifi.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <freertos/event_groups.h>
// #include "lwip/inet.h"
// #include "lwip/netdb.h"
// #include "lwip/sockets.h"
// #include "esp_mac.h"
// #include "esp_ota_ops.h"
// #include "esp_chip_info.h"
// #include "mbedtls/base64.h"
// #include "esp_radar.h"
// #include "wifi_test_code.h"
// #include "esp_now.h"

// #define QUEUE_SIZE 64 // Adjust based on your needs

// const char *TAG = "SENSING_C";
// const char *WIFI_TAG = "wifi_station";
// const int WIFI_CONNECTED_BIT = BIT0;
// const int WIFI_FAIL_BIT = BIT1;
// int s_retry_num = 0;
// EventGroupHandle_t s_wifi_event_group;

// sensingStruct sensing;
// char bufDataString_data[700];
// TaskHandle_t myTaskHandlecsi;
// QueueHandle_t g_csi_info_queue = NULL;

// struct console_input_config
// {
//     bool train_start;
//     float predict_someone_threshold;
//     float predict_move_threshold;
//     uint32_t predict_buff_size;
//     uint32_t predict_outliers_number;
//     char collect_taget[16];
//     uint32_t collect_number;
//     char csi_output_type[16];
//     char csi_output_format[16];
// } g_console_input_config = {
//     .predict_someone_threshold = 0.01,
//     .predict_move_threshold = 0.01,
//     .predict_buff_size = 5,
//     .predict_outliers_number = 2,
//     .train_start = false,
//     .collect_taget = "unknown",
//     .csi_output_type = "LLFT",
//     .csi_output_format = "decimal"};

// // CSI Data Print Function
// void csi_print(const uint8_t *data, size_t len)
// {
//     printf("CSI Data: ");
//     for (size_t i = 0; i < len; i++)
//     {
//         printf("%d ", data[i]);
//     }
//     printf("\n");
// }

// // CSI Data Collection Task
// void csi_data_print_task(void *arg)
// {
//     printf("csi_data_print_task\n");
//     wifi_csi_filtered_info_t *info = NULL;
//     static uint32_t count = 0;

//     while (xQueueReceive(g_csi_info_queue, &info, portMAX_DELAY))
//     {
//         printf("xQueueReceive\n");

//         // Print the CSI data using the csi_print function with type cast
//         // csi_print((const uint8_t *)info->valid_data, info->valid_len);

//         // Prepare the buffer for formatted data if needed
//         char *buffer = malloc(5 * 1024);
//         if (buffer == NULL)
//         {
//             ESP_LOGE(TAG, "Failed to allocate buffer");
//             vTaskDelete(NULL);
//             return;
//         }

//         size_t len = 0;
//         wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

//         if (count == 0)
//         {
//             len += sprintf(buffer + len, "type,sequence,timestamp,target_seq,target,mac,rssi,rate,sig_mode,mcs,bandwidth,...,first_word,data\n");
//         }
//         info->valid_len = 112;
//         len += sprintf(buffer + len, "CSI_DATA,%d,%u,%u,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,",
//                        count++, esp_log_timestamp(), g_console_input_config.collect_number, g_console_input_config.collect_taget,
//                        MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
//                        rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
//                        rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
//                        rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
//                        rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state, info->valid_len, 0);

//         if (!strcasecmp(g_console_input_config.csi_output_format, "base64"))
//         {
//             size_t size = 0;
//             mbedtls_base64_encode((uint8_t *)buffer + len, 5 * 1024 - len, &size, (uint8_t *)info->valid_data, info->valid_len);
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

//         ESP_LOGI(TAG, "Formatted data: %s", buffer);

//         free(buffer);
//     }
//     // else
//     // {
//     //     ESP_LOGE(TAG, "Failed to receive data from queue");
//     // }
//     vTaskDelete(NULL);
// }

// // ESP-NOW Send Callback
// void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
// {
//     ESP_LOGI(TAG, "ESP-NOW Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Success" : "Fail");
// }

// // ESP-NOW Receive Callback
// void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
// {
//     ESP_LOGI(TAG, "ESP-NOW Received Data:");
//     for (int i = 0; i < len; i++)
//     {
//         printf("%02x ", data[i]);
//     }
//     printf("\n");

//     // Assuming `data` contains CSI data
//     // Allocate memory for the info structure and process it
//     wifi_csi_filtered_info_t *info = malloc(sizeof(wifi_csi_filtered_info_t));
//     if (info == NULL)
//     {
//         ESP_LOGE(TAG, "Failed to allocate memory for CSI info");
//         return;
//     }

//     // Fill `info` with appropriate data based on `data`
//     // This depends on the actual structure and format of `data`
//     // For example, you may need to parse `data` and fill `info` fields

//     // Send `info` to the queue
//     if (xQueueSend(g_csi_info_queue, &info, portMAX_DELAY) != pdPASS)
//     {
//         ESP_LOGE(TAG, "Failed to send data to queue");
//         free(info);
//     }
// }

// // ESP-NOW Initialization
// void espnow_init(void)
// {
//     esp_err_t ret = esp_now_init();
//     if (ret != ESP_OK)
//     {
//         ESP_LOGE(TAG, "ESP-NOW Init Failed: %s", esp_err_to_name(ret));
//         return;
//     }

//     esp_now_register_send_cb(espnow_send_cb);
//     esp_now_register_recv_cb(espnow_recv_cb);

//     // Add peer devices if needed
//     // esp_now_peer_info_t peer_info = {0};
//     // memcpy(peer_info.peer_addr, "30:ae:a4:99:22:f4", ESP_NOW_ETH_ALEN);
//     // peer_info.channel = 0;
//     // peer_info.encrypt = false;
//     // esp_now_add_peer(&peer_info);
// }

// // Radar Configuration
// void radar_config(sensingStruct *sensing)
// {
//     ESP_LOGI(TAG, "RADAR_CONFIG");
//     wifi_radar_config_t radar_config = {
//         .wifi_csi_filtered_cb = wifi_csi_raw_cb,
//         .filter_mac = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4},
//     };
//     sensing->radarStarted = 1;
//     g_csi_info_queue = xQueueCreate(QUEUE_SIZE, sizeof(wifi_csi_filtered_info_t *));
//     if (g_csi_info_queue == NULL)
//     {
//         ESP_LOGE(TAG, "Failed to create CSI info queue");
//         // Handle error
//     }
//     xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 5 * 1024, NULL, 7, &myTaskHandlecsi, 0);
// }

// // WiFi Event Handler
// void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
// {
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
//     {
//         esp_wifi_connect();
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
//     {
//         if (s_retry_num < 10)
//         {
//             esp_wifi_connect();
//             s_retry_num++;
//             ESP_LOGI(WIFI_TAG, "retry to connect to the AP");
//         }
//         else
//         {
//             xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
//         }
//     }
//     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
//     {
//         ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
//         ESP_LOGI(WIFI_TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
//         s_retry_num = 0;
//         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//     }
// }

// // Main Application Entry Point
// void app_main_wifi(void)
// {
//     ESP_ERROR_CHECK(nvs_flash_init());
//     esp_netif_init();
//     esp_event_loop_create_default();

//     s_wifi_event_group = xEventGroupCreate();
//     esp_netif_create_default_wifi_sta();

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     esp_wifi_init(&cfg);
//     esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
//     esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = "ACCENT_LAN",
//             .password = "127.0.0.111",
//         },
//     };
//     esp_wifi_set_mode(WIFI_MODE_STA);
//     esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
//     esp_wifi_start();

//     espnow_init();
//     radar_config(&sensing);

//     // Additional initialization and task creation if needed
// }

// // #include <stdio.h>
// // #include <stdint.h>
// // #include <stddef.h>
// // #include <string.h>
// // #include "esp_system.h"
// // #include "driver/gpio.h"
// // #include "driver/uart.h"
// // #include "nvs_flash.h"
// // #include "esp_event.h"
// // #include "esp_netif.h"
// // #include "esp_log.h"
// // #include "mqtt_client.h"
// // #include "esp_rom_gpio.h"
// // #include <sys/param.h>
// // #include "cJSON.h"
// // #include "mqtt.h"
// // #include <esp_wifi.h>
// // #include <freertos/FreeRTOS.h>
// // #include <freertos/task.h>
// // #include <freertos/event_groups.h>
// // #include "lwip/inet.h"
// // #include "lwip/netdb.h"
// // #include "lwip/sockets.h"
// // #include "esp_mac.h"
// // #include "esp_ota_ops.h"
// // #include "esp_netif.h"
// // #include "esp_chip_info.h"
// // #include "mbedtls/base64.h"
// // #include "esp_radar.h"
// // #include "wifi_test_code.h"
// // #include "esp_now.h"
// // #define QUEUE_SIZE 10 // Adjust based on your needs
// // const char *TAG = "SENSING_C";
// // const char *WIFI_TAG = "wifi_station";
// // const int WIFI_CONNECTED_BIT = BIT0;
// // const int WIFI_FAIL_BIT = BIT1;
// // int s_retry_num = 0;
// // EventGroupHandle_t s_wifi_event_group;

// // sensingStruct sensing;
// // char bufDataString_data[700];
// // TaskHandle_t myTaskHandlecsi;
// // QueueHandle_t g_csi_info_queue = NULL;

// // struct console_input_config
// // {
// //     bool train_start;
// //     float predict_someone_threshold;
// //     float predict_move_threshold;
// //     uint32_t predict_buff_size;
// //     uint32_t predict_outliers_number;
// //     char collect_taget[16];
// //     uint32_t collect_number;
// //     char csi_output_type[16];
// //     char csi_output_format[16];
// // } g_console_input_config = {
// //     .predict_someone_threshold = 0.01,
// //     .predict_move_threshold = 0.01,
// //     .predict_buff_size = 5,
// //     .predict_outliers_number = 2,
// //     .train_start = false,
// //     .collect_taget = "unknown",
// //     .csi_output_type = "LLFT",
// //     .csi_output_format = "decimal"};

// // // CSI Data Print Function
// // void csi_print(const uint8_t *data, size_t len)
// // {
// //     printf("CSI Data: ");
// //     for (size_t i = 0; i < len; i++)
// //     {
// //         printf("%d ", data[i]);
// //     }
// //     printf("\n");
// // }

// // // CSI Data Collection Task
// // // CSI Data Collection Task
// // void csi_data_print_task(void *arg)
// // {
// //     printf("csi_data_print_task\n");
// //     wifi_csi_filtered_info_t *info = NULL;
// //     static uint32_t count = 0;

// //     while (xQueueReceive(g_csi_info_queue, &info, portMAX_DELAY))
// //     {
// //         printf("xQueueReceive\n");

// //         // Print the CSI data using the csi_print function with type cast
// //         csi_print((const uint8_t *)info->valid_data, info->valid_len);

// //         // Prepare the buffer for formatted data if needed
// //         char *buffer = malloc(5 * 1024);
// //         if (buffer == NULL)
// //         {
// //             ESP_LOGE(TAG, "Failed to allocate buffer");
// //             vTaskDelete(NULL);
// //             return;
// //         }

// //         size_t len = 0;
// //         wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

// //         if (count == 0)
// //         {
// //             len += sprintf(buffer + len, "type,sequence,timestamp,target_seq,target,mac,rssi,rate,sig_mode,mcs,bandwidth,...,first_word,data\n");
// //         }

// //         len += sprintf(buffer + len, "CSI_DATA,%d,%u,%u,%s," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d,%d,",
// //                        count++, esp_log_timestamp(), g_console_input_config.collect_number, g_console_input_config.collect_taget,
// //                        MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
// //                        rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
// //                        rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
// //                        rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
// //                        rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state, info->valid_len, 0);

// //         if (!strcasecmp(g_console_input_config.csi_output_format, "base64"))
// //         {
// //             size_t size = 0;
// //             mbedtls_base64_encode((uint8_t *)buffer + len, 5 * 1024 - len, &size, (uint8_t *)info->valid_data, info->valid_len);
// //             len += size;
// //             len += sprintf(buffer + len, "\n");
// //         }
// //         else
// //         {
// //             len += sprintf(buffer + len, "\"[%d", info->valid_data[0]);
// //             for (int i = 1; i < info->valid_len; i++)
// //             {
// //                 len += sprintf(buffer + len, ",%d", info->valid_data[i]);
// //             }
// //             len += sprintf(buffer + len, "]\"\n");
// //         }

// //         ESP_LOGI(TAG, "Formatted data: %s", buffer);

// //         free(buffer);
// //     }

// //     vTaskDelete(NULL);
// // }

// // // ESP-NOW Send Callback
// // void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
// // {
// //     ESP_LOGI(TAG, "ESP-NOW Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Success" : "Fail");
// // }

// // // ESP-NOW Receive Callback
// // void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
// // {
// //     ESP_LOGI(TAG, "ESP-NOW Received Data:");
// //     for (int i = 0; i < len; i++)
// //     {
// //         printf("%02x ", data[i]);
// //     }
// //     printf("\n");
// // }

// // // ESP-NOW Initialization
// // void espnow_init(void)
// // {
// //     esp_err_t ret = esp_now_init();
// //     if (ret != ESP_OK)
// //     {
// //         ESP_LOGE(TAG, "ESP-NOW Init Failed: %s", esp_err_to_name(ret));
// //         return;
// //     }

// //     esp_now_register_send_cb(espnow_send_cb);
// //     esp_now_register_recv_cb(espnow_recv_cb);

// //     // Add peer devices if needed
// //     // esp_now_peer_info_t peer_info = {0};
// //     // memcpy(peer_info.peer_addr, <PEER_MAC_ADDRESS>, ESP_NOW_ETH_ALEN);
// //     // peer_info.channel = 0;
// //     // peer_info.encrypt = false;
// //     // esp_now_add_peer(&peer_info);
// // }

// // // Radar Configuration
// // void radar_config(sensingStruct *sensing)
// // {
// //     ESP_LOGI(TAG, "RADAR_CONFIG");
// //     wifi_radar_config_t radar_config = {
// //         .wifi_csi_filtered_cb = wifi_csi_raw_cb,
// //         .filter_mac = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4},
// //     };
// //     sensing->radarStarted = 1;
// //     // g_csi_info_queue = xQueueCreate(64, sizeof(void *));
// //     g_csi_info_queue = xQueueCreate(QUEUE_SIZE, sizeof(wifi_csi_filtered_info_t *));
// //     if (g_csi_info_queue == NULL)
// //     {
// //         ESP_LOGE(TAG, "Failed to create CSI info queue");
// //         // Handle error
// //     }
// //     xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 5 * 1024, NULL, 7, &myTaskHandlecsi, 0);
// // }

// // // WiFi Event Handler
// // void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
// // {
// //     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
// //     {
// //         esp_wifi_connect();
// //     }
// //     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
// //     {
// //         if (s_retry_num < 10)
// //         {
// //             esp_wifi_connect();
// //             s_retry_num++;
// //             ESP_LOGI(WIFI_TAG, "retry to connect to the AP");
// //         }
// //         else
// //         {
// //             xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
// //         }
// //         ESP_LOGI(WIFI_TAG, "connect to the AP fail");
// //     }
// //     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
// //     {
// //         ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
// //         ESP_LOGI(WIFI_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
// //         s_retry_num = 0;
// //         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
// //     }
// // }

// // // WiFi Initialization

// // void wifi_init_sta(void)
// // {
// //     s_wifi_event_group = xEventGroupCreate();
// //     ESP_ERROR_CHECK(s_wifi_event_group == NULL ? ESP_ERR_NO_MEM : ESP_OK);

// //     ESP_ERROR_CHECK(esp_netif_init());
// //     ESP_ERROR_CHECK(esp_event_loop_create_default());

// //     esp_netif_create_default_wifi_sta();

// //     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
// //     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

// //     esp_event_handler_instance_t instance_any_id;
// //     esp_event_handler_instance_t instance_got_ip;
// //     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
// //     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

// //     wifi_config_t wifi_config = {
// //         .sta = {
// //             .ssid = "ACCENT_LAN",
// //             .password = "127.0.0.111",
// //             .threshold.authmode = WIFI_AUTH_WPA2_PSK,
// //         },
// //     };

// //     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
// //     ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
// //     ESP_ERROR_CHECK(esp_wifi_start());

// //     ESP_LOGI(WIFI_TAG, "wifi_init_sta finished.");

// //     EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
// //     if (bits & WIFI_CONNECTED_BIT)
// //     {
// //         ESP_LOGI(WIFI_TAG, "Connected to AP SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
// //     }
// //     else if (bits & WIFI_FAIL_BIT)
// //     {
// //         ESP_LOGI(WIFI_TAG, "Failed to connect to SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
// //     }
// //     else
// //     {
// //         ESP_LOGE(WIFI_TAG, "UNEXPECTED EVENT");
// //     }

// //     ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
// //     ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
// //     vEventGroupDelete(s_wifi_event_group);
// // }
// // // Main Application Entry Point
// // void app_main_wifi(void)
// // {
// //     ESP_ERROR_CHECK(nvs_flash_init());
// //     esp_log_level_set("*", ESP_LOG_INFO);
// //     esp_log_level_set("wifi", ESP_LOG_INFO);
// //     esp_log_level_set("http", ESP_LOG_INFO);
// //     esp_log_level_set("mqtt", ESP_LOG_INFO);

// //     wifi_init_sta();
// //     espnow_init();
// //     radar_config(&sensing);
// // }
