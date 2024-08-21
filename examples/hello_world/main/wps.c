/* WiFi Connection Example using WPS

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example demonstrates how to use WPS.
   It supports two modes, which can be selected in menuconfig.

   WPS_TYPE_PBC:
        Start ESP32 and it will enter WPS PBC mode. Then push WPS button on the router.
        ESP32 will receive SSID and password, and connect to the router.

   WPS_TYPE_PIN:
        Start ESP32, you'll see an eight-digit PIN number in log output.
        Enter the PIN code on the router and then the ESP32 will get connected to router.
*/

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

/*set wps mode via project configuration */
#if CONFIG_EXAMPLE_WPS_TYPE_PBC
#define WPS_MODE WPS_TYPE_PBC
#elif CONFIG_EXAMPLE_WPS_TYPE_PIN
#define WPS_MODE WPS_TYPE_PIN
#else
#define WPS_MODE WPS_TYPE_DISABLE
#endif /*CONFIG_EXAMPLE_WPS_TYPE_PBC*/
// extern priseStruct prise;

#define MAX_RETRY_ATTEMPTS 2
esp_err_t ret;
#ifndef PIN2STR
#define PIN2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7]
#define PINSTR "%c%c%c%c%c%c%c%c"
#endif

static const char *TAG = "example_wps";
static esp_wps_config_t config = WPS_CONFIG_INIT_DEFAULT(WPS_MODE);
static wifi_config_t wps_ap_creds[MAX_WPS_AP_CRED];
static int s_ap_creds_num = 0;
static int s_retry_num = 0;

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    static int ap_idx = 1;

    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
        if (s_retry_num < MAX_RETRY_ATTEMPTS)
        {
            ESP_LOGI(TAG, "test1");

            esp_wifi_connect();
            s_retry_num++;
        }
        else if (ap_idx < s_ap_creds_num)
        {
            /* Try the next AP credential if first one fails */
            ESP_LOGI(TAG, "test2");

            if (ap_idx < s_ap_creds_num)
            {
                ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s",
                         wps_ap_creds[ap_idx].sta.ssid, wps_ap_creds[ap_idx].sta.password);
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_ap_creds[ap_idx++]));
                esp_wifi_connect();
            }
            s_retry_num = 0;
        }
        else
        {
            ESP_LOGI(TAG, "Failed to connect!");
        }

        break;
    case WIFI_EVENT_STA_WPS_ER_SUCCESS:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
        // LED_setDuty(&prise.leds[0], 10, 0);
        // LED_setDuty(&prise.leds[1], 10, 5);
        {
            wifi_event_sta_wps_er_success_t *evt =
                (wifi_event_sta_wps_er_success_t *)event_data;
            int i;

            if (evt)
            {
                s_ap_creds_num = evt->ap_cred_cnt;
                for (i = 0; i < s_ap_creds_num; i++)
                {
                    memcpy(wps_ap_creds[i].sta.ssid, evt->ap_cred[i].ssid,
                           sizeof(evt->ap_cred[i].ssid));
                    memcpy(wps_ap_creds[i].sta.password, evt->ap_cred[i].passphrase,
                           sizeof(evt->ap_cred[i].passphrase));
                }
                /* If multiple AP credentials are received from WPS, connect with first one */
                ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s",
                         wps_ap_creds[0].sta.ssid, wps_ap_creds[0].sta.password);
                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_ap_creds[0]));
            }
            /*
             * If only one AP credential is received from WPS, there will be no event data and
             * esp_wifi_set_config() is already called by WPS modules for backward compatibility
             * with legacy apps. So directly attempt connection here.
             */
            ESP_ERROR_CHECK(esp_wifi_wps_disable());
            esp_wifi_connect();
        }
        break;
    case WIFI_EVENT_STA_WPS_ER_FAILED:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
        ESP_ERROR_CHECK(esp_wifi_wps_disable());
        ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
        ESP_ERROR_CHECK(esp_wifi_wps_start(0));
        break;
    case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
        ESP_ERROR_CHECK(esp_wifi_wps_disable());
        ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
        ESP_ERROR_CHECK(esp_wifi_wps_start(0));
        break;
    case WIFI_EVENT_STA_WPS_ER_PIN:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PIN");
        /* display the PIN code */
        wifi_event_sta_wps_er_pin_t *event = (wifi_event_sta_wps_er_pin_t *)event_data;
        ESP_LOGI(TAG, "WPS_PIN = " PINSTR, PIN2STR(event->pin_code));
        break;
    case IP_EVENT_STA_GOT_IP:

        ESP_LOGI(TAG, "Got Ip let's connect");
  
        // test_mqtt();

        break;
    case WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP:
        ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP");

        ret = esp_wifi_wps_disable();
        ret = esp_wifi_wps_enable(&config);
        ret = esp_wifi_wps_start(0);
        break;
    default:
        break;
    }
}

void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    // gpio_set_level(19, 0);
    //  gpio_set_level(7, 0);
}

/*init wifi as sta and start wps*/
void start_wps(void)
{

    ESP_LOGI(TAG, "start wps...");

    ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
    ESP_ERROR_CHECK(esp_wifi_wps_start(0));
}
