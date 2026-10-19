#pragma once

// ===========================================================================
// qr_draw — a QR code on the panel, and the "WIFI:" text a phone's camera
// joins a network from. Shared by the Wifi page's guest QR codes
// (screen_wifi_networks) and setting up the remote's own Wi-Fi from a phone
// (wifi_provision).
//
// QR rendering uses the ESP-IDF `espressif__qrcode` component, which ships
// as a transitive dependency of the Arduino-ESP32 framework (esp_wifi's own
// provisioning uses it) — no extra lib_deps entry needed, just <qrcode.h>.
// ===========================================================================

#include <qrcode.h>
#include <string.h>

#include "ui.h"

namespace qrdraw {

// Escape the WIFI: URI's reserved characters (\ ; , " :) with a backslash, per
// the de-facto "MECARD-like" Wi-Fi QR format most phone camera apps parse.
inline void wifiEscape(char* out, size_t outCap, const char* in) {
  size_t o = 0;
  for (const char* p = in; *p && o + 2 < outCap; ++p) {
    if (strchr("\\;,\":", *p)) out[o++] = '\\';
    out[o++] = *p;
  }
  out[o] = 0;
}

// The text a phone's camera offers to join a network from.
inline void wifiUri(char* out, size_t outCap, const char* ssid, const char* pass, bool open) {
  char ssidEsc[80], passEsc[160];
  wifiEscape(ssidEsc, sizeof(ssidEsc), ssid);
  wifiEscape(passEsc, sizeof(passEsc), pass);
  snprintf(out, outCap, "WIFI:T:%s;S:%s;P:%s;;", open ? "nopass" : "WPA", ssidEsc, passEsc);
}

// The handle esp_qrcode_generate() hands to display_func — only valid inside
// that call and immediately after it returns (the library owns the buffer).
inline esp_qrcode_handle_t g_handle = nullptr;
inline void onReady(esp_qrcode_handle_t h) { g_handle = h; }

// `text` as a QR code, centred in a white box at most `maxBox` px square with
// its top at y0 (a 2-module quiet zone inside it, the module size snapped
// down so it fits). Returns false if the code couldn't be made.
inline bool draw(Ui& ui, const char* text, int16_t y0, int16_t maxBox) {
  g_handle = nullptr;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = onReady;
  cfg.max_qrcode_version = 10;  // plenty for a WIFI: URI at MED ECC
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  if (esp_qrcode_generate(&cfg, text) != ESP_OK || g_handle == nullptr) return false;
  const int size = esp_qrcode_get_size(g_handle);
  int16_t scale = static_cast<int16_t>(maxBox / (size + 4));
  if (scale < 1) scale = 1;
  const int16_t boxW = static_cast<int16_t>((size + 4) * scale);
  const int16_t x0 = static_cast<int16_t>((Ui::W - boxW) / 2);
  ui.fillRect(x0, y0, boxW, boxW, freeink::ui::Color::White);
  ui.strokeRect(x0, y0, boxW, boxW, 2, 4);
  const int16_t qx0 = static_cast<int16_t>(x0 + 2 * scale), qy0 = static_cast<int16_t>(y0 + 2 * scale);
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (esp_qrcode_get_module(g_handle, x, y))
        ui.fillRect(static_cast<int16_t>(qx0 + x * scale), static_cast<int16_t>(qy0 + y * scale), scale, scale,
                    freeink::ui::Color::Black);
    }
  }
  return true;
}

}  // namespace qrdraw
