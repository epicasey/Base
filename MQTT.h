// MQTT.h version 1.3  Sep 21/2026 -- FIX: publish() sent every message twice (esp_mqtt_client_publish was called twice
//                                    per publish). It now publishes once. Any project using base/ (e.g. Generator) sends
//                                    half the MQTT traffic after this change. The 500 ms duplicate filter in the
//                                    MQTT_EVENT_DATA handler in MQTT.cpp was probably hiding this; it is left in place.
//                                    Also: publish() now passes the payload to the client via a std::string that outlives
//                                    the call instead of a pointer into a temporary.
// MQTT.h version 1.2  Jun 09/2026 -- Fixed reconnect issues
// MQTT.h version 1.1  Jan 14/2024 -- Added State Control
// MQTT.h version 1.0  Oct 23/202
#ifndef MQTT_H_
#define MQTT_H_

//#include "Common.h"
#include "base.h"
#include <unordered_map>
#include <functional>

#include "esp_wifi.h"
#include "mqtt_client.h"

struct Func {std::function<void(std::string s)> F; bool clear = true;};//structure of function pointer and bool
struct Message{std::string Topic; std::string Text;};

class mqtt{
public:
    mqtt(const mqtt&) = delete;
    static mqtt& This(void){static mqtt Instance; return Instance;}
    mqtt& operator=(const mqtt&)=delete;
    bool start_process(bool prerequisite = true);
  
    /// @brief Format and publish data to a topic.
    /// @tparam T: type of the variable.
    /// @param data: data of type T to be published 
    /// @param subtopic: subtopic to be added to base_topic; defaults to NULL std::string. 
    /// @param precision: number of decimal places for floats or doubles; defaults to 1.
    /// @param full: if true, don't insert the base_topic, defaults to false.
    /// @return message ID of the published message or -1 for failure.
    template <typename T>
   mqtt& publish(T data,std::string subtopic="",int precision=1,bool full = false){
        if(state!=RUNNING) return *this;
        auto full_topic = (full)?subtopic:base_topic+"/"+subtopic;
        std::ostringstream os; 
        os.precision(precision);
        os<<std::fixed<<data;
        // Publish once. (Before 2026-09-21 this called esp_mqtt_client_publish twice, so every message went out twice.)
        const std::string payload = os.str();
        this->log_error_if_nonzero("esp_mqtt_client_publish", esp_mqtt_client_publish(client, full_topic.c_str(), payload.c_str(), 0, 0, 0));
        return *this;
    };

    /// @brief Add a subscription entry to call a function when message arrives - Base topic pre-appended.
    /// @param topic no leading "/".
    /// @param do_this function pointer to be executed. 
    /// @param clear true=send a "" message to delete the topic.
    mqtt& add_subscription(const std::string& topic, std::function<void(std::string s)> do_this=nullptr, bool clear=false); 

    /// @brief Add a subscription entery to call a function when message arrives - No Base topic pre-appended.
    ///        Use this when subscribing to un-related messages
    /// @param topic function pointer to be executed.
    /// @param do_this function pointer to be executed. 
    /// @param clear true=send a "" message to delete the topic.
    mqtt& add_subscription_raw(const std::string& topic, std::function<void(std::string s)> do_this=nullptr, bool clear=false);
    mqtt& done_adding(void); 
    /// @brief Post all added subscriptions to server.
    /// @return Number of subscriptions posted.
    int subscribe(void);

    /// @brief Set flag so subscribe(void)is not blocked
    void re_subscribe(void);

    /// @brief Remove a subscription from map and server.
    /// @param topic 
    void unsubscribe(std::string topic);

    /// @brief getter for base topic
    /// @return std::string containing the base topic
    std::string base_topic_string(void);

private:
    mqtt(){};
    ~mqtt(){};
    enum State{STATE_OFF,STARTED,RUNNING};
    State  state {STATE_OFF};
    void load_event_handlers();
    void do_message_functions(Message s);
    esp_mqtt_client_config_t mqtt_cfg {};
    esp_mqtt_client_handle_t client;
    bool subscribed {false};
    std::string base_topic {"ESP"};
    std::unordered_map <std::string,Func>subscriptions;
    void load_subscriptions(void);
    void log_error_if_nonzero(const char *message, int error_code);
};
//extern mqtt& MQTT;

    mqtt& MQTTRef();
    #define MQTT (MQTTRef())

#endif //MQTT_H_