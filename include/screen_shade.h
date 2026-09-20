#pragma once

// ===========================================================================
// screen_shade — the control panel bottom sheet, opened by HOLDING the Home
// key from the carousel. Solid-fill styling throughout: filled black buttons
// with white glyphs, a filled progress bar, no outlines or dotted rules. Big
// [-] / [+] touch buttons drive the frontlight; brightness 0% == lamp off.
// Home key again, or a tap on the strip above the sheet, closes it.
//
// Frontlight state (flBrightPct/flWarmPct + the RTC mirror) stays in
// main.cpp — it's general sleep/wake state (the lamp must resume exactly as
// the user left it across a deep sleep), not owned by this one screen.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "screen_settings.h"

// main.cpp owns flBrightPct/flWarmPct/applyBrightness/applyWarmth (general
// sleep/wake state, not this screen's) and defines them — along with these
// two step functions — BEFORE #include-ing this header. The two globals are
// visible below by plain textual order; ctlStepBrightness/ctlStepWarmth are
// referenced as function pointers ahead of their definition, so they need an
// explicit (file-scope, matching where main.cpp actually defines them —
// NOT inside namespace screen_shade, or lookup would never fall back to the
// real ones) forward declaration.
static void ctlStepBrightness(int d);
static void ctlStepWarmth(int d);

namespace screen_shade {

inline bool open = false;
inline bool dirty = false;        // a control changed; redraw the panel once
inline bool dragging = false;     // a finger is on a slider capsule
// Set by tapDispatch() to the +/- button that was hit (0..3), so the loop can
// draw it pressed for one partial refresh; -1 otherwise.
inline int pressed = -1;

inline constexpr int16_t kSheetH   = 430;
inline constexpr int16_t kSheetTop = Ui::H - kSheetH;
inline constexpr int16_t kCtlBtn   = 64;                         // [-] / [+] squares
inline constexpr int16_t kBriRowY  = kSheetTop + 100;            // top of the button row
inline constexpr int16_t kWarmRowY = kSheetTop + 214;
inline constexpr int16_t kTilesY   = kSheetTop + 312;
inline constexpr int16_t kTileH    = 92;
inline constexpr int16_t kTileGap  = 16;
inline constexpr int16_t kBarX0    = kShPad + kCtlBtn + 16;
inline constexpr int16_t kBarX1    = Ui::W - kShPad - kCtlBtn - 16;
inline constexpr int     kCtlLevels = 20;                        // pips per bar
inline constexpr int     kBtnStep  = 100 / kCtlLevels;           // % per press (5)

inline void drawStepBtn(int16_t x, int16_t y, bool plus, bool btnPressed = false) {
  const Color bg = btnPressed ? Color::White : Color::Black;
  const Color fg = btnPressed ? Color::Black : Color::White;
  if (btnPressed) ui.strokeRect(x, y, kCtlBtn, kCtlBtn, 3, 14);
  else            ui.fillRect(x, y, kCtlBtn, kCtlBtn, bg, 14);
  const int16_t cx = static_cast<int16_t>(x + kCtlBtn / 2), cy = static_cast<int16_t>(y + kCtlBtn / 2);
  ui.fillRect(static_cast<int16_t>(cx - 16), static_cast<int16_t>(cy - 3), 32, 6, fg, 3);
  if (plus)
    ui.fillRect(static_cast<int16_t>(cx - 3), static_cast<int16_t>(cy - 16), 6, 32, fg, 3);
}

// "Label ............. 50%" then  [-]  [pip pip pip . . .]  [+]
// A SEGMENTED bar, not a continuous fill: adjacent levels differ by exactly one
// pip, so a partial refresh only has to flip that one small block — no
// black->white-in-place churn, which is what ghosts on the DU/fast waveform.
inline void drawCtlRow(int16_t y, const char* label, int valPct, int rowPressed = -1) {
  const int16_t lblY = static_cast<int16_t>(y - 28);
  ui.text(label, kShPad, lblY, 220, 22, TextAlign::Left, Color::Black, 1, Ui::kFontSmall);
  char v[8];
  snprintf(v, sizeof(v), "%d%%", valPct);
  ui.text(v, kShPad, lblY, static_cast<int16_t>(Ui::W - kShPad * 2), 22, TextAlign::Right,
          Color::Black, 1, Ui::kFontSmall);

  drawStepBtn(kShPad, y, /*plus=*/false, rowPressed == 0);
  drawStepBtn(static_cast<int16_t>(Ui::W - kShPad - kCtlBtn), y, /*plus=*/true, rowPressed == 1);

  const int16_t barW = static_cast<int16_t>(kBarX1 - kBarX0);
  const int16_t barH = 22;
  const int16_t barY = static_cast<int16_t>(y + (kCtlBtn - barH) / 2);
  const int lit = (valPct * kCtlLevels + 50) / 100;   // pips to fill
  const int16_t gap = 3;
  const int16_t pw = static_cast<int16_t>((barW - gap * (kCtlLevels - 1)) / kCtlLevels);
  for (int i = 0; i < kCtlLevels; ++i) {
    const int16_t px = static_cast<int16_t>(kBarX0 + i * (pw + gap));
    if (i < lit)
      ui.fillRect(px, barY, pw, barH, Color::Black, 2);
    else
      ui.fillRect(px, static_cast<int16_t>(barY + barH - 3), pw, 3, Color::Black);  // empty: stub
  }
}

inline void drawTile(int16_t x, int16_t w, const freeink::Icon& ic, const char* label) {
  ui.fillRect(x, kTilesY, w, kTileH, Color::Black, 14);
  ui.icon(ic, static_cast<int16_t>(x + (w - ic.w) / 2), static_cast<int16_t>(kTilesY + 16),
          Color::White);
  ui.text(label, x, static_cast<int16_t>(kTilesY + kTileH - 28), w, 20, TextAlign::Center,
          Color::White, 1, Ui::kFontSmall);
}

inline int16_t tileW() { return static_cast<int16_t>((Ui::W - kShPad * 2 - kTileGap) / 2); }

// `pressedBtn`: -1 none, 0/1 = Backlight [-]/[+], 2/3 = Warmth [-]/[+] —
// inverts that button for tap feedback in the same partial refresh as the new
// level. `r` defaults to Fast for that ordinary control-feedback redraw
// (stepper/slider changes, main.cpp's dirty-flag repaint loop); opening the
// sheet is a sub-screen push, so main.cpp's Home-long-press handler passes
// Full there instead.
inline void draw(int pressedBtn = -1, Rf r = Rf::Fast) {
  ui.fillRect(0, kSheetTop, Ui::W, kSheetH, Color::White);
  ui.fillRect(0, kSheetTop, Ui::W, 3, Color::Black);                              // top edge rule
  ui.fillRect(static_cast<int16_t>(Ui::W / 2 - 26), static_cast<int16_t>(kSheetTop + 12), 52, 5,
              Color::Black, 2);                                                   // grabber
  ui.text("Controls", kShPad, static_cast<int16_t>(kSheetTop + 28), 200, 24, TextAlign::Left);

  if (frontlight.present()) {
    drawCtlRow(kBriRowY, "Backlight", flBrightPct, pressedBtn <= 1 ? pressedBtn : -1);
    if (frontlight.hasColorTemperature())
      drawCtlRow(kWarmRowY,
                 flWarmPct <= 20 ? "Warmth  (cool)" : flWarmPct >= 80 ? "Warmth  (warm)" : "Warmth",
                 flWarmPct, pressedBtn >= 2 ? pressedBtn - 2 : -1);
  } else {
    ui.text("(no backlight on this board)", kShPad, kBriRowY,
            static_cast<int16_t>(Ui::W - kShPad * 2), 22, TextAlign::Left, Color::DarkGray, 1,
            Ui::kFontSmall);
  }

  const int16_t tw = tileW();
  drawTile(kShPad, tw, kWx_ui_refresh, "Full refresh");
  drawTile(static_cast<int16_t>(kShPad + tw + kTileGap), tw, kWx_ui_cog, "Settings");

  commitFrame(r);
}

inline void close() {
  open = false;
  dragging = false;
  drawStandby(/*sleeping=*/false, Rf::Full, -1);  // sub-screen pop — clears the overlay's ghost
}

// Finger held on a bar: set the value from x.
inline void sliderDrag(int16_t px, int16_t py) {
  if (!frontlight.present()) return;
  if (px < kBarX0 - 12 || px > kBarX1 + 12) return;
  const auto setFrom = [&](uint8_t& val, void (*apply)()) {
    int pct = static_cast<int>(static_cast<int32_t>(px - kBarX0) * 100 / (kBarX1 - kBarX0));
    pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (static_cast<uint8_t>(pct) != val) {
      val = static_cast<uint8_t>(pct);
      apply();
      dirty = true;
    }
    dragging = true;
  };
  if (py >= kBriRowY && py < kBriRowY + kCtlBtn) {
    setFrom(flBrightPct, applyBrightness);
  } else if (frontlight.hasColorTemperature() && py >= kWarmRowY && py < kWarmRowY + kCtlBtn) {
    setFrom(flWarmPct, applyWarmth);
  }
}

// Act on a tap at logical (px,py). Returns false only when the tap fell on the
// strip above the sheet (-> close); a tap anywhere inside the sheet is swallowed.
inline bool tapDispatch(int16_t px, int16_t py) {
  if (py < kSheetTop) return false;

  const auto rowHit = [&](int16_t y, void (*step)(int), int base) -> bool {
    if (py < y || py >= y + kCtlBtn) return false;
    if (px >= kShPad && px < kShPad + kCtlBtn) { step(-kBtnStep); pressed = base; return true; }
    const int16_t plusX = static_cast<int16_t>(Ui::W - kShPad - kCtlBtn);
    if (px >= plusX && px < plusX + kCtlBtn) { step(+kBtnStep); pressed = base + 1; return true; }
    if (px >= kBarX0 - 8 && px <= kBarX1 + 8) { sliderDrag(px, py); return true; }
    return true;
  };

  if (frontlight.present()) {
    if (rowHit(kBriRowY, ctlStepBrightness, 0)) return true;
    if (frontlight.hasColorTemperature() && rowHit(kWarmRowY, ctlStepWarmth, 2)) return true;
  }

  if (py >= kTilesY && py < kTilesY + kTileH) {
    const int16_t tw = tileW();
    if (px >= kShPad && px < kShPad + tw) {  // Full refresh: manual ghost-purge scrub
      open = false;
      // Half, not Full: this is the deliberate manual ghost-cleanup control,
      // the same DTM1-inverse-seed scrub commitFrame()'s periodic kCleanEvery
      // runs on its own — not a from-white flash (that's what a screen switch
      // gets). The tile's label describes the visible effect (a full clean of
      // every ghost), not the underlying driver mode.
      drawStandby(/*sleeping=*/false, Rf::Clean, -1);
      return true;
    }
    if (px >= kShPad + tw + kTileGap && px < kShPad + tw + kTileGap + tw) {  // Settings
      open = false;
      screen_settings::enter();
      return true;
    }
  }
  return true;  // inside the sheet, hit nothing — swallow (don't close)
}

// Route a logical tap: a tap on the strip above the sheet closes it.
inline void handleTap(int16_t px, int16_t py) {
  if (!tapDispatch(px, py)) close();
}

}  // namespace screen_shade
