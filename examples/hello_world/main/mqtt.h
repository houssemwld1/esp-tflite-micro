#ifndef MQTT_H
#define MQTT_H

#include <stddef.h>
#include <stdint.h>


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