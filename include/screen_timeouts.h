#pragma once

// ===========================================================================
// screen_timeouts — Settings -> Timeouts: every configurable timeout in the
// project, each its own row cycling through localsettings::kTimeoutChoicesMin
// (1/3/5/15/30 min) on tap — same convention screen_debug.h uses for its
// backlight/warmth steppers.
//
// A child of screen_settings the same way screen_developer is: it never
// references screen_settings itself — main.cpp's Stage::Timeouts case owns
// returning to Settings, exactly like Stage::Developer / Stage::RoomPick do.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"

namespace screen_timeouts {

struct TimeoutItem { const char* title; const char* help; };
inline constexpr TimeoutItem kItems[] = {
    {"Screen timeout", "Idle time before the carousel sleeps"},
    {"Control page timeout", "Idle time on Lighting/Blinds/... before reverting to Status"},
    {"Refresh interval", "Overrides the server's per-room refresh cadence"},
    {"Back", "Return to Settings"},
};
inline constexpr int kCount = 4;
inline int sel = 0;
inline int pressed = -1;

inline const char* subtitleFor(int i) {
  static char buf[24];
  switch (i) {
    case 0:
      snprintf(buf, sizeof(buf), "%u min", localsettings::idleToSleepMin);
      return buf;
    case 1:
      snprintf(buf, sizeof(buf), "%u min", localsettings::controlPageRevertMin);
      return buf;
    case 2:
      if (localsettings::refreshOverrideMin == 0) return "Off (server-set)";
      snprintf(buf, sizeof(buf), "%u min", localsettings::refreshOverrideMin);
      return buf;
    default:
      return kItems[i].help;
  }
}

inline constexpr int16_t kRowH    = 92;
inline constexpr int16_t kRowPadX = 24;

inline void drawList() {
  ui.clear();
  drawStatusBar("Timeouts", false, &kWx_ui_refresh);
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
    // The actual value (e.g. "5 min") is the thing users come to this screen
    // to read at a glance, so it gets the same size as the row title above
    // it, not the small caption face the "Back" row's help text still uses.
    ui.text(subtitleFor(i), textX, static_cast<int16_t>(y + kRowH / 2 + 4), textW, 28,
            TextAlign::Left, subFg, 1, i < 3 ? Ui::kFont28 : Ui::kFontSmall);

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

// `r` defaults to Fast: draw() is also called from main.cpp's Left/Right
// highlight navigation and cycle()'s value-stepping (control feedback), so
// enter() overrides it to Full below.
inline void draw(Rf r = Rf::Fast) {
  drawList();
  commitFrame(r);
}
inline void enter() {
  stage = Stage::Timeouts;
  sel = 0;
  pressed = -1;
  draw(Rf::Full);
}

// Rows 0-2 cycle their own value on tap/Power; row 3 ("Back") and every other
// exit gesture are handled by main.cpp's Stage::Timeouts case, same as
// screen_developer's "Back" row.
inline void cycle(int i) {
  switch (i) {
    case 0: localsettings::setIdleToSleepMin(localsettings::nextTimeoutChoice(localsettings::idleToSleepMin)); break;
    case 1: localsettings::setControlPageRevertMin(localsettings::nextTimeoutChoice(localsettings::controlPageRevertMin)); break;
    case 2: localsettings::setRefreshOverrideMin(localsettings::nextTimeoutChoiceOrOff(localsettings::refreshOverrideMin)); break;
    default: return;
  }
  draw();
}

}  // namespace screen_timeouts
