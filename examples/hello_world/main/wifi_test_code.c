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

#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_DISCONNECTED_BIT BIT1

static bool s_reconnect = true;

static EventGroupHandle_t s_wifi_event_group;
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
// #include "main_functions.h"
#include "wifi_test_code.h"
esp_err_t ret;

static int rssi;
TaskHandle_t myTaskHandle = NULL;
nvs_handle_t nvs;
#define DEFAULT_SCAN_LIST_SIZE 20
volatile char wifi_ssids[1024] = "";

#define RECV_ESPNOW_CSI
#define CONFIG_LESS_INTERFERENCE_CHANNEL 11
#define RADER_EVALUATE_SERVER_PORT 3232
// static led_strip_t *g_strip_handle = NULL;
static xQueueHandle g_csi_info_queue = NULL;

static const char *TAG = "app_main";
csiStruct csi;
// priseStruct prise;
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

// void PRISE_setState(priseStruct *prise, PriseStateStruct newState)
// {
//     prise->state = newState;
//     switch (prise->state)
//     {
//     case IDLE:
//         ESP_LOGI(TAG, "IDLE \n");
//         break;
//     case READY:
//         ESP_LOGI(TAG, "READY \n");
//         break;
//     case INTRUSION:
//         ESP_LOGI(TAG, "INTRUSION \n");
//     case GRACE:
//         ESP_LOGI(TAG, "GRACE \n");
//         break;
//     }
// }
// void PRISE_Init(priseStruct *prise)
// {

//     LED_init(&prise->leds[0], LED0_PIN, 10, 0);
//     LED_init(&prise->leds[1], LED1_PIN, 10, 0);
// #ifndef DISABLE_KEY
//     ESP_LOGI(TAG, "*******INIT KEY*******");

//     prise->pressed_5s = 0;
//     KEY_init(&prise->key, KEY_PIN);
// #endif
//     prise->connected = 0;
//     prise->mac_test = 0;
//     prise->connectedMqtt = 0;
//     test = 0;
//     calib_test = 0;
//     prise->test_someone_th = 0;
//     prise->test_move_th = 0;
//     prise->test_buff_size = 0;
//     prise->test_outliers_number = 0;
//     prise->test_request_ssid = 0;
//     prise->tableSize = 0;
//     prise->last_wander = 0;
//     prise->last_jitter = 0;
//     prise->last_room_status = 0;
//     prise->last_human_status = 0;
//     prise->test_send_data = 0;
//     prise->test_stop_send_data = 0;
//     FIR_init(&prise, prise->filter);
//     prise->wifi_radar_cb_counter = 0;
//     prise->test_count_start = 0;
//     prise->test_send1 = 0;

//     prise->state_count = 0;
//     prise->mouvement_count = 0;
//     prise->cmp_intrusion = 0;
//     prise->cmp_ras = 0;
//     prise->cmp_send_response = 0;
//     prise->cpt_radar_dada = 0;
//     PRISE_setState(prise, IDLE);
//     printf("STATE IN PRISE INIT %d \n", prise->state);

//     sprintf(prise->topic_radar, "%s/RADAR_DADA", device_name_get());
// }

// esp_err_t led_init()
// {
// #ifdef CONFIG_IDF_TARGET_ESP32C3
// #define CONFIG_LED_STRIP_GPIO GPIO_NUM_8
// #elif CONFIG_IDF_TARGET_ESP32S3
// #define CONFIG_LED_STRIP_GPIO GPIO_NUM_48
// #else
// #define CONFIG_LED_STRIP_GPIO GPIO_NUM_18
// #endif

//     rmt_config_t config = RMT_DEFAULT_CONFIG_TX(CONFIG_LED_STRIP_GPIO, RMT_CHANNEL_0);
//     // set counter clock to 40MHz
//     config.clk_div = 2;
//     ESP_ERROR_CHECK(rmt_config(&config));
//     ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
//     led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(1, (led_strip_dev_t)config.channel);
//     g_strip_handle = led_strip_new_rmt_ws2812(&strip_config);
//     g_strip_handle->set_pixel(g_strip_handle, 0, 255, 255, 255);
//     ESP_ERROR_CHECK(g_strip_handle->refresh(g_strip_handle, 100));

//     return ESP_OK;
// }

// esp_err_t led_set(uint8_t red, uint8_t green, uint8_t blue)
// {
//     g_strip_handle->set_pixel(g_strip_handle, 0, red, green, blue);
//     g_strip_handle->refresh(g_strip_handle, 100);
//     return ESP_OK;
// }

// void print_device_info()
// {
//     esp_chip_info_t chip_info = {0};
//     const char *chip_name = NULL;
//     const esp_app_desc_t *app_desc = esp_ota_get_app_description();
//     esp_netif_ip_info_t local_ip = {0};
//     wifi_ap_record_t ap_info = {0};

//     esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &local_ip);
//     esp_chip_info(&chip_info);
//     esp_wifi_sta_get_ap_info(&ap_info);

//     switch (chip_info.model)
//     {
//     case CHIP_ESP32:
//         chip_name = "ESP32";
//         break;

//     case CHIP_ESP32S2:
//         chip_name = "ESP32-S2";
//         break;

//     case CHIP_ESP32S3:
//         chip_name = "ESP32-S3";
//         break;

//     case CHIP_ESP32C3:
//         chip_name = "ESP32-C3";
//         break;

//     default:
//         chip_name = "Unknown";
//         break;
//     }

//     sprintf(csi.csi_device_info, "DEVICE_INFO,%u,%s %s,%s,%d,%s,%s,%d,%d,%s," IPSTR ",%u\n",
//             esp_log_timestamp(), app_desc->date, app_desc->time, chip_name,
//             chip_info.revision, app_desc->version, app_desc->idf_ver,
//             heap_caps_get_total_size(MALLOC_CAP_DEFAULT), esp_get_free_heap_size(),
//             ap_info.ssid, IP2STR(&local_ip.ip), RADER_EVALUATE_SERVER_PORT);
// }

// void FIR_init(priseStruct *prise, int filter)
// {
//     int i;
//     for (i = 0; i < MAX_LEN; i++)
//     {
//         prise->vals[i] = 0;
//     }
//     prise->cursor = 0;
//     prise->output = 0;
//     prise->sum = 0;
//     prise->zeroValCounts = MAX_LEN;
//     prise->valide = 0;
//     prise->filter = 10;
// }

// void FIR_insertVal(priseStruct *prise, int newVal)
// {
//     prise->sum = prise->sum + newVal - prise->vals[prise->cursor];
//     prise->vals[prise->cursor] = newVal;
//     prise->cursor++;
//     prise->cursor = prise->cursor % MAX_LEN;
//     if (prise->cursor == 0)
//         prise->valide = 1;
//     if (prise->zeroValCounts == 0 && (abs(newVal - prise->output) > prise->filter))
//     {
//         FIR_init(prise, prise->filter);
//     }
//     else if (prise->zeroValCounts)
//         prise->zeroValCounts--;
//     if (prise->zeroValCounts == 0)
//         prise->output = prise->sum / MAX_LEN;
// }

// int FIR_getOutput(priseStruct *prise)
// {
//     return prise->output;
// }
// int FIR_getValidity(priseStruct *prise)
// {
//     return prise->valide;
// }

// void promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
// {

//     if (type != WIFI_PKT_MGMT)
//         return;

//     const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
//     const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
//     const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;
//     int rssi_val = ppkt->rx_ctrl.rssi;
//     FIR_insertVal(&prise, rssi_val);

//     int output = FIR_getOutput(&prise);
//     int validity = FIR_getValidity(&prise);
//     if (validity == 1)
//     {
//         rssi = ppkt->rx_ctrl.rssi;
//     }
// }
// void WIFI_Scan()
// {

//     uint16_t ap_count = 0;
//     uint16_t number = DEFAULT_SCAN_LIST_SIZE;
//     wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];

//     esp_err_t err = esp_wifi_scan_start(NULL, true);
//     if (err)
//     {
//         ESP_LOGE(TAG, "erreur scan");
//     }
//     else
//     {
//         ESP_LOGI(TAG, "Succes scan");
//     }
//     ret = esp_wifi_scan_get_ap_records(&number, ap_info);
//     printf("number =%d,ap_info=%s ", number, (char *)ap_info);
//     ret = esp_wifi_scan_get_ap_num(&ap_count);
//     ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
//     char string[100];
//     strcpy(wifi_ssids, "[ \n");

//     for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++)
//     {
//         ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
//         ESP_LOGI(TAG, "RSSI \t\t%d", ap_info[i].rssi);
//         sprintf(string, "{\"SSID\":\"%s\" ,\"RSSI\":\"%d\"}%s \n", ap_info[i].ssid, ap_info[i].rssi, (i < ap_count - 1) ? "," : "");
//         strcat(wifi_ssids, string);
//     }
//     strcat(wifi_ssids, "] \n");

//     strcpy(prise.list_wifi_scan, wifi_ssids);
//     printf("list wifi : %s", prise.list_wifi_scan);
// }

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

// void collect_timercb(void *timer)
// {
//     g_console_input_config.collect_number--;

//     if (!g_console_input_config.collect_number)
//     {
//         xTimerStop(g_collect_timer_handele, 0);
//         xTimerDelete(g_collect_timer_handele, 0);
//         g_collect_timer_handele = NULL;
//         strcpy(g_console_input_config.collect_taget, "unknown");
//         return;
//     }
// }

// int wifi_cmd_radar(int argc, char **argv)
// {
//     if (arg_parse(argc, argv, (void **)&radar_args) != ESP_OK)
//     {
//         arg_print_errors(stderr, radar_args.end, argv[0]);
//         return ESP_FAIL;
//     }

//     if (radar_args.train_start->count)
//     {
//         if (!radar_args.train_add->count)
//         {
//             esp_radar_train_remove();
//         }

//         esp_radar_train_start();
//         g_console_input_config.train_start = true;
//     }

//     if (radar_args.train_stop->count)
//     {
//         esp_radar_train_stop(&g_console_input_config.predict_someone_threshold,
//                              &g_console_input_config.predict_move_threshold);
//         printf("predict_someone_threshold %f \n", g_console_input_config.predict_someone_threshold);
//         printf("predict_move_threshold %f \n", g_console_input_config.predict_move_threshold);
//         g_console_input_config.predict_someone_threshold *= 1.1;
//         g_console_input_config.predict_move_threshold *= 1.1;
//         g_console_input_config.train_start = false;
//         printf("predict_someone_threshold*1.1 %f \n", g_console_input_config.predict_someone_threshold);
//         printf("predict_move_threshold*1.1 %f \n", g_console_input_config.predict_move_threshold);
//         printf("RADAR_DADA_CALIB,0,0,0,%.6f,0,0,%.6f,0\n",
//                g_console_input_config.predict_someone_threshold,
//                g_console_input_config.predict_move_threshold);
//     }

//     if (radar_args.predict_move_threshold->count)
//     {
//         g_console_input_config.predict_move_threshold = atof(radar_args.predict_move_threshold->sval[0]);
//     }

//     if (radar_args.predict_someone_threshold->count)
//     {
//         g_console_input_config.predict_someone_threshold = atof(radar_args.predict_someone_threshold->sval[0]);
//         //  printf("g_console_input_config.predict_someone_threshold %f \n", g_console_input_config.predict_someone_threshold);
//     }

//     if (radar_args.predict_buff_size->count)
//     {
//         g_console_input_config.predict_buff_size = radar_args.predict_buff_size->ival[0];
//     }

//     if (radar_args.predict_outliers_number->count)
//     {
//         g_console_input_config.predict_outliers_number = radar_args.predict_outliers_number->ival[0];
//     }

//     if (radar_args.collect_taget->count && radar_args.collect_number->count && radar_args.collect_duration->count)
//     {
//         g_console_input_config.collect_number = radar_args.collect_number->ival[0];
//         strcpy(g_console_input_config.collect_taget, radar_args.collect_taget->sval[0]);

//         if (g_collect_timer_handele)
//         {
//             xTimerStop(g_collect_timer_handele, portMAX_DELAY);
//             xTimerDelete(g_collect_timer_handele, portMAX_DELAY);
//         }

//         g_collect_timer_handele = xTimerCreate("collect", pdMS_TO_TICKS(radar_args.collect_duration->ival[0]),
//                                                true, NULL, collect_timercb);
//         xTimerStart(g_collect_timer_handele, portMAX_DELAY);
//     }

//     if (radar_args.csi_output_format->count)
//     {
//         strcpy(g_console_input_config.csi_output_format, radar_args.csi_output_format->sval[0]);
//     }

//     if (radar_args.csi_output_type->count)
//     {
//         wifi_radar_config_t radar_config = {0};
//         esp_radar_get_config(&radar_config);

//         if (!strcasecmp(radar_args.csi_output_type->sval[0], "NULL"))
//         {
//             radar_config.wifi_csi_filtered_cb = NULL;
//         }
//         else
//         {

//             void wifi_csi_raw_cb(const wifi_csi_filtered_info_t *info, void *ctx);
//             radar_config.wifi_csi_filtered_cb = wifi_csi_raw_cb;
//             strcpy(g_console_input_config.csi_output_type, radar_args.csi_output_type->sval[0]);
//         }

//         esp_radar_set_config(&radar_config);
//     }

//     if (radar_args.csi_start->count)
//     {
//         esp_radar_csi_start();
//     }

//     if (radar_args.csi_stop->count)
//     {
//         esp_radar_csi_stop();
//     }

//     return ESP_OK;
// }

// void cmd_register_radar(void)
// {
//     radar_args.train_start = arg_lit0(NULL, "train_start", "Start calibrating the 'Radar' algorithm");
//     radar_args.train_stop = arg_lit0(NULL, "train_stop", "Stop calibrating the 'Radar' algorithm");
//     radar_args.train_add = arg_lit0(NULL, "train_add", "Calibrate on the basis of saving the calibration results");

//     radar_args.predict_someone_threshold = arg_str0(NULL, "predict_someone_threshold", "<0 ~ 1.0>", "Configure the threshold for someone");
//     radar_args.predict_move_threshold = arg_str0(NULL, "predict_move_threshold", "<0 ~ 1.0>", "Configure the threshold for move");
//     radar_args.predict_buff_size = arg_int0(NULL, "predict_buff_size", "1 ~ 100", "Buffer size for filtering outliers");
//     radar_args.predict_outliers_number = arg_int0(NULL, "predict_outliers_number", "<1 ~ 100>", "The number of items in the buffer queue greater than the threshold");

//     radar_args.collect_taget = arg_str0(NULL, "collect_tagets", "<0 ~ 20>", "Type of CSI data collected");
//     radar_args.collect_number = arg_int0(NULL, "collect_number", "sequence", "Number of times CSI data was collected");
//     radar_args.collect_duration = arg_int0(NULL, "collect_duration", "duration", "Time taken to acquire one CSI data");

//     radar_args.csi_start = arg_lit0(NULL, "csi_start", "Start collecting CSI data from Wi-Fi");
//     radar_args.csi_stop = arg_lit0(NULL, "csi_stop", "Stop CSI data collection from Wi-Fi");
//     radar_args.csi_output_type = arg_str0(NULL, "csi_output_type", "<NULL, LLFT, HT-LFT, STBC-HT-LTF>", "Type of CSI data");
//     radar_args.csi_output_format = arg_str0(NULL, "csi_output_format", "<decimal, base64>", "Format of CSI data");
//     radar_args.end = arg_end(8);

//     const esp_console_cmd_t radar_cmd = {
//         .command = "radar",
//         .help = "Radar config",
//         .hint = NULL,
//         .func = &wifi_cmd_radar,
//         .argtable = &radar_args};

//     ESP_ERROR_CHECK(esp_console_cmd_register(&radar_cmd));
// }

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
    wifi_csi_filtered_info_t *q_data = malloc(sizeof(wifi_csi_filtered_info_t) + info->valid_len);
    *q_data = *info;
    memcpy(q_data->valid_data, info->valid_data, info->valid_len);

    if (!g_csi_info_queue || xQueueSend(g_csi_info_queue, &q_data, 0) == pdFALSE)
    {
        ESP_LOGW(TAG, "g_csi_info_queue full");
        free(q_data);
    }
}

// void wifi_radar_cb(const wifi_radar_info_t *info, void *ctx)
// {
//     prise.wifi_radar_cb_counter = 0;
//     prise.test_count_start = 1;
//     static float *s_buff_wander = NULL;
//     static float *s_buff_jitter = NULL;
//     free_32bit_in_clb = heap_caps_get_free_size(MALLOC_CAP_32BIT);

//     ESP_LOGI(TAG, "heap in clb  : %d", free_32bit_in_clb);

//     if (!s_buff_wander)
//     {
//         s_buff_wander = calloc(100, sizeof(float));
//     }

//     if (!s_buff_jitter)
//     {
//         s_buff_jitter = calloc(100, sizeof(float));
//     }

//     static uint32_t s_buff_count = 0;
//     uint32_t buff_max_size = g_console_input_config.predict_buff_size;
//     uint32_t buff_outliers_num = g_console_input_config.predict_outliers_number;
//     uint32_t someone_count = 0;
//     uint32_t move_count = 0;
//     bool room_status = false;
//     bool human_status = false;

//     s_buff_wander[s_buff_count % buff_max_size] = info->waveform_wander;
//     s_buff_jitter[s_buff_count % buff_max_size] = info->waveform_jitter;
//     s_buff_count++;

//     if (s_buff_count < buff_max_size)
//     {
//         return;
//     }

//     for (int i = 0; i < buff_max_size; i++)
//     {
//         if (s_buff_wander[i] > g_console_input_config.predict_someone_threshold)
//         {
//             someone_count++;
//         }

//         if (s_buff_jitter[i] > g_console_input_config.predict_move_threshold)
//         {
//             move_count++;
//         }
//     }
//     // printf("buff_outliers_num is : %d \n ", buff_outliers_num);
//     if (someone_count >= buff_outliers_num)
//     {
//         room_status = true;
//     }

//     if (move_count >= buff_outliers_num)
//     {
//         human_status = true;
//     }

//     static uint32_t s_count = 0;

//     if (!s_count)
//     {
//         ESP_LOGI(TAG, "================ RADAR RECV ================");
//         ESP_LOGI(TAG, "type,sequence,timestamp,waveform_wander,someone_threshold,someone_status,waveform_jitter,move_threshold,move_status\n");
//     }

//     char timestamp_str[32] = {0};
//     sprintf(timestamp_str, "%u", esp_log_timestamp());

//     if (ctx)
//     {
//         strncpy(timestamp_str, (char *)ctx, 31);
//     }

//     printf("RADAR_DADA,%d,%s,%.6f,%.6f,%d,%.6f,%.6f,%d\n",
//            s_count++, timestamp_str,
//            info->waveform_wander, g_console_input_config.predict_someone_threshold, room_status,
//            info->waveform_jitter, g_console_input_config.predict_move_threshold, human_status);

//     prise.presence = room_status;
//     prise.mouvement = human_status;
//     prise.jitter = info->waveform_jitter;
//     prise.wander = info->waveform_wander;
//     prise.test_send1 = 1;

//     // printf("PRESENCE %d \n", prise.presence);
//     // printf("MOUVEMENT  %d \n", prise.mouvement);
//     // printf("JITTER %.6f \n", prise.jitter);
//     // printf("WANDER %.6f \n", prise.wander);

//     sprintf(csi.csi_radar, "RADAR_DADA,%d,%s,%.6f,%.6f,%d,%.6f,%.6f,%d,%d\n",
//             s_count++, timestamp_str,
//             info->waveform_wander, g_console_input_config.predict_someone_threshold, room_status,
//             info->waveform_jitter, g_console_input_config.predict_move_threshold, human_status, rssi);

//     prise.new_room_status = room_status;
//     prise.new_human_status = human_status;
//     static uint32_t s_last_move_time = 0;
//     static uint32_t s_last_someone_time = 0;

//     if (g_console_input_config.train_start)
//     {
//         static bool led_status = false;

//         if (led_status)
//         {
//             led_set(0, 0, 0);
//         }
//         else
//         {
//             led_set(255, 255, 0);
//         }

//         led_status = !led_status;

//         return;
//     }
//     if (room_status)
//     {
//         if (human_status)
//         {
//             led_set(0, 255, 0);
//             ESP_LOGI(TAG, "Someone moved");
//             s_last_move_time = esp_log_timestamp();
//         }
//         else if (esp_log_timestamp() - s_last_move_time > 3 * 1000)
//         {
//             led_set(0, 0, 255);
//             ESP_LOGI(TAG, "Someone");
//         }

//         s_last_someone_time = esp_log_timestamp();
//     }
//     else if (esp_log_timestamp() - s_last_someone_time > 3 * 1000)
//     {
//         if (human_status)
//         {
//             led_set(255, 0, 0);
//         }
//         else
//         {
//             led_set(255, 255, 255);
//         }
//     }
// }

// void task_mqtt(priseStruct *prise)
// {

//     int msg_id = esp_mqtt_client_publish(prise->client, prise->topic_radar, csi.csi_radar, strlen(csi.csi_radar), 0, 0);
// }

// void state_process(priseStruct *prise)
// {
//     /*
//     // printf("fonction state process \n");
//     // printf("STATE :%d \n", prise->state);
//     switch (prise->state)
//     {
//     case IDLE:
//         ESP_LOGI(TAG, "IDLE \n");
//         //  printf("presence  in state machine in IDLE   %d \n", prise->presence);
//         // printf("MVT in state machine IN IDLE: %d\n", prise->mouvement);
//         if (prise->mouvement == 1 && prise->presence == 1)
//         {
//             PRISE_setState(prise, READY);
//             prise->state_count = 0;
//         }
//         break;
//     case READY:
//         ESP_LOGI(TAG, "READY \n");
//         // printf("presence  in state machine IN READY  %d \n", prise->presence);
//         // printf("MVT in state machine IN READY: %d\n", prise->mouvement);
//         if (prise->mouvement == 1 && prise->presence == 1)
//         {
//             prise->state_count++;
//             //  printf("state_count %d \n", prise->state_count);
//             if (prise->state_count > 5)
//             {
//                 PRISE_setState(prise, INTRUSION);
//                 prise->mouvement_count++;
//             }
//         }
//         else
//         {
//             PRISE_setState(prise, IDLE);
//         }
//         break;
//     case INTRUSION:
//         ESP_LOGI(TAG, "INTRUSION \n");
//         // printf("MVT in state machine IN INTRUSION : %d \n", prise->mouvement);
//         if (prise->mouvement == 0)
//         {
//             PRISE_setState(prise, GRACE);
//             prise->state_count = 0;
//         }
//         break;
//     case GRACE:
//         ESP_LOGI(TAG, "GRACE \n");
//         prise->state_count++;
//         //     printf("state_count %d \n", prise->state_count);
//         //   printf("MVT in state machine IN GRACE: %d\n", prise->mouvement);
//         if (prise->mouvement)
//         {
//             PRISE_setState(prise, INTRUSION);
//         }
//         if (prise->state_count > 20)
//         {
//             PRISE_setState(prise, IDLE);
//             prise->state_count = 0;
//         }

//         break;
//     }
//     */
// }
/*************** Routine prise ***********************/
// void prise_Task_routine(priseStruct *prise)
// {

//     int test_sub = 1;
//     int cpt_test1 = 0;
//     while (1)
//     {

//         LED_routine(&prise->leds[0]);
//         LED_routine(&prise->leds[1]);
//         KEY_Routines(&(prise->key));
//         //  state_process(prise);

//         if (prise->state == INTRUSION || prise->state == GRACE)
//         {
//             if (prise->connectedMqtt)
//             {
//                 prise->cmp_intrusion++;

//                 if (prise->cmp_intrusion == 1 || prise->cmp_intrusion % 1 == 0)
//                 {
//                     //  printf("INTRUSION ROUTINE  \n");
//                     response_state_intrusion(prise);
//                     prise->cmp_ras = 0;
//                 }
//             }
//         }
//         else
//         {
//             if (prise->connectedMqtt && prise->test_send1 == 1)

//             {
//                 prise->cmp_ras++;
//                 if (prise->cmp_ras == 1 || (prise->cmp_ras % 1 == 0))
//                 {

//                     // printf("R.A.S  ROUTINE \n");
//                     response_state_RAS(prise);

//                     prise->cmp_intrusion = 0;
//                 }
//             }
//         }

//         if (prise->connectedMqtt && prise->test_count_start == 1)
//         {
//             prise->wifi_radar_cb_counter++;
//             //  printf("wifi_radar_cb_counter : %d \n", prise->wifi_radar_cb_counter);
//         }
//         if (prise->wifi_radar_cb_counter == 200)
//         {
//             esp_radar_stop();
//             esp_radar_start();
//         }

//         if (prise->pressed_5s == 1)
//         {

//             esp_wifi_restore();
//             printf("erase flash in 5s pressed \n");
//             ESP_ERROR_CHECK(nvs_flash_erase());
//             nvs_flash_init();
//             esp_restart();
//         }

//         if (test == 1 && prise->connectedMqtt && test_sub == 1)
//         {

//             printf("mac saved and mqtt connected \n");
//             response_radar(prise->client);
//             radar_config();
//             vTaskDelay(1000 / portTICK_PERIOD_MS);
//             wifi_cmd_radar(5, args_init);

//             test = 0;
//             prise->connected = 1;

//             test_sub = 0;
//         }

//         if (prise->test_someone_th == 1 && prise->connectedMqtt)
//         {

//             args_someone_threshold[2] = prise->someone_threshold;

//             wifi_cmd_radar(3, args_someone_threshold);
//             prise->test_someone_th = 0;
//         }

//         if (prise->test_move_th == 1 && prise->connectedMqtt)
//         {
//             // printf("22222222222222222222222 \n");

//             args_move_threshold[2] = prise->move_threshold;

//             wifi_cmd_radar(3, args_move_threshold);
//             prise->test_move_th = 0;
//         }

//         if (calib_test == 1 && prise->connectedMqtt)
//         {

//             if (cpt == 0)
//             {
//                 printf("START \n");
//                 response_calib_start(prise->client);
//                 wifi_cmd_radar(2, args_start);
//             }
//             else if (cpt == 100)
//             {
//                 printf("STOP \n");
//                 response_calib_stop(prise->client);
//                 wifi_cmd_radar(2, args_stop);
//                 /* args_someone_threshold[2] = "0.003";
//                  wifi_cmd_radar(3, args_someone_threshold);

//                  args_move_threshold[2] = "0.0004";
//                  wifi_cmd_radar(3, args_move_threshold);
//                  */
//                 cpt = 0;
//                 calib_test = 0;
//             }

//             cpt++;
//         }

//         vTaskDelay(100 / portTICK_PERIOD_MS);
//     }
// }

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_reconnect) {
            ESP_LOGI(TAG, "sta disconnect, s_reconnect...");
            esp_wifi_connect();
        } else {
            // ESP_LOGI(TAG, "sta disconnect");
        }

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        ESP_LOGI(TAG, "Connected to %s (bssid: "MACSTR", channel: %d)", event->ssid,
                 MAC2STR(event->bssid), event->channel);
    }  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "STA Connecting to the AP again...");
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

// void str2mac(priseStruct *prise)
// {
//     sscanf(prise->mac_address, "%2hhX%2hhX%2hhX%2hhX%2hhX%2hhX", &prise->mac_parse[0], &prise->mac_parse[1], &prise->mac_parse[2], &prise->mac_parse[3], &prise->mac_parse[4], &prise->mac_parse[5]);
//     printf(" MAC adress SET %2hhX%2hhX%2hhX%2hhX%2hhX%2hhX \n", prise->mac_parse[0], prise->mac_parse[1], prise->mac_parse[2], prise->mac_parse[3], prise->mac_parse[4], prise->mac_parse[5]);
//     nvs_set_u8(nvs, "mac_parse0", prise->mac_parse[0]);
//     nvs_set_u8(nvs, "mac_parse1", prise->mac_parse[1]);
//     nvs_set_u8(nvs, "mac_parse2", prise->mac_parse[2]);
//     nvs_set_u8(nvs, "mac_parse3", prise->mac_parse[3]);
//     nvs_set_u8(nvs, "mac_parse4", prise->mac_parse[4]);
//     nvs_set_u8(nvs, "mac_parse5", prise->mac_parse[5]);
// }

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
    xTaskCreate(csi_data_print_task, "csi_data_print", 4 * 1024, NULL, 0, NULL);
}

// void response_state_intrusion(priseStruct *prise)
// {
//     int msg_id;
//     //   sprintf(prise->state_intrusion, "{\"STATE\" : \"INTRUSION\",\"RSSI\":\"%d\",\"Wander\":\"%.6f\",\"Jitter\":\"%.6f\"}", rssi, prise->wander, prise->jitter);
//     // sprintf(prise->state_intrusion, "{\"STATE\" : \"INTRUSION\"}");

//     //  msg_id = esp_mqtt_client_publish(prise->client, prise->topic_radar, prise->state_intrusion, strlen(prise->state_intrusion), 0, 0);
//     msg_id = esp_mqtt_client_publish(prise->client, prise->topic_radar, csi.csi_radar, strlen(csi.csi_radar), 0, 0);
// }

// void response_state_RAS(priseStruct *prise)
// {
//     int msg_id;
//     // sprintf(prise->state_ras, "{\"STATE\" : \"RAS\"}");
//     //  msg_id = esp_mqtt_client_publish(prise->client, prise->topic_radar, prise->state_ras, strlen(prise->state_ras), 0, 0);
//     msg_id = esp_mqtt_client_publish(prise->client, prise->topic_radar, csi.csi_radar, strlen(csi.csi_radar), 0, 0);
// }

//***************************MAIN **********************************//
void App_main_wifi()
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
