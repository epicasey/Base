

//#include "sdkconfig.h"
//#ifndef CONFIG_BT_ENABLED 
//#include "freertos/FreeRTOS.h"   // in base.h
//#include "freertos/task.h"       // in base.h
//#include "driver/uart.h"         // unused
#include "lwip/sockets.h"
//#include "esp_log.h"             // in base.h

//#define UART_TX_PIN 1  // Example GPIO   // unused
//#define UART_RX_PIN 3  // Example GPIO   // unused

//#include <lwip/sockets.h>        // duplicate
#include <lwip/inet.h>
//#include <esp_log.h>             // in base.h
//#include <lwip/netdb.h>          // unused
#include <string.h>
#include <stdarg.h>
//#include <unistd.h>              // unused
#include <fcntl.h>
//#include <sys/types.h>           // unused
//#include <sys/stat.h>            // unused
//#include <stdio.h>               // in base.h (<cstdio>)
//#include <errno.h>               // unused
//#include "freertos/FreeRTOS.h"   // in base.h
//#include "freertos/task.h"       // in base.h
//#include "freertos/queue.h"      // in base.h

#include "base.h"//#include "Common.h"
//#include <fcntl.h>               // duplicate
//#include "freertos/FreeRTOS.h"   // in base.h
//#include "freertos/task.h"       // in base.h
//#include "freertos/queue.h"      // in base.h


static int udp_sock = -1;
static struct sockaddr_in udp_dest;

#define UDP_LOG_QUEUE_LENGTH 32
#define UDP_LOG_MSG_SIZE 256
static QueueHandle_t udp_log_queue = NULL;

typedef struct {
    char msg[UDP_LOG_MSG_SIZE];
    int len;
} udp_log_msg_t;

static void udp_log_task(void *param) {
    udp_log_msg_t log_msg;
    while (1) {
        if (xQueueReceive(udp_log_queue, &log_msg, portMAX_DELAY) == pdTRUE) {
            if (udp_sock >= 0) {
                sendto(udp_sock, log_msg.msg, log_msg.len, 0, (struct sockaddr *)&udp_dest, sizeof(udp_dest));
            }
        }
    }
}

#ifdef __cplusplus
extern "C" {
#endif

// Custom vprintf handler for ESP-IDF logging

// Helper to strip ANSI escape sequences and ensure CRLF at end
static int strip_ansi_and_crlf(const char* in, char* out, int out_max) {
    int i = 0, j = 0;
//    char last;
    while (in[i] && j < out_max - 3) { // Reserve space for CRLF and null
        if (in[i] == '\x1b' && in[i+1] == '[') {
            // Skip until a letter (end of ANSI sequence)
            i += 2;
            while (in[i] && ((in[i] < '@' || in[i] > '~'))) i++;
            if (in[i]) i++; // skip final letter
        } else {
 //           last = in[i];
            out[j++] = in[i++];
        }
    }
    // Ensure CRLF at end
    if (j > 0) {
        if (out[j-1] == '\n') {
            out[j-1] = '\r';
            out[j++] = '\n';
        } else {
            out[j++] = '\r';
            out[j++] = '\n';
        }
    }
    out[j] = '\0';
    return j;
}

int udp_log_vprintf(const char *fmt, va_list args) {
    char raw_buf[UDP_LOG_MSG_SIZE];
    udp_log_msg_t log_msg;
    vsnprintf(raw_buf, sizeof(raw_buf) - 1, fmt, args);
    log_msg.len = strip_ansi_and_crlf(raw_buf, log_msg.msg, UDP_LOG_MSG_SIZE);
    if (log_msg.len > 0 && log_msg.len < UDP_LOG_MSG_SIZE) {
        xQueueSendToBack(udp_log_queue, &log_msg, 0);
    }
    return log_msg.len;
}

// Call this once at startup (e.g., in app_main or setup/init)
void udp_log_init( uint16_t port,const char* broadcast_ip) {
    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int broadcastEnable = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    // Set socket to non-blocking mode
    int flags = fcntl(udp_sock, F_GETFL, 0);
    fcntl(udp_sock, F_SETFL, flags | O_NONBLOCK);
    memset(&udp_dest, 0, sizeof(udp_dest));
    udp_dest.sin_family = AF_INET;
    udp_dest.sin_port = htons(port);
    udp_dest.sin_addr.s_addr = inet_addr(broadcast_ip); // e.g., "255.255.255.255"
    if (!udp_log_queue) {
        udp_log_queue = xQueueCreate(UDP_LOG_QUEUE_LENGTH, sizeof(udp_log_msg_t));
        // Use larger stack and lowest priority for UDP log task
        xTaskCreate(udp_log_task, "udp_log_task", 4096, NULL, 1, NULL);
    }
    esp_log_set_vprintf(udp_log_vprintf);
    // To reduce log volume for testing, add this to your app_main or setup:
    // esp_log_level_set("*", ESP_LOG_WARN);
}

#ifdef __cplusplus
}
#endif
//#endif