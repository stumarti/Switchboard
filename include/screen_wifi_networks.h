#pragma once

// ===========================================================================
// screen_wifi_networks — the Wifi carousel page: a list of the shared
// configured networks (globalsclient::wifiNets[], from /api/globals —
// distinct from the SSID/password THIS device itself joined with, which is
// Settings -> Wi-Fi setup). Tapping a row opens a full-screen "WIFI:..." QR
// code (Stage::WifiQr) a guest's phone can scan to join, without typing a
// password.
//
// The list itself is carousel CONTENT, like screen_music/screen_lighting —
// no status bar / commitFrame of its own, main.cpp's drawStandbyContent()
// owns that. The QR view is a separate full-screen Stage (like
// screen_settings_info), since a dense module grid wants the whole panel and
// a Full refresh, not a partial-refresh carousel repaint.
//
// QR rendering uses the ESP-IDF `espressif__qrcode` component, which ships
// as a transitive dependency of the Arduino-ESP32 framework (esp_wifi's own
// provisioning uses it) — no extra lib_deps entry needed, just <qrcode.h>.
// ===========================================================================

#include <qrcode.h>

#include "screen_common.h"
#include "screen_fwd.h"
#include "globals_client.h"

namespace screen_wifi_networks {

// --- carousel content: the network list -----------------------------------
inline constexpr int16_t kListTop = kStatusBarH + 12 + kPad;
inline constexpr int16_t kRowH = 88, kRowInner = kRowH - 12;

inline void draw() {
  if (globalsclient::wifiNetCount == 0) {
    ui.text("No networks configured", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text("add one in the server's globals config", 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray,
            1, Ui::kFontSmall);
    return;
  }
  for (int i = 0; i < globalsclient::wifiNetCount; ++i) {
    const int16_t y = static_cast<int16_t>(kListTop + i * kRowH);
    const globalsclient::WifiNetItem& n = globalsclient::wifiNets[i];
    ui.strokeRect(kShPad, y, static_cast<int16_t>(Ui::W - 2 * kShPad), kRowInner, 2, 14);
    ui.text(n.name, static_cast<int16_t>(kShPad + 20), static_cast<int16_t>(y + 14),
            static_cast<int16_t>(Ui::W - 2 * kShPad - 150), 26, TextAlign::Left, Color::Black);
    ui.text(n.ssid, static_cast<int16_t>(kShPad + 20), static_cast<int16_t>(y + 42),
            static_cast<int16_t>(Ui::W - 2 * kShPad - 150), 20, TextAlign::Left, Color::DarkGray, 1,
            Ui::kFontSmall);
    ui.text(n.open ? "open" : "show QR", static_cast<int16_t>(Ui::W - kShPad - 130),
            static_cast<int16_t>(y + 24), 106, 22, TextAlign::Right, Color::DarkGray, 1,
            Ui::kFontSmall);
  }
}
// Row index at (tx,ty), or -1.
inline int hitTest(int16_t tx, int16_t ty) {
  (void)tx;  // rows are full-width; only the row (y) matters here
  if (globalsclient::wifiNetCount == 0 || ty < kListTop) return -1;
  const int i = (ty - kListTop) / kRowH;
  return i >= 0 && i < globalsclient::wifiNetCount ? i : -1;
}

// --- QR screen -------------------------------------------------------------

// Escape the WIFI: URI's reserved characters (\ ; , " :) with a backslash, per
// the de-facto "MECARD-like" Wi-Fi QR format most phone camera apps parse.
inline void escapeInto(char* out, size_t outCap, const char* in) {
  size_t o = 0;
  for (const char* p = in; *p && o + 2 < outCap; ++p) {
    if (strchr("\\;,\":", *p)) out[o++] = '\\';
    out[o++] = *p;
  }
  out[o] = 0;
}

inline int g_qrIdx = -1;
// The handle esp_qrcode_generate() hands to display_func — only valid inside
// that call and immediately after it returns (the library owns the buffer).
inline esp_qrcode_handle_t g_qrHandle = nullptr;
inline void onQrReady(esp_qrcode_handle_t h) { g_qrHandle = h; }

inline void drawQr() {
  ui.clear();
  drawStatusBar("Wi-Fi networks", false, &kWx_ui_wifi);
  if (g_qrIdx < 0 || g_qrIdx >= globalsclient::wifiNetCount) {
    commitFrame(Rf::Clean);
    return;
  }
  const globalsclient::WifiNetItem& n = globalsclient::wifiNets[g_qrIdx];

  char ssidEsc[80], passEsc[160];
  escapeInto(ssidEsc, sizeof(ssidEsc), n.ssid);
  escapeInto(passEsc, sizeof(passEsc), n.password);
  char uri[320];
  snprintf(uri, sizeof(uri), "WIFI:T:%s;S:%s;P:%s;;", n.open ? "nopass" : "WPA", ssidEsc, passEsc);

  g_qrHandle = nullptr;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = onQrReady;
  cfg.max_qrcode_version = 10;  // plenty for a WIFI: URI at MED ECC
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
  const bool ok = esp_qrcode_generate(&cfg, uri) == ESP_OK && g_qrHandle != nullptr;

  ui.text(n.name, 0, static_cast<int16_t>(kStatusBarH + 12 + kPad), Ui::W, 30, TextAlign::Center,
          Color::Black, 1, Ui::kFont28);
  ui.text(n.ssid, 0, static_cast<int16_t>(kStatusBarH + 12 + kPad + 34), Ui::W, 22,
          TextAlign::Center, Color::DarkGray, 1, Ui::kFontSmall);

  if (ok) {
    const int size = esp_qrcode_get_size(g_qrHandle);
    // Scale to fill most of the width, leaving room for the labels above/
    // below; snap the module size down so the quiet zone (2 modules) plus the
    // code fits without overflowing the panel width.
    const int16_t maxBox = static_cast<int16_t>(Ui::W - 2 * kShPad);
    int16_t scale = static_cast<int16_t>(maxBox / (size + 4));
    if (scale < 1) scale = 1;
    const int16_t boxW = static_cast<int16_t>((size + 4) * scale);
    const int16_t x0 = static_cast<int16_t>((Ui::W - boxW) / 2);
    const int16_t y0 = static_cast<int16_t>(kStatusBarH + 12 + kPad + 74);
    ui.fillRect(x0, y0, boxW, boxW, Color::White);
    ui.strokeRect(x0, y0, boxW, boxW, 2, 4);
    const int16_t qx0 = static_cast<int16_t>(x0 + 2 * scale), qy0 = static_cast<int16_t>(y0 + 2 * scale);
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        if (esp_qrcode_get_module(g_qrHandle, x, y))
          ui.fillRect(static_cast<int16_t>(qx0 + x * scale), static_cast<int16_t>(qy0 + y * scale),
                     scale, scale, Color::Black);
      }
    }
  } else {
    ui.text("Couldn't generate QR code", 0, 400, Ui::W, 28, TextAlign::Center, Color::DarkGray);
  }

  ui.text("Home / tap  -  back", 0, static_cast<int16_t>(Ui::H - 40), Ui::W, 20, TextAlign::Center,
          Color::DarkGray, 1, Ui::kFontSmall);
  commitFrame(Rf::Full);  // a dense module grid ghosts badly under a partial refresh
}

inline void enterQr(int idx) {
  if (idx < 0 || idx >= globalsclient::wifiNetCount) return;
  g_qrIdx = idx;
  stage = Stage::WifiQr;
  drawQr();
}

}  // namespace screen_wifi_networks
