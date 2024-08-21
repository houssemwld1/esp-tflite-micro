#ifndef WPS_H
#define WPS_H
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_wps.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include <string.h>
// #include "key.h"
// #include "led.h"
// #include "app_main.h"

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data);
void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data);
void start_wps(void) ;

#endif