// OTA_Update.cpp Version 1.0, October 23, 2023
/* OTA
   This code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
//#include "Common.h"
#include "base.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "OTA_Update.h"
#include "NVStorage.h"
#include "JsonObject.h"
#include "WIFI.h"
#include "HTTP.h" // HTTP.TlsMutex() - see ota_task() below
#include <sys/socket.h>
#include "esp_wifi.h"
#include "esp_mac.h" // esp_read_mac() - see set_update_time()'s per-device check-time offset
#include "esp_timer.h"
#include <mutex>

#ifdef USE_CERT_BUNDLE
#include "esp_crt_bundle.h"
#else
extern const uint8_t server_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");
#endif

#define HASH_LEN 32
#define OTA_URL_SIZE 256
#define OTA_HTTP_TIMEOUT_MS 30000
#define OTA_CONFIRM_MIN_UPTIME_S 60      // New firmware must run this long with WiFi up before it is marked valid
#define OTA_CONFIRM_TIMEOUT_S    (15*60) // Roll back if not confirmed within this time

//OTAUpdate& OTA = OTAUpdate::This();
// Do not call firmware accessors during global/static initialization.
// Query version lazily at runtime instead.
double OTAUpdate::firmware_version {0.0};
std::atomic<bool> OTAUpdate::updating {false};

void OTAUpdate::set_update_time(int update_hour, int update_minute){
    if (update_minute < 0){
        // Auto (the default): a per-device offset derived from the MAC address, so the several other devices
        // sharing this same OTA server don't all hit it in the same second every day - stable across reboots (the
        // same device always lands on the same minute) without needing every project to pick its own by hand.
        // 1..55, never on the hour (also avoids colliding with an hourly cycle of this app's own - see the comment
        // on set_update_time() in the header) nor within the last few minutes of it.
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        update_minute = 1 + (mac[5] % 55);
    }
    next_update_check = time(NULL)+(86400);
    struct tm t;
    localtime_r(&next_update_check, &t);
    t.tm_hour = update_hour;
    t.tm_min  = update_minute;
    t.tm_sec  = 0;
    next_update_check = mktime(&t);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%c", &t);
    LOGW("Next OTA update check: %s", time_str);
}

esp_err_t OTAUpdate::http_event_handler(esp_http_client_event_t *evt){
    switch ((esp_http_client_event_id_t)evt->event_id) {
    case HTTP_EVENT_ERROR:
        LOGD("HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        LOGD("HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        LOGD("HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        LOGD("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        LOGD("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                std::string *s = (std::string *)evt->user_data;
                if (s != nullptr) {
                    s->append((const char*)evt->data, evt->data_len);
                }
            }
        break;
    case HTTP_EVENT_ON_FINISH:
        LOGD("HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        LOGD("HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        LOGD("HTTP_EVENT_REDIRECT");
        break;
    default:
        LOGD("Other event id:%d", evt->event_id);
        break;
    }
    return ESP_OK;
}

void OTAUpdate::ota_task(void *pvParameter){
    esp_log_level_set("esp-x509-crt-bundle",ESP_LOG_WARN);
    std::string rcv_buffer;
    LOGW("Starting");
    auto log_ota_tls_diag = [](const char *phase, const esp_http_client_config_t &cfg) {
        std::string url = (cfg.url != nullptr) ? std::string(cfg.url) : std::string("<null>");
        std::string host = "<parse-failed>";
        size_t scheme_pos = url.find("://");
        size_t host_start = (scheme_pos == std::string::npos) ? 0 : scheme_pos + 3;
        if (host_start < url.size()) {
            size_t host_end = url.find_first_of("/:?", host_start);
            host = url.substr(host_start, host_end - host_start);
        }

        time_t now = time(nullptr);
        struct tm tm_utc {};
        struct tm tm_local {};
        gmtime_r(&now, &tm_utc);
        localtime_r(&now, &tm_local);
        char utc_buf[32] = {0};
        char local_buf[32] = {0};
        strftime(utc_buf, sizeof(utc_buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
        strftime(local_buf, sizeof(local_buf), "%Y-%m-%d %H:%M:%S %Z", &tm_local);

        LOGD("OTA TLS diag [%s] url=%s host=%s timeout_ms=%d", phase, url.c_str(), host.c_str(), cfg.timeout_ms);
        LOGD("OTA TLS diag [%s] now_epoch=%lld now_utc=%s now_local=%s", phase, static_cast<long long>(now), utc_buf, local_buf);
#ifdef USE_CERT_BUNDLE
        LOGD("OTA TLS diag [%s] tls_verify=crt_bundle_attach (%s)", phase, cfg.crt_bundle_attach ? "set" : "null");
#else
        // Explicit pointer decay: comparing/subtracting the arrays themselves (rather than decayed pointers) is
        // rejected as of C++26 (-Warray-compare) - this branch was never actually compiled before DISABLE_CERT_BUNDLE
        // existed (USE_CERT_BUNDLE was unconditionally defined), so the error only surfaced now.
        const uint8_t *certStart = server_cert_pem_start, *certEnd = server_cert_pem_end;
        ptrdiff_t cert_len = (certEnd > certStart) ? (certEnd - certStart) : 0;
        LOGD("OTA TLS diag [%s] tls_verify=cert_pem ptr=%p len=%d", phase, cfg.cert_pem, (int)cert_len);
#endif
    };

    // The body is a lambda, called once below, so every path - including the two early-outs that used to call
    // vTaskDelete(NULL) directly - returns normally and releases the lock_guard. vTaskDelete() terminates the task
    // immediately without unwinding the stack, so a lock taken directly in ota_task() and released by RAII would
    // never actually unlock on those paths, deadlocking every later HTTPS/OTA attempt against TlsMutex() forever.
    auto run = [&](){
        std::lock_guard<std::mutex> lock(HTTP.TlsMutex()); // see HTTP.h: at most one transient TLS connection at a time
        esp_http_client_config_t config {};
        config.url = FIRMWARE.UPGRADE_URL();
#ifdef USE_CERT_BUNDLE
        config.crt_bundle_attach = esp_crt_bundle_attach;
#else
        config.cert_pem = (char *)server_cert_pem_start;
#endif // USE_CERT_BUNDLE
        config.user_data = (void*)&rcv_buffer;
        config.event_handler = OTAUpdate::http_event_handler;
        config.timeout_ms = OTA_HTTP_TIMEOUT_MS;
        config.keep_alive_enable = false;

        log_ota_tls_diag("manifest", config);
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);// downloading the json file
        if(err == ESP_OK) {
            // parse the json file
            ParseJson Json(rcv_buffer.c_str());
            int err = 0;
            double new_version = Json.get_value("version",&err);
            if(err<0) {
                LOGE("Invalid JSON format: missing 'version' field");
                esp_http_client_close(client); // see HTTP.cpp's https_get(): belt-and-suspenders, not a confirmed fix
                esp_http_client_cleanup(client);
                if (OTA.next_update_check == 1) OTA.set_update_time(); else OTA.next_update_check+=86400;
                return;
            }
            std::string file_str = Json.get_value("file",&err);
            if(err<0) {
                LOGE("Invalid JSON format: missing 'file' field");
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                if (OTA.next_update_check == 1) OTA.set_update_time(); else OTA.next_update_check+=86400;
                return;
            }

            // Release manifest client/TLS context before opening OTA TLS connection.
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            client = nullptr;

            std::string rejected;
            NVS.read("OTARejected", rejected);
            if(to_string(new_version,5) > to_string(firmware_version,5) && to_string(new_version,5) == rejected) {
                LOGW("Firmware version %.5f was rolled back previously; not installing it again", new_version);
            }
            else if(to_string(new_version,5) > to_string(firmware_version,5)) {
                LOGW("New firmware version (%.5f) is available, upgrading from %.5f...", new_version, firmware_version);
                config.url = file_str.c_str();
                config.user_data = nullptr;  // Don't accumulate firmware binary in rcv_buffer
                log_ota_tls_diag("firmware", config);
                esp_https_ota_config_t ota_config = {};
                ota_config.http_config = &config;
                // Record the version being installed so a rollback can be detected after reboot (see check_boot_state)
                NVS.write("OTAPending", to_string(new_version,5));
                NVS.commit();
                updating = true; // see OTA_Update.h: let other tasks skip non-essential heap use while this holds a lot of it
                esp_err_t ret = esp_https_ota(&ota_config);
                updating = false;
                if (ret == ESP_OK) {
                    LOGW("OTA OK, restarting...");
                    esp_restart(); // the mutex's state does not survive a reboot, so holding it here is harmless
                } else {
                    LOGE("OTA update failed...");
                    NVS.write("OTAPending", std::string());
                    NVS.commit();
                }
            }
            else LOGW("Current firmware version (%.2f) is up to date.", firmware_version);
        }
        else {
            int http_status = esp_http_client_get_status_code(client);
            LOGE("Unable to download OTA manifest from %s (err=%s, status=%d)", config.url, esp_err_to_name(err), http_status);
        }
        if (client != nullptr) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
        if (OTA.next_update_check == 1) OTA.set_update_time(); else OTA.next_update_check+=86400;
    };
    run();
    LOGW("Exiting");
    vTaskDelete(NULL);
}

void OTAUpdate::print_sha256(const uint8_t *image_hash, const char *label){
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    LOGD("SHA-256 for %s: %s", label, hash_print);
}

void OTAUpdate::get_sha256_of_partitions(void){
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

void OTAUpdate::update(double version, bool prerequisite){
    if (next_update_check == 1 || time(NULL)<next_update_check || !prerequisite || !WIFI.got_ip()) return;
	next_update_check = 1;
    firmware_version = version;
    get_sha256_of_partitions();
    xTaskCreate(OTAUpdate::ota_task, "ota_update_task", 8192, NULL, 5, NULL);
}

// With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE a newly OTA-installed app boots as PENDING_VERIFY.
// It is marked valid once WiFi is up and it has run for OTA_CONFIRM_MIN_UPTIME_S; a crash or reset before
// then makes the bootloader revert to the previous app, and no network within OTA_CONFIRM_TIMEOUT_S forces it.
void OTAUpdate::confirm_task(void *pvParameter){
    while (true) {
        int64_t uptime_s = esp_timer_get_time() / 1000000;
        if (WIFI.got_ip() && uptime_s >= OTA_CONFIRM_MIN_UPTIME_S) {
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                LOGW("New firmware confirmed after %lld s with WiFi up; rollback cancelled", (long long)uptime_s);
                NVS.write("OTAPending", std::string());
                NVS.commit();
            } else {
                LOGE("Failed to confirm new firmware: %s", esp_err_to_name(err));
            }
            break;
        }
        if (uptime_s >= OTA_CONFIRM_TIMEOUT_S) {
            LOGE("New firmware not confirmed within %d s (no WiFi); rolling back", OTA_CONFIRM_TIMEOUT_S);
            esp_ota_mark_app_invalid_rollback_and_reboot(); // Does not return on success
            LOGE("Rollback not possible; keeping this firmware");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void OTAUpdate::check_boot_state(void){
    std::string current = to_string(FIRMWARE.VERSION(1.0),5);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        LOGW("Firmware %s is pending verification", current.c_str());
        xTaskCreate(OTAUpdate::confirm_task, "ota_confirm", 4096, NULL, 5, NULL);
        return;
    }
    std::string pending;
    NVS.read("OTAPending", pending);
    if (pending.empty()) return;
    // The update went to the partition we are not running from; if the bootloader marked it failed, it was rolled back
    const esp_partition_t* updated = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t updated_state;
    bool rolled_back = updated && esp_ota_get_state_partition(updated, &updated_state) == ESP_OK
                    && (updated_state == ESP_OTA_IMG_ABORTED || updated_state == ESP_OTA_IMG_INVALID);
    if (rolled_back && pending != current) {
        LOGE("Firmware %s failed verification and was rolled back; it will not be installed again", pending.c_str());
        NVS.write("OTARejected", pending);
    } else if (pending != current) {
        LOGW("Installed firmware reports version %s but the manifest said %s; check the manifest version", current.c_str(), pending.c_str());
    }
    NVS.write("OTAPending", std::string());
    NVS.commit();
}

void OTAUpdate::run_once(std::function<void(void)> prerequisite){
    NVS.load();
    check_boot_state();
    std::string ver_str = "NOT_FOUND";
	NVS.read("Version",ver_str);
    double stored_version = (ver_str == "NOT_FOUND")?0:std::stod(ver_str);
    if (stored_version >= FIRMWARE.VERSION(1.0)) return; //already at this version or higher, so do nothing
    LOGD("Running FIRMWARE_UPGRADE_PAYLOAD once");
    NVS.write("Version",FIRMWARE.VERSION<std::string>("1.0"));
    NVS.commit();
    prerequisite();
}

OTAUpdate& OTARef(){ // .cpp
	return OTAUpdate::This();
}