#pragma once

// ===========================================================================
// screen_power — the power menu, opened by holding the Power button for 10s
// from any stage once the input task is running. Restart / Shut down /
// Cancel, laid out like the Settings list. Left/Right move the cursor
// (outlined), Power activates it, or tap a row directly. Rows are
// only drawn and hit-tested here — app/stages.h's tickPowerMenu() owns the
// actions (restart, shutdown), since those need its sleep machinery.
// ===========================================================================

#include "screen_common.h"

namespace screen_power {

struct PowerItem { const char* title; const char* subtitle; const freeink::Icon* icon; };
// Not constexpr: icons::get() is a runtime lookup (see screen_settings.h).
inline const PowerItem kItems[] = {
    {"Restart",   "Reboot the device",           &icons::get("wx_ui_restart")},
    {"Shut down", "Press Power to turn back on", &icons::get("wx_tv_power")},
    {"Cancel",    "Return to Standby",           &icons::get("wx_ui_back")},
};
inline constexpr int kCount = 3;
inline constexpr int kRestart = 0;
inline constexpr int kShutdown = 1;
inline constexpr int kCancel = 2;

// The cursor starts on Cancel, so a stray Power click right after the menu
// opens can't restart or shut the device down.
inline int sel = kCancel;
inline int pressed = -1;  // one-frame black fill on the row being activated

// Nobody touched the menu for this long -> close it (Cancel).
inline constexpr uint32_t kAutoCancelMs = 30000;
inline uint32_t openedMs = 0;

inline constexpr int16_t kRowH    = 92;
inline constexpr int16_t kRowPadX = 24;
inline constexpr int16_t kListTop = static_cast<int16_t>(kStatusBarH + 12 + kPad);

inline void draw(Rf r = Rf::Fast) {
  ui.clear();
  drawStatusBar("Power", false, &icons::get("wx_tv_power"));
  for (int i = 0; i < kCount; ++i) {
    const int16_t y = static_cast<int16_t>(kListTop + i * kRowH);
    const int16_t bx = kRowPadX, by = static_cast<int16_t>(y + 4);
    const int16_t bw = static_cast<int16_t>(Ui::W - 2 * kRowPadX), bh = static_cast<int16_t>(kRowH - 8);
    const bool press = i == pressed;
    // Unlike Settings, the cursor row IS boxed: this menu is reached by a
    // button hold, so Left/Right + Power is the expected way through it.
    if (press)
      ui.fillRect(bx, by, bw, bh, Color::Black, 16);
    else if (i == sel)
      ui.strokeRect(bx, by, bw, bh, 2, 16);
    const Color fg = press ? Color::White : Color::Black;
    const Color subFg = press ? Color::White : Color::DarkGray;

    const freeink::Icon& ic = *kItems[i].icon;
    const int16_t iconX = static_cast<int16_t>(kRowPadX + 16);
    const int16_t textX = static_cast<int16_t>(iconX + ic.w + 16);
    const int16_t textW = static_cast<int16_t>(Ui::W - kRowPadX - 16 - textX);
    ui.icon(ic, iconX, static_cast<int16_t>(y + kRowH / 2 - ic.h / 2), fg);
    ui.text(kItems[i].title, textX, static_cast<int16_t>(y + kRowH / 2 - 30), textW, 32,
            TextAlign::Left, fg, 1, Ui::kFont28);
    ui.text(kItems[i].subtitle, textX, static_cast<int16_t>(y + kRowH / 2 + 4), textW, 28,
            TextAlign::Left, subFg);
  }
  ui.centered("Left / Right to choose, Power to select",
              static_cast<int16_t>(kListTop + kCount * kRowH + 24), 28, Color::DarkGray);
  commitFrame(r);
}

inline void enter() {
  stage = Stage::PowerMenu;
  sel = kCancel;
  pressed = -1;
  openedMs = millis();
  draw(Rf::Full);
}

inline int hitTest(int16_t ty) {
  if (ty < kListTop) return -1;
  const int i = (ty - kListTop) / kRowH;
  return i < kCount ? i : -1;
}

// --- the off screen --------------------------------------------------------
// Left on the panel through shutdown (e-ink holds it with no power): a big
// power glyph on a tonal disc, a title, and an arrow off the right edge at
// the physical Power button — the only thing that turns the device back on.
//
// Where the Power button sits along the panel's right edge, in logical
// pixels from the top. Nudge this if the arrow doesn't line up on hardware.
inline constexpr int16_t kOffPowerBtnY = 140;

inline void drawOff() {
  ui.clear();

  // Pointer: "Power" label + a bold arrow hard against the right edge.
  const freeink::Icon& arrow = icons::get("wx_off_arrow");
  const int16_t arrowX = static_cast<int16_t>(Ui::W - arrow.w);
  ui.icon(arrow, arrowX, static_cast<int16_t>(kOffPowerBtnY - arrow.h / 2), Color::Black);
  ui.text("Power", 0, static_cast<int16_t>(kOffPowerBtnY - 18), static_cast<int16_t>(arrowX - 4), 32,
          TextAlign::Right, Color::Black, 1, Ui::kFont28);

  // Hero: the power glyph centered on a light disc.
  constexpr int16_t kDisc = 232, kDiscTop = 250;
  const int16_t discX = static_cast<int16_t>((Ui::W - kDisc) / 2);
  ui.fillRect(discX, kDiscTop, kDisc, kDisc, Color::LightGray, kDisc / 2);
  const freeink::Icon& hero = icons::get("wx_off_power");
  ui.icon(hero, static_cast<int16_t>((Ui::W - hero.w) / 2),
          static_cast<int16_t>(kDiscTop + (kDisc - hero.h) / 2), Color::Black);

  ui.text("Switched off", 0, static_cast<int16_t>(kDiscTop + kDisc + 48), Ui::W, 32,
          TextAlign::Center, Color::Black, 1, Ui::kFont28);
  ui.centered("Press the Power button to turn on", static_cast<int16_t>(kDiscTop + kDisc + 92), 28,
              Color::DarkGray);
  commitFrame(Rf::Full);
}

}  // namespace screen_power
