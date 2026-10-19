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
// no status bar / commitFrame of its own, app/carousel.h's drawStandbyContent()
// owns that. The QR view is a separate full-screen Stage (like
// screen_settings_info), since a dense module grid wants the whole panel and
// a Full refresh, not a partial-refresh carousel repaint.
//
// The QR code itself is qr_draw.h's.
// ===========================================================================

#include "qr_draw.h"
#include "screen_common.h"
#include "refresh_policy.h"
#include "app/input.h"
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

inline int g_qrIdx = -1;

inline void drawQr() {
  ui.clear();
  drawStatusBar("Wi-Fi networks", false, &icons::get("wx_ui_wifi"));
  if (g_qrIdx < 0 || g_qrIdx >= globalsclient::wifiNetCount) {
    commitFrame(Rf::Full);  // only ever reached via enterQr() — a sub-screen push
    return;
  }
  const globalsclient::WifiNetItem& n = globalsclient::wifiNets[g_qrIdx];

  char uri[320];
  qrdraw::wifiUri(uri, sizeof(uri), n.ssid, n.password, n.open);

  ui.text(n.name, 0, static_cast<int16_t>(kStatusBarH + 12 + kPad), Ui::W, 30, TextAlign::Center,
          Color::Black, 1, Ui::kFont28);
  ui.text(n.ssid, 0, static_cast<int16_t>(kStatusBarH + 12 + kPad + 34), Ui::W, 22,
          TextAlign::Center, Color::DarkGray, 1, Ui::kFontSmall);

  // Scale to fill most of the width, leaving room for the labels above/below.
  if (!qrdraw::draw(ui, uri, static_cast<int16_t>(kStatusBarH + 12 + kPad + 74),
                    static_cast<int16_t>(Ui::W - 2 * kShPad))) {
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

// --- input (the carousel dispatches here while this page is showing) --

// Wifi page: tap a network row -> straight to its QR screen (no
// press-flash needed, same as Settings -> Select room's row tap).
inline bool handleTap(const InFrame& in) {
  const int i = hitTest(in.tx, in.ty);
  if (i >= 0) {
    standbyIdleSinceMs = millis();
    enterQr(i);
    return true;
  }
  return false;
}

}  // namespace screen_wifi_networks
