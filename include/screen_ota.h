#pragma once

// ===========================================================================
// screen_ota — Settings -> Device info -> Check for update: asks the server
// (ota_update.h), offers the new version with INSTALL / NOT NOW, then the
// updating screen until the remote restarts into it.
//
// The updating screen: the old -> new version, a segmented progress bar,
// the percentage and the time left (estimated from the download so far),
// and when it started. A scheduled update (app/boot.h's timer wake) first
// counts down for kCountdownSec so anyone holding the remote can postpone
// it with any button.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "app/input.h"
#include "ota_update.h"

namespace screen_ota {

enum class Phase : uint8_t { Checking, UpToDate, Offer, Countdown, Installing, Restarting, Failed };
inline Phase phase = Phase::Checking;
inline ota::Offer offer;
inline char message[96] = "";

// The install in progress: for the percentage, the time left and "Started".
inline uint32_t g_startMs = 0;
inline char g_startedAt[8] = "";
inline uint8_t g_pct = 0;
inline uint8_t g_countdown = 0;
inline bool g_unattended = false;
inline constexpr uint8_t kCountdownSec = 10;

inline constexpr int16_t kBtnY = 600;
inline constexpr int16_t kBtnH = 84;
inline constexpr int16_t kBtnW = (Ui::W - 3 * kShPad) / 2;
inline int16_t btnX(int col) { return static_cast<int16_t>(kShPad + col * (kBtnW + kShPad)); }
inline int buttonHit(int16_t tx, int16_t ty) {
  if (ty < kBtnY || ty >= kBtnY + kBtnH) return -1;
  for (int c = 0; c < 2; ++c)
    if (tx >= btnX(c) && tx < btnX(c) + kBtnW) return c;
  return -1;
}

inline void drawButton(int col, const char* label, bool filled) {
  const int16_t x = btnX(col);
  if (filled) ui.fillRect(x, kBtnY, kBtnW, kBtnH, Color::Black, 18);
  else        ui.strokeRect(x, kBtnY, kBtnW, kBtnH, 2, 18);
  ui.text(label, x, static_cast<int16_t>(kBtnY + kBtnH / 2 - 16), kBtnW, 32, TextAlign::Center,
          filled ? Color::White : Color::Black, 1, Ui::kFont28);
}

inline void line(const char* s, int16_t y, Color c = Color::Black, fu::FontId f = 0, int16_t h = 30) {
  ui.text(s, 20, y, static_cast<int16_t>(Ui::W - 40), h, TextAlign::Center, c, 1, f);
}

// The big round badge at the top: the update glyph in a ring.
inline void drawBadge(int16_t cy, bool filled) {
  constexpr int16_t d = 132;
  const int16_t x = static_cast<int16_t>((Ui::W - d) / 2), y = static_cast<int16_t>(cy - d / 2);
  if (filled) ui.fillRect(x, y, d, d, Color::Black, d / 2);
  else        ui.strokeRect(x, y, d, d, 4, d / 2);
  constexpr int16_t ic = 72;
  ui.iconScaled(icons::get("wx_ui_refresh"), static_cast<int16_t>((Ui::W - ic) / 2), static_cast<int16_t>(cy - ic / 2),
                ic, ic, filled ? Color::White : Color::Black);
}

// "v0.1.8  ->  v0.2.0", the arrow drawn between the two versions.
inline void drawVersions(int16_t y) {
  const fu::Size a = ui.measure(FIRMWARE_VERSION, Ui::kFont28);
  const fu::Size b = ui.measure(offer.version, Ui::kFont28);
  constexpr int16_t gap = 60;
  int16_t x = static_cast<int16_t>((Ui::W - (a.width + gap + b.width)) / 2);
  ui.text(FIRMWARE_VERSION, x, y, a.width, 34, TextAlign::Left, Color::DarkGray, 1, Ui::kFont28);
  x = static_cast<int16_t>(x + a.width);
  const int16_t mid = static_cast<int16_t>(y + 17);
  ui.fillRect(static_cast<int16_t>(x + 14), static_cast<int16_t>(mid - 2), gap - 30, 4, Color::Black, 1);
  for (int i = 0; i < 9; ++i)  // arrow head
    ui.fillRect(static_cast<int16_t>(x + gap - 16 - i), static_cast<int16_t>(mid - i), 2, static_cast<int16_t>(2 * i + 1), Color::Black, 0);
  ui.text(offer.version, static_cast<int16_t>(x + gap), y, b.width, 34, TextAlign::Left, Color::Black, 1, Ui::kFont28);
}

// 20 blocks, filled as the download goes.
inline void drawBar(int16_t y, uint8_t pct) {
  constexpr int kSeg = 20;
  constexpr int16_t x0 = 40, h = 40, gap = 4;
  constexpr int16_t w = (Ui::W - 80 - (kSeg - 1) * gap) / kSeg;
  const int filled = (pct * kSeg + 50) / 100;
  for (int i = 0; i < kSeg; ++i) {
    const int16_t x = static_cast<int16_t>(x0 + i * (w + gap));
    if (i < filled) ui.fillRect(x, y, w, h, Color::Black, 4);
    else            ui.strokeRect(x, y, w, h, 2, 4);
  }
}

// "about 40 s left" once there's enough of the download to judge by.
inline void timeLeftText(char* out, size_t cap) {
  out[0] = 0;
  if (g_pct < 10 || g_pct >= 100) return;
  const uint32_t elapsed = millis() - g_startMs;
  const uint32_t left = static_cast<uint32_t>((static_cast<uint64_t>(elapsed) * (100 - g_pct)) / g_pct / 1000);
  if (left < 5)        snprintf(out, cap, "almost done");
  else if (left < 60)  snprintf(out, cap, "about %u s left", static_cast<unsigned>((left + 4) / 5 * 5));
  else                 snprintf(out, cap, "about %u min left", static_cast<unsigned>((left + 30) / 60));
}

inline void draw(Rf r = Rf::Fast) {
  ui.clear();
  char b[96];
  switch (phase) {
    case Phase::Checking:
      drawStatusBar("Firmware update");
      drawBadge(290, false);
      line("Checking for an update...", 400, Color::Black, Ui::kFont28, 34);
      snprintf(b, sizeof(b), "This remote runs %s", FIRMWARE_VERSION);
      line(b, 450, Color::DarkGray);
      break;
    case Phase::UpToDate:
      drawStatusBar("Firmware update");
      drawBadge(290, false);
      line("Up to date", 400, Color::Black, Ui::kFont28, 34);
      snprintf(b, sizeof(b), "%s is the newest for this remote", FIRMWARE_VERSION);
      line(b, 450, Color::DarkGray);
      drawButton(1, "BACK", false);
      break;
    case Phase::Offer:
      drawStatusBar("Firmware update");
      drawBadge(250, false);
      line("An update is ready", 360, Color::Black, Ui::kFont28, 34);
      drawVersions(410);
      snprintf(b, sizeof(b), "%.1f MB - about a minute", offer.size / 1048576.0);
      line(b, 470, Color::DarkGray);
      line("The remote restarts when it's done.", 505, Color::DarkGray);
      drawButton(0, "INSTALL", true);
      drawButton(1, "NOT NOW", false);
      break;
    case Phase::Countdown:
      drawStatusBar("Scheduled update");
      drawBadge(250, false);
      snprintf(b, sizeof(b), "Updating in %u s", g_countdown);
      line(b, 360, Color::Black, Ui::kFont28, 34);
      drawVersions(410);
      line("Press any button to postpone it.", 480, Color::DarkGray);
      break;
    case Phase::Installing:
    case Phase::Restarting: {
      drawStatusBar(phase == Phase::Restarting ? "Restarting" : "Updating");
      drawBadge(230, true);
      line(phase == Phase::Restarting ? "Installed" : "Updating firmware", 330, Color::Black, Ui::kFont28, 34);
      drawVersions(380);
      drawBar(460, phase == Phase::Restarting ? 100 : g_pct);
      char left[32];
      timeLeftText(left, sizeof(left));
      if (phase == Phase::Restarting) snprintf(b, sizeof(b), "Restarting into %s", offer.version);
      else if (left[0])               snprintf(b, sizeof(b), "%u%%  -  %s", g_pct, left);
      else                            snprintf(b, sizeof(b), "%u%%", g_pct);
      line(b, 520, Color::Black, Ui::kFont28, 34);
      if (g_startedAt[0]) {
        snprintf(b, sizeof(b), "Started at %s", g_startedAt);
        line(b, 570, Color::DarkGray);
      }
      line("Don't switch the remote off.", 680, Color::DarkGray);
      line("It restarts by itself when it's done.", 712, Color::DarkGray);
      break;
    }
    case Phase::Failed:
      drawStatusBar("Firmware update");
      drawBadge(250, false);
      line("The update didn't install", 360, Color::Black, Ui::kFont28, 34);
      ui.text(message, 30, 410, static_cast<int16_t>(Ui::W - 60), 64, TextAlign::Center, Color::Black);
      snprintf(b, sizeof(b), "This remote still runs %s.", FIRMWARE_VERSION);
      line(b, 490, Color::DarkGray);
      drawButton(1, "BACK", false);
      break;
  }
  commitFrame(r);
}

inline void onProgress(uint8_t pct) {
  g_pct = pct;
  draw(Rf::Fast);
}

// Install `offer` now. Restarts into it on success; otherwise shows why
// (not when nobody's there to see it: a scheduled update just carries on
// to sleep).
inline void install(bool unattended) {
  waitForBackgroundIdle();  // the network worker must not share the radio mid-download
  g_unattended = unattended;
  g_pct = 0;
  g_startMs = millis();
  ota::localTimeText(g_startedAt, sizeof(g_startedAt));
  phase = Phase::Installing;
  draw(Rf::Clean);
  pollBattery(/*force=*/true);
  if (ota::install(offer, g_battPct, onProgress, message, sizeof(message))) {
    phase = Phase::Restarting;
    draw(Rf::Clean);
    delay(1500);
    restartDevice();  // boots the new firmware; it confirms itself once it reaches the server
  }
  phase = Phase::Failed;
  if (!unattended) draw(Rf::Clean);
}

// Settings -> Device info -> Check for update.
inline void enter() {
  stage = Stage::Ota;
  standbyIdleSinceMs = millis();
  phase = Phase::Checking;
  draw(Rf::Full);
  if (!wifilink::isUp()) {
    wifilink::ensureStarted();
    const uint32_t t0 = millis();
    while (!wifilink::isUp() && millis() - t0 < 15000) {
      wifilink::poll();
      delay(50);
    }
  }
  waitForBackgroundIdle();
  const ota::Check c = ota::fetchOffer(offer, message, sizeof(message));
  phase = c == ota::Check::Offer ? Phase::Offer : c == ota::Check::UpToDate ? Phase::UpToDate : Phase::Failed;
  standbyIdleSinceMs = millis();
  draw(Rf::Clean);
}

// Counts down kCountdownSec on screen; false if someone pressed a button
// (postponed: tried again on a later wake inside the window).
inline bool countdown() {
  phase = Phase::Countdown;
  for (int s = kCountdownSec; s > 0; --s) {
    g_countdown = static_cast<uint8_t>(s);
    if (s == kCountdownSec || s % 5 == 0 || s <= 3) draw(s == kCountdownSec ? Rf::Clean : Rf::Fast);
    const uint32_t t0 = millis();
    while (millis() - t0 < 1000) {
      if (readInputFrame().any()) return false;
      delay(20);
    }
  }
  return true;
}

// A timer wake inside the update window (app/boot.h): the offer re-checked
// with the server, a countdown, then the install.
inline void runScheduled() {
  char why[96];
  if (ota::fetchOffer(offer, why, sizeof(why)) != ota::Check::Offer) return;
  Serial.printf("[ota] scheduled update to %s\n", offer.version);
  if (!countdown()) {
    Serial.println("[ota] postponed by a button press");
    return;
  }
  install(/*unattended=*/true);
}

}  // namespace screen_ota
