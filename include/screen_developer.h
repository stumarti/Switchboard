#pragma once

// ===========================================================================
// screen_developer — Settings -> Developer: on-device debug toggles that
// don't belong in the ordinary Settings list. Currently just the pixel grid
// overlay (screen_common.h's drawPixelGrid); more toggles can be added as
// rows the same way.
//
// A child of screen_settings the same way screen_room_pick / screen_settings_
// info are: it never references screen_settings itself (that would make a
// circular #include) — main.cpp's Stage::Developer case owns returning to
// Settings, exactly like Stage::RoomPick / Stage::SettingsInfo already do.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "screen_debug.h"
#include "persist.h"

namespace screen_developer {

struct DevItem { const char* title; };
inline constexpr DevItem kItems[] = {
    {"Pixel grid overlay"},
    {"Disable standby"},
    {"Button checker"},
    {"Hard reset"},
    {"Back"},
};
inline constexpr int kCount = 5;
inline int sel = 0;
// Same one-frame-flash-then-act convention as screen_settings::pressed.
inline int pressed = -1;

inline const char* subtitleFor(int i) {
  switch (i) {
    case 0: return localsettings::pixelGrid ? "ON" : "OFF";
    case 1: return localsettings::standbyDisabled ? "ON - won't sleep" : "OFF";
    case 2: return "Buttons, touch, backlight";
    case 3: return "Wipes room config, reboots";
    default: return "Return to Settings";
  }
}

inline constexpr int16_t kRowH    = 92;
inline constexpr int16_t kRowPadX = 24;

inline void drawList() {
  ui.clear();
  drawStatusBar("Developer", false, &kWx_ui_cog);
  const int16_t top = static_cast<int16_t>(kStatusBarH + 12 + kPad);
  for (int i = 0; i < kCount; ++i) {
    const int16_t y = static_cast<int16_t>(top + i * kRowH);
    const bool press = i == pressed;
    if (press)
      ui.fillRect(kRowPadX, static_cast<int16_t>(y + 4),
                 static_cast<int16_t>(Ui::W - 2 * kRowPadX), static_cast<int16_t>(kRowH - 8),
                 Color::Black, 16);
    const Color fg = press ? Color::White : Color::Black;
    const Color subFg = press ? Color::White : Color::DarkGray;

    const int16_t textX = static_cast<int16_t>(kRowPadX + 16);
    const int16_t textW = static_cast<int16_t>(Ui::W - kRowPadX - 16 - textX);
    ui.text(kItems[i].title, textX, static_cast<int16_t>(y + kRowH / 2 - 30), textW, 32,
            TextAlign::Left, fg, 1, Ui::kFont28);
    ui.text(subtitleFor(i), textX, static_cast<int16_t>(y + kRowH / 2 + 4), textW, 28,
            TextAlign::Left, subFg);

    if (i < kCount - 1)
      drawDottedLine(kRowPadX, static_cast<int16_t>(y + kRowH - 1),
                    static_cast<int16_t>(Ui::W - 2 * kRowPadX));
  }
}
inline int hitTest(int16_t ty) {
  const int16_t top = static_cast<int16_t>(kStatusBarH + 12 + kPad);
  if (ty < top) return -1;
  const int i = (ty - top) / kRowH;
  return (i >= 0 && i < kCount) ? i : -1;
}

inline void draw() {
  drawList();
  commitFrame(Rf::Clean);
}
inline void enter() {
  stage = Stage::Developer;
  sel = 0;
  pressed = -1;
  draw();
}

// Rows 0-3 act here; the last row ("Back") and every other exit gesture are
// handled by main.cpp's Stage::Developer case, same as screen_room_pick /
// screen_settings_info.
inline void activate(int i) {
  switch (i) {
    case 0: localsettings::setPixelGrid(!localsettings::pixelGrid); draw(); break;
    case 1: localsettings::standbyDisabled = !localsettings::standbyDisabled; draw(); break;
    case 2: screen_debug::enter(); break;
    case 3:
      // Wipe the picked room + its cached state first — the rescue path out
      // of a room whose server config wedges the device on every boot (see
      // deviceconfig::resetSlug()'s comment). A plain restart alone would
      // just boot straight back into the same bad room.
      deviceconfig::resetSlug();
      persist::wipe();
      ESP.restart();
      break;
    default: break;
  }
}

}  // namespace screen_developer
