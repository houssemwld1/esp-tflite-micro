#ifndef __WIFI_SENSING_H__
#define __WIFI_SENSING_H__
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "esp_console.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"
// #include "ota.h"
#include "mqtt_client.h"
#include "esp_radar.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MAX_LEN 8

typedef struct
{
    unsigned frame_ctrl : 16;
    unsigned duration_id : 16;
    uint8_t addr1[6]; /* receiver address */
    uint8_t addr2[6]; /* sender address */
    uint8_t addr3[6]; /* filtering address */
    unsigned sequence_ctrl : 16;
    uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct
{
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;
/*
typedef enum
{
    IDLEP = 0,
    MOUVEMENT,
    DETECTION,
    HUMAN,
    BLIND,
} PriseProfil1Struct;
*/
// typedef enum
// {
//     IDLE = 0,
//     READY,
//     HUMAN_DOUBT,
//     PET_DOUBT,
//     HUMAN_ALERT,
//     PET_ALERT,
// } PriseStatPredeStruct;
/*
typedef struct
{
    float threshold_mvt_profil;
    float threshold_presence_profil;
    int rssi_threshold;
    float wander_th;
    float jitter_th;
    int count_th_human;
    int count_th_nothuman;
    float wander_detect_blind;
    int filter_rssi

} profilStruct;
*/
typedef struct
{
    uint8_t human_doubt_to_human_alert;
    uint8_t human_doubt_to_ready;
    uint8_t human_doubt_to_idle;

    uint8_t pet_doubt_to_pet_alert;
    uint8_t pet_doubt_to_human_doubt;
    uint8_t pet_doubt_to_idle;

    // uint8_t human_alert_to_idle;
    //  uint8_t pet_alert_to_idle;
    float threshold_jitter;

} confgStateStruct;

typedef struct
{
    int index_config;
    float percentageDifference;

    uint8_t start_collect, stop_collect, testtest;
    int type_collectData;
    PriseStatPredeStruct statePred;
    confgStateStruct configState;
    char csi_radar[700];
    int filter, cursor, valide, newVal, sum, output, zeroValCounts, rssi, vals[MAX_LEN];
    uint8_t mac_parse[6];
    char cnct_mac[30], mac_address[30], mac_address_sender[10], topic_radar[200];
    nvs_handle_t nvs_sensing;
    bool test_count_start;
    int index, index1, receivedData, wifi_radar_cb_counter, radar_cb_ok;
    bool presence, mouvement;
    float jitter, wander;
    char bufDataString[500], num_config[15], configs_coeff[20], TypeClassData[700];
    int sender_configured, receiver_configured;
    uint8_t modeReceiver, modeSender, receiver_mode, sender_mode;
    time_t currentTime;
    int mac_received, mac_saved_in_nvs;
    bool calib_started, calibrage;
    int mode_debug, mode_debug_stop;
    int classData, testcsi, count_stop, numCsi;
    char topic_prediction[30];
    int validity, rssi_val;
    int sendpred;
    char type_classe[13];
    char timeRecord[10];
    int countCsi, type_classe_test, update_csi_data;
    int predict, startPrediction, stopPrediction;
    int radarStarted;
    float predNoAct, predHuman, predPet, max_value;
    int countPredHum, countPredPet, countPredNoAct, countIntrusion;
    int rssiPrecedent, diffRssi, rssiMaxAtteint;
    char predresult[15];
} sensingStruct;

static struct
{
    struct arg_lit *train_start;
    struct arg_lit *train_stop;
    struct arg_lit *train_add;
    struct arg_str *predict_someone_threshold;
    struct arg_str *predict_move_threshold;
    struct arg_int *predict_buff_size;
    struct arg_int *predict_outliers_number;
    struct arg_str *collect_taget;
    struct arg_int *collect_number;
    struct arg_int *collect_duration;
    struct arg_lit *csi_start;
    struct arg_lit *csi_stop;
    struct arg_str *csi_output_type;
    struct arg_str *csi_output_format;
    struct arg_end *end;
} radar_args;




void radar_config(sensingStruct *sensing);
// void init_WifiSensing(sensingStruct *sensing, nvs_handle_t nvs);
// int wifi_initialize(void);
void str2mac(sensingStruct *sensing, nvs_handle_t nvs);
void collect_timercb(void *timer);
int wifi_cmd_radar(int argc, char **argv);
void cmd_register_radar();
void csi_data_print_task(void *arg);

// void promiscuous_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type);
// void FIR_init(sensingStruct *sensing, int filter);
// void FIR_insertVal(sensingStruct *sensing, int newVal);
// int FIR_getOutput(sensingStruct *sensing);
// int FIR_getValidity(sensingStruct *sensing);
// void log_profil(sensingStruct *sensing);
// void wifi_radar();
// void publish_pred_result(sensingStruct *sensing);

void Sensing_routine(sensingStruct *sensing);
// float max_pred(sensingStruct *sensing);
// void state_process(sensingStruct *sensing);


#ifdef __cplusplus
}
#endif

#endif // __WIFI_SENSING_H__