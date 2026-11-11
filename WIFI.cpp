/*
 *  wifi.cpp - A class to manage wifi connections on the ESP32, including SmartConfig and AP mode.
 * 
 *  Created on: Oct 23, 2023
 *  Updated May 19, 2026
 *  Updated June 17, 2026 version 1.1 -- fixed connection retry issues
 */
#include "wifi.h"
#include "esp_wifi_types.h"
#include <ranges>
#include <span>
#include <cstring>

static const int64_t SOFTAP_TIMEOUT_US = 5LL * 60LL * 1000000LL; // minutes * seconds * microseconds

static void softap_timeout_cb(void *arg) {
    wifi *self = static_cast<wifi*>(arg);
    if (!self) return;
    LOGW("SoftAP did not get a STA connection within 5 minutes, restarting WiFi");
    WIFI.ReStart(true); //restart Wifi with a fresh_start (sta_connect_attempt_count = 0)
}
// replacment for strncpy() to copy string to a c-style array
auto copy = [](std::string s, uint8_t(&arr)[],size_t sz){std::span<uint8_t> sp(arr,sz); for(int i=0; i<sp.size() ;i++){if(i==s.size()){sp[i]='\0'; break;}sp[i]=s[i];}};

//wifi&   WIFI = wifi::This();

void wifi::register_wifi_events(){ // emap is loaded then cleared once events are registered
    #define typeM void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data
    std::unordered_map<ev,void(*)(typeM)>emap; //map of event handlers; the key is an <event_base, event_id> pair, and the value is a pointer to the corresponding event handler function; this allows us to easily register all event handlers in a loop, and also keeps the code organized by keeping all event handlers in one place

    //************** Start of event map functions ************************************
        emap[{WIFI_EVENT_STA_START,WIFI_EVENT}] = 
    [](typeM){
        LOGI("WIFI_EVENT_STA_START - Scanning for SSIDs");
        esp_wifi_scan_start(NULL, true);
    };   

    emap[{WIFI_EVENT_SCAN_DONE,WIFI_EVENT}] = 
    [](typeM){
        uint16_t number,idx {0};
        esp_wifi_scan_get_ap_num(&number);
        std::vector<wifi_ap_record_t> ap_records(number); // heap allocation in case array is too big for stack - esp_wifi_scan_get_ap_records() will write to this array
        esp_wifi_scan_get_ap_records(&number, ap_records.data());
        std:: ostringstream os;
        while (idx < number){//create a map of all active ssids for which we have credentials
            SSID SS {(char*)ap_records[idx].ssid,""};
            std::string s("   Discarded: ");
            if(NVS.read(SS.ssid, SS.password,"SSIDStorage")){
                This().ap_rec[0-ap_records[idx].rssi] = SS;
                s = "   Retained:  ";
            } 
            os<<std::endl<<s<<ap_records[idx].ssid << " (AuthMode "<< ap_records[idx].authmode << "; rssi " << (int)ap_records[idx].rssi << ")"; 
            idx++;
        }
        LOGI("WIFI_EVENT_SCAN_DONE - Found %i SSIDs:%s",number,os.str().c_str());
        if (This().Connect() == ESP_FAIL) This().wifi_init_softap();
    };
    
    emap[{WIFI_EVENT_STA_DISCONNECTED,WIFI_EVENT}] = 
    [](typeM){
        wifi_event_sta_disconnected_t * event = (wifi_event_sta_disconnected_t*) event_data;
        This().GOT_IP = false;
        This().ip_string.clear();
        LOGI("WIFI_EVENT_STA_DISCONNECTED from %s Reason = %s",event->ssid,This().wifiErrors[event->reason].c_str());
        This().ReStart(false); //restart with existing sta_connect_attempt_count
    };

    emap[{WIFI_EVENT_STA_CONNECTED,WIFI_EVENT}] = 
    [](typeM){
        wifi_config_t conf;
        esp_wifi_get_config(WIFI_IF_STA,&conf);
        LOGI("WIFI_EVENT_STA_CONNECTED - STA Connected to %s ",conf.sta.ssid);
    };

    emap[{IP_EVENT_STA_GOT_IP,IP_EVENT}] =
    [](typeM){
        ip_event_got_ip_t *ipst = (ip_event_got_ip_t*)event_data;
        This().ip_string = This().ip4_str(ipst->ip_info.ip);
        This().GOT_IP=true;
        LOGW("IP_EVENT_STA_GOT_IP - Got IP {%s}",This().ip_string.c_str());
    };

    emap[{WIFI_EVENT_AP_START,WIFI_EVENT}] =
    [](typeM){
        LOGI("WIFI_EVENT_AP_START - waiting for client to connect");
    };

    emap[{WIFI_EVENT_AP_STACONNECTED,WIFI_EVENT}] = 
    [](typeM){
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        LOGI("WIFI_EVENT_AP_STACONNECTED - station " MACSTR " connected, AID=%d",
                MAC2STR(event->mac), event->aid);
        This().cancel_softap_timeout();
    };

    emap[{WIFI_EVENT_AP_STADISCONNECTED,WIFI_EVENT}] = 
    [](typeM){
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        LOGI("WIFI_EVENT_AP_STADISCONNECTED - station " MACSTR " leave, AID=%d", MAC2STR(event->mac), event->aid);
        This().ReStart(true); //retart fresh (with sta_connect_attempt_count = 0)
    };
    //************** End of event map functions ************************************
    for(auto [key,val]:emap) ESP_ERROR_CHECK( esp_event_handler_register(key.base, key.id,  val, (void*)This) );
}

void wifi::initialise_wifi(bool Block){
    ESP_ERROR_CHECK(esp_netif_init());
    NVS.load("SSIDStorage");
    LOGI("Initializing");
    if(!netif_sta) netif_sta = esp_netif_create_default_wifi_sta();
    std::string hostname("ESP32"); // Default hostname
    NVS.read("disp_name",hostname);
    std::replace(hostname.begin(), hostname.end(), ' ', '_');
    esp_netif_set_hostname(netif_sta, hostname.c_str());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg) );
    register_wifi_events();
    ReStart(true); //restart fresh (with attempt_count = 0)
    LOGI("Waiting for valid IP");
    ap_rec.clear();
    if(Block) while(!got_ip())vTaskDelay(5);
    LOGI("Valid IP assigned");
}

bool wifi::got_ip(void){return GOT_IP;}
std::string_view wifi::get_ip(void){return ip_string;}   
std::string_view wifi::get_id(void){
	std::size_t lastDotPos = ip_string.find_last_of('.');
    return ip_string.substr(lastDotPos + 1);} //xxx.xxx.xxx.xxx


/////SoftAP - Become an Access Point if no known SSIDs are found, and wait for a connection to get new credentials via SmartConfig
esp_err_t wifi::wifi_init_softap(void){
    if (!netif_ap)  netif_ap = esp_netif_create_default_wifi_ap();
    wifi_config_t wifi_config {};
    copy(ESP_AP_WIFI_SSID,wifi_config.ap.ssid,sizeof(wifi_config.sta.ssid));
    copy(ESP_AP_WIFI_PASS,wifi_config.ap.password,sizeof(wifi_config.sta.password));
    wifi_config.ap.ssid_len = strlen(ESP_AP_WIFI_SSID);
    wifi_config.ap.channel = ESP_WIFI_CHANNEL;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.max_connection = MAX_STA_CONN;
    wifi_config.ap.pmf_cfg.required = false;
    if (strlen(ESP_WIFI_PASS) == 0) {wifi_config.ap.authmode = WIFI_AUTH_OPEN;}
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    start_softap_timeout();
    return ESP_OK;
}

void wifi::start_softap_timeout(void) {
    cancel_softap_timeout();
    esp_timer_create_args_t timer_args;
    timer_args.callback = softap_timeout_cb;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "softap_timeout";
    timer_args.skip_unhandled_events = false;
    if (esp_timer_create(&timer_args, &ap_timeout_timer) == ESP_OK) {
        esp_timer_start_once(ap_timeout_timer, SOFTAP_TIMEOUT_US);
    }
}

void wifi::cancel_softap_timeout(void) {
    if (ap_timeout_timer) {
        esp_timer_stop(ap_timeout_timer);
        esp_timer_delete(ap_timeout_timer);
        ap_timeout_timer = nullptr;
    }
}

std::string wifi::ip4_str(esp_ip4_addr_t ip) {
    const uint8_t* bytes = (const uint8_t*)(&ip);
    return to_string(bytes[0]) + "." + 
           to_string(bytes[1]) + "." + 
           to_string(bytes[2]) + "." + 
           to_string(bytes[3]);
}
esp_err_t wifi::Connect(){  
//    esp_log_level_set("wifi", ESP_LOG_NONE);
    wifi_config_t wifi_config {};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA,&wifi_config));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    while (sta_connect_attempt_count<tries && !ap_rec.empty()) {
        // rotate through the retained SSIDs (strongest first) instead of erasing each after one try,
        // so a transient failure (e.g. AUTH_EXPIRE) is retried before we give up and start the softAP
        auto it = std::next(ap_rec.begin(), sta_connect_attempt_count % ap_rec.size());
        copy(it->second.ssid,wifi_config.sta.ssid,sizeof(wifi_config.sta.ssid));
        copy(it->second.password,wifi_config.sta.password,sizeof(wifi_config.sta.password));
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        LOGI("   Trying to connect to %s with '%s' sta_connect_attempt_count %i", wifi_config.sta.ssid, wifi_config.sta.password, sta_connect_attempt_count);
        sta_connect_attempt_count++;
        if (esp_wifi_connect() == ESP_OK) return ESP_OK;
        vTaskDelay(250 / portTICK_PERIOD_MS);
    }
    return ESP_FAIL;
}

esp_err_t wifi::ReStart(bool fresh_start){
    if (fresh_start) sta_connect_attempt_count = 0;  
    if(sta_connect_attempt_count){ 
        if(sta_connect_attempt_count<tries && !ap_rec.empty() ) return Connect();
        sta_connect_attempt_count = 0; return wifi_init_softap();
    }
    LOGI("Restarting WiFi");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return esp_wifi_start();
}

wifi& wifiRef(){ // .cpp
	return wifi::This();
}