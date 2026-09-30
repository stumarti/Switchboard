#pragma once
#include "FreeRTOS.h"
inline QueueHandle_t xQueueCreate(int, int) { static int q; return &q; }
inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdFALSE; }
inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
