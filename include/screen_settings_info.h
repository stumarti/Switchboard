#pragma once

// ===========================================================================
// screen_settings_info — the read-only key/value device info dump, behind
// Settings -> Device info.
// ===========================================================================

#include "screen_common.h"
#include "config.h"
#include "globals_client.h"
#include "device_config_client.h"

namespace screen_settings_info {

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
  ui.text("Home / Left  -  back", 0, static_cast<int16_t>(Ui::H - 40), Ui::W, 20, TextAlign::Center,
          Color::DarkGray, 1, Ui::kFontSmall);
  commitFrame(Rf::Clean);
}

inline void enter() {
  stage = Stage::SettingsInfo;
  draw();
}

}  // namespace screen_settings_info
