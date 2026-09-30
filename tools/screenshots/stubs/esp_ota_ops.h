#pragma once
#include <cstdint>
typedef int esp_err_t;
typedef struct { uint32_t address; uint32_t size; const char* label; } esp_partition_t;
typedef enum { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID, ESP_OTA_IMG_INVALID, ESP_OTA_IMG_ABORTED, ESP_OTA_IMG_UNDEFINED } esp_ota_img_states_t;
inline const esp_partition_t* esp_ota_get_running_partition() { static esp_partition_t p{0x10000, 0x640000, "app0"}; return &p; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { static esp_partition_t p{0x650000, 0x640000, "app1"}; return &p; }
inline esp_err_t esp_ota_get_state_partition(const esp_partition_t*, esp_ota_img_states_t* s) { *s = ESP_OTA_IMG_VALID; return 0; }
inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() { return 0; }
