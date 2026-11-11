/*
 * Created on 01-June-2020
 * Updated on 17-June-2026
 *  Author:  Ed
 */
#include "HTTP.h"
#include "wifi.h"
// load_uri_handlers() (called below) is declared in HTTP.h (a private http_ member) and defined per-project -
// genuinely project-specific (its own URI routing table), so it must not be compiled from here now that base/ is its
// own component: it pulls in main/-only content (HTML.h, stpm01.h, ...) base/ has no business compiling against.
// The consuming project compiles it exactly once in its own component instead, in main/HTTPuriHandlers.cpp (with
// WHOLE_ARCHIVE in main/CMakeLists.txt so the linker keeps it) - the linker resolves the call below across component
// boundaries the same way it always does between separately-compiled translation units.
#include "esp_tls.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
//#include "Common.h"
#include "esp_http_client.h"

// Same DISABLE_CERT_BUNDLE toggle as OTA_Update.h (set via target_compile_definitions on a project's own component,
// so it doesn't affect any other project sharing this file): pin server_root_cert.pem instead of using
// esp_crt_bundle_attach, to sidestep the cert-bundle/PSA-crypto verification path as a suspect for per-call heap loss.
#ifndef DISABLE_CERT_BUNDLE
#define USE_CERT_BUNDLE
#endif
#ifdef USE_CERT_BUNDLE
#include "esp_crt_bundle.h"
#else
extern const uint8_t server_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");
#endif

//http_& HTTP = http_::This();

#ifdef USE_CERT_BUNDLE
esp_tls_cfg_t get_tls_config_with_bundle() {// Helper function to get esp_tls_cfg_t using the ESP cert bundle
    esp_tls_cfg_t cfg = {};
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.use_global_ca_store = false;
    cfg.skip_common_name = false;
    LOGI("Using ESP cert bundle for server verification.");
    return cfg;
}
#endif

// Integrated HTTPS GET request using esp_http_client and cert bundle
namespace { // Anonymous namespace for internal linkage
    struct http_resp_buf_t {
        std::string* str = nullptr;
        size_t buf_size = 0;
        size_t total_len = 0;
        bool alloc_failed = false;
    };
    static esp_err_t http_resp_handler(esp_http_client_event_t *evt) {
        http_resp_buf_t* ctx = (http_resp_buf_t*)evt->user_data;
        if (!ctx || !ctx->str) return ESP_OK;
        if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && strcmp(evt->header_key, "Content-Length") == 0) {
            size_t content_length = atoi(evt->header_value);
            if (content_length > 0 && ctx->str->empty()) {
                ctx->str->resize(content_length + 1, 0);
                ctx->buf_size = content_length + 1;
            }
        } else if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && !ctx->alloc_failed) {
            // Fallback if Content-Length was not set
            if (ctx->str->empty()) {
                ctx->buf_size = 16384; // 16 KB fallback
                ctx->str->resize(ctx->buf_size, 0);
            }
            size_t copy_len = std::min((size_t)evt->data_len, ctx->buf_size - ctx->total_len - 1);
            if (copy_len > 0) {
                memcpy(&((*ctx->str)[ctx->total_len]), evt->data, copy_len);
                ctx->total_len += copy_len;
                (*ctx->str)[ctx->total_len] = 0;
            }
        }
        return ESP_OK;
    }
}

std::mutex& http_::TlsMutex(void){
    static std::mutex m;
    return m;
}

esp_err_t http_::https_get(const char* url, std::string& response) {
    std::lock_guard<std::mutex> lock(TlsMutex());
    uint32_t before = esp_get_free_heap_size();
    http_resp_buf_t ctx;
    ctx.str = &response;
    response.clear();

    esp_http_client_config_t config = {};
    config.url = url;
#ifdef USE_CERT_BUNDLE
    config.crt_bundle_attach = esp_crt_bundle_attach;
#else
    config.cert_pem = (const char *)server_cert_pem_start;
#endif
    config.event_handler = http_resp_handler;
    config.user_data = &ctx;
    // Not keep-alive: this handle is created and destroyed within this one call, with no second request to ever
    // reuse the connection, so keep-alive buys nothing - and leaving it on is a known way for a one-shot client's
    // transport resources to not be fully released by cleanup() on some IDF versions. See the heap log below: if
    // removing this isn't the whole story, it will at least tell us exactly which https_get()/post() call is losing
    // memory, rather than only a device-wide number hours later.

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    // perform()/cleanup() are documented to already close the transport themselves for a non-keep-alive request, so
    // this should be a no-op - added anyway (several official IDF examples do the same) in case some code path
    // leaves the TLS context/socket open for cleanup() to then miss. Belt-and-suspenders, not a confirmed fix.
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    int32_t delta = (int32_t)before - (int32_t)esp_get_free_heap_size();
    // Largest contiguous block alongside total free: a big gap between the two is fragmentation, not a leak - total
    // free can look fine while nothing of a given size (e.g. a 4-16KB TLS buffer) can actually still be allocated.
    if (delta > 1024) LOGW("https_get(%s) heap: %u -> %u (lost %d), largest block now %u", url, (unsigned)before,
                           (unsigned)esp_get_free_heap_size(), delta, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (ctx.alloc_failed) {
        LOGE("Failed to allocate heap for HTTP response buffer");
        response.clear();
        return ESP_ERR_NO_MEM;
    }
    // Resize to actual data length (remove trailing nulls)
    response.resize(ctx.total_len);
    return err;
}
esp_err_t http_::https_post(const char* url, const std::string& post_data, std::string& response) {
    std::lock_guard<std::mutex> lock(TlsMutex());
    uint32_t before = esp_get_free_heap_size();
    http_resp_buf_t ctx;
    ctx.str = &response;
    response.clear();

    esp_http_client_config_t config = {};
    config.url = url;
#ifdef USE_CERT_BUNDLE
    config.crt_bundle_attach = esp_crt_bundle_attach;
#else
    config.cert_pem = (const char *)server_cert_pem_start;
#endif
    config.event_handler = http_resp_handler;
    config.user_data = &ctx;
    // See https_get() above: not keep-alive, same one-shot reasoning.
    config.method = HTTP_METHOD_POST;
    config.buffer_size_tx = post_data.size() + 512;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client, post_data.c_str(), post_data.size());
    esp_err_t err = esp_http_client_perform(client);
    // See https_get() above: belt-and-suspenders, not a confirmed fix.
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    int32_t delta = (int32_t)before - (int32_t)esp_get_free_heap_size();
    if (delta > 1024) LOGW("https_post(%s) heap: %u -> %u (lost %d), largest block now %u", url, (unsigned)before,
                           (unsigned)esp_get_free_heap_size(), delta, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (ctx.alloc_failed) {
        LOGE("Failed to allocate heap for HTTP response buffer");
        response.clear();
        return ESP_ERR_NO_MEM;
    }
    // Resize to actual data length (remove trailing nulls)
    response.resize(ctx.total_len);
    return err;
}

// Example usage (pseudo-code):
// esp_tls_cfg_t cfg = get_tls_config_with_bundle();
// esp_tls_t *tls = esp_tls_conn_http_new("https://example.com", &cfg);
// if (tls) { /* use tls->sockfd, etc. */ esp_tls_conn_destroy(tls); }
// else { LOGE("TLS connection failed"); }

#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

esp_err_t http_::auth_request_handler(httpd_req_t *req){
    if(WIFI.got_ip()) if(basic_auth_get_handler(req)!=ESP_OK) return ESP_FAIL;
    return request_handler(req);
}

esp_err_t http_::request_handler(httpd_req_t *req){
    std::string url = req->uri;
    auto x = url.find("?");
    if (x != std::string::npos)url = url.substr(0,x);
    auto it = This().uriHandler.find(url);
    if (it != This().uriHandler.end()){
        return it->second.h(req);
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Resource not found");
    return ESP_FAIL;
}

bool http_::http_auth_basic(basic_auth_info_t &user, std::string &requested ){
    std::string user_info = user.username+":"+user.password;
    size_t n = 0, out = 0;
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info.c_str(), user_info.size());
    std::string digest("Basic ");
    digest.append(n+1,'\0');
    esp_crypto_base64_encode((unsigned char *)digest.data() + 6, n, &out, (const unsigned char *)user_info.c_str(), user_info.size());
    return digest == requested;
}

esp_err_t http_::basic_auth_get_handler(httpd_req_t *req){
    esp_err_t ret = ESP_FAIL;
    const size_t buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    std::string buf(buf_len+1,'\0');
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf.data(), buf_len) == ESP_OK){ 
        if( !http_auth_basic(*(basic_auth_info_t *)req->user_ctx,buf)){
            LOGE("Not authenticated");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(  req, "text/javascript");
            httpd_resp_set_hdr(   req, "Connection", "keep-alive");
            httpd_resp_set_hdr(   req, "WWW-Authenticate", "Basic realm=\"Hello\"");
            httpd_resp_send(      req, NULL, 0);
        } else {
            LOGI("Authenticated user: %s!",((basic_auth_info_t *)req->user_ctx)->username.c_str());
            httpd_resp_set_status(req, HTTPD_200);
            httpd_resp_set_type(  req, "text/html");
            httpd_resp_set_hdr(   req, "Connection", "keep-alive");
            ret = ESP_OK;
        }
    } else {
        LOGE("%s", buf_len < 2?"No auth header received":"No 'Authroization' value received");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(  req, "application/json");
        httpd_resp_set_hdr(   req, "Connection", "keep-alive");
        httpd_resp_set_hdr(   req, "WWW-Authenticate", "Basic realm=\"Hello\"");
        httpd_resp_send(      req, NULL, 0);  
    }
    return ret;
}

void http_::httpd_register_basic_auth(httpd_handle_t server){
    if (basic_auth_info_v.size()>0){
        for (auto [key,val]:uriHandler){
            httpd_uri_t u {};
                u.uri = key.c_str();
                u.method = val.u.method;
                u.handler = val.u.user_ctx>=0?auth_request_handler:request_handler;
                u.user_ctx =val.u.user_ctx>=0?&basic_auth_info_v[val.u.user_ctx]:NULL;
            httpd_register_uri_handler(server, &u);
        }
    }
}

httpd_handle_t http_::start_webserver(void){
    ESP_ERROR_CHECK(esp_netif_init());
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;
    config.stack_size       = 6400;
    //config.server_port = 443;
    LOGI("Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        LOGI("Registering authorization and error URI handlers");
        httpd_register_basic_auth(server);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
        return server;
    }
    LOGI("Error starting server!");
    return NULL;
}

esp_err_t http_::http_404_error_handler(httpd_req_t *req, httpd_err_code_t err){
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Resource not found");
    return ESP_FAIL;
}
esp_err_t http_::stop_webserver(httpd_handle_t server){
    return httpd_stop(server);
}

void http_::disconnect_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        LOGI("Stopping webserver");
        if (This().stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            LOGE("Failed to stop http server");
        }
    }
}

void http_::connect_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data){
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        LOGI("Starting webserver");
        *server = This().start_webserver();
    }
}

void http_::start_process(void){
    load_uri_handlers();
    basic_auth_info_v.push_back({ESP_WIFI_SSID,ESP_WIFI_PASS});
    NVS.read("http_usr",basic_auth_info_v.back().username);
    NVS.read("http_pwd",basic_auth_info_v.back().password);
    COUT<<"with User: "<<basic_auth_info_v.back().username<<std::endl;

    static httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,        &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   IP_EVENT_ASSIGNED_IP_TO_CLIENT,  &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
    /* Start the server for the first time */
    server = start_webserver();
}

static void delayed_reboot_callback(TimerHandle_t xTimer) {
    COUT << "Rebooting now..." << std::endl;
    esp_restart();
}

void http_::ScheduleReboot(int delay_seconds){
    // Schedule a delayed reboot to allow the HTTP response to be sent before restarting the device. A handler that
    // still has its own blocking work to do after urlDecode2Map() (e.g. waiting for Wi-Fi to reconnect before
    // replying) should pass auto_reboot=false there and call this itself once it is actually ready to reboot -
    // otherwise this fixed delay can expire first and reset the connection out from under the pending response.
    TimerHandle_t reboot_timer = xTimerCreate("reboot_timer", pdMS_TO_TICKS(delay_seconds * 1000), pdFALSE, NULL, delayed_reboot_callback);
    if (reboot_timer != NULL) {
        xTimerStart(reboot_timer, 0);
    }
    LOGI("Rebooting in %d seconds...", delay_seconds);
}

void http_::urlDecode2Map(std::string &content, bool auto_reboot){
    std::cout<<"Content: "<<content<<std::endl;
    std::string key,value;
    char c;
    bool kv=true;
    std::unordered_map<char,std::function<void(std::string::iterator&)>> decoder {
        {'%', [&](std::string::iterator& it){c=(char)stoi(std::string{(*(it+1)),(*(it+2))},NULL,16);it++;*(it+1)=c;}},
        {'\t',[&](std::string::iterator& it){c='\0';}},
        {'=', [&](std::string::iterator& it){kv=false; c='\0';}},
        {'&', [&](std::string::iterator& it){kv=true;  c='\0';NVS.write(key,value);key=""; value="";}},
        {'+', [&](std::string::iterator& it){c=' ';}}       };
    auto done = [&](){if(c!='\0') {if (kv) key+=c; else value+=c;}};
    for (auto it = content.begin(); it != content.end(); ++it){//(auto &it:content){
        c=*it;
        decoder.find(c)!=decoder.end()?decoder[c](it):done();
    }
    NVS.write(key,value);
    std::vector<std::string> vs(2);
    if(NVS.read("wifi_ssid",vs[0]) && NVS.read("wifi_pwd",vs[1])){
        NVS.write(vs[0],vs[1],"SSIDStorage");
        //NVS.erase(vs,"NVStorage");
    }
    NVS.commit();
    NVS.commit("SSIDStorage");

    LOGI("Properties updated.");
    if (auto_reboot) ScheduleReboot();
}

http_& HTTPRef(){ // .cpp
	return http_::This();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
