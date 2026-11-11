#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize UDP log broadcast
// Call this once at startup, e.g.:
//  udp_log_init() for default port and broadcast address
void udp_log_init(uint16_t port = atoi(WIFI.get_id().data()),const char* broadcast_ip = "255.255.255.255");

#ifdef __cplusplus
}
#endif
