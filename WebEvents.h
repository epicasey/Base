/*
 * WebEvents.h
 *
 *  Sends live data from the app straight to the web page as Server-Sent Events, without going through MQTT.
 *  The page opens EventSource('/events') and listens for one event per topic (e.g. WattMeter's "Monthly" and
 *  "Elapsed", WaterHeater's "State", "TOD" and "Name").
 *  Use: register   uriHandler["/events"] = {{HTTP_GET,NOAUTH}, [](httpd_req_t *req){ return WebEvents::Stream(req); }};
 *       and call   WebEvents::Publish("Topic", payload);   whenever a topic's data changes.
 *  Suits topics where only the current value matters (state, readings): every page sees every topic's latest value.
 *    - only the latest payload of each topic is kept, so memory use is small and does not grow when nobody is looking;
 *    - it is not consumed: any number of pages can watch, and a page that connects (or reconnects) is sent the
 *      latest data of every topic straight away.
 *  The browser reconnects after every /events response and sends the id of the last event it saw (Last-Event-ID);
 *  a request is answered with whatever is newer, or after a short wait with a keep-alive comment.
 *
 *  Main-loop wake-up (replaces base/AppEvents): a task that changes state calls WebEvents::Notify(); the main loop
 *  sleeps in WebEvents::Wait(timeout), then works out what changed and publishes it:
 *      while (1) { WebEvents::Wait(pdMS_TO_TICKS(1000)); if (thing.changed()) WebEvents::Publish("State", thing.json()); }
 *  Nothing to initialise: the first call to either sets it up.
 */
#ifndef WEBEVENTS_H_
#define WEBEVENTS_H_

#include <string>
#include <esp_http_server.h>
#include "freertos/FreeRTOS.h"

namespace WebEvents {
    /// @brief Wake the task waiting in Wait() (the main loop), e.g. after a state change it should publish.
    ///        Safe to call from any task, before or after Wait() is first called. Not from an ISR.
    void Notify(void);

    /// @brief Sleep until Notify() is called or timeout passes. A Notify() made while nobody was waiting is not lost:
    ///        the next Wait() returns straight away.
    /// @return true if woken by Notify(), false on timeout
    bool Wait(TickType_t timeout);

    /// @brief Make payload the latest data of a topic. Ignored if it is the same as the previous payload.
    ///        The payload may contain newlines. Safe to call from any task.
    void Publish(const std::string &topic, const std::string &payload);

    /// @brief The GET /events handler: register it in load_uri_handlers().
    esp_err_t Stream(httpd_req_t *req);
}

#endif /* WEBEVENTS_H_ */
