#ifndef WIFI_H
#define WIFI_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

// WiFi configuration
#define EXAMPLE_ESP_WIFI_SSID      "ACCENT_LAN"
#define EXAMPLE_ESP_WIFI_PASS      "127.0.0.111"
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

// FreeRTOS event group to signal when we are connected
extern EventGroupHandle_t s_wifi_event_group;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Function declarations
void wifi_init_sta(void);
void WIFI_CONNECT(void);

#endif // WIFI_H