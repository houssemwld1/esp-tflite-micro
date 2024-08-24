


#ifndef SENSING_H
#define SENSING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "esp_console.h"
#include "esp_radar.h"
#include "main_functions.h"
// #include "key.h"
// #include "led.h"
// #include "wps.h"
#include "esp_wifi_types.h"
#include "mqtt.h"
// #include "ota.h"

#define PRISE_VESION "5.0"
#define MAX_LEN 8

#define USE_S2
#define DISABLE_KEY

#ifdef USE_S2
#define LED0_PIN 19
#define LED1_PIN 17
#define KEY_PIN 21
#define RELAY_PIN 12
#define CF_PIN 18
#define CF1_PIN 10
#define SEL_PIN 14
#else
#define LED0_PIN 19
#define LED1_PIN 7
#define KEY_PIN 21
#define RELAY_PIN 6
#define CF_PIN 18
#define CF1_PIN 10
#define SEL_PIN 4
#endif

// typedef struct
// {
//     unsigned frame_ctrl : 16;
//     unsigned duration_id : 16;
//     uint8_t addr1[6]; /* receiver address */
//     uint8_t addr2[6]; /* sender address */
//     uint8_t addr3[6]; /* filtering address */
//     unsigned sequence_ctrl : 16;
//     uint8_t addr4[6]; /* optional */
// } wifi_ieee80211_mac_hdr_t;

// typedef struct
// {
//     wifi_ieee80211_mac_hdr_t hdr;
//     uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
// } wifi_ieee80211_packet_t;

typedef enum
{
    IDLE = 0,
    READY,
    INTRUSION,
    GRACE,
} PriseStateStruct;



typedef struct
{
    char csi_data[1024];
    char csi_radar[1024];
    char csi_device_info[1024];

} csiStruct;

static struct console_input_config
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
    false,
    0.001,
    0.001,
    5,
    2,
    "unknown",
    0,  // Assuming some default value for collect_number
    "LLFT",
    "decimal"
};
#define BUFFER_SIZE 3 // Number of matrices in the buffer

typedef struct
{
    float buffer[BUFFER_SIZE][50][55]; // Buffer to hold CSI matrices
    int head;                          // Index for writing to the buffer
    int tail;                          // Index for reading from the buffer
    int count;                         // Number of items in the buffer
    SemaphoreHandle_t mutex;           // Mutex for thread safety
} CircularBuffer;

// static struct console_input_config
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
//     .predict_someone_threshold = 0.001,
//     .predict_move_threshold = 0.001,
//     .predict_buff_size = 5,
//     .predict_outliers_number = 2,
//     .train_start = false,
//     .collect_taget = "unknown",
//     .csi_output_type = "LLFT",
//     .csi_output_format = "decimal"};
void print_device_info();
void wifi_init();
// void PRISE_Init(priseStruct *prise);
// void PRISE_CheckKey(priseStruct *prise);
// void task_mqtt(priseStruct *prise);
void radar_config();
// void PRISE_setState(priseStruct *prise, PriseStateStruct newState);
// void state_process(priseStruct *prise);

// void prise_Task_routine(priseStruct *prise);
int wifi_initialize(void);
// void str2mac(priseStruct *prise);
void collect_timercb(void *timer);
int wifi_cmd_radar(int argc, char **argv);
void cmd_register_radar();
void csi_data_print_task(void *arg);
void promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type);
void WIFI_Scan();
void wifi_radar();
void App_main_wifi();
// void FIR_init(priseStruct *prise, int filter);
// void FIR_insertVal(priseStruct *prise, int newVal);
// int FIR_getOutput(priseStruct *prise);
// int FIR_getValidity(priseStruct *prise);
// void response_state_intrusion(priseStruct *prise);
// void response_state_RAS(priseStruct *prise);

#ifdef __cplusplus
}
#endif

#endif // SENSING_H














































// #ifndef SENSING_H
// #define SENSING_H

// #ifdef __cplusplus
// extern "C" {
// #endif

// #include <stdio.h>
// #include <stdint.h>
// #include <stdbool.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <freertos/event_groups.h>
// #include "esp_event.h"
// #include "esp_wifi.h"
// #include "esp_log.h"
// #include "esp_system.h"
// #include "esp_netif.h"
// #include "mqtt_client.h"
// #include "wifi_sensing.h"

// // Define constants and macros
// #define WIFI_SSID "your_SSID"          // Replace with your WiFi SSID
// #define WIFI_PASSWORD "your_PASSWORD"  // Replace with your WiFi Password
// #define WIFI_MAXIMUM_RETRY 10

// // Global Variables
// extern char bufDataString_data[700];
// extern TaskHandle_t myTaskHandlecsi;
// extern QueueHandle_t g_csi_info_queue;

// // Definition for sensingStruct (make sure this matches the actual type definition)


// extern sensingStruct sensing;

// // Event group for WiFi connection
// extern EventGroupHandle_t s_wifi_event_group;
// extern const int WIFI_CONNECTED_BIT;
// extern const int WIFI_FAIL_BIT;

// // Console input configuration structure


// // Function Prototypes
// void wifi_init_sta(void);
// void radar_config(sensingStruct *sensing);
// void wifi_csi_raw_cb(const wifi_csi_filtered_info_t *info, void *ctx);
// void csi_data_print_task(void *arg);
// void app_main_wifi(void);
// void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

// #ifdef __cplusplus
// }
// #endif

// #endif // SENSING_H
