# base/ as a real ESP-IDF component (not merged into a project's "main" via SRC_DIRS): see base.h's setup doc.
# REQUIRES must be explicit here, unlike every consuming project's "main": ESP-IDF's "main" component is a special
# case that auto-requires every component in the build when it declares no REQUIRES of its own - that's what made
# the old SRC_DIRS-merge setup "just work" despite main/CMakeLists.txt never listing any. A non-"main" component
# gets no such free pass, so every ESP-IDF component any base/*.cpp or base/*.h actually #includes from has to be
# named below, or that file fails to compile the moment it's no longer folded into "main".
idf_component_register(SRC_DIRS "."
                        INCLUDE_DIRS "."
                        REQUIRES freertos log esp_timer esp_common esp_system esp_event esp_hw_support
                                 esp_wifi esp_netif nvs_flash esp_http_client esp_http_server esp_https_ota
                                 app_update esp-tls mbedtls fatfs vfs driver esp_driver_uart lwip bt mqtt
                                 esp_app_format heap)
