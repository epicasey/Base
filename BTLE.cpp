#include "sdkconfig.h"
#if CONFIG_BT_ENABLED == 1
#include "btle.h"
#include "NVStorage.h"

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool btle::matches_ble_base_uuid(const string &uuid) {
    static const char *base_uuid_suffix = "-0000-1000-8000-00805f9b34fb";
    if (uuid.size() != 36) {return false; }
    if (uuid.compare(8, 28, base_uuid_suffix) != 0) {return false;}
    for (size_t i = 0; i < 8; ++i) {if (hex_nibble(uuid[i]) < 0) {return false;} }
    return true;
}

btle::btle(){
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_cb = gatts_profile_event_handler;
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = ESP_GATT_IF_NONE;};
btle::~btle(){};

string btle::uuid_array2string(uint8_t* u, bool uu){
    ostringstream os;
    string s;
    if(uu)
        for (int i=15;i>=0;i--)
        os<<setw(2)<<setfill('0')<<hex<< (int)u[i];
    else{ 
        for (int i=15;i>=14;i--)
            os<<setw(2)<<setfill('0')<<hex<< (int)service_uuid[i];
        os<<setw(2)<<setfill('0')<<hex<< (int)u[1];
        os<<setw(2)<<setfill('0')<<hex<< (int)u[0];
        for (int i=11;i>=0;i--)
            os<<setw(2)<<setfill('0')<<hex<< (int)service_uuid[i];
    }
    s=os.str();s.insert(20,"-");s.insert(16,"-");s.insert(12,"-");s.insert(8,"-");
    return s;
}

bool btle::uuid_str_to_le_bytes(const string &uuid, uint8_t out[ESP_UUID_LEN_128]){
    string hex;
    hex.reserve(32);
    for (char c : uuid) {
        if (c != '-') {
            hex.push_back(c);
        }
    }
    if (hex.size() != 32) {
        return false;
    }

    for (int i = 0; i < ESP_UUID_LEN_128; ++i) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[(i * 2) + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        // ESP-IDF UUID arrays are little-endian.
        out[ESP_UUID_LEN_128 - 1 - i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

btle& btle::add_subscription(string topic,function<void(string s)> do_this,bool clear){
    if (subscriptions.find(topic)==subscriptions.end()){
        subscriptions[topic]={do_this,clear};
        COUT<<"'"<<topic<<"'  = "<<topic.size()<<endl;
    }
    return *this;
}

void btle::do_message_functions(string s){
        auto fit = subscriptions.find(s);
        if (fit != subscriptions.end()  && (s.size() > 0)){
            if(subscriptions[s].F != nullptr){
                COUT<<s<<endl;
                subscriptions[s].F(s);}
        }
}

void btle::gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param){
    switch (event) {
        case ESP_GATTS_REG_EVT:{
            const string dev_name = BTLE.Device_name.empty() ? string("HX711") : BTLE.Device_name;
            esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(dev_name.c_str()/*SAMPLE_DEVICE_NAME*/);
            if (set_dev_name_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "set device name failed, error code = %x", set_dev_name_ret);
            } else {
                ESP_LOGI(GATTS_TABLE_TAG, "device name set: %s", dev_name.c_str());
            }
            //config adv data
            esp_err_t ret = esp_ble_gap_config_adv_data(&BTLE.adv_data);
            if (ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config adv data failed, error code = %x", ret);
            }
            BTLE.adv_config_done |= ADV_CONFIG_FLAG;
            //config scan response data
            ret = esp_ble_gap_config_adv_data(&BTLE.scan_rsp_data);
            if (ret){
                ESP_LOGE(GATTS_TABLE_TAG, "config scan response data failed, error code = %x", ret);
            }
            vector<esp_gatts_attr_db_t> gatt_db;
            gatt_db.assign(BTLE.tmap.size(),{});
            for(auto it:BTLE.tmap)
                gatt_db[it.second.Descriptor_index] = {{ESP_GATT_AUTO_RSP},{ESP_UUID_LEN_16,it.second.UUID,it.second.perm,it.second.max_length,it.second.length,it.second.value}};
            BTLE.adv_config_done |= SCAN_RSP_CONFIG_FLAG;
            esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db.data(), gatts_if, gatt_db.size(), SVC_INST_ID);
            cout << "Service Declaration: "<<BTLE.uuid_array2string(BTLE.tmap["Service_Declaration"].value)<<endl;
            cout << "Weight UUID: " <<BTLE.uuid_array2string(BTLE.tmap["Weight"].UUID)<<endl;
            cout << "Units UUID:  "  <<BTLE.uuid_array2string(BTLE.tmap["Units"].UUID)<<endl;
            cout << "Char_C UUID: " <<BTLE.uuid_array2string(BTLE.tmap["Char_C"].UUID)<<endl;
            if (create_attr_ret){
                ESP_LOGE(GATTS_TABLE_TAG, "create attr table failed, error code = %x", create_attr_ret);
            }
        }
       	    break;
        case ESP_GATTS_READ_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_READ_EVT");
       	    break;
        case ESP_GATTS_WRITE_EVT:
            if (!param->write.is_prep){
                // the data length of gattc write  must be less than GATTS_CHAR_VAL_LEN_MAX.
                ESP_LOGI(GATTS_TABLE_TAG, "GATT_WRITE_EVT, handle = %d, value len = %d, value : ", param->write.handle, param->write.len);

                // Only parse text commands from the dedicated command characteristic.
                // Descriptor and binary writes (e.g. CCCD) are not commands.
                if (param->write.handle == BTLE.heart_rate_handle_table[BTLE.tmap["Char_C"].Descriptor_index] && param->write.len > 0) {
                    string s;
                    s.assign((char*)param->write.value, param->write.len);
                    if (!s.empty() && s.back() == '\0') {
                        s.pop_back();
                    }
                    BTLE.do_message_functions(s);
                }
                
                if (BTLE.heart_rate_handle_table[BTLE.tmap["Weight_CFG"].Descriptor_index] == param->write.handle && param->write.len == 2){
                    uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                    if (descr_value == 0x0001){
                        ESP_LOGI(GATTS_TABLE_TAG, "notify enable");
                        printf("gatt_if: %d  conn_id: %d  handle: %d\n", gatts_if,param->write.conn_id,param->write.handle );///////////////////
                   }else if (descr_value == 0x0002){
                        ESP_LOGI(GATTS_TABLE_TAG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i % 0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, BTLE.heart_rate_handle_table[BTLE.tmap["Weight"].Descriptor_index],
                                            sizeof(indicate_data), indicate_data, true);
                    }
                    else if (descr_value == 0x0000){
                        ESP_LOGI(GATTS_TABLE_TAG, "notify/indicate disable ");
                    }else{
                        ESP_LOGE(GATTS_TABLE_TAG, "unknown descr value");
                        ESP_LOG_BUFFER_HEX(GATTS_TABLE_TAG, param->write.value, param->write.len);
                    }
                }
                /* send response when param->write.need_rsp is true*/
                if (param->write.need_rsp){
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                }
            }else{
                /* handle prepare write */
                BTLE.example_prepare_write_event_env(gatts_if, &BTLE.prepare_write_env, param);
            }
      	    break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            // the length of gattc prepare write data must be less than GATTS_CHAR_VAL_LEN_MAX.
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            BTLE.example_exec_write_event_env(&BTLE.prepare_write_env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_CONF_EVT:
            //ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONF_EVT, status = %d, attr_handle %d gatts_id %d", param->conf.status, param->conf.handle, gatts_if);
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
            BTLE.base_handle = param->start.service_handle;
            break;
        case ESP_GATTS_CONNECT_EVT:{
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
            ESP_LOG_BUFFER_HEX(GATTS_TABLE_TAG, param->connect.remote_bda, 6);           
            memcpy(BTLE.conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            // For the iOS system, please refer to Apple official documents about the BLE connection parameters restrictions.
            BTLE.conn_params.latency = 0;
            BTLE.conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
            BTLE.conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
            BTLE.conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
            //start sent the update connection parameters to the peer device.
            esp_err_t conn_ret = esp_ble_gap_update_conn_params(&BTLE.conn_params);
            if (conn_ret != ESP_OK) {
                ESP_LOGW(GATTS_TABLE_TAG, "update conn params failed: %s", esp_err_to_name(conn_ret));
            }
            //set variables used to access the Characteristics from ourside of the event loop
            BTLE.state = RUNNING;
            BTLE.gatt_if   = gatts_if;
            BTLE.conn_id   = param->connect.conn_id;
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT:{
            BTLE.state = STARTED;
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = 0x%x", param->disconnect.reason);
            if(BTLE.use_bt) {
                esp_err_t adv_ret = esp_ble_gap_start_advertising(&BTLE.adv_params);
                if (adv_ret != ESP_OK) {
                    ESP_LOGE(GATTS_TABLE_TAG, "restart advertising failed: %s", esp_err_to_name(adv_ret));
                }
            }
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
            if (param->add_attr_tab.status != ESP_GATT_OK){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
            }
            else if (param->add_attr_tab.num_handle != BTLE.tmap.size() /*HRS_IDX_NB*/){
                ESP_LOGE(GATTS_TABLE_TAG, "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, BTLE.tmap.size()/*HRS_IDX_NB*/);
            }
            else {
                ESP_LOGI(GATTS_TABLE_TAG, "create attribute table successfully, the number handle = %d\n",param->add_attr_tab.num_handle);
                BTLE.heart_rate_handle_table.assign(param->add_attr_tab.handles,param->add_attr_tab.handles+BTLE.tmap.size());
                esp_ble_gatts_start_service(BTLE.heart_rate_handle_table[BTLE.tmap["Service_Declaration"].Descriptor_index /*IDX_SVC*/]);
            }
            break;
        }
        case ESP_GATTS_STOP_EVT:
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        case ESP_GATTS_UNREG_EVT:
        case ESP_GATTS_DELETE_EVT:
        default:
            break;
    }
}

void btle::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param){
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            BTLE.adv_config_done &= (~ADV_CONFIG_FLAG);
            if (BTLE.adv_config_done == 0 && BTLE.use_bt){
                esp_err_t adv_ret = esp_ble_gap_start_advertising(&BTLE.adv_params);
                if (adv_ret != ESP_OK) {
                    ESP_LOGE(GATTS_TABLE_TAG, "start advertising (adv data done) failed: %s", esp_err_to_name(adv_ret));
                }
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            BTLE.adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
            if (BTLE.adv_config_done == 0 && BTLE.use_bt){
                esp_err_t adv_ret = esp_ble_gap_start_advertising(&BTLE.adv_params);
                if (adv_ret != ESP_OK) {
                    ESP_LOGE(GATTS_TABLE_TAG, "start advertising (scan rsp done) failed: %s", esp_err_to_name(adv_ret));
                }
            }
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            /* advertising start complete event to indicate advertising start successfully or failed */
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "advertising start failed");
            }else{
                ESP_LOGI(GATTS_TABLE_TAG, "advertising start successfully");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed");
            }
            else {
                ESP_LOGI(GATTS_TABLE_TAG, "Stop adv successfully\n");
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
                  BTLE.do_message_functions("Reload");
            break;
        default:
            break;
    }
}

void btle::example_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);
    esp_gatt_status_t status = ESP_GATT_OK;
    if (prepare_write_env->prepare_buf == NULL) {
        prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
        prepare_write_env->prepare_len = 0;
        if (prepare_write_env->prepare_buf == NULL) {
            ESP_LOGE(GATTS_TABLE_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    } else {
        if(param->write.offset > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_OFFSET;
        } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
            status = ESP_GATT_INVALID_ATTR_LEN;
        }
    }
    /*send response when param->write.need_rsp is true */
    if (param->write.need_rsp){
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL){
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK){
               ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
            }
            free(gatt_rsp);
        }else{
            ESP_LOGE(GATTS_TABLE_TAG, "%s, malloc failed", __func__);
        }
    }
    if (status != ESP_GATT_OK){
        return;
    }
    memcpy(prepare_write_env->prepare_buf + param->write.offset,
           param->write.value,
           param->write.len);
    prepare_write_env->prepare_len += param->write.len;
}

void btle::example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param){
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf){
        string s; s.assign((char*)prepare_write_env->prepare_buf, prepare_write_env->prepare_len);////////////////// to turn message to text
        cout <<"Here is: "<<s<<endl;
        ESP_LOG_BUFFER_HEX(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }else{
        ESP_LOGI(GATTS_TABLE_TAG,"ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf) {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    prepare_write_env->prepare_len = 0;
}

void btle::gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param){
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            BTLE.heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGE(GATTS_TABLE_TAG, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == BTLE.heart_rate_profile_tab[idx].gatts_if) {
                if (BTLE.heart_rate_profile_tab[idx].gatts_cb) {
                    BTLE.heart_rate_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

bool btle::start_process(){
    if (state == STARTED) return true;
    if (state == RUNNING) return false;
    string n {"Unnamed"}; NVS.read("disp_name",n);
    string u {"Not Set"}; NVS.read("UUID",u);
    int b =atoi(NVS.cread("Bluetooth"));
    string new_uuid = uuid_base;
    if (matches_ble_base_uuid(u)) {
        new_uuid = u;
    } else {
        LOGW("Invalid UUID in NVS: '%s', using default UUID", u.c_str());
    }
    LOGI( "Starting Bluetooth: %s  UUID: %s", n.c_str(), new_uuid.c_str());
    Device_name = n;
    use_bt = b;
    uuid_base = new_uuid;
    stop_process();
    if (use_bt) restart_process();
    return true;
}

void btle::stop_process(){
    if (state!=RUNNING) return;
    esp_ble_gatts_close(gatt_if, conn_id);
    esp_ble_gap_disconnect(conn_params.bda);
    esp_ble_gap_update_conn_params(&conn_params);
    esp_ble_gap_stop_advertising();

    esp_ble_gatts_app_unregister(gatt_if);
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    state = OFF;
    return;
}

void btle::restart_process(){
    if(state == RUNNING) return;

    uint8_t parsed_uuid[ESP_UUID_LEN_128];
    if (uuid_str_to_le_bytes(uuid_base, parsed_uuid)) {
        memcpy(service_uuid, parsed_uuid, ESP_UUID_LEN_128);
    } else {
        ESP_LOGW(GATTS_TABLE_TAG, "invalid UUID '%s', using previous/default UUID", uuid_base.c_str());
    }

    // Keep ADV packet short and place 128-bit UUID in scan response for compatibility.
    adv_data.p_service_uuid = nullptr;
    adv_data.service_uuid_len = 0;
    scan_rsp_data.p_service_uuid = service_uuid;
    scan_rsp_data.service_uuid_len = ESP_UUID_LEN_128;
    adv_config_done = 0;
    esp_err_t ret;

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));return;}

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));return;}

    ret = esp_bluedroid_init();
    if (ret) {ESP_LOGE(GATTS_TABLE_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));return;}

    ret = esp_bluedroid_enable();
    if (ret) {ESP_LOGE(GATTS_TABLE_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));return;}

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret){ESP_LOGE(GATTS_TABLE_TAG, "gatts register error, error code = %x", ret);return;}

    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret){ESP_LOGE(GATTS_TABLE_TAG, "gap register error, error code = %x", ret);return;}

    ret = esp_ble_gatts_app_register(ESP_APP_ID);
    if (ret){ESP_LOGE(GATTS_TABLE_TAG, "gatts app register error, error code = %x", ret);return;}

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){ESP_LOGE(GATTS_TABLE_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);return;}

    LOGI("Bluetooth started with name: %s  UUID: %s", Device_name.c_str(), uuid_base.c_str());
    state = STARTED;
    return;
}

btle& BTLERef(){ // .cpp
	return btle::This();
}

#endif // CONFIG_BT_ENABLED