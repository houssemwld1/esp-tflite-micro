

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
// #include "wifi.h"
// #include "thingsboard.h"
// #include "esp_gatts_api.h"
// #include "esp_bt_defs.h"
// #include "esp_bt_main.h"
#include <esp_wifi.h>
// #include "esp_bt.h"
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
// #include "bl.h"
// #include "timing.h"
// #include "log.h"
// #include "file_syst.h"
#include "wifi_sensing.h"
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
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
#include "esp_radar.h"

#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_netif.h"
#include "esp_chip_info.h"
#include "mbedtls/base64.h"

static const char *TAG = "SENSING_C";
sensingStruct sensing;
char bufDataString_data[700];
TaskHandle_t myTaskHandlecsi; // Declare a variable to hold the task handle
TaskHandle_t myTaskHandlpred; // Declare a variable to hold the task handle
TaskHandle_t myTaskHandludp;  // Declare a variable to hold the task handle

int mutexPredict = 0;
int msg_id;
float Amp[57][28];
char tmpString[700];
int cmpt_pred;
int k = 0;
int init1 = 1;
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

static TimerHandle_t g_collect_timer_handele = NULL;

void collect_timercb(void *timer)
{
    g_console_input_config.collect_number--;

    if (!g_console_input_config.collect_number)
    {
        xTimerStop(g_collect_timer_handele, 0);
        xTimerDelete(g_collect_timer_handele, 0);
        g_collect_timer_handele = NULL;
        strcpy(g_console_input_config.collect_taget, "unknown");
        return;
    }
}

int wifi_cmd_radar(int argc, char **argv)
{
    return ESP_OK;
}

void cmd_register_radar(void)
{
}

void csi_data_print_task(void *arg)
{
    // check if this works
    printf("csi_data_print_task\n");
    wifi_csi_filtered_info_t *info = NULL;
    char *buffer = malloc(5 * 1024);
    static uint32_t count = 0;
    int k_prev;
    int initCsi = 1;
    int rssiPrev;
    int initCsiFill = 1;
    float tmp;
    while (xQueueReceive(g_csi_info_queue, &info, portMAX_DELAY))
    {
        printf("xQueueReceive g_csi_info_queue \n");
        size_t len = 0;
        wifi_pkt_rx_ctrl_t *rx_ctrl = &info->rx_ctrl;
        if (!count)
        {
            // ESP_LOGI(TAG, "================ CSI RECV1 ================");
            len += sprintf(buffer + len, "type,sequence,timestamp,taget_seq,taget,mac,rssi,rate,sig_mode,mcs,bandwidth,smoothing,not_sounding,aggregation,stbc,fec_coding,sgi,noise_floor,ampdu_cnt,channel,secondary_channel,local_timestamp,ant,sig_len,rx_state,len,first_word,data\n");
        }
        // info->valid_len = 112;
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
            sprintf(bufDataString_data, "%d", info->valid_data[0]);

            for (int i = 1; i < info->valid_len; i++)
            {
                len += sprintf(buffer + len, ",%d", info->valid_data[i]);
                snprintf(bufDataString_data + strlen(bufDataString_data), sizeof(bufDataString_data) - strlen(bufDataString_data), ",%d", info->valid_data[i]);
            }
            // print some value of bufDataString_data
            ESP_LOGI(TAG, "bufDataString_data: %s", bufDataString_data);

            len += sprintf(buffer + len, "]\"\n");
            if (mutexPredict == 0)
            {
                uint8_t count_amp = 0;
                for (int i = 52; i < info->valid_len; i++)
                {
                    {
                        Amp[k][count_amp] = sqrt(info->valid_data[i] * info->valid_data[i] + info->valid_data[i + 1] * info->valid_data[i + 1]);
                        count_amp++;
                    }
                }
                for (int i = 0; i < 52; i++)
                {
                    {

                        Amp[k][count_amp] = sqrt(info->valid_data[i] * info->valid_data[i] + info->valid_data[i + 1] * info->valid_data[i + 1]);
                        count_amp++;
                    }
                }
                k++;
            }
        }

        free(buffer);
        vTaskDelete(NULL);
    }
}

void wifi_csi_raw_cb(const wifi_csi_filtered_info_t *info, void *ctx)
{
    wifi_csi_filtered_info_t *q_data = malloc(sizeof(wifi_csi_filtered_info_t) + info->valid_len);
    *q_data = *info;
    memcpy(q_data->valid_data, info->valid_data, info->valid_len);

    if (!g_csi_info_queue || xQueueSend(g_csi_info_queue, &q_data, 0) == pdFALSE)
    {
        ESP_LOGE(TAG, "Failed to send data to the queue");
        free(q_data);
    }
}

void radar_config(sensingStruct *sensing)
{ // 30:ae:a4:99:22:f4

    ESP_LOGI(TAG, "RADAR_CONFIG \n");
    wifi_radar_config_t radar_config = {
        //   .wifi_radar_cb = wifi_radar_cb,
        .wifi_csi_filtered_cb = wifi_csi_raw_cb,
        .filter_mac = {0x30, 0xae, 0xa4, 0x99, 0x22, 0xf4},
    };
    // esp_radar_init();
    esp_radar_set_config(&radar_config);
    esp_radar_start();
    sensing->radarStarted = 1;

    g_csi_info_queue = xQueueCreate(64, sizeof(void *));

    xTaskCreatePinnedToCore(csi_data_print_task, "csi_data_print", 5 * 1024, NULL, 7, &myTaskHandlecsi, 0);
}

void Sensing_routine(sensingStruct *sensing)
{

    radar_config(sensing);
    sensing->receiver_configured = 0;
    sensing->sender_configured = 1;
}
