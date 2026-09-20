#pragma once

// ===========================================================================
// screen_settings — a vertical list of full-width buttons (reached from the
// control shade's cog tile, or the carousel jump list). Left/Right move the
// highlight; a tap on a row (or Power on the highlighted row) activates it.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "screen_settings_info.h"
#include "screen_room_pick.h"
#include "screen_developer.h"
#include "screen_timeouts.h"

namespace screen_settings {

// A vertical list of full-width rows: icon, title, and a status subtitle that
// mirrors the room's actual current setting (room name, refresh cadence,
// Wi-Fi SSID) rather than a fixed caption. The selected row is a filled black
// card; the rest sit flush against the background, separated by hairlines.
struct SettingsItem { const char* title; const freeink::Icon* icon; };
inline constexpr SettingsItem kItems[] = {
    {"Select room", &kWx_ui_room},
    {"Device info", &kWx_ui_info},
    {"Wi-Fi setup", &kWx_ui_wifi},
    {"Refresh now", &kWx_ui_refresh},
    {"Timeouts",    &kWx_ui_refresh},
    {"Developer",   &kWx_ui_cog},
    {"Restart",     &kWx_ui_restart},
    {"Back",        &kWx_ui_back},
};
inline constexpr int kCount = 8;
inline int sel = 0;
// The row actually being pressed right now (tap, or Power on the cursor row)
// — filled black for that one frame of feedback, same convention as the
// Climate/Blinds/Lighting buttons. `sel` (Left/Right) only tracks which row
// Power will activate; it's drawn with a light outline, never a fill.
inline int pressed = -1;

inline const char* subtitleFor(int i) {
  static char buf[48];
  switch (i) {
    case 0:
      snprintf(buf, sizeof(buf), "%s",
              deviceconfig::name[0] ? deviceconfig::name : deviceconfig::activeSlug);
      return buf;
    case 1:
      return "Firmware - link status";
    case 2:
      snprintf(buf, sizeof(buf), "%s",
              WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "Not connected");
      return buf;
    case 3:
      snprintf(buf, sizeof(buf), "Standby - every %u min", deviceconfig::refreshIntervalMin);
      return buf;
    case 4:
      snprintf(buf, sizeof(buf), "Screen %u min - Control %u min",
              localsettings::idleToSleepMin, localsettings::controlPageRevertMin);
      return buf;
    case 5:
      return localsettings::pixelGrid ? "Pixel grid ON" : "Debug tools";
    case 6:
      return "Reboot the device";
    default:
      return "Return to Standby";
  }
}

inline constexpr int16_t kRowH    = 92;
inline constexpr int16_t kRowPadX = 24;

// Shifted up 35px from the usual kStatusBarH+12+kPad list top — kCount grew
// to 8 rows (Timeouts/Developer added) and would otherwise run past the
// bottom of the 800px canvas.
inline constexpr int16_t kListTop = static_cast<int16_t>(kStatusBarH + 12 + kPad - 35);

inline void drawList() {
  ui.clear();
  drawStatusBar("Settings", false, &kWx_ui_cog);
  const int16_t top = kListTop;
  for (int i = 0; i < kCount; ++i) {
    const int16_t y = static_cast<int16_t>(top + i * kRowH);
    const bool press = i == pressed;
    // No box for the Left/Right cursor row — `sel` only tracks which row
    // Power will activate; the dotted separators alone divide the list.
    if (press)
      ui.fillRect(kRowPadX, static_cast<int16_t>(y + 4),
                 static_cast<int16_t>(Ui::W - 2 * kRowPadX), static_cast<int16_t>(kRowH - 8),
                 Color::Black, 16);
    const Color fg = press ? Color::White : Color::Black;
    const Color subFg = press ? Color::White : Color::DarkGray;

    const freeink::Icon& ic = *kItems[i].icon;
    const int16_t iconX = static_cast<int16_t>(kRowPadX + 16);
    const int16_t textX = static_cast<int16_t>(iconX + ic.w + 16);
    const int16_t textW = static_cast<int16_t>(Ui::W - kRowPadX - 16 - textX);
    ui.icon(ic, iconX, static_cast<int16_t>(y + kRowH / 2 - ic.h / 2), fg);
    ui.text(kItems[i].title, textX, static_cast<int16_t>(y + kRowH / 2 - 30), textW, 32,
            TextAlign::Left, fg, 1, Ui::kFont28);
    ui.text(subtitleFor(i), textX, static_cast<int16_t>(y + kRowH / 2 + 4), textW, 28,
            TextAlign::Left, subFg);

    if (i < kCount - 1)
      drawDottedLine(kRowPadX, static_cast<int16_t>(y + kRowH - 1),
                    static_cast<int16_t>(Ui::W - 2 * kRowPadX));
  }
}
inline int listHitTest(int16_t ty) {
  const int16_t top = kListTop;
  if (ty < top) return -1;
  const int i = (ty - top) / kRowH;
  if (i < 0 || i >= kCount) return -1;
  return i;
}

// `r` defaults to Fast: draw() is also called from main.cpp's Left/Right
// highlight navigation and row-press feedback (control feedback), so
// enter() overrides it to Full below.
inline void draw(Rf r = Rf::Fast) {
  drawList();
  commitFrame(r);
}
inline void enter() {
  stage = Stage::Settings;
  sel = 0;
  pressed = -1;
  draw(Rf::Full);
}

inline int hitTest(int16_t ty) { return listHitTest(ty); }

inline void activate(int i) {
  switch (i) {
    case 0: screen_room_pick::enter(); break;
    case 1: screen_settings_info::enter(); break;
    case 2:  // Wi-Fi setup: forget the network, reboot into provisioning
      WiFi.disconnect(true, /*eraseap=*/true);
      delay(150);
      ESP.restart();
      break;
    case 3:  // Refresh now
      kickWeatherRefresh();
      carouselPage = 0;
      enterStandby();
      break;
    case 4: screen_timeouts::enter(); break;   // Timeouts
    case 5: screen_developer::enter(); break;  // Developer
    case 6: ESP.restart(); break;              // Restart
    default: carouselPage = 0; enterStandby(); break;  // Back
  }
}

}  // namespace screen_settings
