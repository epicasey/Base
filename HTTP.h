/*
 * Created on 01-June-2020
 * Updated on 17-June-2026
 * Updated on 21-Sep-2026: webpage::chunkf() no longer sends zero-length chunks. A zero-length chunk ends a chunked HTTP response,
 *   so a page containing an empty substituted value used to be cut off at that point (affects every project using base/).
 *  Author:  Ed
 */
#ifndef HTP_H_
#define HTP_H_
#include <map>
#include <esp_tls_crypto.h>
#include <esp_http_server.h>
#include <unordered_map>
#include <mutex>
//#include "Common.h"
#include "base.h"
#include "NVStorage.h"
#include "functional"

constexpr int NOAUTH = -1;
constexpr int AUTH = 0;

typedef struct {std::string username; std::string password;} basic_auth_info_t;
typedef struct {httpd_method_t method; int user_ctx;} uri_t;
typedef struct {int fsize; std::string rep;} find_t;
typedef struct {
    std::function<void(std::unordered_map<std::string,std::string> &)> load; 
    const std::string_view html_in; 
    std::string html=""; 
    std::map<size_t,find_t> place_map = {};
    esp_err_t chunkf(std::unique_ptr<httpd_req_t> req){
        place_map.clear(); // rebuild substitutions from current NVS/live values on every request
        std::unordered_map<std::string,std::string> s_map;
        load(s_map);
        for (auto [key,value]:s_map){ // iterate through all keys in s_map
            size_t pos {0};
            while(html_in.size()>pos){// capture all instances of key in html_in
                pos = html_in.find("<<<"+key+">>>",pos);
                if (pos == std::string::npos) break;
                place_map.emplace(pos++,find_t(key.size()+6,value));
            }
        }
        size_t start = 0;
        // A zero-length chunk ends an HTTP chunked response, so an empty piece (an empty property value, or two
        // placeholders side by side) must not be sent, or the page is cut off there.
        auto send = [&](const char *data, size_t len){ if (len > 0) httpd_resp_send_chunk(req.get(), data, len); };
        for (auto [pos,value]:place_map){
            send(html_in.substr(start,pos).data(), pos-start);
            send(value.rep.c_str(), value.rep.size());
            start = pos + value.fsize;
        }
        send(html_in.substr(start).data(), html_in.size() - start);
        return httpd_resp_send_chunk(req.get(), NULL, 0);  // End chunked response
    }

    void chunk(std::unique_ptr<httpd_req_t> req){
        if(html.size() == 0){
            html = html_in;
            std::unordered_map<std::string,std::string> s_map;
            load(s_map); 
            for (auto [key,value]:s_map){
                size_t pos = html.find("<<<"+key+">>>");
                if (pos != std::string::npos) html.replace(pos, key.size()+6, value);
            }
        }
        httpd_resp_send(req.get(),html.c_str(),html.size());
    }
    void clean_up(void){html = "";}
} webpage;

class http_{
public:
    http_(const http_&)= delete;
    static http_& This(void){static http_ Instance; return Instance;}
    http_& operator=(const http_&)=delete;
    void start_process(void);
    esp_err_t https_get(const char* url, std::string& response); // HTTPS GET method using ESP cert bundle
    esp_err_t https_post(const char* url, const std::string& post_data, std::string& response); // HTTPS POST method using ESP cert bundle
    // Guards every transient HTTPS/TLS connection this device opens - https_get()/https_post() above take it
    // internally, and OTA_Update.cpp's manifest fetch and firmware install take it explicitly, since they don't go
    // through those - so at most one is ever open at a time. Each one's mbedTLS session costs tens of KB of heap,
    // and several opening together has been observed to exhaust it and panic the device. MQTT's own long-lived TLS
    // connection is independent of this and is not covered: serializing against a connection that is meant to stay
    // open indefinitely isn't workable, and it isn't opened/closed repeatedly the way these transient ones are.
    // A caller holding this must return normally (not vTaskDelete() or similar) so the lock_guard's destructor
    // actually runs - see OTA_Update.cpp's ota_task() for how it arranges that.
    std::mutex& TlsMutex(void);
private:
    http_(){};
    ~http_(){};

    std::vector<basic_auth_info_t> basic_auth_info_v;
    struct urh{uri_t u; std::function<int(httpd_req_t *req)> h;};
    std::unordered_map<std::string,urh> uriHandler;
    void load_uri_handlers();
    static esp_err_t auth_request_handler(httpd_req_t *req);
    static esp_err_t request_handler(httpd_req_t *req);
    static bool http_auth_basic(basic_auth_info_t &user, std::string &requested);
    static esp_err_t basic_auth_get_handler(httpd_req_t *req);
    void httpd_register_basic_auth(httpd_handle_t server);
    httpd_handle_t start_webserver(void);
    esp_err_t stop_webserver(httpd_handle_t server);
    static void disconnect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void connect_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);
    static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err);
    // Parses "key=value&key=value..." and writes each pair to NVS (and, if wifi_ssid/wifi_pwd were among them, to
    // SSIDStorage too), then commits. By default also reboots after a few seconds (ScheduleReboot() below) to apply
    // the change - pass auto_reboot=false if the caller has its own blocking work left to do before it can safely
    // reply (e.g. waiting for Wi-Fi to reconnect), and call ScheduleReboot() itself once that is done.
    void urlDecode2Map(std::string &content, bool auto_reboot = true);
    // Reboots after delay_seconds, long enough (by default) for an HTTP response already underway to be sent first.
    void ScheduleReboot(int delay_seconds = 5);
};
//extern http_& HTTP;

http_& HTTPRef();
#define HTTP (HTTPRef())

#endif //HTTP_H_