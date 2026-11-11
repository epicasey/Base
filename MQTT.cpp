// MQTT.cpp version 1.2  Jun 09/2026 -- Fixed reconnect issues
// MPTT.cpp version 1.1  Jan 14/2024 //Added State Control
// MPTT.cpp version 1.0  Oct 23/2023

#include "MQTT.h"
#include "wifi.h"
#include "NVStorage.h"
#include "esp_timer.h"
    
//mqtt&   MQTT = mqtt::This();
void mqtt::log_error_if_nonzero(const char *message, int error_code){
    if (error_code != 0) {
        LOGE("Last error %s: 0x%x", message, error_code);
    }
}

void mqtt::load_event_handlers(){
    #define typeM void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data
    std::unordered_map<esp_mqtt_event_id_t,void(*)(void*, esp_event_base_t, int32_t, void *)>event_map;
    event_map[MQTT_EVENT_CONNECTED]=
        [](typeM){
            LOGI("MQTT_EVENT_CONNECTED");
            esp_mqtt_client_publish(This().client, This().base_topic.c_str(), "On Line", 0, 0, 0);
            This().state = RUNNING;
            This().subscribe();       
        };

    event_map[MQTT_EVENT_DISCONNECTED]=
        [](typeM){
            LOGI("MQTT_EVENT_DISCONNECTED");
            while (!WIFI.got_ip()) {
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
            This().re_subscribe();
            esp_mqtt_client_reconnect(This().client);     
        };

    event_map[MQTT_EVENT_SUBSCRIBED]=
        [](typeM){
            LOGI("MQTT_EVENT_SUBSCRIBED, msg_id=%d", static_cast<esp_mqtt_event_handle_t>(event_data)->msg_id);        
        };

    event_map[MQTT_EVENT_UNSUBSCRIBED]=
        [](typeM){
            LOGI("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", static_cast<esp_mqtt_event_handle_t>(event_data)->msg_id);         
        };   

    event_map[MQTT_EVENT_PUBLISHED]=
        [](typeM){
            LOGI("MQTT_EVENT_PUBLISHED, msg_id=%d",static_cast<esp_mqtt_event_handle_t>(event_data)->msg_id);         
        }; 

    event_map[MQTT_EVENT_DATA]=
        [](typeM){
            auto event = static_cast<esp_mqtt_event_handle_t>(event_data);
            std::string t(event->topic,event->topic_len);
            std::string d(event->data, event->data_len);
//            LOGW("MQTT_EVENT_DATA, topic=%s, data=%s", t.c_str(), d.c_str());
            d.erase(d.begin(), std::find_if(d.begin(), d.end(), [](char c){ return c != ' '; }));
            if(d.size()>0) {
                static std::unordered_map<std::string, std::pair<std::string, int64_t>> last_by_topic;
                constexpr int64_t dedupe_window_us = 500000; // 500 ms
                int64_t now_us = esp_timer_get_time();
                auto &entry = last_by_topic[t];
                if (entry.first == d && (now_us - entry.second) < dedupe_window_us) {
                    LOGW("Duplicate message ignored, topic=%s, data=%s", t.c_str(), d.c_str());
                    return;
                }
                entry = {d, now_us};
                LOGW("MQTT_EVENT_DATA, topic=%s, data=%s", t.c_str(), d.c_str());
                This().do_message_functions({t.c_str(),d.c_str()});
            } else {
                LOGW("MQTT_EVENT_DATA, topic=%s, data is empty", t.c_str());
            }
        }; 

    event_map[MQTT_EVENT_ERROR]=
        [](typeM){
           auto event = static_cast<esp_mqtt_event_handle_t>(event_data);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                This().log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                This().log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                This().log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                LOGI("Last errno std::string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
        };
        for (auto [key,val]:event_map){
           esp_mqtt_client_register_event(client, key,val, NULL);
        };
}

bool mqtt::start_process(bool prerequisite){
    if(state == STARTED) return true;
    if (state != STATE_OFF || !prerequisite) return false;
    NVS.load();
    std::string s;
    // Set up default mqtt server connection
    if(!NVS.read("mqtt_uri", (s="mqtt://192.168.50.8"))) NVS.write("mqtt_uri", s);
    if(!NVS.read("mqtt_port",(s="1883")))  NVS.write("mqtt_port", s);
    if(!NVS.read("mqtt_usr", (s="ESP32"))) NVS.write("mqtt_usr",  s);
    if(!NVS.read("mqtt_pwd", (s="2A4RQ"))) NVS.write("mqtt_pwd",  s);

    LOGD("URI = %s;",NVS.cread("mqtt_uri"));
    base_topic += WIFI.get_ip().substr(WIFI.get_ip().find_last_of(".")+1);
    mqtt_cfg.broker.address.uri    = NVS.cread("mqtt_uri");
    mqtt_cfg.broker.address.port   = atoi(NVS.cread("mqtt_port"));
    mqtt_cfg.credentials.username  = NVS.cread("mqtt_usr");
    mqtt_cfg.credentials.client_id = base_topic.c_str();
    mqtt_cfg.credentials.authentication.password = NVS.cread("mqtt_pwd");
    mqtt_cfg.session.last_will.topic = base_topic.c_str();
    mqtt_cfg.session.last_will.msg   = "Off Line";
    mqtt_cfg.task.stack_size   = 3072;
    mqtt_cfg.buffer.size       = 256;
    mqtt_cfg.buffer.out_size   = 256;
    mqtt_cfg.session.keepalive = 250;
    client = esp_mqtt_client_init(&mqtt_cfg); /* The last argument may be used to pass data to mqtt_event_handler */
    load_event_handlers();
    esp_mqtt_client_start(client);
    state = STARTED;
    return true;
}

 mqtt& mqtt::add_subscription(const std::string& topic, std::function<void(std::string s) > do_this, bool clear){
    if(subscriptions.find(base_topic+"/"+topic) == subscriptions.end())
        subscriptions[base_topic+"/"+topic] = {do_this,clear};
    return *this;
 }

 mqtt& mqtt::add_subscription_raw(const std::string& topic, std::function<void(std::string s) > do_this, bool clear){
    if(subscriptions.find(topic) == subscriptions.end())
        subscriptions[topic] = {do_this,clear};
    return *this;
 }

 mqtt& mqtt::done_adding(void){
    if(state!=RUNNING) return *this;
    load_subscriptions();
    return *this;
 }

int mqtt::subscribe(){
    if(state!=RUNNING) return -1;
	if (subscribed) return true;
	subscribed = true;
    load_subscriptions();
	return subscriptions.size();
}

void mqtt::re_subscribe() {
    subscribed = false;
 }

void mqtt::load_subscriptions(void){
    for (auto & it:subscriptions){
        LOGI("MQTT Subscribing to %s",it.first.c_str());
        esp_mqtt_client_subscribe(client, it.first.c_str(), 0);
    }
 }

 void mqtt::unsubscribe(std::string topic){
    std::string s = base_topic+"/"+topic;
    esp_mqtt_client_unsubscribe(client, s.c_str());
    auto it = subscriptions.find(s);
    if (it != subscriptions.end()) subscriptions.erase(it);
 }

void mqtt::do_message_functions(Message s){
     auto fit = subscriptions.find(s.Topic);
        if (fit != subscriptions.end()  && (s.Text.size() > 0)){
            if(subscriptions[s.Topic].F != nullptr)
                subscriptions[s.Topic].F(s.Text);
            if(subscriptions[s.Topic].clear)
                publish("",s.Topic,0,true);
        }
}

std::string mqtt::base_topic_string(void){return base_topic;};

mqtt& MQTTRef(){ // .cpp
	return mqtt::This();
}