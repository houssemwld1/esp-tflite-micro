#ifndef MQTT_H
#define MQTT_H

#include <stddef.h>
#include <stdint.h>
#include "mqtt_client.h"
#include <esp_event.h>
// priseStruct is a struct that contains the prise information
typedef struct {
    char topic_radar[64];
    char topic_logs[64];
    char topic_prediction[64];
    char list_wifi_scan[256]; // Assuming this is a buffer for the WiFi scan list
    int radarStarted;         // To indicate if radar has started: 0 - not started, 1 - started, 2 - stopped
    int countDisconnected;    // Counter for disconnect events
    int state;                // To represent the state of the system, such as factory mode
    int timeoutPing;          // Ping timeout counter
    struct {
        char led[2];          // Assuming two LEDs for the system
    } leds;
} priseSensingStruct;

typedef struct {
    priseSensingStruct sensing; // Nested structure for sensing-related fields
    int state;                  // Overall system state, such as factory mode or connected
} priseStruct;


/* Event callback types */
typedef void (*mqtt_on_connected_cb_t)(void);
typedef void (*mqtt_on_disconnected_cb_t)(void);
typedef void (*mqtt_on_message_received_cb_t)(const char *topic,
                                              const uint8_t *payload, size_t len, void *ctx);
typedef void (*mqtt_free_ctx_cb_t)(void *ctx);

/* Event handlers */
void mqtt_set_on_connected_cb(mqtt_on_connected_cb_t cb);
void mqtt_set_on_disconnected_cb(mqtt_on_disconnected_cb_t cb);
void mqtt_app_init(priseStruct *prise);
void mqtt_app_start(priseStruct *prise);
// void listWifiScan(esp_mqtt_client_handle_t client) ;

#endif