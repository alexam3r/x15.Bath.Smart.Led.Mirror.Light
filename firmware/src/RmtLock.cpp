#include "RmtLock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace rmt_lock {

namespace {
SemaphoreHandle_t mutex = nullptr;
}  // namespace

bool init() {
    if (mutex == nullptr) mutex = xSemaphoreCreateMutex();
    return mutex != nullptr;
}

bool take(uint32_t timeoutMs) {
    return mutex != nullptr && xSemaphoreTake(mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void give() {
    if (mutex != nullptr) xSemaphoreGive(mutex);
}

}  // namespace rmt_lock
