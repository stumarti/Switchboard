#pragma once

// ===========================================================================
// screen_common — the shared substrate every screen_*.h (and main.cpp) builds
// on: the hardware singletons, the Stage enum + current stage, the e-ink
// refresh-cadence helper, the shared status-bar chrome, and a handful of
// small widgets (action bar, chip list, big-percent readout) reused by more
// than one carousel page. Nothing screen-specific lives here.
// ===========================================================================

#include <Arduino.h>
#include <WiFi.h>

#include "ui.h"
#include "assets.h"
#include "weather_icons.h"
#include "device_config_client.h"
#include "local_settings.h"

using Color = freeink::ui::Color;
using TextAlign = freeink::ui::TextAlign;

inline Ui ui;
inline InputManager input;
inline FrontlightManager frontlight;
inline BatteryMonitor battery;

// Every screen this firmware can show. Stage::Standby is the carousel itself
// (main.cpp); everything else is one screen_*.h.
enum class Stage : uint8_t {
  Splash, Wifi, Standby, Settings, SettingsInfo, RoomPick, Developer, Timeouts, Debug, NoHA,
  NoRoom, ErrPreview, LowBattery, WifiQr
};
inline Stage stage = Stage::Splash;

// The last input (of any kind, on any screen) — every screen's enter()
// resets this; main.cpp's carousel idle timeout reads it.
inline uint32_t standbyIdleSinceMs = 0;

// Which carousel page is showing. The carousel itself (paging, the dots, the
// sleep/wake page-memory in RTC) is main.cpp's "carousel logic"; the raw page
// index lives here because most non-carousel screens need to reset it to 0
// when an action sends the user back to the carousel.
inline uint8_t carouselPage = 0;

// Set while the carousel (or a screen that sleeps like it, e.g. No-HA) owns
// the device, so a timer wake goes straight back to the fast wake path
// instead of a cold boot. Retained through deep sleep.
RTC_DATA_ATTR inline bool rtcStandbyActive = false;

// Set by the "No Home Assistant" / "Charge the device" screens just before a
// deep sleep that must resume on the SAME screen rather than the carousel;
// setup() (main.cpp) reads these to route a wake correctly. Retained through
// deep sleep.
RTC_DATA_ATTR inline bool rtcNoHA = false;
RTC_DATA_ATTR inline bool rtcLowBattery = false;

// --- e-ink refresh cadence -----------------------------------------------
// FAST is instant but ghosts when repeated; a HALF (Clean) is a self-contained
// charge scrub that leaves nothing behind (~0.5 s); FULL is the hard flash.
// Screen transitions and every kCleanEvery-th repaint use a clean scrub;
// cheap live repaints (a status glyph, a slider) use FAST.
enum class Rf : uint8_t { Fast, Clean, Full };
inline constexpr uint8_t kCleanEvery = 3;
inline uint8_t g_fastRun = 0;

// Settings -> Developer -> Pixel grid overlay: a coordinate grid over
// whatever the current screen already drew, for lining up layout constants —
// 20px minor lines, 100px major lines (labeled on both axes). Purely visual,
// drawn last so it never affects hit-testing (every hitTest() reads raw
// touch/logical coordinates, not the framebuffer).
inline void drawPixelGrid() {
  for (int16_t x = 0; x <= Ui::W; x += 20)
    ui.fillRect(x, 0, 1, Ui::H, (x % 100 == 0) ? Color::DarkGray : Color::LightGray);
  for (int16_t y = 0; y <= Ui::H; y += 20)
    ui.fillRect(0, y, Ui::W, 1, (y % 100 == 0) ? Color::DarkGray : Color::LightGray);

  char buf[8];
  for (int16_t x = 0; x <= Ui::W; x += 100) {
    snprintf(buf, sizeof(buf), "%d", x);
    ui.text(buf, static_cast<int16_t>(x + 2), 0, 44, 14, TextAlign::Left, Color::DarkGray, 1,
            Ui::kFontSmall);
  }
  for (int16_t y = 100; y <= Ui::H; y += 100) {  // y=0 would collide with the x=0 label
    snprintf(buf, sizeof(buf), "%d", y);
    ui.text(buf, 2, static_cast<int16_t>(y + 2), 44, 14, TextAlign::Left, Color::DarkGray, 1,
            Ui::kFontSmall);
  }
}

inline void commitFrame(Rf r) {
  if (localsettings::pixelGrid) drawPixelGrid();
  if (r == Rf::Full) {
    ui.flushFull();
    g_fastRun = 0;
  } else if (r == Rf::Clean || g_fastRun >= kCleanEvery) {
    ui.flushHalf();
    g_fastRun = 0;
  } else {
    ui.flushFast();
    ++g_fastRun;
  }
}

// --- battery ---------------------------------------------------------------
// The CW2017 fuel gauge shares the I2C bus with the GT911 touch controller, so
// a live read frequently collides and returns 0. Keep the last good reading;
// only a successful readPercentageChecked() in 1..100 updates it. g_battPct
// == 0 means "not read yet" — the status bar shows a placeholder.
inline uint8_t g_battPct = 0;
inline uint32_t g_battPollMs = 0;
inline void pollBattery(bool force = false) {
  const uint32_t now = millis();
  if (!force && now - g_battPollMs < 1500) return;
  g_battPollMs = now;
  uint16_t p = 0;
  if (battery.readPercentageChecked(p) && p >= 1 && p <= 100) g_battPct = static_cast<uint8_t>(p);
}

// --- shared chrome layout ---------------------------------------------------
inline constexpr int16_t kStatusBarH = 52;
inline constexpr int16_t kFooterBarH = 72;
// Extra breathing room between the status-bar rule and the first content row,
// applied on every screen.
inline constexpr int16_t kPad = 15;
// Left/right margin used by every full-width panel (settings rows, error
// screens, the control sheet, ...).
inline constexpr int16_t kShPad = 22;

// Every status-bar element (battery, Wi-Fi, the small % text) is centered on
// this one line, so they line up regardless of their own box height. +10 per
// request — nudges the title text down. The icons (leftIcon, battery, Wi-Fi,
// moon, updating) then got moved back up 10px per a follow-up request, so
// they're centered on the ORIGINAL, un-nudged line (kStatusIconMidY) while
// the title text stays on the nudged one.
inline constexpr int16_t kStatusMidY = kStatusBarH / 2 + 10;
inline constexpr int16_t kStatusIconMidY = kStatusBarH / 2;
inline constexpr int16_t kStatusTextH = 22;  // comfortably fits Ui::kFontSmall's line height (14px Atkinson Hyperlegible face, 19px yAdvance)
inline constexpr int16_t kStatusTextY = kStatusMidY - kStatusTextH / 2;
// The page title (top left) uses the full 24px face — bigger than the rest of
// the bar chrome. -5 per request, on top of kStatusMidY's own +10 nudge.
inline constexpr int16_t kStatusTitleH = 28;
inline constexpr int16_t kStatusTitleY = kStatusMidY - kStatusTitleH / 2 - 5;

// Battery glyph: a rounded outline + terminal nub, filled left-to-right by
// percentage. Drawn small enough to sit inline in the status bar.
inline constexpr int16_t kBattW = 22, kBattH = 11, kBattNub = 2;
inline void drawBatteryGlyph(int16_t x, int16_t y, uint8_t percent) {
  ui.strokeRect(x, y, kBattW, kBattH, 1, 2);
  ui.fillRect(static_cast<int16_t>(x + kBattW), static_cast<int16_t>(y + (kBattH - 5) / 2),
              kBattNub, 5, Color::Black, 1);
  const uint8_t pct = percent > 100 ? 100 : percent;
  const int16_t innerW = kBattW - 4;
  const int16_t fillW = static_cast<int16_t>(innerW * pct / 100);
  if (fillW > 0) ui.fillRect(static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 2), fillW,
                             kBattH - 4, Color::Black);
}

// A horizontal dotted rule — small square dots, evenly spaced — used in place
// of a solid hline() for the status/footer bar dividers.
inline void drawDottedLine(int16_t x, int16_t y, int16_t w, int16_t dot = 2, int16_t gap = 4) {
  for (int16_t dx = 0; dx < w; dx = static_cast<int16_t>(dx + dot + gap)) {
    const int16_t seg = static_cast<int16_t>(dx + dot <= w ? dot : w - dx);
    if (seg > 0) ui.fillRect(static_cast<int16_t>(x + dx), y, seg, dot, Color::Black);
  }
}
inline void drawDottedVLine(int16_t x, int16_t y, int16_t h, int16_t dot = 2, int16_t gap = 4) {
  for (int16_t dy = 0; dy < h; dy = static_cast<int16_t>(dy + dot + gap)) {
    const int16_t seg = static_cast<int16_t>(dy + dot <= h ? dot : h - dy);
    if (seg > 0) ui.fillRect(x, static_cast<int16_t>(y + dy), dot, seg, Color::Black);
  }
}

// --- shared status bar ---------------------------------------------------
// The thin chrome bar every post-boot screen carries: a left label, then a
// right-aligned battery meter and Wi-Fi glyph, under a dotted rule. Small
// font throughout — the default 24px face is too heavy for a 44px bar. Every
// element is centered on kStatusMidY so icons and text line up exactly.
// `showMoon` adds a crescent at the far right — the carousel sets it the
// moment it powers down the frontlight and deep-sleeps. `updating` adds a
// small refresh glyph left of the Wi-Fi icon — a post-wake refresh in
// flight, painted once over the still-good cached page rather than a modal
// card, and cleared in the same repaint that shows the fresh data.
inline void drawStatusBar(const char* leftLabel, bool showMoon = false,
                          const freeink::Icon* leftIcon = nullptr, bool updating = false) {
  int16_t labelX = 16;
  if (leftIcon) {
    ui.icon(*leftIcon, 14, static_cast<int16_t>(kStatusIconMidY - leftIcon->h / 2 + 5));
    labelX = static_cast<int16_t>(14 + leftIcon->w + 8);
  }
  ui.text(leftLabel, labelX, kStatusTitleY, static_cast<int16_t>(280 - labelX + 16), kStatusTitleH,
          TextAlign::Left, Color::Black, 1, /*font 0 = 24px face*/ 0);

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;

  int16_t rightEdge = static_cast<int16_t>(Ui::W - 16);
  if (showMoon) {
    const int16_t moonX = static_cast<int16_t>(rightEdge - kMoon.w);
    ui.icon(kMoon, moonX, static_cast<int16_t>(kStatusIconMidY - kMoon.h / 2));
    rightEdge = static_cast<int16_t>(moonX - 8);
  }

  // Battery meter — the outline fill IS the level, so no % text.
  const int16_t battX = static_cast<int16_t>(rightEdge - kBattW - kBattNub);
  const int16_t battY = static_cast<int16_t>(kStatusIconMidY - kBattH / 2);
  drawBatteryGlyph(battX, battY, g_battPct);

  const freeink::Icon& wifiIcon = wifiConnected ? kWifiRadiating : kWifiEmpty;
  const int16_t wifiX = static_cast<int16_t>(battX - 10 - wifiIcon.w);
  const int16_t wifiY = static_cast<int16_t>(kStatusIconMidY - wifiIcon.h / 2);
  ui.icon(wifiIcon, wifiX, wifiY);

  if (updating) {
    const int16_t updX = static_cast<int16_t>(wifiX - 10 - kWx_ui_refresh.w);
    ui.icon(kWx_ui_refresh, updX, static_cast<int16_t>(kStatusIconMidY - kWx_ui_refresh.h / 2));
  }

  // Solid rule under the bar.
  ui.fillRect(0, kStatusBarH, Ui::W, 2, Color::Black);
}

// --- shared carousel-page widgets ------------------------------------------
// Reused by more than one content page (Climate+Blinds share the action bar
// look; Lighting+Blinds share the chip list and the big-percent readout), so
// they live here rather than being duplicated or owned by just one screen.

// "<n>" big + small "%" (the big face is digits-only), centred on cx. Used by
// the Lighting group card and the Blinds position readout.
inline void drawBigPct(int16_t cx, int16_t y, int pct) {
  char n[6];
  snprintf(n, sizeof(n), "%d", pct);
  const fu::Size ns = ui.measure(n, Ui::kFontTemp);
  const fu::Size ps = ui.measure("%", 0);
  const int16_t total = static_cast<int16_t>(ns.width + 6 + ps.width);
  const int16_t x = static_cast<int16_t>(cx - total / 2);
  ui.text(n, x, y, ns.width, ns.height, TextAlign::Left, Color::Black, 1, Ui::kFontTemp);
  ui.text("%", static_cast<int16_t>(x + ns.width + 6),
          static_cast<int16_t>(y + ns.height - ps.height - 4), ps.width, ps.height, TextAlign::Left,
          Color::Black, 1, 0);
}

// Outline "chip" buttons (scenes / individual lights / individual blinds).
// Two per row; layout is computed the same way in draw + hit-test so no rects
// need storing. Shared by the Lighting and Blinds pages.
inline constexpr int16_t kChipCols = 2;
inline constexpr int16_t kChipGap  = 12;
inline constexpr int16_t kChipW = (Ui::W - kShPad * 2 - (kChipCols - 1) * kChipGap) / kChipCols;
inline constexpr int16_t kChipH = 60;
inline void chipPos(int16_t y0, int i, int16_t& x, int16_t& y) {
  x = static_cast<int16_t>(kShPad + (i % kChipCols) * (kChipW + kChipGap));
  y = static_cast<int16_t>(y0 + (i / kChipCols) * (kChipH + kChipGap));
}
// `onStates`, if given, draws a small on/off bulb icon before each item's
// label (the Lighting page's individual-lights tab). `selectedIdx`, if >= 0,
// draws that one item filled black instead of outlined (the Lighting page's
// scenes tab, marking the last one activated). Neither is used by Blinds.
inline void drawChips(int16_t y0, const char* heading, const deviceconfig::LightItem* items, int n,
                      const bool* onStates = nullptr, int selectedIdx = -1) {
  ui.text(heading, kShPad, static_cast<int16_t>(y0 - 24), 200, 20, TextAlign::Left, Color::DarkGray,
          1, Ui::kFontSmall);
  for (int i = 0; i < n; ++i) {
    int16_t x, y;
    chipPos(y0, i, x, y);
    const bool sel = i == selectedIdx;
    if (sel) ui.fillRect(x, y, kChipW, kChipH, Color::Black, 14);
    else     ui.strokeRect(x, y, kChipW, kChipH, 2, 14);
    const Color fg = sel ? Color::White : Color::Black;
    if (onStates) {
      const freeink::Icon& ic = onStates[i] ? kWx_ui_bulb_on : kWx_ui_bulb_off;
      ui.icon(ic, static_cast<int16_t>(x + 10), static_cast<int16_t>(y + (kChipH - ic.h) / 2), fg);
      ui.text(items[i].name, static_cast<int16_t>(x + 10 + ic.w + 8),
              static_cast<int16_t>(y + (kChipH - 24) / 2),
              static_cast<int16_t>(kChipW - 10 - ic.w - 8 - 8), 24, TextAlign::Left, fg);
    } else {
      ui.text(items[i].name, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + (kChipH - 26) / 2),
              static_cast<int16_t>(kChipW - 16), 26, TextAlign::Center, fg);
    }
  }
}
inline int chipHit(int16_t y0, int n, int16_t tx, int16_t ty) {
  for (int i = 0; i < n; ++i) {
    int16_t x, y;
    chipPos(y0, i, x, y);
    if (tx >= x && tx < x + kChipW && ty >= y && ty < y + kChipH) return i;
  }
  return -1;
}

// The bottom action bar (Climate: COOLER/MODE/WARMER, Blinds: CLOSE/STOP/OPEN)
// — three outline rounded buttons; `pressed` fills that one for tap feedback,
// plus the small glyphs drawn inside each button.
inline constexpr int16_t kBarBtnH  = 84;
inline constexpr int16_t kBarBtnY  = Ui::H - 64 - kBarBtnH;  // sits well above the carousel dots
inline constexpr int16_t kBarGap   = 12;
inline constexpr int16_t kBarBtnW  = (Ui::W - kShPad * 2 - 2 * kBarGap) / 3;

// `font`/`textH` default to the original small (10px) label used by
// Blinds/Tv; Music passes the bigger default face for its PREV/PLAY/NEXT
// labels (see screen_music.h's drawBar).
inline void drawActionBtn(int col, bool pressed, const char* label, fu::FontId font = Ui::kFontSmall,
                          int16_t textH = 20) {
  const int16_t x = static_cast<int16_t>(kShPad + col * (kBarBtnW + kBarGap));
  if (pressed) ui.fillRect(x, kBarBtnY, kBarBtnW, kBarBtnH, Color::Black, 18);
  else         ui.strokeRect(x, kBarBtnY, kBarBtnW, kBarBtnH, 2, 18);
  const int16_t textY = static_cast<int16_t>(kBarBtnY + kBarBtnH - textH - 6);
  ui.text(label, x, textY, kBarBtnW, textH, TextAlign::Center,
          pressed ? Color::White : Color::Black, 1, font);
}
inline int16_t barBtnCx(int col) {
  return static_cast<int16_t>(kShPad + col * (kBarBtnW + kBarGap) + kBarBtnW / 2);
}
inline void glyphMinus(int16_t cx, int16_t cy, Color c) {
  ui.fillRect(static_cast<int16_t>(cx - 13), static_cast<int16_t>(cy - 3), 26, 6, c, 3);
}
inline void glyphPlus(int16_t cx, int16_t cy, Color c) {
  glyphMinus(cx, cy, c);
  ui.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 13), 6, 26, c, 3);
}
// Small filled triangle (blinds close = down, open = up) and a stop square.
// `rowH`/`widthStep` scale the glyph; defaults match the original size used
// by Blinds' OPEN/CLOSE arrows. Tv's D-pad passes bigger values for a larger
// glyph on its bigger buttons.
inline void glyphTri(int16_t cx, int16_t cy, bool up, Color c, int16_t rowH = 3,
                     int16_t widthStep = 4) {
  for (int i = 0; i < 6; ++i) {
    const int16_t rw = static_cast<int16_t>((up ? i + 1 : 6 - i) * widthStep);
    const int16_t yy = static_cast<int16_t>(cy - 3 * rowH + i * rowH);
    ui.fillRect(static_cast<int16_t>(cx - rw / 2), yy, rw, rowH, c);
  }
}
inline void glyphStop(int16_t cx, int16_t cy, Color c) {
  ui.fillRect(static_cast<int16_t>(cx - 9), static_cast<int16_t>(cy - 9), 18, 18, c, 3);
}

// One "icon  label ............ value" detail row (wind / humidity / etc, on
// the Status page).
inline void drawDetailRow(int16_t y, const freeink::Icon& ic, const char* label,
                          const char* value) {
  const int16_t pad = 24;
  const int16_t iconY = static_cast<int16_t>(y + 11 - ic.h / 2);
  ui.icon(ic, pad, iconY);
  ui.text(label, static_cast<int16_t>(pad + ic.w + 10), y, 220, 24, TextAlign::Left);
  ui.text(value, static_cast<int16_t>(Ui::W - pad - 200), y, 200, 24, TextAlign::Right);
}

// Draw "<value>" plus a superscript degree ring, in the given font slot,
// left-aligned at (x, y). Returns the total drawn width. The bundled fonts
// only cover ASCII, so the degree mark is a stroked ring, not a glyph. Shared
// by the Status page (current + indoor temp) and the Climate page (target).
inline int16_t drawDegreesStr(int16_t x, int16_t y, fu::FontId font, const char* n) {
  const fu::Size s = ui.measure(n, font);
  ui.text(n, x, y, s.width, s.height, TextAlign::Left, Color::Black, 1, font);

  const bool big = font == Ui::kFontTemp;
  const int16_t d = big ? 16 : 8;
  const int16_t gap = big ? 8 : 4;
  const int16_t ringY = big ? static_cast<int16_t>(y + 6) : static_cast<int16_t>(y + 2);
  ui.strokeRect(static_cast<int16_t>(x + s.width + gap), ringY, d, d, big ? 3 : 2,
                static_cast<uint8_t>(d / 2));
  return static_cast<int16_t>(s.width + gap + d);
}
inline int16_t drawDegrees(int16_t x, int16_t y, fu::FontId font, int value) {
  char n[8];
  snprintf(n, sizeof(n), "%d", value);
  return drawDegreesStr(x, y, font, n);
}
// "<n>" + degree ring, centred on cx. No unit letter — just the degree mark.
// Used by the Climate page's target setpoint.
inline void drawTempC(int16_t cx, int16_t y, fu::FontId font, const char* n) {
  const bool big = font == Ui::kFontTemp;
  const int16_t d = big ? 18 : 8;
  const int16_t gap = big ? 8 : 3;
  const fu::Size ns = ui.measure(n, font);
  const int16_t total = static_cast<int16_t>(ns.width + gap + d);
  const int16_t x = static_cast<int16_t>(cx - total / 2);
  ui.text(n, x, y, ns.width, ns.height, TextAlign::Left, Color::Black, 1, font);
  const int16_t ringX = static_cast<int16_t>(x + ns.width + gap);
  const int16_t ringY = big ? static_cast<int16_t>(y + 8) : static_cast<int16_t>(y + 2);
  ui.strokeRect(ringX, ringY, d, d, big ? 3 : 2, static_cast<uint8_t>(d / 2));
}
