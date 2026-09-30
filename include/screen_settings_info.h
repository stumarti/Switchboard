#pragma once

// ===========================================================================
// screen_settings_info — the key/value device info dump, behind Settings ->
// Device info, with a CHECK FOR UPDATE button when the server allows updates
// from the remote (screen_ota.h; app/stages.h routes the tap).
// ===========================================================================

#include "screen_common.h"
#include "config.h"
#include "globals_client.h"
#include "device_config_client.h"

namespace screen_settings_info {

// CHECK FOR UPDATE, when the server's Remote updates page allows it.
inline constexpr int16_t kUpdX = 60, kUpdY = 650, kUpdW = Ui::W - 120, kUpdH = 64;
inline bool updateButtonShown() { return deviceconfig::fwEnabled && deviceconfig::fwButton; }
inline bool updateButtonHit(int16_t tx, int16_t ty) {
  return updateButtonShown() && tx >= kUpdX && tx < kUpdX + kUpdW && ty >= kUpdY - 8 && ty < kUpdY + kUpdH + 8;
}

inline void draw() {
  ui.clear();
  drawStatusBar("Device info");
  int16_t y = static_cast<int16_t>(76 + kPad);
  auto row = [&](const char* k, const char* v) {
    ui.text(k, kShPad, y, 150, 22, TextAlign::Left, Color::DarkGray, 1, Ui::kFontSmall);
    ui.text(v, static_cast<int16_t>(kShPad + 150), y, static_cast<int16_t>(Ui::W - kShPad * 2 - 150),
            22, TextAlign::Left, Color::Black, 1, Ui::kFontSmall);
    y = static_cast<int16_t>(y + 34);
  };
  char b[64];
  row("Firmware", FIRMWARE_VERSION);
  row("Room", deviceconfig::name[0] ? deviceconfig::name : deviceconfig::activeSlug);
  row("Slug", deviceconfig::activeSlug);
  row("Server", SWITCHBOARD_SERVER_HOST);
  row("HA host", globalsclient::haHost[0] ? globalsclient::haHost : "-");
  row("HA linked", globalsclient::ok ? "yes" : "no");
  row("Weather", deviceconfig::weatherEntity[0] ? deviceconfig::weatherEntity : "-");
  row("Climate", deviceconfig::climateEntity[0] ? deviceconfig::climateEntity : "-");
  row("Light", deviceconfig::lightGroupEnabled ? deviceconfig::lightGroupEntity : "-");
  if (deviceconfig::refreshAligned)
    snprintf(b, sizeof(b), "%u min, on the clock +%us", static_cast<unsigned>(deviceconfig::refreshIntervalMin),
             static_cast<unsigned>(deviceconfig::refreshStaggerSec));
  else
    snprintf(b, sizeof(b), "%u min", static_cast<unsigned>(deviceconfig::refreshIntervalMin));
  row("Refresh", b);
  snprintf(b, sizeof(b), "%s%s", WiFi.localIP().toString().c_str(),
           WiFi.status() == WL_CONNECTED ? "" : "  (offline)");
  row("IP", b);
  if (g_battPct > 0) snprintf(b, sizeof(b), "%u%%", g_battPct);
  else               snprintf(b, sizeof(b), "reading...");
  row("Battery", b);
  snprintf(b, sizeof(b), "%u KB free", static_cast<unsigned>(ESP.getFreeHeap() / 1024));
  row("Heap", b);
  if (!deviceconfig::fwEnabled)          snprintf(b, sizeof(b), "Off (server)");
  else if (deviceconfig::fwOfferVersion[0]) snprintf(b, sizeof(b), "%s available", deviceconfig::fwOfferVersion);
  else                                   snprintf(b, sizeof(b), "Up to date");
  row("Updates", b);
  if (updateButtonShown()) {
    ui.strokeRect(kUpdX, kUpdY, kUpdW, kUpdH, 2, 16);
    ui.text("CHECK FOR UPDATE", kUpdX, static_cast<int16_t>(kUpdY + kUpdH / 2 - 14), kUpdW, 28, TextAlign::Center,
            Color::Black);
  }
  ui.text("Home / Left  -  back", 0, static_cast<int16_t>(Ui::H - 40), Ui::W, 20, TextAlign::Center,
          Color::DarkGray, 1, Ui::kFontSmall);
  commitFrame(Rf::Full);  // sub-screen push
}

inline void enter() {
  stage = Stage::SettingsInfo;
  draw();
}

}  // namespace screen_settings_info
