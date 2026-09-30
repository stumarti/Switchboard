#pragma once
// ESP-IDF's qrcode component, over the library it wraps (Nayuki's qrcodegen,
// fetched by render.sh), so the Wi-Fi page draws a real, scannable code.
#include <cstdint>
extern "C" {
#include "qrcodegen.h"
}
#ifndef ESP_OK
#define ESP_OK 0
#endif
#define ESP_FAIL -1
typedef const uint8_t* esp_qrcode_handle_t;
enum { ESP_QRCODE_ECC_LOW, ESP_QRCODE_ECC_MED, ESP_QRCODE_ECC_QUART, ESP_QRCODE_ECC_HIGH };
struct esp_qrcode_config_t {
  void (*display_func)(esp_qrcode_handle_t);
  int max_qrcode_version;
  int qrcode_ecc_level;
};
#define ESP_QRCODE_CONFIG_DEFAULT() (esp_qrcode_config_t{nullptr, 10, ESP_QRCODE_ECC_LOW})
inline int esp_qrcode_generate(esp_qrcode_config_t* cfg, const char* text) {
  static uint8_t code[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
  if (!qrcodegen_encodeText(text, tmp, code, static_cast<qrcodegen_Ecc>(cfg->qrcode_ecc_level), qrcodegen_VERSION_MIN,
                            cfg->max_qrcode_version, qrcodegen_Mask_AUTO, true))
    return ESP_FAIL;
  if (cfg->display_func) cfg->display_func(code);
  return ESP_OK;
}
inline int esp_qrcode_get_size(esp_qrcode_handle_t q) { return qrcodegen_getSize(q); }
inline bool esp_qrcode_get_module(esp_qrcode_handle_t q, int x, int y) { return qrcodegen_getModule(q, x, y); }
