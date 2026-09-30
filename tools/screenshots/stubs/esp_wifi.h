#pragma once
#include <cstdint>
#ifndef ESP_OK
#define ESP_OK 0
#endif
typedef enum { WIFI_IF_STA, WIFI_IF_AP } wifi_interface_t;
typedef struct { uint8_t ssid[32]; uint8_t password[64]; } wifi_sta_config_t;
typedef union { wifi_sta_config_t sta; } wifi_config_t;
inline int esp_wifi_get_config(wifi_interface_t, wifi_config_t* c) { *c = wifi_config_t{}; return ESP_OK; }
