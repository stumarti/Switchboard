#pragma once
typedef void* SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex() { static int m; return &m; }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return 1; }
inline int xSemaphoreGive(SemaphoreHandle_t) { return 1; }
