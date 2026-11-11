#pragma once

/* ================================================================================================================
 * NOTE: Setting up a project to use base/ with OTA packaging and the one-click upload
 * ================================================================================================================
 * Projects live next to base/ (ESP32\<Project>\ and ESP32\base\); every path below relies on that.
 *
 * 1. main/CMakeLists.txt - base/ is its own component (base/CMakeLists.txt); main requires it:
 *        idf_component_register(SRC_DIRS "."
 *                               INCLUDE_DIRS "."
 *                               REQUIRES base esp_driver_gpio
 *                               WHOLE_ARCHIVE)
 *    Any REQUIRES turns off main's default of requiring every component, so also name any other IDF component
 *    main/ includes directly that base doesn't already require publicly (e.g. esp_driver_gpio for driver/gpio.h).
 *    WHOLE_ARCHIVE links every object in main. Without it, main/HTTPuriHandlers.cpp (step 4) is dropped: the linker
 *    only takes objects from libmain.a that something already needs, and the only caller of load_uri_handlers()
 *    is base/HTTP.cpp, linked after libmain.a -> "undefined reference to http_::load_uri_handlers()".
 *    Adding a new .cpp to main/ needs CMake to run again (Full Clean or idf.py reconfigure): SRC_DIRS is only
 *    scanned at configure time.
 *
 * 2. Top-level CMakeLists.txt - set the version, find base/, set the project name, then add the post-build step that
 *    writes OTA/firmware.json + OTA/<project>.bin (base/make_ota_package.py):
 *        set(PROJECT_VER "1.00")          # raise for every release; devices only install a higher version
 *        set(EXTRA_COMPONENT_DIRS ../base)
 *        include($ENV{IDF_PATH}/tools/cmake/project.cmake)
 *        project(MyProject)               # becomes the .bin name
 *
 *        idf_build_get_property(python PYTHON)
 *        add_custom_command(TARGET app POST_BUILD
 *            COMMAND ${python} ${CMAKE_SOURCE_DIR}/../base/make_ota_package.py
 *                    --version ${PROJECT_VER}
 *                    --common-header ${CMAKE_SOURCE_DIR}/main/Common.h
 *                    --bin ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
 *                    --out-dir ${CMAKE_SOURCE_DIR}/OTA
 *            COMMENT "Assembling OTA/ package (firmware.json + ${CMAKE_PROJECT_NAME}.bin)"
 *            VERBATIM
 *        )
 *
 * 3. main/Common.h - where the devices fetch updates from (the folder also decides where the upload goes):
 *        #define FIRMWARE_UPGRADE_URL "https://data.kennedylane.com/MyProject_ota/firmware.json"
 *        #define FIRMWARE_UPGRADE_PAYLOAD
 *
 * 4. main.cpp - the usual start-up order:
 *        FIRMWARE.UPGRADE_URL(FIRMWARE_UPGRADE_URL);
 *        ESP_ERROR_CHECK(esp_event_loop_create_default());
 *        OTA.run_once();                  // also loads NVS - start anything that reads NVS after this
 *        HTTP.start_process();
 *        WIFI.initialise_wifi();
 *        SNTP.Set_system_time();
 *    then the main loop - sleep until a task calls WebEvents::Notify() (or the timeout), publish what changed:
 *        while (1) {
 *            WebEvents::Wait(pdMS_TO_TICKS(1000));
 *            if (thing.changed()) WebEvents::Publish("State", thing.json());   // to every page on /events
 *            ...once time is valid (e.g. once a minute):  OTA.update(FIRMWARE.VERSION(1.0f));
 *        }
 *    with   uriHandler["/events"] = {{HTTP_GET,NOAUTH}, [](httpd_req_t *req){ return WebEvents::Stream(req); }};
 *    The project defines http_::load_uri_handlers() (its URI table) in main/HTTPuriHandlers.cpp, which is the only
 *    file that includes main/HTML.h (its pages). HTML.h holds definitions, so including it anywhere else (e.g.
 *    Common.h) would define the pages twice and fail to link. Needs WHOLE_ARCHIVE in main/CMakeLists.txt (step 1).
 *
 * 5. sdkconfig / sdkconfig.defaults - base/ needs C++ exceptions and RTTI (NVStorage, JsonObject, FTPServer), and
 *    OTA needs two app slots:
 *        CONFIG_COMPILER_CXX_EXCEPTIONS=y
 *        CONFIG_COMPILER_CXX_RTTI=y
 *        CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
 *        CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
 *        CONFIG_PARTITION_TABLE_CUSTOM=y
 *    An existing sdkconfig overrides sdkconfig.defaults: change it too (or delete it), then Full Clean.
 *    partitions.csv (copy from an existing project):
 *        nvs,      data, nvs,    , 0x6000,
 *        otadata,  data, ota,    , 0x2000,
 *        phy_init, data, phy,    , 0x1000,
 *        ota_0,    app,  ota_0,  , 1500k,
 *        ota_1,    app,  ota_1,  , 1500k,
 *
 * 6. Upload - nothing per project. VS Code user settings already contain
 *        "idf.customTask": "..\\base\\upload_ota.cmd"
 *    so the ESP-IDF status bar's "Execute Custom Task" button runs base/upload_ota.cmd from the open project, which
 *    sends OTA/firmware.json and the .bin it names to the folder in its URL, through WinSCP's saved site "OTA".
 *    Without the button: run ..\base\upload_ota.cmd from the project folder (or pass the project folder as argument).
 *
 * Release: raise PROJECT_VER -> Build -> Execute Custom Task. Devices pick it up at their next OTA.update().
 * ================================================================================================================
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/timers.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <math.h>
#include <memory>
#include <cstdio>

// esp_log_format() (esp-idf, under CONFIG_LIBC_NEWLIB) wraps its own
// prefix+message+newline composition in flockfile(stdout)/funlockfile(stdout)
// so two tasks' ESP_LOG*/LOGx calls can't interleave on the wire. std::cout
// doesn't necessarily participate in that same lock, so a concurrent COUT
// write from another task could still interleave with (or corrupt) an
// ESP_LOG line's output even with that fix in place. Route COUT/ECOUT
// through the identical lock explicitly rather than assuming libstdc++
// shares it. The StdioLockGuard temporary's lifetime spans the whole
// COUT<<a<<b<<...; statement (not just this macro's own prefix), since a
// temporary created via the comma operator lives until the end of the full
// expression it's part of - exactly the scope that needs to stay locked.
struct StdioLockGuard {
	StdioLockGuard() { flockfile(stdout); }
	~StdioLockGuard() { funlockfile(stdout); }
};

#define COUT  (StdioLockGuard(), std::cout<<"["<<__FILE__<<"; "<<__func__ << ": " << __LINE__ <<"] "<<std::setprecision(3)<<std::fixed)
#define ECOUT (StdioLockGuard(), std::cout<<std::setprecision(16)<<std::endl<<"["<<__FILE__<<"; "<<__func__ << ": " << __LINE__ <<"] ")
#define TimeTick (esp_timer_get_time()/1000)
#define Secs     (esp_timer_get_time()/1000000)

#define LOGI(text, ...)  ESP_LOGI(__func__, "\b\b[%d]: " text, __LINE__, ##__VA_ARGS__)
#define LOGW(text, ...)  ESP_LOGW(__func__, "\b\b[%d]: " text, __LINE__, ##__VA_ARGS__)
#define LOGE(text, ...)  ESP_LOGE(__func__, "\b\b[%d]: " text, __LINE__, ##__VA_ARGS__)
#define LOGD(text, ...)  ESP_LOGD(__func__, "\b\b[%d]: " text, __LINE__, ##__VA_ARGS__)
#define TimeTick (esp_timer_get_time()/1000)
#define Secs     (esp_timer_get_time()/1000000)
#define High_Water if(uxTaskGetStackHighWaterMark(NULL)<500){StdioLockGuard High_Water_lock{}; cout<<pcTaskGetName(NULL)<<" High Water Mark "<<(int)uxTaskGetStackHighWaterMark(NULL)<<std::endl;}
#define High_Water1 {StdioLockGuard High_Water1_lock{}; std::cout<<pcTaskGetName(NULL)<<" High Water Mark "<<(int)uxTaskGetStackHighWaterMark(NULL)<<std::endl;}

#define MOUNT_POINT "/sdcard"

template<typename T> // replacing std::to_string() that bloats the code
std::string to_string(T value, int precision = 3){
    std::ostringstream oss;
    if constexpr(std::is_same_v<T, double> || std::is_same_v<T, float>)
        oss<<std::fixed<< std::setprecision(precision);
    if constexpr(sizeof(value)==1) oss << (int) value;
    else
        oss << value;
    return oss.str();
}

class Timer {
    int64_t start {0};
    public:
    void Delay(int64_t duration_ms){
        auto now = esp_timer_get_time(); 
        auto delay = ((start + duration_ms*1000)-now)/1000;
        start = now;
        vTaskDelay(pdMS_TO_TICKS(delay>0?delay:0));
    }
};
#include "esp_app_desc.h"
class firmware {
    public:
        firmware(const firmware&)= delete;
        static firmware& This(void){static firmware Instance; return Instance;}
        firmware& operator=(const firmware&)=delete;
        template<typename T>
        T VERSION(T default_value = T()){
            if constexpr(std::is_same_v<T, std::string>) return version;
            else if constexpr(std::is_same_v<T, const char*>) return version.c_str();
            else if constexpr(std::is_same_v<T, int>) return std::stoi(version);
            else if constexpr(std::is_same_v<T, float>) return std::stof(version);
            else if constexpr(std::is_same_v<T, double>) return std::stod(version);
            else return default_value;
        }
        const char* UPGRADE_URL(std::string u = "") {if (!u.empty()) url = u; return url.c_str();}
    private:
        std::string version {"1"};
        std::string date {"2024-01-01"};
        std::string time {"00:00:00"};
        std::string url {"https://data.kennedylane.com/firmware.json"};
        firmware(){
            const esp_app_desc_t* app_info = esp_app_get_description();
            if (app_info != nullptr) {
                version = app_info->version;
                date = app_info->date;
                time = app_info->time;
            }
        }
};
    firmware& FirmwareRef();
    #define FIRMWARE (FirmwareRef())

//SDCard interface
#define PIN_NUM_MOSI    GPIO_NUM_11 //35 //12
#define PIN_NUM_MISO    GPIO_NUM_13 //37 //10
#define PIN_NUM_CLK     GPIO_NUM_12 //36 //11
#define PIN_NUM_CS      GPIO_NUM_10 //13
#define PIN_NUM_WP      GPIO_NUM_33
#define PIN_NUM_CD      GPIO_NUM_32