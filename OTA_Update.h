//OTA_Update.h version 1.0, October 23, 2023

#pragma once
#define OTA_UPDATE_H_
#include <string>
#include "esp_http_client.h"
#include <functional>
#include <atomic>

// A project can opt out by defining DISABLE_CERT_BUNDLE (e.g. via target_compile_definitions on its own component,
// so it doesn't affect any other project sharing this file) and embedding its own server_root_cert.pem, to use a
// pinned CA instead of esp_crt_bundle_attach - see HTTP.cpp for the matching https_get()/https_post() side of this.
#ifndef DISABLE_CERT_BUNDLE
#define USE_CERT_BUNDLE
#endif

class OTAUpdate {
public:
    OTAUpdate(const OTAUpdate&)= delete;
    static OTAUpdate& This(void){static OTAUpdate Instance; return Instance;}
    OTAUpdate& operator=(const OTAUpdate&)=delete;

    /// @brief set the daily update time to a specific hour and minute
    /// @param hour -- the hour of the day after which an update check is done (defaults to 3am)
    /// @param minute -- minutes past that hour; defaults to -1, meaning "pick one automatically from this device's
    ///        MAC address" (1..55, stable across reboots) rather than 0 (the top of the hour). This OTA server is
    ///        shared by several devices, so landing all of them on the same fixed minute would mean every one of
    ///        them hitting it in the same second each day; it also avoids colliding with an app's own periodic work
    ///        that happens to be anchored to the top of the hour (an hourly log record, say) - not a rare
    ///        coincidence but a guaranteed one every day, since both would be pinned to the same instant.
    void set_update_time(int hour = 3, int minute = -1);

    /// @brief OTA update check once per day; only updates if FIRMWARE_UPGRADE_URL version is higher than version parameter
    /// @param version firmware version of this program to compare to update available.
    void update(double version, bool prerequisite = true);
    static double Version(void){return firmware_version;};

    /// @brief True only while esp_https_ota() is actually downloading/writing the new image: a large transfer held
    ///        for its whole duration, competing with the app's own routine heap use (MQTT publishes etc.) for the
    ///        device's limited RAM. Callers with non-essential, skippable periodic work (telemetry publishes, not
    ///        the metering/billing itself) should check this and skip that work while it is true, rather than risk
    ///        an allocation failure during the one window where heap is under the most sustained pressure.
    static bool IsUpdating(void){return updating.load();}

    /// @brief Run a one-time update check with an optional prerequisite function.
    /// Also handles OTA rollback: a newly installed app confirms itself once WiFi is up, or rolls back.
    /// @param prerequisite A void(void) function or lambda to be executed only once per version update
    void run_once(std::function<void(void)> prerequisite = [](){});

private:
    OTAUpdate(){};
    ~OTAUpdate(){};
    static esp_err_t http_event_handler(esp_http_client_event_t *evt);
    static void ota_task(void *pvParameter);
    static void confirm_task(void *pvParameter);
    void check_boot_state(void);
    void print_sha256(const uint8_t *image_hash, const char *label);
    void get_sha256_of_partitions(void);
    time_t next_update_check {0};
    static double firmware_version;
    static std::atomic<bool> updating;
};
//extern OTAUpdate& OTA;

OTAUpdate& OTARef();
#define OTA (OTARef())