#pragma once
#include "FreeRTOS.h"
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, TaskHandle_t*, int) { return pdPASS; }
inline void vTaskDelay(TickType_t) {}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return nullptr; }
