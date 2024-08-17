#ifndef MQTT_H
#define MQTT_H

#include <stddef.h>
#include <stdint.h>
#include "mqtt_client.h"
#include <esp_event.h>
// priseStruct is a struct that contains the prise information
#ifdef __cplusplus
extern "C" {
#endif
void mqtt_app_start(void);

#ifdef __cplusplus
}

#endif

#endif /* MQTT_H */