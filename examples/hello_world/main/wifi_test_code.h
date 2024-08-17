#ifndef SENSING_H
#define SENSING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "wifi_sensing.h"

// Define constants and macros
#define WIFI_SSID "your_SSID"          // Replace with your WiFi SSID
#define WIFI_PASSWORD "your_PASSWORD"  // Replace with your WiFi Password
#define WIFI_MAXIMUM_RETRY 10

// Global Variables
extern char bufDataString_data[700];
extern TaskHandle_t myTaskHandlecsi;
extern QueueHandle_t g_csi_info_queue;

// Definition for sensingStruct (make sure this matches the actual type definition)


extern sensingStruct sensing;

// Event group for WiFi connection
extern EventGroupHandle_t s_wifi_event_group;
extern const int WIFI_CONNECTED_BIT;
extern const int WIFI_FAIL_BIT;

// Console input configuration structure


// Function Prototypes
void wifi_init_sta(void);
void radar_config(sensingStruct *sensing);
void wifi_csi_raw_cb(const wifi_csi_filtered_info_t *info, void *ctx);
void csi_data_print_task(void *arg);
void app_main_wifi(void);
void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

#ifdef __cplusplus
}
#endif

#endif // SENSING_H
