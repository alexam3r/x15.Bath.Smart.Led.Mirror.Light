// Task 7: Core 0 network task (ARCHITECTURE.md 3.1, 3.4) — WiFi, MQTT,
// watchdog, publishing. Everything but start() is file-static in Network.cpp.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace network {

// Creates and pins the NetworkTask to Core 0. cmdQueue: Core 0 -> Core 1
// commands. snapQueue: Core 1 -> Core 0 state mailbox (1 element).
void start(QueueHandle_t cmdQueue, QueueHandle_t snapQueue);

}  // namespace network
