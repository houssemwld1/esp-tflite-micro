
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_rom_gpio.h"
#include <sys/param.h>
#include "cJSON.h"
#include "mqtt.h"
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_chip_info.h"
#include "mbedtls/base64.h"
#include "esp_radar.h"
#include "wifi_test_code.h"
#include "esp_now.h"

#define QUEUE_SIZE 64 // Adjust based on your needs

const char *TAG = "SENSING_C";
const char *WIFI_TAG = "wifi_station";
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;
int s_retry_num = 0;
EventGroupHandle_t s_wifi_event_group;

sensingStruct sensing;
char bufDataString_data[700];
TaskHandle_t myTaskHandlecsi;
QueueHandle_t g_csi_info_queue = NULL;

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

        // Print the CSI data using the csi_print function with type cast
        // csi_print((const uint8_t *)info->valid_data, info->valid_len);

        // Prepare the buffer for formatted data if needed
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
    // else
    // {
    //     ESP_LOGE(TAG, "Failed to receive data from queue");
    // }
    vTaskDelete(NULL);
}

// ESP-NOW Send Callback
void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "ESP-NOW Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Success" : "Fail");
}

// ESP-NOW Receive Callback
void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "ESP-NOW Received Data:");
    for (int i = 0; i < len; i++)
    {
        printf("%02x ", data[i]);
    }
    printf("\n");

    // Assuming `data` contains CSI data
    // Allocate memory for the info structure and process it
    wifi_csi_filtered_info_t *info = malloc(sizeof(wifi_csi_filtered_info_t));
    if (info == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for CSI info");
        return;
    }

    // Fill `info` with appropriate data based on `data`
    // This depends on the actual structure and format of `data`
    // For example, you may need to parse `data` and fill `info` fields

    // Send `info` to the queue
    if (xQueueSend(g_csi_info_queue, &info, portMAX_DELAY) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to send data to queue");
        free(info);
    }
}

// ESP-NOW Initialization
void espnow_init(void)
{
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ESP-NOW Init Failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);

    // Add peer devices if needed
    // esp_now_peer_info_t peer_info = {0};
    // memcpy(peer_info.peer_addr, "30:ae:a4:99:22:f4", ESP_NOW_ETH_ALEN);
    // peer_info.channel = 0;
    // peer_info.encrypt = false;
    // esp_now_add_peer(&peer_info);
}

// Radar Configuration
void radar_config(sensingStruct *sensing)
{
    ESP_LOGI(TAG, "RADAR_CONFIG");
    wifi_radar_config_t radar_config = {
        .wifi_csi_filtered_cb = wifi_csi_raw_cb,
        .filter_mac = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4},
    };
    sensing->radarStarted = 1;
    g_csi_info_queue = xQueueCreate(QUEUE_SIZE, sizeof(wifi_csi_filtered_info_t *));
    if (g_csi_info_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create CSI info queue");
        // Handle error
    }
    xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 5 * 1024, NULL, 7, &myTaskHandlecsi, 0);
}

// WiFi Event Handler
void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < 10)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Main Application Entry Point
void app_main_wifi(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_netif_init();
    esp_event_loop_create_default();

    s_wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "ACCENT_LAN",
            .password = "127.0.0.111",
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    espnow_init();
    radar_config(&sensing);

    // Additional initialization and task creation if needed
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
// #include "esp_netif.h"
// #include "esp_chip_info.h"
// #include "mbedtls/base64.h"
// #include "esp_radar.h"
// #include "wifi_test_code.h"
// #include "esp_now.h"
// #define QUEUE_SIZE 10 // Adjust based on your needs
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
//         csi_print((const uint8_t *)info->valid_data, info->valid_len);

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
//     // memcpy(peer_info.peer_addr, <PEER_MAC_ADDRESS>, ESP_NOW_ETH_ALEN);
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
//     // g_csi_info_queue = xQueueCreate(64, sizeof(void *));
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
//         ESP_LOGI(WIFI_TAG, "connect to the AP fail");
//     }
//     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
//     {
//         ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
//         ESP_LOGI(WIFI_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
//         s_retry_num = 0;
//         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//     }
// }

// // WiFi Initialization

// void wifi_init_sta(void)
// {
//     s_wifi_event_group = xEventGroupCreate();
//     ESP_ERROR_CHECK(s_wifi_event_group == NULL ? ESP_ERR_NO_MEM : ESP_OK);

//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());

//     esp_netif_create_default_wifi_sta();

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     esp_event_handler_instance_t instance_any_id;
//     esp_event_handler_instance_t instance_got_ip;
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = "ACCENT_LAN",
//             .password = "127.0.0.111",
//             .threshold.authmode = WIFI_AUTH_WPA2_PSK,
//         },
//     };

//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     ESP_LOGI(WIFI_TAG, "wifi_init_sta finished.");

//     EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
//     if (bits & WIFI_CONNECTED_BIT)
//     {
//         ESP_LOGI(WIFI_TAG, "Connected to AP SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
//     }
//     else if (bits & WIFI_FAIL_BIT)
//     {
//         ESP_LOGI(WIFI_TAG, "Failed to connect to SSID:%s password:%s", wifi_config.sta.ssid, wifi_config.sta.password);
//     }
//     else
//     {
//         ESP_LOGE(WIFI_TAG, "UNEXPECTED EVENT");
//     }

//     ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
//     ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
//     vEventGroupDelete(s_wifi_event_group);
// }
// // Main Application Entry Point
// void app_main_wifi(void)
// {
//     ESP_ERROR_CHECK(nvs_flash_init());
//     esp_log_level_set("*", ESP_LOG_INFO);
//     esp_log_level_set("wifi", ESP_LOG_INFO);
//     esp_log_level_set("http", ESP_LOG_INFO);
//     esp_log_level_set("mqtt", ESP_LOG_INFO);

//     wifi_init_sta();
//     espnow_init();
//     radar_config(&sensing);
// }






#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_rom_gpio.h"
#include <sys/param.h>
#include "cJSON.h"
#include "mqtt.h"
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_chip_info.h"
#include "mbedtls/base64.h"
#include "esp_radar.h"
#include "wifi_test_code.h"
#include "esp_now.h"

#define QUEUE_SIZE 64 // Adjust based on your needs

const char *TAG = "SENSING_C";
const char *WIFI_TAG = "wifi_station";
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT = BIT1;
int s_retry_num = 0;
EventGroupHandle_t s_wifi_event_group;

sensingStruct sensing;
char bufDataString_data[700];
TaskHandle_t myTaskHandlecsi;
QueueHandle_t g_csi_info_queue = NULL;

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

        // Print the CSI data using the csi_print function with type cast
        // csi_print((const uint8_t *)info->valid_data, info->valid_len);

        // Prepare the buffer for formatted data if needed
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
    // else
    // {
    //     ESP_LOGE(TAG, "Failed to receive data from queue");
    // }
    vTaskDelete(NULL);
}

// ESP-NOW Send Callback
void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "ESP-NOW Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Success" : "Fail");
}

// ESP-NOW Receive Callback
void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "ESP-NOW Received Data:");
    for (int i = 0; i < len; i++)
    {
        printf("%02x ", data[i]);
    }
    printf("\n");

    // Assuming `data` contains CSI data
    // Allocate memory for the info structure and process it
    wifi_csi_filtered_info_t *info = malloc(sizeof(wifi_csi_filtered_info_t));
    if (info == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for CSI info");
        return;
    }

    // Fill `info` with appropriate data based on `data`
    // This depends on the actual structure and format of `data`
    // For example, you may need to parse `data` and fill `info` fields

    // Send `info` to the queue
    if (xQueueSend(g_csi_info_queue, &info, portMAX_DELAY) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to send data to queue");
        free(info);
    }
}

// ESP-NOW Initialization
void espnow_init(void)
{
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ESP-NOW Init Failed: %s", esp_err_to_name(ret));
        return;
    }

    g_csi_info_queue = xQueueCreate(QUEUE_SIZE, sizeof(wifi_csi_filtered_info_t *));
    if (g_csi_info_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create CSI info queue");
        // Handle error
    }
    xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 5 * 1024, NULL, 7, &myTaskHandlecsi, 0);
    // esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);

    // Add peer devices if needed
    // esp_now_peer_info_t peer_info = {0};
    // memcpy(peer_info.peer_addr, "30:ae:a4:99:22:f4", ESP_NOW_ETH_ALEN);
    // peer_info.channel = 0;
    // peer_info.encrypt = false;
    // esp_now_add_peer(&peer_info);
}

// Radar Configuration
void radar_config(sensingStruct *sensing)
{
    ESP_LOGI(TAG, "RADAR_CONFIG");
    wifi_radar_config_t radar_config = {
        .wifi_csi_filtered_cb = wifi_csi_raw_cb,
        .filter_mac = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4},
    };
    sensing->radarStarted = 1;
    g_csi_info_queue = xQueueCreate(QUEUE_SIZE, sizeof(wifi_csi_filtered_info_t *));
    if (g_csi_info_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create CSI info queue");
        // Handle error
    }
    xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 5 * 1024, NULL, 7, &myTaskHandlecsi, 0);
}

// WiFi Event Handler
void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < 10)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(WIFI_TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(WIFI_TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Main Application Entry Point
void app_main_wifi(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_netif_init();
    esp_event_loop_create_default();

    s_wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "ACCENT_LAN",
            .password = "127.0.0.111",
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_start();

    espnow_init();
    // radar_config(&sensing);

    // Additional initialization and task creation if needed
}

// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>

// #include "nvs_flash.h"

// #include "esp_mac.h"
// #include "rom/ets_sys.h"
// #include "esp_log.h"
// #include "esp_wifi.h"
// #include "esp_netif.h"
// #include "esp_now.h"

// #define CONFIG_LESS_INTERFERENCE_CHANNEL 11
// #define CONFIG_SEND_FREQUENCY 100

// static const uint8_t CONFIG_CSI_SEND_MAC[] = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4},
// static const char *TAG = "csi_recv";

// static void wifi_init()
// {
//     ESP_ERROR_CHECK(esp_event_loop_create_default());

//     ESP_ERROR_CHECK(esp_netif_init());
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

//     // Corrected type conversion
//     ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT20));
//     // ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     // Corrected type conversion
//     ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_MCS0_SGI));
//     ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

//     ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
//     ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, CONFIG_CSI_SEND_MAC));
// }

// static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info)
// {
//     if (!info || !info->buf)
//     {
//         ESP_LOGW(TAG, "<%s> wifi_csi_cb", esp_err_to_name(ESP_ERR_INVALID_ARG));
//         return;
//     }

//     if (memcmp(info->mac, CONFIG_CSI_SEND_MAC, 6))
//     {
//         return;
//     }

//     static int s_count = 0;
//     const wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;

//     if (!s_count)
//     {
//         ESP_LOGI(TAG, "================ CSI RECV ================");
//         ets_printf("type,id,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
//     }

//     // info->len = 112;
//     ets_printf("CSI_DATA,%d," MACSTR ",%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
//                s_count++, MAC2STR(info->mac), rx_ctrl->rssi, rx_ctrl->rate, rx_ctrl->sig_mode,
//                rx_ctrl->mcs, rx_ctrl->cwb, rx_ctrl->smoothing, rx_ctrl->not_sounding,
//                rx_ctrl->aggregation, rx_ctrl->stbc, rx_ctrl->fec_coding, rx_ctrl->sgi,
//                rx_ctrl->noise_floor, rx_ctrl->ampdu_cnt, rx_ctrl->channel, rx_ctrl->secondary_channel,
//                rx_ctrl->timestamp, rx_ctrl->ant, rx_ctrl->sig_len, rx_ctrl->rx_state);

//     ets_printf(",%d,%d,\"[%d", info->len, info->first_word_invalid, info->buf[0]);

//     for (int i = 1; i < info->len; i++)
//     {
//         ets_printf(",%d", info->buf[i]);
//     }

//     ets_printf("]\"\n");
// }

// static void wifi_csi_init()
// {
//     ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
//     // ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(g_wifi_radar_config->wifi_sniffer_cb));

//     /**< default config */
//     wifi_csi_config_t csi_config = {
//         .lltf_en = true,
//         .htltf_en = true,
//         .stbc_htltf2_en = true,
//         .ltf_merge_en = true,
//         .channel_filter_en = true,
//         .manu_scale = false,
//         .shift = false,
//     };
//     ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
//     ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
//     ESP_ERROR_CHECK(esp_wifi_set_csi(true));
// }

// void app_main_wifi()
// {
//     // Initialize NVS
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
//     {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);

//     wifi_init();
//     wifi_csi_init();
// }

// sender
//  //sender code for esp32

// #include <stdio.h>
// #include <string.h>
// #include <stdlib.h>
// #include <unistd.h>

// #include "nvs_flash.h"

// #include "esp_mac.h"
// #include "esp_log.h"
// #include "esp_wifi.h"
// #include "esp_netif.h"
// #include "esp_now.h"

// #define CONFIG_LESS_INTERFERENCE_CHANNEL    11
// #define CONFIG_SEND_FREQUENCY               100

// static const uint8_t CONFIG_CSI_SEND_MAC[] = {0x1a, 0x00, 0x00, 0x00, 0x00, 0x00};
// static const char *TAG = "csi_send";

// static void wifi_init()
// {
//     ESP_ERROR_CHECK(esp_event_loop_create_default());

//     ESP_ERROR_CHECK(esp_netif_init());
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));

//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
//     ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT20));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, WIFI_PHY_RATE_MCS0_SGI));
//     ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

//     ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LESS_INTERFERENCE_CHANNEL, WIFI_SECOND_CHAN_BELOW));
//     ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, CONFIG_CSI_SEND_MAC));
// }

// void app_main_wifi()
// {
//     /**
//      * @breif Initialize NVS
//      */
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//       ESP_ERROR_CHECK(nvs_flash_erase());
//       ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);

//     /**
//      * @breif Initialize Wi-Fi
//      */
//     wifi_init();

//     /**
//      * @breif Initialize ESP-NOW
//      *        ESP-NOW protocol see: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html
//      */
//     ESP_ERROR_CHECK(esp_now_init());
//     ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)"pmk1234567890123"));

//     esp_now_peer_info_t peer = {
//         .channel   = CONFIG_LESS_INTERFERENCE_CHANNEL,
//         .ifidx     = WIFI_IF_STA,
//         .encrypt   = false,
//         .peer_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
//     };
//     ESP_ERROR_CHECK(esp_now_add_peer(&peer));

//     ESP_LOGI(TAG, "================ CSI SEND ================");
//     ESP_LOGI(TAG, "wifi_channel: %d, send_frequency: %d, mac: " MACSTR,
//              CONFIG_LESS_INTERFERENCE_CHANNEL, CONFIG_SEND_FREQUENCY, MAC2STR(CONFIG_CSI_SEND_MAC));

//     for (uint8_t count = 0; ;++count) {
//         esp_err_t ret = esp_now_send(peer.peer_addr, &count, sizeof(uint8_t));

//         if(ret != ESP_OK) {
//             ESP_LOGW(TAG, "<%s> ESP-NOW send error", esp_err_to_name(ret));
//         }

//         usleep(1000 * 1000 / CONFIG_SEND_FREQUENCY);
//     }
// }








///config file
#
# Automatically generated file. DO NOT EDIT.
# Espressif IoT Development Framework (ESP-IDF) Project Configuration
#
CONFIG_IDF_CMAKE=y
CONFIG_IDF_TARGET_ARCH_XTENSA=y
CONFIG_IDF_TARGET="esp32"
CONFIG_IDF_TARGET_ESP32=y
CONFIG_IDF_FIRMWARE_CHIP_ID=0x0000

#
# SDK tool configuration
#
CONFIG_SDK_TOOLPREFIX="xtensa-esp32-elf-"
# CONFIG_SDK_TOOLCHAIN_SUPPORTS_TIME_WIDE_64_BITS is not set
# end of SDK tool configuration

#
# Build type
#
CONFIG_APP_BUILD_TYPE_APP_2NDBOOT=y
# CONFIG_APP_BUILD_TYPE_ELF_RAM is not set
CONFIG_APP_BUILD_GENERATE_BINARIES=y
CONFIG_APP_BUILD_BOOTLOADER=y
CONFIG_APP_BUILD_USE_FLASH_SECTIONS=y
# end of Build type

#
# Application manager
#
CONFIG_APP_COMPILE_TIME_DATE=y
# CONFIG_APP_EXCLUDE_PROJECT_VER_VAR is not set
# CONFIG_APP_EXCLUDE_PROJECT_NAME_VAR is not set
# CONFIG_APP_PROJECT_VER_FROM_CONFIG is not set
CONFIG_APP_RETRIEVE_LEN_ELF_SHA=16
# end of Application manager

#
# Bootloader config
#
CONFIG_BOOTLOADER_OFFSET_IN_FLASH=0x1000
CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_SIZE=y
# CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_DEBUG is not set
# CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_PERF is not set
# CONFIG_BOOTLOADER_COMPILER_OPTIMIZATION_NONE is not set
# CONFIG_BOOTLOADER_LOG_LEVEL_NONE is not set
# CONFIG_BOOTLOADER_LOG_LEVEL_ERROR is not set
# CONFIG_BOOTLOADER_LOG_LEVEL_WARN is not set
CONFIG_BOOTLOADER_LOG_LEVEL_INFO=y
# CONFIG_BOOTLOADER_LOG_LEVEL_DEBUG is not set
# CONFIG_BOOTLOADER_LOG_LEVEL_VERBOSE is not set
CONFIG_BOOTLOADER_LOG_LEVEL=3
# CONFIG_BOOTLOADER_SPI_CUSTOM_WP_PIN is not set
CONFIG_BOOTLOADER_SPI_WP_PIN=7
CONFIG_BOOTLOADER_VDDSDIO_BOOST_1_9V=y
# CONFIG_BOOTLOADER_FACTORY_RESET is not set
# CONFIG_BOOTLOADER_APP_TEST is not set
CONFIG_BOOTLOADER_WDT_ENABLE=y
# CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE is not set
CONFIG_BOOTLOADER_WDT_TIME_MS=9000
# CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set
# CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP is not set
# CONFIG_BOOTLOADER_SKIP_VALIDATE_ON_POWER_ON is not set
# CONFIG_BOOTLOADER_SKIP_VALIDATE_ALWAYS is not set
CONFIG_BOOTLOADER_RESERVE_RTC_SIZE=0
# CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC is not set
CONFIG_BOOTLOADER_FLASH_XMC_SUPPORT=y
# end of Bootloader config

#
# Security features
#
# CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT is not set
# CONFIG_SECURE_BOOT is not set
# CONFIG_SECURE_FLASH_ENC_ENABLED is not set
# end of Security features

#
# Serial flasher config
#
CONFIG_ESPTOOLPY_BAUD_OTHER_VAL=115200
# CONFIG_ESPTOOLPY_NO_STUB is not set
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
# CONFIG_ESPTOOLPY_FLASHMODE_QOUT is not set
# CONFIG_ESPTOOLPY_FLASHMODE_DIO is not set
# CONFIG_ESPTOOLPY_FLASHMODE_DOUT is not set
CONFIG_ESPTOOLPY_FLASH_SAMPLE_MODE_STR=y
CONFIG_ESPTOOLPY_FLASHMODE="dio"
# CONFIG_ESPTOOLPY_FLASHFREQ_120M is not set
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
# CONFIG_ESPTOOLPY_FLASHFREQ_40M is not set
# CONFIG_ESPTOOLPY_FLASHFREQ_26M is not set
# CONFIG_ESPTOOLPY_FLASHFREQ_20M is not set
CONFIG_ESPTOOLPY_FLASHFREQ="80m"
# CONFIG_ESPTOOLPY_FLASHSIZE_1MB is not set
# CONFIG_ESPTOOLPY_FLASHSIZE_2MB is not set
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
# CONFIG_ESPTOOLPY_FLASHSIZE_8MB is not set
# CONFIG_ESPTOOLPY_FLASHSIZE_16MB is not set
CONFIG_ESPTOOLPY_FLASHSIZE="4MB"
CONFIG_ESPTOOLPY_FLASHSIZE_DETECT=y
CONFIG_ESPTOOLPY_BEFORE_RESET=y
# CONFIG_ESPTOOLPY_BEFORE_NORESET is not set
CONFIG_ESPTOOLPY_BEFORE="default_reset"
CONFIG_ESPTOOLPY_AFTER_RESET=y
# CONFIG_ESPTOOLPY_AFTER_NORESET is not set
CONFIG_ESPTOOLPY_AFTER="hard_reset"
# CONFIG_ESPTOOLPY_MONITOR_BAUD_CONSOLE is not set
# CONFIG_ESPTOOLPY_MONITOR_BAUD_9600B is not set
# CONFIG_ESPTOOLPY_MONITOR_BAUD_57600B is not set
# CONFIG_ESPTOOLPY_MONITOR_BAUD_115200B is not set
# CONFIG_ESPTOOLPY_MONITOR_BAUD_230400B is not set
CONFIG_ESPTOOLPY_MONITOR_BAUD_921600B=y
# CONFIG_ESPTOOLPY_MONITOR_BAUD_2MB is not set
# CONFIG_ESPTOOLPY_MONITOR_BAUD_OTHER is not set
CONFIG_ESPTOOLPY_MONITOR_BAUD_OTHER_VAL=115200
CONFIG_ESPTOOLPY_MONITOR_BAUD=921600
# end of Serial flasher config

#
# Partition Table
#
# CONFIG_PARTITION_TABLE_SINGLE_APP is not set
# CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE is not set
# CONFIG_PARTITION_TABLE_TWO_OTA is not set
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
CONFIG_PARTITION_TABLE_MD5=y
# end of Partition Table

#
# Example Configuration
#
CONFIG_ESP_WIFI_SSID="myssid"
CONFIG_ESP_WIFI_PASSWORD="mypassword"
CONFIG_ESP_WIFI_CHANNEL=1
CONFIG_ESP_MAX_STA_CONN=4
# end of Example Configuration

#
# ESP-NN
#
# CONFIG_NN_ANSI_C is not set
CONFIG_NN_OPTIMIZED=y
CONFIG_NN_OPTIMIZATIONS=1
# end of ESP-NN

#
# Compiler options
#
# CONFIG_COMPILER_OPTIMIZATION_DEFAULT is not set
# CONFIG_COMPILER_OPTIMIZATION_SIZE is not set
CONFIG_COMPILER_OPTIMIZATION_PERF=y
# CONFIG_COMPILER_OPTIMIZATION_NONE is not set
CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE=y
# CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT is not set
# CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE is not set
CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL=2
# CONFIG_COMPILER_OPTIMIZATION_CHECKS_SILENT is not set
CONFIG_COMPILER_HIDE_PATHS_MACROS=y
# CONFIG_COMPILER_CXX_EXCEPTIONS is not set
# CONFIG_COMPILER_CXX_RTTI is not set
CONFIG_COMPILER_STACK_CHECK_MODE_NONE=y
# CONFIG_COMPILER_STACK_CHECK_MODE_NORM is not set
# CONFIG_COMPILER_STACK_CHECK_MODE_STRONG is not set
# CONFIG_COMPILER_STACK_CHECK_MODE_ALL is not set
# CONFIG_COMPILER_WARN_WRITE_STRINGS is not set
# CONFIG_COMPILER_DISABLE_GCC8_WARNINGS is not set
# CONFIG_COMPILER_DUMP_RTL_FILES is not set
# end of Compiler options

#
# Component config
#

#
# Application Level Tracing
#
# CONFIG_APPTRACE_DEST_JTAG is not set
CONFIG_APPTRACE_DEST_NONE=y
CONFIG_APPTRACE_LOCK_ENABLE=y
# end of Application Level Tracing

#
# ESP-ASIO
#
# CONFIG_ASIO_SSL_SUPPORT is not set
# end of ESP-ASIO

#
# Bluetooth
#
# CONFIG_BT_ENABLED is not set
# end of Bluetooth

#
# CoAP Configuration
#
CONFIG_COAP_MBEDTLS_PSK=y
# CONFIG_COAP_MBEDTLS_PKI is not set
# CONFIG_COAP_MBEDTLS_DEBUG is not set
CONFIG_COAP_LOG_DEFAULT_LEVEL=0
# end of CoAP Configuration

#
# Driver configurations
#

#
# ADC configuration
#
# CONFIG_ADC_FORCE_XPD_FSM is not set
CONFIG_ADC_DISABLE_DAC=y
# end of ADC configuration

#
# MCPWM configuration
#
# CONFIG_MCPWM_ISR_IN_IRAM is not set
# end of MCPWM configuration

#
# SPI configuration
#
# CONFIG_SPI_MASTER_IN_IRAM is not set
CONFIG_SPI_MASTER_ISR_IN_IRAM=y
# CONFIG_SPI_SLAVE_IN_IRAM is not set
CONFIG_SPI_SLAVE_ISR_IN_IRAM=y
# end of SPI configuration

#
# TWAI configuration
#
# CONFIG_TWAI_ISR_IN_IRAM is not set
# CONFIG_TWAI_ERRATA_FIX_BUS_OFF_REC is not set
# CONFIG_TWAI_ERRATA_FIX_TX_INTR_LOST is not set
# CONFIG_TWAI_ERRATA_FIX_RX_FRAME_INVALID is not set
# CONFIG_TWAI_ERRATA_FIX_RX_FIFO_CORRUPT is not set
# end of TWAI configuration

#
# UART configuration
#
# CONFIG_UART_ISR_IN_IRAM is not set
# end of UART configuration

#
# RTCIO configuration
#
# CONFIG_RTCIO_SUPPORT_RTC_GPIO_DESC is not set
# end of RTCIO configuration

#
# GPIO Configuration
#
# CONFIG_GPIO_ESP32_SUPPORT_SWITCH_SLP_PULL is not set
# end of GPIO Configuration
# end of Driver configurations

#
# eFuse Bit Manager
#
# CONFIG_EFUSE_CUSTOM_TABLE is not set
# CONFIG_EFUSE_VIRTUAL is not set
# CONFIG_EFUSE_CODE_SCHEME_COMPAT_NONE is not set
CONFIG_EFUSE_CODE_SCHEME_COMPAT_3_4=y
# CONFIG_EFUSE_CODE_SCHEME_COMPAT_REPEAT is not set
CONFIG_EFUSE_MAX_BLK_LEN=192
# end of eFuse Bit Manager

#
# ESP-TLS
#
CONFIG_ESP_TLS_USING_MBEDTLS=y
# CONFIG_ESP_TLS_USE_SECURE_ELEMENT is not set
# CONFIG_ESP_TLS_SERVER is not set
# CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS is not set
# CONFIG_ESP_TLS_PSK_VERIFICATION is not set
# CONFIG_ESP_TLS_INSECURE is not set
# end of ESP-TLS

#
# ESP32-specific
#
CONFIG_ESP32_REV_MIN_0=y
# CONFIG_ESP32_REV_MIN_1 is not set
# CONFIG_ESP32_REV_MIN_2 is not set
# CONFIG_ESP32_REV_MIN_3 is not set
CONFIG_ESP32_REV_MIN=0
CONFIG_ESP32_DPORT_WORKAROUND=y
# CONFIG_ESP32_DEFAULT_CPU_FREQ_80 is not set
# CONFIG_ESP32_DEFAULT_CPU_FREQ_160 is not set
CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y
CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ=240
# CONFIG_ESP32_SPIRAM_SUPPORT is not set
# CONFIG_ESP32_TRAX is not set
CONFIG_ESP32_TRACEMEM_RESERVE_DRAM=0x0
# CONFIG_ESP32_ULP_COPROC_ENABLED is not set
CONFIG_ESP32_ULP_COPROC_RESERVE_MEM=0
CONFIG_ESP32_DEBUG_OCDAWARE=y
CONFIG_ESP32_BROWNOUT_DET=y
CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_0=y
# CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_1 is not set
# CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_2 is not set
# CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_3 is not set
# CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_4 is not set
# CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_5 is not set
# CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_6 is not set
# CONFIG_ESP32_BROWNOUT_DET_LVL_SEL_7 is not set
CONFIG_ESP32_BROWNOUT_DET_LVL=0
CONFIG_ESP32_TIME_SYSCALL_USE_RTC_FRC1=y
# CONFIG_ESP32_TIME_SYSCALL_USE_RTC is not set
# CONFIG_ESP32_TIME_SYSCALL_USE_FRC1 is not set
# CONFIG_ESP32_TIME_SYSCALL_USE_NONE is not set
CONFIG_ESP32_RTC_CLK_SRC_INT_RC=y
# CONFIG_ESP32_RTC_CLK_SRC_EXT_CRYS is not set
# CONFIG_ESP32_RTC_CLK_SRC_EXT_OSC is not set
# CONFIG_ESP32_RTC_CLK_SRC_INT_8MD256 is not set
CONFIG_ESP32_RTC_CLK_CAL_CYCLES=1024
CONFIG_ESP32_DEEP_SLEEP_WAKEUP_DELAY=2000
CONFIG_ESP32_XTAL_FREQ_40=y
# CONFIG_ESP32_XTAL_FREQ_26 is not set
# CONFIG_ESP32_XTAL_FREQ_AUTO is not set
CONFIG_ESP32_XTAL_FREQ=40
# CONFIG_ESP32_DISABLE_BASIC_ROM_CONSOLE is not set
# CONFIG_ESP32_NO_BLOBS is not set
# CONFIG_ESP32_COMPATIBLE_PRE_V2_1_BOOTLOADERS is not set
# CONFIG_ESP32_COMPATIBLE_PRE_V3_1_BOOTLOADERS is not set
# CONFIG_ESP32_USE_FIXED_STATIC_RAM_SIZE is not set
CONFIG_ESP32_DPORT_DIS_INTERRUPT_LVL=5
# end of ESP32-specific

#
# ADC-Calibration
#
CONFIG_ADC_CAL_EFUSE_TP_ENABLE=y
CONFIG_ADC_CAL_EFUSE_VREF_ENABLE=y
CONFIG_ADC_CAL_LUT_ENABLE=y
# end of ADC-Calibration

#
# Common ESP-related
#
CONFIG_ESP_ERR_TO_NAME_LOOKUP=y
# end of Common ESP-related

#
# Ethernet
#
CONFIG_ETH_ENABLED=y
CONFIG_ETH_USE_ESP32_EMAC=y
CONFIG_ETH_PHY_INTERFACE_RMII=y
CONFIG_ETH_RMII_CLK_INPUT=y
# CONFIG_ETH_RMII_CLK_OUTPUT is not set
CONFIG_ETH_RMII_CLK_IN_GPIO=0
CONFIG_ETH_DMA_BUFFER_SIZE=512
CONFIG_ETH_DMA_RX_BUFFER_NUM=10
CONFIG_ETH_DMA_TX_BUFFER_NUM=10
CONFIG_ETH_USE_SPI_ETHERNET=y
# CONFIG_ETH_SPI_ETHERNET_DM9051 is not set
# CONFIG_ETH_SPI_ETHERNET_W5500 is not set
# CONFIG_ETH_SPI_ETHERNET_KSZ8851SNL is not set
# CONFIG_ETH_USE_OPENETH is not set
# end of Ethernet

#
# Event Loop Library
#
# CONFIG_ESP_EVENT_LOOP_PROFILING is not set
CONFIG_ESP_EVENT_POST_FROM_ISR=y
CONFIG_ESP_EVENT_POST_FROM_IRAM_ISR=y
# end of Event Loop Library

#
# GDB Stub
#
# end of GDB Stub

#
# ESP HTTP client
#
CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y
# CONFIG_ESP_HTTP_CLIENT_ENABLE_BASIC_AUTH is not set
CONFIG_ESP_HTTP_CLIENT_ENABLE_DIGEST_AUTH=y
# end of ESP HTTP client

#
# HTTP Server
#
CONFIG_HTTPD_MAX_REQ_HDR_LEN=512
CONFIG_HTTPD_MAX_URI_LEN=512
CONFIG_HTTPD_ERR_RESP_NO_DELAY=y
CONFIG_HTTPD_PURGE_BUF_LEN=32
# CONFIG_HTTPD_LOG_PURGE_DATA is not set
# CONFIG_HTTPD_WS_SUPPORT is not set
# end of HTTP Server

#
# ESP HTTPS OTA
#
# CONFIG_OTA_ALLOW_HTTP is not set
# end of ESP HTTPS OTA

#
# ESP HTTPS server
#
# CONFIG_ESP_HTTPS_SERVER_ENABLE is not set
# end of ESP HTTPS server

#
# Hardware Settings
#

#
# MAC Config
#
CONFIG_ESP_MAC_ADDR_UNIVERSE_WIFI_STA=y
CONFIG_ESP_MAC_ADDR_UNIVERSE_WIFI_AP=y
CONFIG_ESP_MAC_ADDR_UNIVERSE_BT=y
CONFIG_ESP_MAC_ADDR_UNIVERSE_ETH=y
# CONFIG_ESP32_UNIVERSAL_MAC_ADDRESSES_TWO is not set
CONFIG_ESP32_UNIVERSAL_MAC_ADDRESSES_FOUR=y
CONFIG_ESP32_UNIVERSAL_MAC_ADDRESSES=4
# end of MAC Config

#
# Sleep Config
#
CONFIG_ESP_SLEEP_POWER_DOWN_FLASH=y
CONFIG_ESP_SLEEP_RTC_BUS_ISO_WORKAROUND=y
# CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND is not set
# CONFIG_ESP_SLEEP_FLASH_LEAKAGE_WORKAROUND is not set
# end of Sleep Config
# end of Hardware Settings

#
# IPC (Inter-Processor Call)
#
CONFIG_ESP_IPC_TASK_STACK_SIZE=1024
CONFIG_ESP_IPC_USES_CALLERS_PRIORITY=y
CONFIG_ESP_IPC_ISR_ENABLE=y
# end of IPC (Inter-Processor Call)

#
# LCD and Touch Panel
#

#
# LCD Peripheral Configuration
#
CONFIG_LCD_PANEL_IO_FORMAT_BUF_SIZE=32
# end of LCD Peripheral Configuration
# end of LCD and Touch Panel

#
# ESP NETIF Adapter
#
CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL=120
CONFIG_ESP_NETIF_TCPIP_LWIP=y
# CONFIG_ESP_NETIF_LOOPBACK is not set
CONFIG_ESP_NETIF_TCPIP_ADAPTER_COMPATIBLE_LAYER=y
# end of ESP NETIF Adapter

#
# PHY
#
CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y
# CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION is not set
CONFIG_ESP_PHY_MAX_WIFI_TX_POWER=20
CONFIG_ESP_PHY_MAX_TX_POWER=20
CONFIG_ESP_PHY_REDUCE_TX_POWER=y
# end of PHY

#
# Power Management
#
# CONFIG_PM_ENABLE is not set
# end of Power Management

#
# ESP System Settings
#
# CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT is not set
CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y
# CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT is not set
# CONFIG_ESP_SYSTEM_PANIC_GDBSTUB is not set
# CONFIG_ESP_SYSTEM_GDBSTUB_RUNTIME is not set

#
# Memory protection
#
# end of Memory protection

CONFIG_ESP_SYSTEM_EVENT_QUEUE_SIZE=32
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=2304
CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584
CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y
# CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1 is not set
# CONFIG_ESP_MAIN_TASK_AFFINITY_NO_AFFINITY is not set
CONFIG_ESP_MAIN_TASK_AFFINITY=0x0
CONFIG_ESP_MINIMAL_SHARED_STACK_SIZE=2048
# CONFIG_ESP_CONSOLE_UART_DEFAULT is not set
CONFIG_ESP_CONSOLE_UART_CUSTOM=y
# CONFIG_ESP_CONSOLE_NONE is not set
CONFIG_ESP_CONSOLE_UART=y
CONFIG_ESP_CONSOLE_MULTIPLE_UART=y
CONFIG_ESP_CONSOLE_UART_CUSTOM_NUM_0=y
# CONFIG_ESP_CONSOLE_UART_CUSTOM_NUM_1 is not set
CONFIG_ESP_CONSOLE_UART_NUM=0
CONFIG_ESP_CONSOLE_UART_TX_GPIO=1
CONFIG_ESP_CONSOLE_UART_RX_GPIO=3
CONFIG_ESP_CONSOLE_UART_BAUDRATE=921600
CONFIG_ESP_INT_WDT=y
CONFIG_ESP_INT_WDT_TIMEOUT_MS=300
CONFIG_ESP_INT_WDT_CHECK_CPU1=y
CONFIG_ESP_TASK_WDT=y
# CONFIG_ESP_TASK_WDT_PANIC is not set
CONFIG_ESP_TASK_WDT_TIMEOUT_S=30
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
# CONFIG_ESP_PANIC_HANDLER_IRAM is not set
# CONFIG_ESP_SYSTEM_CHECK_INT_LEVEL_5 is not set
CONFIG_ESP_SYSTEM_CHECK_INT_LEVEL_4=y
# end of ESP System Settings

#
# High resolution timer (esp_timer)
#
# CONFIG_ESP_TIMER_PROFILING is not set
CONFIG_ESP_TIME_FUNCS_USE_RTC_TIMER=y
CONFIG_ESP_TIME_FUNCS_USE_ESP_TIMER=y
CONFIG_ESP_TIMER_TASK_STACK_SIZE=3584
CONFIG_ESP_TIMER_INTERRUPT_LEVEL=1
# CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD is not set
# CONFIG_ESP_TIMER_IMPL_FRC2 is not set
CONFIG_ESP_TIMER_IMPL_TG0_LAC=y
# end of High resolution timer (esp_timer)

#
# Wi-Fi
#
CONFIG_ESP32_WIFI_ENABLED=y
CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM=10
CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=32
# CONFIG_ESP32_WIFI_STATIC_TX_BUFFER is not set
CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER=y
CONFIG_ESP32_WIFI_TX_BUFFER_TYPE=1
CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM=32
CONFIG_ESP32_WIFI_CSI_ENABLED=y
# CONFIG_ESP32_WIFI_AMPDU_TX_ENABLED is not set
# CONFIG_ESP32_WIFI_AMPDU_RX_ENABLED is not set
CONFIG_ESP32_WIFI_NVS_ENABLED=y
CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_0=y
# CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_1 is not set
CONFIG_ESP32_WIFI_SOFTAP_BEACON_MAX_LEN=752
CONFIG_ESP32_WIFI_MGMT_SBUF_NUM=32
CONFIG_ESP32_WIFI_IRAM_OPT=y
CONFIG_ESP32_WIFI_RX_IRAM_OPT=y
CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE=y
# CONFIG_ESP_WIFI_SLP_IRAM_OPT is not set
# CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE is not set
# CONFIG_ESP_WIFI_GMAC_SUPPORT is not set
# end of Wi-Fi

#
# Core dump
#
# CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH is not set
# CONFIG_ESP_COREDUMP_ENABLE_TO_UART is not set
CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y
# end of Core dump

#
# FAT Filesystem support
#
# CONFIG_FATFS_CODEPAGE_DYNAMIC is not set
CONFIG_FATFS_CODEPAGE_437=y
# CONFIG_FATFS_CODEPAGE_720 is not set
# CONFIG_FATFS_CODEPAGE_737 is not set
# CONFIG_FATFS_CODEPAGE_771 is not set
# CONFIG_FATFS_CODEPAGE_775 is not set
# CONFIG_FATFS_CODEPAGE_850 is not set
# CONFIG_FATFS_CODEPAGE_852 is not set
# CONFIG_FATFS_CODEPAGE_855 is not set
# CONFIG_FATFS_CODEPAGE_857 is not set
# CONFIG_FATFS_CODEPAGE_860 is not set
# CONFIG_FATFS_CODEPAGE_861 is not set
# CONFIG_FATFS_CODEPAGE_862 is not set
# CONFIG_FATFS_CODEPAGE_863 is not set
# CONFIG_FATFS_CODEPAGE_864 is not set
# CONFIG_FATFS_CODEPAGE_865 is not set
# CONFIG_FATFS_CODEPAGE_866 is not set
# CONFIG_FATFS_CODEPAGE_869 is not set
# CONFIG_FATFS_CODEPAGE_932 is not set
# CONFIG_FATFS_CODEPAGE_936 is not set
# CONFIG_FATFS_CODEPAGE_949 is not set
# CONFIG_FATFS_CODEPAGE_950 is not set
CONFIG_FATFS_CODEPAGE=437
CONFIG_FATFS_LFN_NONE=y
# CONFIG_FATFS_LFN_HEAP is not set
# CONFIG_FATFS_LFN_STACK is not set
CONFIG_FATFS_FS_LOCK=0
CONFIG_FATFS_TIMEOUT_MS=10000
CONFIG_FATFS_PER_FILE_CACHE=y
# CONFIG_FATFS_USE_FASTSEEK is not set
# end of FAT Filesystem support

#
# Modbus configuration
#
CONFIG_FMB_COMM_MODE_TCP_EN=y
CONFIG_FMB_TCP_PORT_DEFAULT=502
CONFIG_FMB_TCP_PORT_MAX_CONN=5
CONFIG_FMB_TCP_CONNECTION_TOUT_SEC=20
CONFIG_FMB_COMM_MODE_RTU_EN=y
CONFIG_FMB_COMM_MODE_ASCII_EN=y
CONFIG_FMB_MASTER_TIMEOUT_MS_RESPOND=150
CONFIG_FMB_MASTER_DELAY_MS_CONVERT=200
CONFIG_FMB_QUEUE_LENGTH=20
CONFIG_FMB_PORT_TASK_STACK_SIZE=4096
CONFIG_FMB_SERIAL_BUF_SIZE=256
CONFIG_FMB_SERIAL_ASCII_BITS_PER_SYMB=8
CONFIG_FMB_SERIAL_ASCII_TIMEOUT_RESPOND_MS=1000
CONFIG_FMB_PORT_TASK_PRIO=10
# CONFIG_FMB_PORT_TASK_AFFINITY_NO_AFFINITY is not set
CONFIG_FMB_PORT_TASK_AFFINITY_CPU0=y
# CONFIG_FMB_PORT_TASK_AFFINITY_CPU1 is not set
CONFIG_FMB_PORT_TASK_AFFINITY=0x0
CONFIG_FMB_CONTROLLER_SLAVE_ID_SUPPORT=y
CONFIG_FMB_CONTROLLER_SLAVE_ID=0x00112233
CONFIG_FMB_CONTROLLER_NOTIFY_TIMEOUT=20
CONFIG_FMB_CONTROLLER_NOTIFY_QUEUE_SIZE=20
CONFIG_FMB_CONTROLLER_STACK_SIZE=4096
CONFIG_FMB_EVENT_QUEUE_TIMEOUT=20
# CONFIG_FMB_TIMER_PORT_ENABLED is not set
CONFIG_FMB_TIMER_GROUP=0
CONFIG_FMB_TIMER_INDEX=0
CONFIG_FMB_MASTER_TIMER_GROUP=0
CONFIG_FMB_MASTER_TIMER_INDEX=0
# CONFIG_FMB_TIMER_ISR_IN_IRAM is not set
# end of Modbus configuration

#
# FreeRTOS
#
# CONFIG_FREERTOS_UNICORE is not set
CONFIG_FREERTOS_NO_AFFINITY=0x7FFFFFFF
CONFIG_FREERTOS_TICK_SUPPORT_CORETIMER=y
CONFIG_FREERTOS_CORETIMER_0=y
# CONFIG_FREERTOS_CORETIMER_1 is not set
CONFIG_FREERTOS_SYSTICK_USES_CCOUNT=y
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_ASSERT_ON_UNTESTED_FUNCTION=y
# CONFIG_FREERTOS_CHECK_STACKOVERFLOW_NONE is not set
# CONFIG_FREERTOS_CHECK_STACKOVERFLOW_PTRVAL is not set
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y
# CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK is not set
CONFIG_FREERTOS_INTERRUPT_BACKTRACE=y
CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS=1
CONFIG_FREERTOS_ASSERT_FAIL_ABORT=y
# CONFIG_FREERTOS_ASSERT_FAIL_PRINT_CONTINUE is not set
# CONFIG_FREERTOS_ASSERT_DISABLE is not set
CONFIG_FREERTOS_IDLE_TASK_STACKSIZE=1536
CONFIG_FREERTOS_ISR_STACKSIZE=1536
# CONFIG_FREERTOS_LEGACY_HOOKS is not set
CONFIG_FREERTOS_MAX_TASK_NAME_LEN=16
CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y
# CONFIG_FREERTOS_ENABLE_STATIC_TASK_CLEAN_UP is not set
CONFIG_FREERTOS_TIMER_TASK_PRIORITY=1
CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2048
CONFIG_FREERTOS_TIMER_QUEUE_LENGTH=10
CONFIG_FREERTOS_QUEUE_REGISTRY_SIZE=0
# CONFIG_FREERTOS_USE_TRACE_FACILITY is not set
# CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS is not set
CONFIG_FREERTOS_CHECK_MUTEX_GIVEN_BY_OWNER=y
# CONFIG_FREERTOS_CHECK_PORT_CRITICAL_COMPLIANCE is not set
# CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH is not set
CONFIG_FREERTOS_DEBUG_OCDAWARE=y
# CONFIG_FREERTOS_FPU_IN_ISR is not set
CONFIG_FREERTOS_ENABLE_TASK_SNAPSHOT=y
# CONFIG_FREERTOS_PLACE_SNAPSHOT_FUNS_INTO_FLASH is not set
# end of FreeRTOS

#
# Hardware Abstraction Layer (HAL) and Low Level (LL)
#
CONFIG_HAL_ASSERTION_EQUALS_SYSTEM=y
# CONFIG_HAL_ASSERTION_DISABLE is not set
# CONFIG_HAL_ASSERTION_SILIENT is not set
# CONFIG_HAL_ASSERTION_ENABLE is not set
CONFIG_HAL_DEFAULT_ASSERTION_LEVEL=2
# end of Hardware Abstraction Layer (HAL) and Low Level (LL)

#
# Heap memory debugging
#
CONFIG_HEAP_POISONING_DISABLED=y
# CONFIG_HEAP_POISONING_LIGHT is not set
# CONFIG_HEAP_POISONING_COMPREHENSIVE is not set
CONFIG_HEAP_TRACING_OFF=y
# CONFIG_HEAP_TRACING_STANDALONE is not set
# CONFIG_HEAP_TRACING_TOHOST is not set
# CONFIG_HEAP_ABORT_WHEN_ALLOCATION_FAILS is not set
# end of Heap memory debugging

#
# jsmn
#
# CONFIG_JSMN_PARENT_LINKS is not set
# CONFIG_JSMN_STRICT is not set
# end of jsmn

#
# libsodium
#
# end of libsodium

#
# Log output
#
# CONFIG_LOG_DEFAULT_LEVEL_NONE is not set
# CONFIG_LOG_DEFAULT_LEVEL_ERROR is not set
# CONFIG_LOG_DEFAULT_LEVEL_WARN is not set
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
# CONFIG_LOG_DEFAULT_LEVEL_DEBUG is not set
# CONFIG_LOG_DEFAULT_LEVEL_VERBOSE is not set
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT=y
# CONFIG_LOG_MAXIMUM_LEVEL_DEBUG is not set
# CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE is not set
CONFIG_LOG_MAXIMUM_LEVEL=3
CONFIG_LOG_COLORS=y
CONFIG_LOG_TIMESTAMP_SOURCE_RTOS=y
# CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM is not set
# end of Log output

#
# LWIP
#
CONFIG_LWIP_LOCAL_HOSTNAME="espressif"
# CONFIG_LWIP_NETIF_API is not set
# CONFIG_LWIP_TCPIP_CORE_LOCKING is not set
CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES=y
# CONFIG_LWIP_L2_TO_L3_COPY is not set
# CONFIG_LWIP_IRAM_OPTIMIZATION is not set
CONFIG_LWIP_TIMERS_ONDEMAND=y
CONFIG_LWIP_MAX_SOCKETS=10
# CONFIG_LWIP_USE_ONLY_LWIP_SELECT is not set
# CONFIG_LWIP_SO_LINGER is not set
CONFIG_LWIP_SO_REUSE=y
CONFIG_LWIP_SO_REUSE_RXTOALL=y
# CONFIG_LWIP_SO_RCVBUF is not set
# CONFIG_LWIP_NETBUF_RECVINFO is not set
CONFIG_LWIP_IP4_FRAG=y
CONFIG_LWIP_IP6_FRAG=y
# CONFIG_LWIP_IP4_REASSEMBLY is not set
# CONFIG_LWIP_IP6_REASSEMBLY is not set
# CONFIG_LWIP_IP_FORWARD is not set
# CONFIG_LWIP_STATS is not set
# CONFIG_LWIP_ETHARP_TRUST_IP_MAC is not set
CONFIG_LWIP_ESP_GRATUITOUS_ARP=y
CONFIG_LWIP_GARP_TMR_INTERVAL=60
CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=32
CONFIG_LWIP_DHCP_DOES_ARP_CHECK=y
# CONFIG_LWIP_DHCP_DISABLE_CLIENT_ID is not set
# CONFIG_LWIP_DHCP_RESTORE_LAST_IP is not set
CONFIG_LWIP_DHCP_OPTIONS_LEN=68

#
# DHCP server
#
CONFIG_LWIP_DHCPS=y
CONFIG_LWIP_DHCPS_LEASE_UNIT=60
CONFIG_LWIP_DHCPS_MAX_STATION_NUM=8
# end of DHCP server

# CONFIG_LWIP_AUTOIP is not set
CONFIG_LWIP_IPV6=y
# CONFIG_LWIP_IPV6_AUTOCONFIG is not set
CONFIG_LWIP_IPV6_NUM_ADDRESSES=3
# CONFIG_LWIP_IPV6_FORWARD is not set
# CONFIG_LWIP_NETIF_STATUS_CALLBACK is not set
CONFIG_LWIP_NETIF_LOOPBACK=y
CONFIG_LWIP_LOOPBACK_MAX_PBUFS=8

#
# TCP
#
CONFIG_LWIP_MAX_ACTIVE_TCP=16
CONFIG_LWIP_MAX_LISTENING_TCP=16
CONFIG_LWIP_TCP_HIGH_SPEED_RETRANSMISSION=y
CONFIG_LWIP_TCP_MAXRTX=12
CONFIG_LWIP_TCP_SYNMAXRTX=12
CONFIG_LWIP_TCP_MSS=1440
CONFIG_LWIP_TCP_TMR_INTERVAL=250
CONFIG_LWIP_TCP_MSL=60000
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5744
CONFIG_LWIP_TCP_WND_DEFAULT=5744
CONFIG_LWIP_TCP_RECVMBOX_SIZE=6
CONFIG_LWIP_TCP_QUEUE_OOSEQ=y
# CONFIG_LWIP_TCP_SACK_OUT is not set
# CONFIG_LWIP_TCP_KEEP_CONNECTION_WHEN_IP_CHANGES is not set
CONFIG_LWIP_TCP_OVERSIZE_MSS=y
# CONFIG_LWIP_TCP_OVERSIZE_QUARTER_MSS is not set
# CONFIG_LWIP_TCP_OVERSIZE_DISABLE is not set
CONFIG_LWIP_TCP_RTO_TIME=1500
# end of TCP

#
# UDP
#
CONFIG_LWIP_MAX_UDP_PCBS=16
CONFIG_LWIP_UDP_RECVMBOX_SIZE=6
# end of UDP

#
# Checksums
#
# CONFIG_LWIP_CHECKSUM_CHECK_IP is not set
# CONFIG_LWIP_CHECKSUM_CHECK_UDP is not set
CONFIG_LWIP_CHECKSUM_CHECK_ICMP=y
# end of Checksums

CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=3072
CONFIG_LWIP_TCPIP_TASK_AFFINITY_NO_AFFINITY=y
# CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0 is not set
# CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1 is not set
CONFIG_LWIP_TCPIP_TASK_AFFINITY=0x7FFFFFFF
# CONFIG_LWIP_PPP_SUPPORT is not set
CONFIG_LWIP_IPV6_MEMP_NUM_ND6_QUEUE=3
CONFIG_LWIP_IPV6_ND6_NUM_NEIGHBORS=5
# CONFIG_LWIP_SLIP_SUPPORT is not set

#
# ICMP
#
CONFIG_LWIP_ICMP=y
# CONFIG_LWIP_MULTICAST_PING is not set
# CONFIG_LWIP_BROADCAST_PING is not set
# end of ICMP

#
# LWIP RAW API
#
CONFIG_LWIP_MAX_RAW_PCBS=16
# end of LWIP RAW API

#
# SNTP
#
CONFIG_LWIP_SNTP_MAX_SERVERS=1
# CONFIG_LWIP_DHCP_GET_NTP_SRV is not set
CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000
# end of SNTP

CONFIG_LWIP_ESP_LWIP_ASSERT=y

#
# Hooks
#
# CONFIG_LWIP_HOOK_TCP_ISN_NONE is not set
CONFIG_LWIP_HOOK_TCP_ISN_DEFAULT=y
# CONFIG_LWIP_HOOK_TCP_ISN_CUSTOM is not set
CONFIG_LWIP_HOOK_IP6_ROUTE_NONE=y
# CONFIG_LWIP_HOOK_IP6_ROUTE_DEFAULT is not set
# CONFIG_LWIP_HOOK_IP6_ROUTE_CUSTOM is not set
CONFIG_LWIP_HOOK_ND6_GET_GW_NONE=y
# CONFIG_LWIP_HOOK_ND6_GET_GW_DEFAULT is not set
# CONFIG_LWIP_HOOK_ND6_GET_GW_CUSTOM is not set
CONFIG_LWIP_HOOK_NETCONN_EXT_RESOLVE_NONE=y
# CONFIG_LWIP_HOOK_NETCONN_EXT_RESOLVE_DEFAULT is not set
# CONFIG_LWIP_HOOK_NETCONN_EXT_RESOLVE_CUSTOM is not set
# end of Hooks

# CONFIG_LWIP_DEBUG is not set
# end of LWIP

#
# mbedTLS
#
CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y
# CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC is not set
# CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC is not set
CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
# CONFIG_MBEDTLS_DYNAMIC_BUFFER is not set
# CONFIG_MBEDTLS_DEBUG is not set

#
# Certificate Bundle
#
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN is not set
# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE is not set
# CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE is not set
# end of Certificate Bundle

# CONFIG_MBEDTLS_ECP_RESTARTABLE is not set
# CONFIG_MBEDTLS_CMAC_C is not set
CONFIG_MBEDTLS_HARDWARE_AES=y
CONFIG_MBEDTLS_HARDWARE_MPI=y
CONFIG_MBEDTLS_HARDWARE_SHA=y
CONFIG_MBEDTLS_ROM_MD5=y
# CONFIG_MBEDTLS_ATCA_HW_ECDSA_SIGN is not set
# CONFIG_MBEDTLS_ATCA_HW_ECDSA_VERIFY is not set
CONFIG_MBEDTLS_HAVE_TIME=y
# CONFIG_MBEDTLS_HAVE_TIME_DATE is not set
CONFIG_MBEDTLS_ECDSA_DETERMINISTIC=y
CONFIG_MBEDTLS_SHA512_C=y
CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT=y
# CONFIG_MBEDTLS_TLS_SERVER_ONLY is not set
# CONFIG_MBEDTLS_TLS_CLIENT_ONLY is not set
# CONFIG_MBEDTLS_TLS_DISABLED is not set
CONFIG_MBEDTLS_TLS_SERVER=y
CONFIG_MBEDTLS_TLS_CLIENT=y
CONFIG_MBEDTLS_TLS_ENABLED=y

#
# TLS Key Exchange Methods
#
# CONFIG_MBEDTLS_PSK_MODES is not set
CONFIG_MBEDTLS_KEY_EXCHANGE_RSA=y
CONFIG_MBEDTLS_KEY_EXCHANGE_DHE_RSA=y
CONFIG_MBEDTLS_KEY_EXCHANGE_ELLIPTIC_CURVE=y
CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA=y
CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA=y
CONFIG_MBEDTLS_KEY_EXCHANGE_ECDH_ECDSA=y
CONFIG_MBEDTLS_KEY_EXCHANGE_ECDH_RSA=y
# end of TLS Key Exchange Methods

CONFIG_MBEDTLS_SSL_RENEGOTIATION=y
# CONFIG_MBEDTLS_SSL_PROTO_SSL3 is not set
CONFIG_MBEDTLS_SSL_PROTO_TLS1=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_1=y
CONFIG_MBEDTLS_SSL_PROTO_TLS1_2=y
# CONFIG_MBEDTLS_SSL_PROTO_GMTSSL1_1 is not set
# CONFIG_MBEDTLS_SSL_PROTO_DTLS is not set
CONFIG_MBEDTLS_SSL_ALPN=y
CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS=y
CONFIG_MBEDTLS_X509_CHECK_KEY_USAGE=y
CONFIG_MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE=y
CONFIG_MBEDTLS_SERVER_SSL_SESSION_TICKETS=y

#
# Symmetric Ciphers
#
CONFIG_MBEDTLS_AES_C=y
# CONFIG_MBEDTLS_CAMELLIA_C is not set
# CONFIG_MBEDTLS_DES_C is not set
CONFIG_MBEDTLS_RC4_DISABLED=y
# CONFIG_MBEDTLS_RC4_ENABLED_NO_DEFAULT is not set
# CONFIG_MBEDTLS_RC4_ENABLED is not set
# CONFIG_MBEDTLS_BLOWFISH_C is not set
# CONFIG_MBEDTLS_XTEA_C is not set
CONFIG_MBEDTLS_CCM_C=y
CONFIG_MBEDTLS_GCM_C=y
# CONFIG_MBEDTLS_NIST_KW_C is not set
# end of Symmetric Ciphers

# CONFIG_MBEDTLS_RIPEMD160_C is not set

#
# Certificates
#
CONFIG_MBEDTLS_PEM_PARSE_C=y
CONFIG_MBEDTLS_PEM_WRITE_C=y
CONFIG_MBEDTLS_X509_CRL_PARSE_C=y
CONFIG_MBEDTLS_X509_CSR_PARSE_C=y
# end of Certificates

CONFIG_MBEDTLS_ECP_C=y
CONFIG_MBEDTLS_ECDH_C=y
CONFIG_MBEDTLS_ECDSA_C=y
# CONFIG_MBEDTLS_ECJPAKE_C is not set
CONFIG_MBEDTLS_ECP_DP_SECP192R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_SECP224R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_SECP384R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_SECP521R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_SECP192K1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_SECP224K1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_SECP256K1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_BP256R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_BP384R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_BP512R1_ENABLED=y
CONFIG_MBEDTLS_ECP_DP_CURVE25519_ENABLED=y
CONFIG_MBEDTLS_ECP_NIST_OPTIM=y
# CONFIG_MBEDTLS_POLY1305_C is not set
# CONFIG_MBEDTLS_CHACHA20_C is not set
# CONFIG_MBEDTLS_HKDF_C is not set
# CONFIG_MBEDTLS_THREADING_C is not set
# CONFIG_MBEDTLS_LARGE_KEY_SOFTWARE_MPI is not set
# CONFIG_MBEDTLS_SECURITY_RISKS is not set
# end of mbedTLS

#
# mDNS
#
CONFIG_MDNS_MAX_SERVICES=10
CONFIG_MDNS_TASK_PRIORITY=1
CONFIG_MDNS_TASK_STACK_SIZE=4096
# CONFIG_MDNS_TASK_AFFINITY_NO_AFFINITY is not set
CONFIG_MDNS_TASK_AFFINITY_CPU0=y
# CONFIG_MDNS_TASK_AFFINITY_CPU1 is not set
CONFIG_MDNS_TASK_AFFINITY=0x0
CONFIG_MDNS_SERVICE_ADD_TIMEOUT_MS=2000
# CONFIG_MDNS_STRICT_MODE is not set
CONFIG_MDNS_TIMER_PERIOD_MS=100
# CONFIG_MDNS_NETWORKING_SOCKET is not set
CONFIG_MDNS_MULTIPLE_INSTANCE=y
# end of mDNS

#
# ESP-MQTT Configurations
#
CONFIG_MQTT_PROTOCOL_311=y
CONFIG_MQTT_TRANSPORT_SSL=y
CONFIG_MQTT_TRANSPORT_WEBSOCKET=y
CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE=y
# CONFIG_MQTT_MSG_ID_INCREMENTAL is not set
# CONFIG_MQTT_SKIP_PUBLISH_IF_DISCONNECTED is not set
# CONFIG_MQTT_REPORT_DELETED_MESSAGES is not set
# CONFIG_MQTT_USE_CUSTOM_CONFIG is not set
# CONFIG_MQTT_TASK_CORE_SELECTION_ENABLED is not set
# CONFIG_MQTT_CUSTOM_OUTBOX is not set
# end of ESP-MQTT Configurations

#
# Newlib
#
CONFIG_NEWLIB_STDOUT_LINE_ENDING_CRLF=y
# CONFIG_NEWLIB_STDOUT_LINE_ENDING_LF is not set
# CONFIG_NEWLIB_STDOUT_LINE_ENDING_CR is not set
# CONFIG_NEWLIB_STDIN_LINE_ENDING_CRLF is not set
# CONFIG_NEWLIB_STDIN_LINE_ENDING_LF is not set
CONFIG_NEWLIB_STDIN_LINE_ENDING_CR=y
# CONFIG_NEWLIB_NANO_FORMAT is not set
# end of Newlib

#
# NVS
#
# end of NVS

#
# OpenSSL
#
# CONFIG_OPENSSL_DEBUG is not set
CONFIG_OPENSSL_ERROR_STACK=y
# CONFIG_OPENSSL_ASSERT_DO_NOTHING is not set
CONFIG_OPENSSL_ASSERT_EXIT=y
# end of OpenSSL

#
# OpenThread
#
# CONFIG_OPENTHREAD_ENABLED is not set
# end of OpenThread

#
# PThreads
#
CONFIG_PTHREAD_TASK_PRIO_DEFAULT=5
CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=3072
CONFIG_PTHREAD_STACK_MIN=768
CONFIG_PTHREAD_DEFAULT_CORE_NO_AFFINITY=y
# CONFIG_PTHREAD_DEFAULT_CORE_0 is not set
# CONFIG_PTHREAD_DEFAULT_CORE_1 is not set
CONFIG_PTHREAD_TASK_CORE_DEFAULT=-1
CONFIG_PTHREAD_TASK_NAME_DEFAULT="pthread"
# end of PThreads

#
# SPI Flash driver
#
# CONFIG_SPI_FLASH_VERIFY_WRITE is not set
# CONFIG_SPI_FLASH_ENABLE_COUNTERS is not set
CONFIG_SPI_FLASH_ROM_DRIVER_PATCH=y
CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS=y
# CONFIG_SPI_FLASH_DANGEROUS_WRITE_FAILS is not set
# CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED is not set
# CONFIG_SPI_FLASH_USE_LEGACY_IMPL is not set
# CONFIG_SPI_FLASH_SHARE_SPI1_BUS is not set
# CONFIG_SPI_FLASH_BYPASS_BLOCK_ERASE is not set
CONFIG_SPI_FLASH_YIELD_DURING_ERASE=y
CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS=20
CONFIG_SPI_FLASH_ERASE_YIELD_TICKS=1
CONFIG_SPI_FLASH_WRITE_CHUNK_SIZE=8192
# CONFIG_SPI_FLASH_SIZE_OVERRIDE is not set
# CONFIG_SPI_FLASH_CHECK_ERASE_TIMEOUT_DISABLED is not set
# CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST is not set

#
# Auto-detect flash chips
#
CONFIG_SPI_FLASH_SUPPORT_ISSI_CHIP=y
CONFIG_SPI_FLASH_SUPPORT_MXIC_CHIP=y
CONFIG_SPI_FLASH_SUPPORT_GD_CHIP=y
CONFIG_SPI_FLASH_SUPPORT_WINBOND_CHIP=y
# end of Auto-detect flash chips

CONFIG_SPI_FLASH_ENABLE_ENCRYPTED_READ_WRITE=y
# end of SPI Flash driver

#
# SPIFFS Configuration
#
CONFIG_SPIFFS_MAX_PARTITIONS=3

#
# SPIFFS Cache Configuration
#
CONFIG_SPIFFS_CACHE=y
CONFIG_SPIFFS_CACHE_WR=y
# CONFIG_SPIFFS_CACHE_STATS is not set
# end of SPIFFS Cache Configuration

CONFIG_SPIFFS_PAGE_CHECK=y
CONFIG_SPIFFS_GC_MAX_RUNS=10
# CONFIG_SPIFFS_GC_STATS is not set
CONFIG_SPIFFS_PAGE_SIZE=256
CONFIG_SPIFFS_OBJ_NAME_LEN=32
# CONFIG_SPIFFS_FOLLOW_SYMLINKS is not set
CONFIG_SPIFFS_USE_MAGIC=y
CONFIG_SPIFFS_USE_MAGIC_LENGTH=y
CONFIG_SPIFFS_META_LENGTH=4
CONFIG_SPIFFS_USE_MTIME=y

#
# Debug Configuration
#
# CONFIG_SPIFFS_DBG is not set
# CONFIG_SPIFFS_API_DBG is not set
# CONFIG_SPIFFS_GC_DBG is not set
# CONFIG_SPIFFS_CACHE_DBG is not set
# CONFIG_SPIFFS_CHECK_DBG is not set
# CONFIG_SPIFFS_TEST_VISUALISATION is not set
# end of Debug Configuration
# end of SPIFFS Configuration

#
# TCP Transport
#

#
# Websocket
#
CONFIG_WS_TRANSPORT=y
CONFIG_WS_BUFFER_SIZE=1024
# end of Websocket
# end of TCP Transport

#
# Unity unit testing library
#
CONFIG_UNITY_ENABLE_FLOAT=y
CONFIG_UNITY_ENABLE_DOUBLE=y
# CONFIG_UNITY_ENABLE_64BIT is not set
# CONFIG_UNITY_ENABLE_COLOR is not set
CONFIG_UNITY_ENABLE_IDF_TEST_RUNNER=y
# CONFIG_UNITY_ENABLE_FIXTURE is not set
# CONFIG_UNITY_ENABLE_BACKTRACE_ON_FAIL is not set
# end of Unity unit testing library

#
# Virtual file system
#
CONFIG_VFS_SUPPORT_IO=y
CONFIG_VFS_SUPPORT_DIR=y
CONFIG_VFS_SUPPORT_SELECT=y
CONFIG_VFS_SUPPRESS_SELECT_DEBUG_OUTPUT=y
CONFIG_VFS_SUPPORT_TERMIOS=y

#
# Host File System I/O (Semihosting)
#
CONFIG_VFS_SEMIHOSTFS_MAX_MOUNT_POINTS=1
CONFIG_VFS_SEMIHOSTFS_HOST_PATH_MAX_LEN=128
# end of Host File System I/O (Semihosting)
# end of Virtual file system

#
# Wear Levelling
#
# CONFIG_WL_SECTOR_SIZE_512 is not set
CONFIG_WL_SECTOR_SIZE_4096=y
CONFIG_WL_SECTOR_SIZE=4096
# end of Wear Levelling

#
# Wi-Fi Provisioning Manager
#
CONFIG_WIFI_PROV_SCAN_MAX_ENTRIES=16
CONFIG_WIFI_PROV_AUTOSTOP_TIMEOUT=30
# end of Wi-Fi Provisioning Manager

#
# Supplicant
#
CONFIG_WPA_MBEDTLS_CRYPTO=y
# CONFIG_WPA_WAPI_PSK is not set
# CONFIG_WPA_SUITE_B_192 is not set
# CONFIG_WPA_DEBUG_PRINT is not set
# CONFIG_WPA_TESTING_OPTIONS is not set
# CONFIG_WPA_WPS_STRICT is not set
# CONFIG_WPA_11KV_SUPPORT is not set
# end of Supplicant
# end of Component config

#
# Compatibility options
#
# CONFIG_LEGACY_INCLUDE_COMMON_HEADERS is not set
# end of Compatibility options

# Deprecated options for backward compatibility
CONFIG_TOOLPREFIX="xtensa-esp32-elf-"
# CONFIG_LOG_BOOTLOADER_LEVEL_NONE is not set
# CONFIG_LOG_BOOTLOADER_LEVEL_ERROR is not set
# CONFIG_LOG_BOOTLOADER_LEVEL_WARN is not set
CONFIG_LOG_BOOTLOADER_LEVEL_INFO=y
# CONFIG_LOG_BOOTLOADER_LEVEL_DEBUG is not set
# CONFIG_LOG_BOOTLOADER_LEVEL_VERBOSE is not set
CONFIG_LOG_BOOTLOADER_LEVEL=3
# CONFIG_APP_ROLLBACK_ENABLE is not set
# CONFIG_FLASH_ENCRYPTION_ENABLED is not set
CONFIG_FLASHMODE_QIO=y
# CONFIG_FLASHMODE_QOUT is not set
# CONFIG_FLASHMODE_DIO is not set
# CONFIG_FLASHMODE_DOUT is not set
# CONFIG_MONITOR_BAUD_9600B is not set
# CONFIG_MONITOR_BAUD_57600B is not set
# CONFIG_MONITOR_BAUD_115200B is not set
# CONFIG_MONITOR_BAUD_230400B is not set
CONFIG_MONITOR_BAUD_921600B=y
# CONFIG_MONITOR_BAUD_2MB is not set
# CONFIG_MONITOR_BAUD_OTHER is not set
CONFIG_MONITOR_BAUD_OTHER_VAL=115200
CONFIG_MONITOR_BAUD=921600
# CONFIG_COMPILER_OPTIMIZATION_LEVEL_DEBUG is not set
# CONFIG_COMPILER_OPTIMIZATION_LEVEL_RELEASE is not set
CONFIG_OPTIMIZATION_ASSERTIONS_ENABLED=y
# CONFIG_OPTIMIZATION_ASSERTIONS_SILENT is not set
# CONFIG_OPTIMIZATION_ASSERTIONS_DISABLED is not set
CONFIG_OPTIMIZATION_ASSERTION_LEVEL=2
# CONFIG_CXX_EXCEPTIONS is not set
CONFIG_STACK_CHECK_NONE=y
# CONFIG_STACK_CHECK_NORM is not set
# CONFIG_STACK_CHECK_STRONG is not set
# CONFIG_STACK_CHECK_ALL is not set
# CONFIG_WARN_WRITE_STRINGS is not set
# CONFIG_DISABLE_GCC8_WARNINGS is not set
# CONFIG_ESP32_APPTRACE_DEST_TRAX is not set
CONFIG_ESP32_APPTRACE_DEST_NONE=y
CONFIG_ESP32_APPTRACE_LOCK_ENABLE=y
CONFIG_ADC2_DISABLE_DAC=y
# CONFIG_SPIRAM_SUPPORT is not set
CONFIG_TRACEMEM_RESERVE_DRAM=0x0
# CONFIG_ULP_COPROC_ENABLED is not set
CONFIG_ULP_COPROC_RESERVE_MEM=0
CONFIG_BROWNOUT_DET=y
CONFIG_BROWNOUT_DET_LVL_SEL_0=y
# CONFIG_BROWNOUT_DET_LVL_SEL_1 is not set
# CONFIG_BROWNOUT_DET_LVL_SEL_2 is not set
# CONFIG_BROWNOUT_DET_LVL_SEL_3 is not set
# CONFIG_BROWNOUT_DET_LVL_SEL_4 is not set
# CONFIG_BROWNOUT_DET_LVL_SEL_5 is not set
# CONFIG_BROWNOUT_DET_LVL_SEL_6 is not set
# CONFIG_BROWNOUT_DET_LVL_SEL_7 is not set
CONFIG_BROWNOUT_DET_LVL=0
CONFIG_ESP32_RTC_CLOCK_SOURCE_INTERNAL_RC=y
# CONFIG_ESP32_RTC_CLOCK_SOURCE_EXTERNAL_CRYSTAL is not set
# CONFIG_ESP32_RTC_CLOCK_SOURCE_EXTERNAL_OSC is not set
# CONFIG_ESP32_RTC_CLOCK_SOURCE_INTERNAL_8MD256 is not set
# CONFIG_DISABLE_BASIC_ROM_CONSOLE is not set
# CONFIG_NO_BLOBS is not set
# CONFIG_COMPATIBLE_PRE_V2_1_BOOTLOADERS is not set
# CONFIG_EVENT_LOOP_PROFILING is not set
CONFIG_POST_EVENTS_FROM_ISR=y
CONFIG_POST_EVENTS_FROM_IRAM_ISR=y
# CONFIG_TWO_UNIVERSAL_MAC_ADDRESS is not set
CONFIG_FOUR_UNIVERSAL_MAC_ADDRESS=y
CONFIG_NUMBER_OF_UNIVERSAL_MAC_ADDRESS=4
CONFIG_ESP_SYSTEM_PD_FLASH=y
# CONFIG_ESP32C3_LIGHTSLEEP_GPIO_RESET_WORKAROUND is not set
CONFIG_IPC_TASK_STACK_SIZE=1024
CONFIG_ESP32_PHY_CALIBRATION_AND_DATA_STORAGE=y
# CONFIG_ESP32_PHY_INIT_DATA_IN_PARTITION is not set
CONFIG_ESP32_PHY_MAX_WIFI_TX_POWER=20
CONFIG_ESP32_PHY_MAX_TX_POWER=20
CONFIG_ESP32_REDUCE_PHY_TX_POWER=y
# CONFIG_ESP32S2_PANIC_PRINT_HALT is not set
CONFIG_ESP32S2_PANIC_PRINT_REBOOT=y
# CONFIG_ESP32S2_PANIC_SILENT_REBOOT is not set
# CONFIG_ESP32S2_PANIC_GDBSTUB is not set
CONFIG_SYSTEM_EVENT_QUEUE_SIZE=32
CONFIG_SYSTEM_EVENT_TASK_STACK_SIZE=2304
CONFIG_MAIN_TASK_STACK_SIZE=3584
# CONFIG_CONSOLE_UART_DEFAULT is not set
CONFIG_CONSOLE_UART_CUSTOM=y
# CONFIG_ESP_CONSOLE_UART_NONE is not set
CONFIG_CONSOLE_UART=y
CONFIG_CONSOLE_UART_CUSTOM_NUM_0=y
# CONFIG_CONSOLE_UART_CUSTOM_NUM_1 is not set
CONFIG_CONSOLE_UART_NUM=0
CONFIG_CONSOLE_UART_TX_GPIO=1
CONFIG_CONSOLE_UART_RX_GPIO=3
CONFIG_CONSOLE_UART_BAUDRATE=921600
CONFIG_INT_WDT=y
CONFIG_INT_WDT_TIMEOUT_MS=300
CONFIG_INT_WDT_CHECK_CPU1=y
CONFIG_TASK_WDT=y
# CONFIG_TASK_WDT_PANIC is not set
CONFIG_TASK_WDT_TIMEOUT_S=30
CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1=y
CONFIG_TIMER_TASK_STACK_SIZE=3584
# CONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH is not set
# CONFIG_ESP32_ENABLE_COREDUMP_TO_UART is not set
CONFIG_ESP32_ENABLE_COREDUMP_TO_NONE=y
CONFIG_MB_MASTER_TIMEOUT_MS_RESPOND=150
CONFIG_MB_MASTER_DELAY_MS_CONVERT=200
CONFIG_MB_QUEUE_LENGTH=20
CONFIG_MB_SERIAL_TASK_STACK_SIZE=4096
CONFIG_MB_SERIAL_BUF_SIZE=256
CONFIG_MB_SERIAL_TASK_PRIO=10
CONFIG_MB_CONTROLLER_SLAVE_ID_SUPPORT=y
CONFIG_MB_CONTROLLER_SLAVE_ID=0x00112233
CONFIG_MB_CONTROLLER_NOTIFY_TIMEOUT=20
CONFIG_MB_CONTROLLER_NOTIFY_QUEUE_SIZE=20
CONFIG_MB_CONTROLLER_STACK_SIZE=4096
CONFIG_MB_EVENT_QUEUE_TIMEOUT=20
# CONFIG_MB_TIMER_PORT_ENABLED is not set
CONFIG_MB_TIMER_GROUP=0
CONFIG_MB_TIMER_INDEX=0
# CONFIG_ENABLE_STATIC_TASK_CLEAN_UP_HOOK is not set
CONFIG_TIMER_TASK_PRIORITY=1
CONFIG_TIMER_TASK_STACK_DEPTH=2048
CONFIG_TIMER_QUEUE_LENGTH=10
# CONFIG_L2_TO_L3_COPY is not set
# CONFIG_USE_ONLY_LWIP_SELECT is not set
CONFIG_ESP_GRATUITOUS_ARP=y
CONFIG_GARP_TMR_INTERVAL=60
CONFIG_TCPIP_RECVMBOX_SIZE=32
CONFIG_TCP_MAXRTX=12
CONFIG_TCP_SYNMAXRTX=12
CONFIG_TCP_MSS=1440
CONFIG_TCP_MSL=60000
CONFIG_TCP_SND_BUF_DEFAULT=5744
CONFIG_TCP_WND_DEFAULT=5744
CONFIG_TCP_RECVMBOX_SIZE=6
CONFIG_TCP_QUEUE_OOSEQ=y
# CONFIG_ESP_TCP_KEEP_CONNECTION_WHEN_IP_CHANGES is not set
CONFIG_TCP_OVERSIZE_MSS=y
# CONFIG_TCP_OVERSIZE_QUARTER_MSS is not set
# CONFIG_TCP_OVERSIZE_DISABLE is not set
CONFIG_UDP_RECVMBOX_SIZE=6
CONFIG_TCPIP_TASK_STACK_SIZE=3072
CONFIG_TCPIP_TASK_AFFINITY_NO_AFFINITY=y
# CONFIG_TCPIP_TASK_AFFINITY_CPU0 is not set
# CONFIG_TCPIP_TASK_AFFINITY_CPU1 is not set
CONFIG_TCPIP_TASK_AFFINITY=0x7FFFFFFF
# CONFIG_PPP_SUPPORT is not set
CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT=5
CONFIG_ESP32_PTHREAD_TASK_STACK_SIZE_DEFAULT=3072
CONFIG_ESP32_PTHREAD_STACK_MIN=768
CONFIG_ESP32_DEFAULT_PTHREAD_CORE_NO_AFFINITY=y
# CONFIG_ESP32_DEFAULT_PTHREAD_CORE_0 is not set
# CONFIG_ESP32_DEFAULT_PTHREAD_CORE_1 is not set
CONFIG_ESP32_PTHREAD_TASK_CORE_DEFAULT=-1
CONFIG_ESP32_PTHREAD_TASK_NAME_DEFAULT="pthread"
CONFIG_SPI_FLASH_WRITING_DANGEROUS_REGIONS_ABORTS=y
# CONFIG_SPI_FLASH_WRITING_DANGEROUS_REGIONS_FAILS is not set
# CONFIG_SPI_FLASH_WRITING_DANGEROUS_REGIONS_ALLOWED is not set
CONFIG_SUPPRESS_SELECT_DEBUG_OUTPUT=y
CONFIG_SUPPORT_TERMIOS=y
CONFIG_SEMIHOSTFS_MAX_MOUNT_POINTS=1
CONFIG_SEMIHOSTFS_HOST_PATH_MAX_LEN=128
# End of deprecated options
