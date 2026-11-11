#include "base.h"	// to_string(), FreeRTOS
#include "WebEvents.h"
#include "freertos/event_groups.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace {
    struct Item {uint32_t seq {0}; std::string payload;};
    std::mutex mtx;
    std::map<std::string,Item> latest;   // topic -> its latest payload
    uint32_t counter {0};                // id of the newest event
    constexpr int WAIT_MS = 300;         // how long an idle /events request waits for news before it is answered with a keep-alive
    constexpr int POLL_MS = 20;

    constexpr EventBits_t NOTIFY_BIT = BIT0;
    // Created on first use by whichever task gets here first (C++ guarantees one creation even if two race)
    EventGroupHandle_t NotifyGroup(void){
        static EventGroupHandle_t group = xEventGroupCreate();
        return group;
    }
}

void WebEvents::Notify(void){
    xEventGroupSetBits(NotifyGroup(), NOTIFY_BIT);
}

bool WebEvents::Wait(TickType_t timeout){
    // Clear on exit, so one Wait() consumes all the Notify() calls made since the last one
    return (xEventGroupWaitBits(NotifyGroup(), NOTIFY_BIT, pdTRUE, pdFALSE, timeout) & NOTIFY_BIT) != 0;
}

void WebEvents::Publish(const std::string &topic, const std::string &payload){
    std::lock_guard<std::mutex> lock(mtx);
    Item &item = latest[topic];
    if (item.seq != 0 && item.payload == payload) return;
    item.payload = payload;
    item.seq = ++counter;
}

// One event in the SSE wire format. A payload with newlines is sent as several data: lines.
static void AppendFrame(std::string &out, uint32_t seq, const std::string &topic, const std::string &payload){
    out += "id: " + to_string(seq) + "\nevent: " + topic + "\n";
    size_t start = 0;
    while (true){
        size_t nl = payload.find('\n', start);
        size_t end = (nl == std::string::npos) ? payload.size() : nl;
        std::string line = payload.substr(start, end - start);
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        out += "data: " + line + "\n";
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    out += "\n";
}

// The events newer than lastId, oldest first. A client whose id is ahead of ours (the device restarted) starts again.
static std::string Collect(uint32_t lastId){
    std::lock_guard<std::mutex> lock(mtx);
    if (lastId > counter) lastId = 0;
    std::vector<std::pair<uint32_t,const std::string*>> news; // seq, topic
    for (auto &it : latest)
        if (it.second.seq > lastId) news.push_back({it.second.seq, &it.first});
    std::sort(news.begin(), news.end());
    std::string out;
    for (auto &n : news) AppendFrame(out, n.first, *n.second, latest[*n.second].payload);
    return out;
}

esp_err_t WebEvents::Stream(httpd_req_t *req){
    uint32_t lastId = 0;
    char buf[16] = {};
    if (httpd_req_get_hdr_value_len(req, "Last-Event-ID") > 0 &&
        httpd_req_get_hdr_value_str(req, "Last-Event-ID", buf, sizeof buf) == ESP_OK)
        lastId = strtoul(buf, NULL, 10);

    // Wait a little for news so that a page is not reconnecting many times a second. The HTTP server handles one request
    // at a time, so this is kept short: other requests wait at most this long.
    std::string out = Collect(lastId);
    for (int waited = 0; out.empty() && waited < WAIT_MS; waited += POLL_MS){
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        out = Collect(lastId);
    }
    if (out.empty()) out = ": ping\n\n";

    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    std::string body = "retry: 100\n\n" + out; // how soon the browser reconnects
    if (httpd_resp_send_chunk(req, body.c_str(), body.size()) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}
