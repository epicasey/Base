#ifdef CONFIG_BT_ENABLED
#include "base.h"//#include "Common.h"
//#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#include <stdio.h>
#include <stdlib.h>

#include <map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <functional>

using namespace std;

#ifndef BTLE_H
#define BTLE_H

#define GATTS_TABLE_TAG "BLUETOOTH"

#define PROFILE_NUM                 1
#define PROFILE_APP_IDX             0
#define ESP_APP_ID                  0x55
#define SVC_INST_ID                 0

// The max length of characteristic value. When the GATT client performs a write or prepare write operation,
//    the data length must be less than GATTS_CHAR_VAL_LEN_MAX.
#define GATTS_CHAR_VAL_LEN_MAX 500
#define PREPARE_BUF_MAX_SIZE   1024
#define CHAR_DECLARATION_SIZE  (sizeof(uint8_t))

#define ADV_CONFIG_FLAG        (1 << 0)
#define SCAN_RSP_CONFIG_FLAG   (1 << 1)
#define BASE_UUID "00006532-0000-1000-8000-00805f9b34fb"

typedef struct {uint8_t *prepare_buf; int prepare_len;} prepare_type_env_t;

class btle {
    private:
        btle();
        ~btle();
        enum State{OFF,STARTED,RUNNING};
        State state {OFF};
        void do_message_functions(string s);
        struct Func {function<void(string s)> F; bool clear = true;};//sturcture of function pointer and bool
        uint8_t adv_config_done {0};
        vector<uint16_t>heart_rate_handle_table;
        prepare_type_env_t prepare_write_env {};
        uint8_t char_value[4]{0x11, 0x22, 0x33, 0x44};
        uint16_t conn_id     {9999};
        uint16_t gatt_if     {9999};
        uint16_t base_handle {9999};
        esp_ble_conn_update_params_t conn_params  {};
        map<string,Func>subscriptions;

       // NV Storage Properties
        string Device_name;
        bool use_bt {true};
        string uuid_base {BASE_UUID};
        // Fixed 128-bit UUID in little-endian byte order expected by ESP-IDF.
        uint8_t service_uuid[ESP_UUID_LEN_128]{
            0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
            0x00, 0x10, 0x00, 0x00, 0x32, 0x65, 0x00, 0x00,
        };

        //The 16 bit uuids below are put into positions [12] and [13] in the service_uuid to form the uuids used for each service/charateristic. 
        const uint8_t GATTS_SERV_UUID_A0[2] {0x30,0x65};// Service uuid
        const uint8_t GATTS_CHAR_UUID_A1[2] {0x31,0x65};// Characteristic 1 uuid
        const uint8_t GATTS_CHAR_UUID_A2[2] {0x32,0x65};// Characteristic 2 uuid
        const uint8_t GATTS_CHAR_UUID_A3[2] {0x33,0x65};// Characteristic 3 uuid

        //Standard control variables 
        const uint16_t primary_service_uuid         {ESP_GATT_UUID_PRI_SERVICE};
        const uint16_t character_declaration_uuid   {ESP_GATT_UUID_CHAR_DECLARE};
        const uint16_t character_client_config_uuid {ESP_GATT_UUID_CHAR_CLIENT_CONFIG};
        const uint8_t char_prop_read                {ESP_GATT_CHAR_PROP_BIT_READ};
        const uint8_t char_prop_write               {ESP_GATT_CHAR_PROP_BIT_WRITE};
        const uint8_t char_prop_read_write          {ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ};
        const uint8_t char_prop_read_write_notify   {ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY};
        const uint8_t heart_measurement_ccc[2]      {0x00, 0x00};

        esp_ble_adv_data_t adv_data  {  // The length of adv data must be less than 31 bytes 
            .set_scan_rsp        {false},
            .include_name        {false},
            .include_txpower     {false},
            .min_interval        {0x0000},
            .max_interval        {0x0000},
            .appearance          {0x00},
            .manufacturer_len    {0},       //TEST_MANUFACTURER_DATA_LEN,
            .p_manufacturer_data {nullptr}, //test_manufacturer,
            .service_data_len    {0},
            .p_service_data      {nullptr},
            .service_uuid_len    {0},
            .p_service_uuid      {nullptr},
            .flag                {(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT)},
        };
        
        esp_ble_adv_data_t scan_rsp_data { // scan response data
            .set_scan_rsp        {true},
            .include_name        {true},
            .include_txpower     {false},
            .min_interval        {0x0000},
            .max_interval        {0x0000},
            .appearance          {0x00},
            .manufacturer_len    {0},       //TEST_MANUFACTURER_DATA_LEN,
            .p_manufacturer_data {nullptr}, //&test_manufacturer[0],
            .service_data_len    {0},
            .p_service_data      {nullptr},
            .service_uuid_len    {ESP_UUID_LEN_128},
            .p_service_uuid      {service_uuid},
            .flag                {ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT},
        };

        esp_ble_adv_params_t adv_params { //Advertising parameters
            .adv_int_min       {0x20},                  .adv_int_max       {0x40},
            .adv_type          {ADV_TYPE_IND},          .own_addr_type     {BLE_ADDR_TYPE_PUBLIC},
            .peer_addr         {},                      .peer_addr_type    {},
            .channel_map       {ADV_CHNL_ALL},          .adv_filter_policy {ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY},
        };

        struct gatts_profile_inst {
            esp_gatts_cb_t gatts_cb       {nullptr};
            uint16_t gatts_if             {0};          uint16_t app_id               {0};
            uint16_t conn_id              {0};          uint16_t service_handle       {0};
            esp_gatt_srvc_id_t service_id {};           uint16_t char_handle          {0};
            esp_bt_uuid_t char_uuid       {};           esp_gatt_perm_t perm          {};
            esp_gatt_char_prop_t property {};           uint16_t descr_handle         {0};
            esp_bt_uuid_t descr_uuid      {};
        };

        // One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT 
        struct gatts_profile_inst heart_rate_profile_tab[PROFILE_NUM];
   
        // Database Description - Use this to add attributes into the database
        struct table_element {int Descriptor_index; uint8_t* UUID; uint16_t perm; uint16_t max_length; uint16_t length; uint8_t *value;  };
        map<string,table_element>tmap {
            {"Service_Declaration",{0,(uint8_t *)&primary_service_uuid,         ESP_GATT_PERM_READ,                             sizeof(uint16_t),       sizeof(GATTS_SERV_UUID_A0),(uint8_t *)&GATTS_SERV_UUID_A0}},
            {"Weight_Declaration", {1,(uint8_t *)&character_declaration_uuid,   ESP_GATT_PERM_READ,                             sizeof(uint8_t),        sizeof(uint8_t),                (uint8_t *)&char_prop_read_write_notify}},
            {"Weight",             {2,(uint8_t *)&GATTS_CHAR_UUID_A1,       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,       GATTS_CHAR_VAL_LEN_MAX, sizeof(char_value),             (uint8_t *)char_value}},
            {"Weight_CFG",         {3,(uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,       sizeof(uint16_t),       sizeof(heart_measurement_ccc),  (uint8_t *)heart_measurement_ccc}},
            {"Units_Declaration",  {4,(uint8_t *)&character_declaration_uuid,   ESP_GATT_PERM_READ,                             CHAR_DECLARATION_SIZE,  CHAR_DECLARATION_SIZE,          (uint8_t *)&char_prop_read_write}},
            {"Units",              {5,(uint8_t *)&GATTS_CHAR_UUID_A2,       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,       GATTS_CHAR_VAL_LEN_MAX, sizeof(char_value),             (uint8_t *)char_value}},
            {"Char_C_Declaraton",  {6,(uint8_t *)&character_declaration_uuid,   ESP_GATT_PERM_READ,                             CHAR_DECLARATION_SIZE,  CHAR_DECLARATION_SIZE,          (uint8_t *)&char_prop_write}},
            {"Char_C",             {7,(uint8_t *)&GATTS_CHAR_UUID_A3,       ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,       GATTS_CHAR_VAL_LEN_MAX, sizeof(char_value),             (uint8_t *)char_value}},
        };     

        static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
        void        example_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
        void        example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
        static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
        static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
        static bool matches_ble_base_uuid(const string &uuid);
        string      uuid_array2string(uint8_t* u, bool uu=false);  
        bool        uuid_str_to_le_bytes(const string &uuid, uint8_t out[ESP_UUID_LEN_128]);

    public:
        btle(const btle&)= delete;
        static btle& This(void){static btle Instance; return Instance;}
        btle& operator=(const btle&)=delete;
        bool start_process();
        void stop_process();
        void restart_process();
        // NV Storage Properties setters and getters
        void    set_uuid(string u){uuid_base = u;}
        string  get_uuid(void){return uuid_base;}
        void    set_use_bt(bool bt){if(bt==use_bt)return; use_bt = bt; start_process();}
        bool    get_use_bt(void){return use_bt;}
        btle&    add_subscription(string topic, function<void(string s)> do_this=nullptr,bool clear=false);

        /// @brief Format and publish data to a topic.
        /// @tparam T: type of the variable.
        /// @param data: data of type T to be published 
        /// @param subtopic: subtopic to be added to base_topic; defaults to NULL string. 
        /// @param precision: number of decimal places for floats or doubles; defaults to 1.
        /// @param full: if true, don't insert the base_topic, defaults to false.
        /// @return message ID of the published message or -1 for failure.
        template <typename T>
        btle& publish(T data, string handle_text, int precision=1, uint16_t _gatt_if=9999, uint16_t _conn_id=9999 ){
            if (state !=RUNNING) return *this;
            ostringstream os; 
            os.precision(precision);
            os<<fixed<<data;
            esp_ble_gatts_send_indicate((_gatt_if==9999)?gatt_if:_gatt_if, (_conn_id==9999)?conn_id:_conn_id,tmap[handle_text].Descriptor_index+base_handle,
                                                os.str().size(), (uint8_t*) os.str().c_str(), false);
            return *this;
        };
};
//extern btle& BTLE;

btle& BTLERef();
#define BTLE (BTLERef())

#endif //BHLE_H
#endif // CONFIG_BT_ENABLED