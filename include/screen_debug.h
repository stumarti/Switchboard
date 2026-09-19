#pragma once

// ===========================================================================
// screen_debug — the interactive hardware self-test (hold Home from the
// carousel, or the jump list's "Self-test" row).
//
// Verifies every input surface + the backlight on real hardware:
//   - the four buttons: Left, Right, Power, and the capacitive Home key
//     (each ticks off the first time it's pressed)
//   - the touchscreen: live coordinates, a crosshair at the last touch,
//     and a running contact count
//   - the backlight: a target that steps 0 -> 25 -> 50 -> 75 -> 100%
//
// On the X4 Pro only Left(GPIO0)/Right(GPIO7)/Power(GPIO3) are physical GPIO
// buttons; "Back"/"Confirm" don't exist as keys — the fourth button is the
// GT911 capacitive Home key below the panel. So the four tested buttons are
// Left / Right / Power / Home.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "screen_wifi.h"

namespace screen_debug {

inline bool btnSeen[4] = {false, false, false, false};  // Left, Right, Power, Home
enum { TB_LEFT = 0, TB_RIGHT = 1, TB_POWER = 2, TB_HOME = 3 };
inline uint16_t touchCount = 0;
inline int16_t lastTouchX = -1, lastTouchY = -1;
inline const uint8_t kBacklightSteps[] = {0, 25, 50, 75, 100};
inline uint8_t backlightIdx = 0;
// Warm/cool mix steps: 0 = fully cool, 50 = neutral, 100 = fully warm.
inline const uint8_t kWarmthSteps[] = {0, 50, 100};
inline uint8_t warmthIdx = 1;  // start neutral
// Count of partial (fast) refreshes since the last full refresh, so the test
// can show how much ghosting has accumulated. The full-refresh button resets it.
inline uint16_t partialsSinceFull = 0;

// Three on-screen buttons in a row near the bottom (portrait, 480 wide):
// step brightness, step warmth (warm/cool mix), and force a full refresh.
inline constexpr int16_t kBtnRowY = 700, kBtnRowH = 76;
inline constexpr int16_t kBlBtnX = 30,  kBlBtnW = 138;   // "brightness"
inline constexpr int16_t kWmBtnX = 176, kWmBtnW = 138;   // "warmth"
inline constexpr int16_t kFrBtnX = 322, kFrBtnW = 128;   // "full refresh"

inline void drawCheck(int16_t x, int16_t y, bool on) {
  ui.strokeRect(x, y, 26, 26, 2, 4);
  if (on) {
    // a filled tick
    ui.fillRect(x + 5, y + 5, 16, 16, Color::Black, 3);
  }
}

// Repaint the self-test. `full` forces a clean full refresh (the "Full
// refresh" button / ghost scrub); otherwise it uses a fast partial refresh,
// which is what keeps button/touch feedback snappy.
inline void draw(bool full = false) {
  ui.clear();
  char line[96];

  // --- header ------------------------------------------------------------
  ui.text("Hardware test", 30, 24, Ui::W - 60, 34, TextAlign::Left);
  ui.text("press each key, tap the panel, step the backlight",
          30, 60, Ui::W - 60, 40, TextAlign::Left, Color::DarkGray, 2);
  ui.hline(30, 104, Ui::W - 60, 2);

  // compact system info (wrapped over up to 3 lines in the narrow frame)
  snprintf(line, sizeof(line),
           "X4 Pro - ESP32-S3 @ %luMHz - %ux%u - PSRAM %luMB - wifi: %s",
           (unsigned long)getCpuFrequencyMhz(),
           ui.display().getDisplayWidth(), ui.display().getDisplayHeight(),
           (unsigned long)(ESP.getPsramSize() / (1024 * 1024)),
           screen_wifi::result[0] ? screen_wifi::result : "n/a");
  ui.text(line, 30, 114, Ui::W - 60, 60, TextAlign::Left, Color::DarkGray, 3);

  // --- Buttons -----------------------------------------------------------
  ui.text("Buttons", 30, 190, Ui::W - 60, 26, TextAlign::Left);
  const char* names[4] = {"Left  (GPIO0)", "Right (GPIO7)", "Power (GPIO3)", "Home  (GT911)"};
  int done = 0;
  for (int i = 0; i < 4; ++i) {
    const int16_t y = 226 + i * 44;
    drawCheck(38, y, btnSeen[i]);
    ui.text(names[i], 78, y + 1, 260, 26, TextAlign::Left,
            btnSeen[i] ? Color::Black : Color::DarkGray);
    if (btnSeen[i]) ui.text("OK", Ui::W - 90, y + 1, 60, 26, TextAlign::Right);
    done += btnSeen[i] ? 1 : 0;
  }
  snprintf(line, sizeof(line), "%d / 4 pressed", done);
  ui.text(line, 38, 410, 300, 24, TextAlign::Left, Color::DarkGray);

  // --- Touchscreen -------------------------------------------------------
  ui.text("Touchscreen", 30, 446, Ui::W - 60, 26, TextAlign::Left);
  if (!input.hasTouch()) {
    ui.text("GT911 not detected", 30, 480, Ui::W - 60, 24, TextAlign::Left);
  } else if (lastTouchX < 0) {
    ui.text("touch inside the box", 30, 480, Ui::W - 60, 24, TextAlign::Left, Color::DarkGray);
  } else {
    snprintf(line, sizeof(line), "x=%d  y=%d   (%u taps)", lastTouchX, lastTouchY, touchCount);
    ui.text(line, 30, 480, Ui::W - 60, 24, TextAlign::Left);
  }
  const int16_t tbx = 30, tby = 510, tbw = Ui::W - 60, tbh = 96;
  ui.strokeRect(tbx, tby, tbw, tbh, 2, 8);
  if (lastTouchX >= tbx && lastTouchX < tbx + tbw &&
      lastTouchY >= tby && lastTouchY < tby + tbh) {
    ui.hline(lastTouchX - 12, lastTouchY, 24, 2);
    ui.fillRect(lastTouchX, lastTouchY - 12, 2, 24, Color::Black);
  }

  // --- Backlight ---------------------------------------------------------
  ui.text("Backlight", 30, 612, 160, 24, TextAlign::Left);
  snprintf(line, sizeof(line), "%s%u%%",
           frontlight.present() ? "" : "(no driver) ", kBacklightSteps[backlightIdx]);
  ui.text(line, 160, 612, Ui::W - 190, 24, TextAlign::Left);
  ui.strokeRect(30, 638, Ui::W - 60, 16, 2, 4);
  ui.fillRect(32, 640, ((Ui::W - 64) * kBacklightSteps[backlightIdx]) / 100, 12, Color::Black, 3);

  // --- Warmth (warm/cool mix) -------------------------------------------
  ui.text("Warmth", 30, 662, 160, 24, TextAlign::Left);
  if (frontlight.present() && frontlight.hasColorTemperature()) {
    const uint8_t warm = kWarmthSteps[warmthIdx];
    snprintf(line, sizeof(line), "%s (%u%% warm)",
             warm == 0 ? "cool" : warm == 100 ? "warm" : "neutral", warm);
  } else {
    snprintf(line, sizeof(line), "(no warm/cool channel)");
  }
  ui.text(line, 160, 662, Ui::W - 190, 24, TextAlign::Left);
  // Bar with a center tick marking neutral; fill tracks the warm fraction.
  ui.strokeRect(30, 688, Ui::W - 60, 16, 2, 4);
  ui.fillRect(32, 690, ((Ui::W - 64) * kWarmthSteps[warmthIdx]) / 100, 12, Color::Black, 3);
  ui.fillRect(30 + (Ui::W - 60) / 2, 686, 2, 20, Color::DarkGray);  // neutral marker

  // --- refresh status ----------------------------------------------------
  snprintf(line, sizeof(line), "refresh: %s   (%u partials since full)",
           full ? "FULL" : "partial", partialsSinceFull);
  ui.text(line, 30, 712, Ui::W - 60, 18, TextAlign::Left, Color::DarkGray);

  // --- three buttons: brightness | warmth | full refresh -----------------
  ui.fillRect(kBlBtnX, kBtnRowY, kBlBtnW, kBtnRowH, Color::Black, 12);
  ui.text("Step", kBlBtnX + 8, kBtnRowY + 16, kBlBtnW - 16, 24,
          TextAlign::Center, Color::White);
  ui.text("brightness", kBlBtnX + 8, kBtnRowY + 42, kBlBtnW - 16, 22,
          TextAlign::Center, Color::LightGray);

  ui.fillRect(kWmBtnX, kBtnRowY, kWmBtnW, kBtnRowH, Color::Black, 12);
  ui.text("Step", kWmBtnX + 8, kBtnRowY + 16, kWmBtnW - 16, 24,
          TextAlign::Center, Color::White);
  ui.text("warmth", kWmBtnX + 8, kBtnRowY + 42, kWmBtnW - 16, 22,
          TextAlign::Center, Color::LightGray);

  ui.strokeRect(kFrBtnX, kBtnRowY, kFrBtnW, kBtnRowH, 3, 12);
  ui.text("Full", kFrBtnX + 8, kBtnRowY + 16, kFrBtnW - 16, 24, TextAlign::Center);
  ui.text("refresh", kFrBtnX + 8, kBtnRowY + 42, kFrBtnW - 16, 22,
          TextAlign::Center, Color::DarkGray);

  // exit hint
  ui.text("hold Home to go back", 30, Ui::H - 20, Ui::W - 60, 16,
          TextAlign::Center, Color::DarkGray);

  if (full) {
    ui.flushFull();
    partialsSinceFull = 0;
  } else {
    ui.flushFast();
    partialsSinceFull++;
  }
}

// True if a logical tap fell inside the "step backlight" button.
inline bool tapBacklight(int16_t x, int16_t y) {
  return x >= kBlBtnX && x < kBlBtnX + kBlBtnW && y >= kBtnRowY && y < kBtnRowY + kBtnRowH;
}
inline bool tapWarmth(int16_t x, int16_t y) {
  return x >= kWmBtnX && x < kWmBtnX + kWmBtnW && y >= kBtnRowY && y < kBtnRowY + kBtnRowH;
}
inline bool tapFullRefresh(int16_t x, int16_t y) {
  return x >= kFrBtnX && x < kFrBtnX + kFrBtnW && y >= kBtnRowY && y < kBtnRowY + kBtnRowH;
}

inline void stepBacklight() {
  backlightIdx = (backlightIdx + 1) % (sizeof(kBacklightSteps) / sizeof(kBacklightSteps[0]));
  if (frontlight.present()) frontlight.setBrightness(kBacklightSteps[backlightIdx]);
  Serial.printf("[switchboard] backlight -> %u%%\n", kBacklightSteps[backlightIdx]);
}

inline void stepWarmth() {
  warmthIdx = (warmthIdx + 1) % (sizeof(kWarmthSteps) / sizeof(kWarmthSteps[0]));
  if (frontlight.present() && frontlight.hasColorTemperature())
    frontlight.setColorTemperature(kWarmthSteps[warmthIdx]);
  Serial.printf("[switchboard] warmth -> %u%% warm\n", kWarmthSteps[warmthIdx]);
}

// Enter the debug/self-test screen fresh.
inline void enter() {
  for (int i = 0; i < 4; ++i) btnSeen[i] = false;
  touchCount = 0;
  lastTouchX = lastTouchY = -1;
  backlightIdx = 0;
  warmthIdx = 1;  // neutral
  partialsSinceFull = 0;
  if (frontlight.present()) {
    frontlight.setBrightness(0);
    if (frontlight.hasColorTemperature())
      frontlight.setColorTemperature(kWarmthSteps[warmthIdx]);
  }
  stage = Stage::Debug;
  draw(/*full=*/true);  // clean full refresh on entry
}

}  // namespace screen_debug
