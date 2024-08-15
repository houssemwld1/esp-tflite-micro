#include "mqtt.h"
#include <esp_err.h>
#include <esp_log.h>
#include <mqtt_client.h>
#include <string.h>
#include "esp_tls.h"
#include "wifi.h"
#include <stdlib.h>
#include "log.h"
#include "file_syst.h"
#include "bl0937.h"

/* Constants */
static const char *TAG = "MQTT";
extern priseStruct prise;
extern logElement e;
extern Queue *queue;
static int DISCONNECT_LOGS_mqtt_once = 0;
static int retry_Dis_mqtt = 0;
static int retry = 0;
logElement tmp;
char data2send[64];
char TB_TEST[15];

/* Types */

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef struct mqtt_subscription_t
{
    struct mqtt_subscription_t *next;
    char *topic;
    mqtt_on_message_received_cb_t cb;
    void *ctx;
    mqtt_free_ctx_cb_t free_cb;
} mqtt_subscription_t;

typedef struct mqtt_publications_t
{
    struct mqtt_publications_t *next;
    char *topic;
    uint8_t *payload;
    size_t len;
    int qos;
    uint8_t retained;
} mqtt_publications_t;

/* Internal state */
static esp_mqtt_client_handle_t mqtt_handle = NULL;
static mqtt_subscription_t *subscription_list = NULL;
static mqtt_publications_t *publications_list = NULL;
static uint8_t is_connected = 0;

/* Callback functions */
static mqtt_on_connected_cb_t on_connected_cb = NULL;
static mqtt_on_disconnected_cb_t on_disconnected_cb = NULL;

/*fonction de test en publiant une partie de la partition active  */
static void send_binary(esp_mqtt_client_handle_t client)
{
    spi_flash_mmap_handle_t out_handle;
    const void *binary_address;
    const esp_partition_t *partition = esp_ota_get_running_partition();
    esp_partition_mmap(partition, 0, partition->size, SPI_FLASH_MMAP_DATA, &binary_address, &out_handle);
    int binary_size = MIN(CONFIG_BROKER_BIN_SIZE_TO_SEND, partition->size);
    int msg_id = esp_mqtt_client_publish(client, "/topic/binary", binary_address, binary_size, 0, 0);
    ESP_LOGI(TAG, "binary sent with msg_id=%d", msg_id);
}
/*
void listWifiScan(esp_mqtt_client_handle_t client)
{
    char topic_listwifi[256];
    sprintf(topic_listwifi, "%s/LIST_WIFI", device_name());

    int msg_id = esp_mqtt_client_publish(client, topic_listwifi, prise.list_wifi_scan, strlen(prise.list_wifi_scan), 0, 0);
}
*/
/*Machine d'état de mqtt */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    logElement *element;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        DISCONNECT_LOGS_mqtt_once = 0;
        retry_Dis_mqtt = 0;
        int msgid = 0;

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        sprintf(prise.sensing.topic_radar, "%s/RADAR_DADA", device_name());

        ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
        esp_wifi_set_promiscuous_rx_cb(&promiscuous_rx_cb);
        // TBI_publishAttributes(&prise, client);
        TBI_subscribe_Request(client);
        // sprintf(prise.sensing.topic_logs, "%s/logs", device_name());

        sprintf(TB_TEST, "%s/TESTTEST", device_name());
        int msgId = esp_mqtt_client_publish(client, TB_TEST, "{\"version\":45}", strlen("{\"version\":45}"), 0, 0);
        PRISE_setConnected(&prise, 1);
        if (prise.state != PRISE_FACTORY_MODE)
            PRISE_AppendLog(PTL_MQTT, PTL_LC_MQTT_OK);
        //  ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

        /********************* RECEIVER *********************/
        prise.countDisconnected = 0;
        if (prise.sensing.radarStarted == 2)
        {
            esp_radar_start();
            prise.sensing.radarStarted = 1;
        }
        char topic_mac[12];
        sprintf(topic_mac, "%s/CMD", device_name());
        int msg_id = esp_mqtt_client_subscribe(client, topic_mac, 0);

        char topic_startCollectData[24];
        sprintf(topic_startCollectData, "%s/collectData", device_name());
        msg_id = esp_mqtt_client_subscribe(client, topic_startCollectData, 0);

        sprintf(prise.sensing.topic_prediction, "%s/prediction", device_name());

        char topic_mode_debug[18];
        sprintf(topic_mode_debug, "%s/DEBUG", device_name());
        msg_id = esp_mqtt_client_subscribe(client, topic_mode_debug, 0);

        char topic_mode_sender_receiver[22];
        sprintf(topic_mode_sender_receiver, "%s/priseMode", device_name());
        msg_id = esp_mqtt_client_subscribe(client, topic_mode_sender_receiver, 0);

        char topic_prediction_mode[24];
        sprintf(topic_prediction_mode, "%s/Pred", device_name());
        printf("topic preeeeeeeed : %s \n", topic_prediction_mode);
        msg_id = esp_mqtt_client_subscribe(client, topic_prediction_mode, 0);

        char topic_configs_state[24];
        sprintf(topic_configs_state, "%s/configs", device_name());
        printf("topic preeeeeeeed : %s \n", topic_configs_state);
        msg_id = esp_mqtt_client_subscribe(client, topic_configs_state, 0);

        /*********************** END_RECEIVER  ********************/
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED %d, %d", PRISE_getInConfig(&prise), prise.timeoutPing);
        printf("Free heap : %d \n", esp_get_free_heap_size());
        prise.countDisconnected++;
        if (prise.sensing.radarStarted == 1)
        {
            esp_radar_stop();
            prise.sensing.radarStarted = 2;
        }
        if (!prise.timeoutPing && prise.state != PRISE_FACTORY_MODE)
        {
            ESP_LOGI(TAG, "NOT TIMEOUT PING");

            initialize_ping();

            prise.timeoutPing = 300;
            retry++;
            // printf("retry :%d", retry);
        }

        break;

    case MQTT_EVENT_SUBSCRIBED:
        //  ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        /********** à effacer *************/
        LED_setOutputForXCycle(&prise.leds[0], 0, 20);
        LED_setOutputForXCycle(&prise.leds[1], 0, 20);
        TBI_request_response(event, client);
        TBI_request_response_read_mac(event, client);
        TBI_mode_debug(event, client, &prise);
        TBI_collected_Data(event, client, &prise);
        //  TBI_request_ssid_rssi(event, client, &prise);
        TBI_mode_sender_receiver(event, client, &prise);
        TBI_prediction(event, client, &prise);
        TBI_configs_state_machine(event, client, &prise);

        if (strncmp(event->data, "send binary please", event->data_len) == 0)
        {
            ESP_LOGI(TAG, "Sending the binary");
            send_binary(client);
        }
        TBI_publishAttributes(&prise, client);

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        DISCONNECT_LOGS_mqtt_once = 1;
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            retry_Dis_mqtt++;

            if (DISCONNECT_LOGS_mqtt_once == 1 && retry_Dis_mqtt == 1 && prise.state != PRISE_FACTORY_MODE)
                PRISE_AppendLog(PTL_MQTT, PTL_LC_MQTT_KO_CERT);

            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(TAG, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        }
        else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED)
        {
            retry_Dis_mqtt++;
            ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            if (DISCONNECT_LOGS_mqtt_once == 1 && retry_Dis_mqtt == 1)

                PRISE_AppendLog(PTL_MQTT, PTL_LC_MQTT_KO_TOKEN);
        }
        else
        {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            PRISE_AppendLog(PTL_MQTT, PTL_LC_MQTT_KO_PERT_CNX);
        }
        break;
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    default:
        break;
    }
}
/*Certifications SSL*/
static const uint8_t mqtt_tb_pem_ca_start[] = "-----BEGIN CERTIFICATE-----\nMIIFHTCCAwWgAwIBAgIUNZkEtzF5I9LwVvhXRJCFZPn1sfkwDQYJKoZIhvcNAQEL\nBQAwHTEbMBkGA1UEAwwSUFRMIE1RVFQgQXV0aG9yaXR5MCAXDTIyMTExMDEwNDY0\nMVoYDzIxMTIxMDE5MTA0NjQxWjAdMRswGQYDVQQDDBJQVEwgTVFUVCBBdXRob3Jp\ndHkwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIKAoICAQCzotrKhk5U06k6daqG\nj4VVnTXlvyZ0Fjetk8SeRMZyeZ7eZwvKNbmqFTkk5+RZImZe7jaGDI2X+Q6VjjAG\nf6FjeD3CzZU5lv91TgbZwZlfzu++JPzM2jp/HSa3HfyfxzYi0fzTwLt98VOJNjK8\nG0Fh7T+2htHvEEWvuL9TeefbSQemItYLLzjIsgC2HKZdYSwj5CSczXG+jVdtUj2/\n6qW0YNpueqRmG0K4Ld/2ikUWkOLeAlyOxeafnHMqP1TYBuNCFKk07+9VQ1MSJl8b\nKPg/pmDpekha2v0iqo614XpAmtfi+KpmUaySdZh9QjHtlB0mmNXvk26JNx9ABHYk\nq6L2ziO7buY2xNCHNxvmmuxsKzYoNZMAA22PLmalWEguKi5S9w7t6uBMaCkE1wHW\nvGEedNYioQvS5sPYl9WPkpUH6+4HD8rd9qPBmTWiLYfZavXJQeeY/GAJ1AwyH7QY\nvATMy2u+dXyYbi3+PBv+2jp0uKTL2lvQ8KegGM6+p/19tJsYRBGdGM6rNb488ONY\nGz8smUkLcFa+MbI9Wg92jwButbbNWdmhWf3Kw302uYdSVXdS8UqJNNHwTHfCmoWC\nOLi2Coe53rpUXqDz12IgodvKR7yotJm331JWSglt3pf8srVXKMOc6ib/7m4kbxaK\nh9Ar56xog/bisWsc7auTg6uePQIDAQABo1MwUTAdBgNVHQ4EFgQUh1qDNjT6WGy0\neptwk5yzS+OArw8wHwYDVR0jBBgwFoAUh1qDNjT6WGy0eptwk5yzS+OArw8wDwYD\nVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAgEAbuDxOFl6NJowtISwy8kU\nemC59l7vjw4Bn90dc//MP10AWpXgPe53ou6SEBqBoTt1BSz++9AoQ4YUT5sUY1ms\n7wCMDP3GK4rAHrMjaibVUO6Yc7U3peGDZcjGYw06q9mICS/JLr+K057yEsjKc64h\n0COnEV9ofgnTOpON2QP9eT34QTJUWvuWBvl45Pdl2w2De8ODbM5vtOTQXk9ofLbn\nglLfHiFuwW2jELU9u4tDt2NdzNFqX1MECwG8RcpriuyRNnnAD4x1AnsBykQi8UfB\nwXzESJVvJ0VViNo9DBdJuPQB8cpA/++0FhXwLzE8vtgoNRstdLkTyfV/6MMAMRfg\n/RzVlX6eIZMbwAEEerhcS4i2+rPI+hJ3bq3awu0VmRLOHHkhL1CmrUoQtzy//TxA\nfL6K5pmnxWQEzcgX//mcPaLGLQwVPMkMGa1KGugoeR9FBxSkVU4mReXK6GHkVraU\nL0fqvzuFWuDw0zQ9bHzLga+efoMj3aHVqi5OA+32X0kYajRz6ckkAHVQg5sZ4Xsh\nSkQZ3NkvN/wxgVh8HhyvrIaNj8jfExwMRefoWd24IcQ5zxRSeLtJY4AUPrw5ifu9\nqmKN1aDhbguHMFQYOFRihEgUWhFIVYYlqs7MPe53fYjxQxC99iqWSBvUKGpO5HYZ\neDLS8/PFE6+JSuKY2ceWiSw=\n-----END CERTIFICATE-----";
static const uint8_t mqtt_tb_pem_crt_start[] = "-----BEGIN CERTIFICATE-----\nMIIEqDCCApACAQIwDQYJKoZIhvcNAQELBQAwHTEbMBkGA1UEAwwSUFRMIE1RVFQg\nQXV0aG9yaXR5MCAXDTIyMTExMDExMTc1M1oYDzIxMTIxMDE5MTExNzUzWjAVMRMw\nEQYDVQQDDAptcXR0Q2xpZW50MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKC\nAgEA9rCC9mY6yyOunE3soQBKgEczz9PjYHGhQGVpP04Oa1TpvBMGhmGR6jp50UMy\ny9Fc/kkhQOLAquckKfdtrjk0E6V9YU6k1cS3ZRxcGG+keIhuykOQ62B1U/Y8vWfu\nfKuBJdCPUK0QAGF0HxGZ+CC0oTo0JsNvnY9zrxp/OElVvTooEPYVdVTaYlcXushO\n58EaizMjQUyasekvGut7qzHaA7YfY/t4MquXncWmLAwGJLv7EZLK8MIxIN27Glif\n4u/LGpJbRDKnod4HFKtfFmwI3rMKxRqG9evQ2gQN9y5Nx2jC8aE1PmTvMTglb5RK\no9Z4H+Ipcwe/b8f9D5Gz83F0aMnVvmitUgOK/EmKmbmT0Xq1FJKjk+i9j9dDeE3c\nG87HMnDW6Vezhp71XfcBjiC5pa0bXE3C+kwTZyxupRFESArywCBDH7vGr+cSBwV3\nunIIZyMbICzFatx2kWP7HmI8SV6msABQJ+L0qqtGQGcOusUvQc6TUAyAIlzBDTCu\nqIsTgWy0OUjtpc0E4cDsmGyMctgKNrGY4WuVVLhrjW31eB63YiDKcnciraqqi2pN\n+DnZFVAnkcGqVpbp7OLEJSYsIGp/FLhdKCJAuqUJUektAmFvwXvqDafeToApIaBF\nJ0pKakWqFUsWtREUVqd55qaPKRwJse2tl3qWRhCo1/RNUTMCAwEAATANBgkqhkiG\n9w0BAQsFAAOCAgEAaqlOwdumlWiMIx/68oAb2yzMSVR8sW4NKNUUTqYdkP8FyD9d\nh4JN+b5OO0EzUTw1oxnJTWfPjrp+oLJbHlp4D9WjVfowQgVku1RWdRrW9ML+VBfA\nH6y5vrynoMfYbueP/ItUv3dQTeopWMzVHLxhC1TcpcwawLZ0WbI0w0PSEXp7IwBd\nSp+B9nnwlwM2hWidaiOY8A7vAztTUKF4zIWiaaFHeFY9Wv6wk/4gpYwrFVFrbBfs\nSj95FJBTgmH6pYsG3hrWefZCFSKeOjfEw2xYF8nQK+oBOcBYT5RbjxWZGS0T4y1I\n36Ua+Fsm/Lo6A7dBaOBqAd/lH4lG46769qBxXRby25ad3M5piy6GtN5cn/T7/VyO\ns9gs/CqtJGny4PwvvYPKchSWwxEfYkqrUXVose7j9cIsLXrzKM7KBlfmU0uZ5Kz4\nNDBp2x9BUOv0gBFanV+Mx9qO9vc54hVhfjJXanjEz+FW4GmUxsM2uY3SEHxqsPDB\nAJ74ik9T4YqLGeQSLd3T+/XX/+ndYOs/Ish/mRGtC6tK3XQ4F3GfN12yeFcqp9qL\nxqV05eULWnzBy7CIqqpo9HMCHN4DXC3QBYO3iqZ/iIt9SYMQuvMA0w5/EfCyr0Lc\n5Xf9cM+Wc33jmaBVFGXXob+YQFryleXYss9LZHMdP4Acoq4S8umHJ0Elpl0=\n-----END CERTIFICATE-----";
static const uint8_t mqtt_tb_pem_key_start[] = "-----BEGIN PRIVATE KEY-----\n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\nPMnUr3BVRHoGDaUAV1zy3tOfAQ6WOg==\n-----END PRIVATE KEY-----";

void mqtt_app_init(priseStruct *prise)
{
    prise->client = NULL;
}
/* Configuration MQTT */
void mqtt_app_start(priseStruct *prise)
{
    if (prise->client != NULL)
        return;
    //   ESP_LOGI(TAG, "Connecting to host %s:%d", config_mqtt_host_get(), config_mqtt_port_get());
    bool useSSL = false;
    if (strncmp("mqtt://193.70.72.183", "mqtts", 5) == 0)
    //   if (strncmp(config_mqtt_host_get(), "mqtts", 5) == 0)
    {
        printf("use ssl \n");
        useSSL = true;
    }
    // printf("ssl : %d", useSSL);
    const esp_mqtt_client_config_t mqtt_cfg = {
        /*
               .uri = "mqtts://ws-sensing.ttn.tn",
                .port = 8883,
        */

        .uri = "mqtt://193.70.72.183",
        .port = 21883,
        .username = "alpha",
        .password = "ct123456",
        /*
        .uri = "mqtt://35.180.228.47",
        .port = 1883,
        .username = "sensify",
        .password = "4hQT70mshYRy",
        */

        .keepalive = config_mqtt_keepalive_get(),
        .reconnect_timeout_ms = config_mqtt_reconnect_timeout_ms_get(),
        .cert_pem = useSSL ? (const char *)mqtt_tb_pem_ca_start : NULL,
        .client_cert_pem = useSSL ? (const char *)mqtt_tb_pem_crt_start : NULL,
        .client_key_pem = useSSL ? (const char *)mqtt_tb_pem_key_start : NULL,

    };
    //  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    prise->client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(prise->client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(prise->client);
    prise->timeoutPing = 0;
}
