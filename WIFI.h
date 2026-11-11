// wifi.h version 1.1  Jun 17/2026 -- fixed connection retry issues
// wifi.h version 1.0  Oct 23/2023
#ifndef WIFICOPY_H_
#define WIFICOPY_H_

//#include "Common.h"
#include "base.h"
#include <unordered_map>
#include <map>
#include <functional>

#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_smartconfig.h"
#include "esp_mac.h"

#include "NVStorage.h"

// Defaults in case NV Storage is corrupt or not setup 
#define ESP_AP_WIFI_SSID   "ESP32"
#define ESP_AP_WIFI_PASS   "Webserver" //minimum 8 characters
#define ESP_WIFI_SSID      "ESP32" 
#define ESP_WIFI_PASS      "Webserver"  //minimum 8 characters
#define ESP_WIFI_CHANNEL   6 //CONFIG_ESP_WIFI_CHANNEL
#define MAX_STA_CONN       4 //CONFIG_ESP_MAX_STA_CONN

/// @brief An <event_base, event_id> pair for use as a map key; includes operator< overload and hash specialization to allow use as a key in an unordered_map
struct ev{
    int32_t id;
    esp_event_base_t base;
    friend bool operator<(const ev& l, const ev& r) {
        return std::tie(l.id, l.base) < std::tie(r.id, r.base);
    }
    friend bool operator==(const ev& l, const ev& r) {
        return l.id == r.id && l.base == r.base;
    }
};

namespace std { //hash specialization for ev struct to allow use as a key in an unordered_map
    template<> struct hash<ev> {
        size_t operator()(const ev& e) const noexcept {
            return std::hash<int32_t>()(e.id) ^ (std::hash<const void*>()((const void*)e.base) << 1);
        }
    };
}

class wifi{
private:
    wifi(){};
    ~wifi(){};
    static constexpr int tries = 6;
    bool GOT_IP     {false};
    esp_netif_t     *netif_sta = nullptr;
    esp_netif_t     *netif_ap  = nullptr;
    std::string     ip_string; //string containing the ip address in the xxx.xxx.xxx.xxx format
    typedef struct {std::string ssid; std::string password;} SSID;
    std::map<int,SSID> ap_rec; //map of SSID to password for all known APs; this is populated on startup by scanning for nearby APs and checking if we have credentials stored for any of them in NV memory
    esp_timer_handle_t ap_timeout_timer {nullptr}; //timer handle for the SoftAP timeout timer; if we fail to connect to any known APs, we will start a SoftAP and wait for new credentials to be entered via the app; if no credentials are entered within a certain time period, we will restart the ESP and try agai
    void        register_wifi_events(void);
    esp_err_t       wifi_init_softap(void);
    void        start_softap_timeout(void);
    void        cancel_softap_timeout(void);
    std::string ip4_str(esp_ip4_addr_t ip);
    int sta_connect_attempt_count {0}; //sta_connect_attempt_count of connection attempts to current SSID; if this exceeds a threshold, we will move on to the next SSID in the list
//    int index {0}; //index of current SSID in the list of known SSIDs; if we exhaust the list, we will start the SoftAP and wait for new credentials to be entered via the app

public:
    wifi(const wifi&)= delete;
    static wifi& This(void){static wifi Instance; return Instance;}
    wifi& operator=(const wifi&)=delete;
    
    /// @brief  Attempt to connect to an STA
    /// @return ESP_OK = connection successful; ESP_FAIL = connection failed (e.g., wrong password, AP not found, etc.)
    esp_err_t Connect(void);

    /// @brief Restart the ESP32; this is called if we fail to connect to any known APs and also fail to get new credentials via the SoftAP within the timeout period; this allows us to start the connection process over from the beginning, which can be useful if, for example, we were in an area with no known APs and then moved to an area with known APs
    /// @return ESP_OK if successful, otherwise an error code
    esp_err_t ReStart(bool fresh_start = true);

    /// @brief Disconnect from the current WiFi network
    /// @return ESP_OK if successful, otherwise an error code
    esp_err_t Disconnect(void){
        LOGI("Disconnecting WiFi");
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        return ESP_OK;
    };

    /// @brief Set up and Start the wifi system.
    /// @param Block true (default) = block until a valid IP is obtained; false = no block
    void    initialise_wifi(bool Block=true);

    /// @brief Check if there is a valid IP obtained
    /// @return true = valid ip has been obtained
    bool    got_ip(void);

    /// @brief Get the IP address that has been obtained
    /// @return string_view containing the IP address in the xxx.xxx.xxx.xxx format
    std::string_view get_ip(void);
	
	/// @brief Get the id (list 3 digits of the url) that has been obtained
    /// @return string_view containing the id in the xxx format
	std::string_view get_id(void);

    /// @brief Dump the configuration of a WiFi station to the console for debugging purposes
    /// @param s The wifi_sta_config_t struct containing the station configuration to be dumped    
    void dump_sta(wifi_sta_config_t s){
        std::cout<<std::endl<< s.ssid      /**< SSID of target AP. */
            <<std::endl<<s.password  /**< Password of target AP. */
            <<std::endl<<"scan_metnod: "<<s.scan_method    /**< do all channel scan or fast scan */
            <<std::endl<<"bssid_set: "<<s.bssid_set        /**< whether set MAC address of target AP or not. Generally, station_config.bssid_set needs to be 0; and it needs to be 1 only when users need to check the MAC address of the AP.*/
            <<std::endl<<"bssid "<<s.bssid     /**< MAC address of target AP*/
            <<std::endl<<"channel: "<<(int)s.channel       /**< channel of target AP. Set to 1~13 to scan starting from the specified channel before connecting to AP. If the channel of AP is unknown, set it to 0.*/
            <<std::endl<<"listen_interval: "<<s.listen_interval   /**< Listen interval for ESP32 station to receive beacon when WIFI_PS_MAX_MODEM is set. Units: AP beacon intervals. Defaults to 3 if set to 0. */
            <<std::endl<<"sort_method: "<< s.sort_method    /**< sort the connect AP in the list by rssi or security mode */
            <<std::endl<<"threshold.rssi: "<< (int) s.threshold.rssi     /**< When sort_method is set, only APs which have an auth mode that is more secure than the selected auth mode and a signal stronger than the minimum RSSI will be used. */
            <<std::endl<<"threshold.authmode: "<< s.threshold.authmode
            <<std::endl<<"pmf_cfg.capable: "<< s.pmf_cfg.capable    /**< Configuration for Protected Management Frame. Will be advertized in RSN Capabilities in RSN IE. */
            <<std::endl<<"pmf_cfg.required: "<< s.pmf_cfg.required
            <<std::endl<<"rm_enabled: "<< s.rm_enabled        /**< Whether Radio Measurements are enabled for the connection */
            <<std::endl<<"btm_method: "<<s.btm_enabled       /**< Whether BSS Transition Management is enabled for the connection */
            <<std::endl<<"mbo_enabled: "<< s.mbo_enabled       /**< Whether MBO is enabled for the connection */
            <<std::endl<<"ft_enabled: "<< s.ft_enabled        /**< Whether FT is enabled for the connection */
            <<std::endl<<"owe_enabled: "<< s.owe_enabled       /**< Whether OWE is enabled for the connection */
            <<std::endl<<"reserved1: "<< s.reserved1         /**< Reserved for future feature set */
            <<std::endl<<"sae_pwe_h2e: "<< s.sae_pwe_h2e     /**< Whether SAE hash to element is enabled */
            <<std::endl;
    };

    /// @brief Map of WiFi disconnect reason codes to their corresponding string representations for easier debugging and logging; this can be used, for example, in the WIFI_EVENT_STA_DISCONNECTED event handler to log the reason for disconnection in a human-readable format
    std::unordered_map<uint8_t,std::string>wifiErrors{
        {1,"WIFI_REASON_UNSPECIFIED"},
        {2,"WIFI_REASON_AUTH_EXPIRE"},
        {3,"WIFI_REASON_AUTH_LEAVE"},
        {4,"WIFI_REASON_ASSOC_EXPIRE"},
        {5,"WIFI_REASON_ASSOC_TOOMANY"},
        {6,"WIFI_REASON_NOT_AUTHED"},
        {7,"WIFI_REASON_NOT_ASSOCED"},
        {8,"WIFI_REASON_ASSOC_LEAVE"},
        {9,"WIFI_REASON_ASSOC_NOT_AUTHED"},
        {10,"WIFI_REASON_DISASSOC_PWRCAP_BAD"},
        {11,"WIFI_REASON_DISASSOC_SUPCHAN_BAD"},
        {12,"WIFI_REASON_BSS_TRANSITION_DISASSOC"},
        {13,"WIFI_REASON_IE_INVALID"},
        {14,"WIFI_REASON_MIC_FAILURE (invalid username or password)"},
        {15,"WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT"},
        {16,"WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT"},
        {17,"WIFI_REASON_IE_IN_4WAY_DIFFERS"},
        {18,"WIFI_REASON_GROUP_CIPHER_INVALID"},
        {19,"WIFI_REASON_PAIRWISE_CIPHER_INVALID"},
        {20,"WIFI_REASON_AKMP_INVALID"},
        {21,"WIFI_REASON_UNSUPP_RSN_IE_VERSION"},
        {22,"WIFI_REASON_INVALID_RSN_IE_CAP"},
        {23,"WIFI_REASON_802_1X_AUTH_FAILED"},
        {24,"WIFI_REASON_CIPHER_SUITE_REJECTED"},
        {53,"WIFI_REASON_INVALID_PMKID"},
        {200,"WIFI_REASON_BEACON_TIMEOUT"},
        {201,"WIFI_REASON_NO_AP_FOUND"},
        {202,"WIFI_REASON_AUTH_FAIL"},
        {203,"WIFI_REASON_ASSOC_FAIL"},
        {204,"WIFI_REASON_HANDSHAKE_TIMEOUT"},
        {205,"WIFI_REASON_CONNECTION_FAIL"},
        {206,"WIFI_REASON_AP_TSF_RESET"},
        {207,"WIFI_REASON_ROAMING"},
    };
};
//extern wifi& WIFI;

wifi& wifiRef();
#define WIFI (wifiRef())

#endif //WIFI_H_   
    


