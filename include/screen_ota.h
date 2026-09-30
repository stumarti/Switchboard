#pragma once

// ===========================================================================
// screen_ota — Settings -> Device info -> Check for update: asks the server
// (ota_update.h), offers the new version with INSTALL / NOT NOW, then shows
// the download's progress and restarts into it. Also draws the unattended
// scheduled update (app/boot.h's timer wake), with no buttons.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "ota_update.h"

namespace screen_ota {

enum class Phase : uint8_t { Checking, UpToDate, Offer, Installing, Failed };
inline Phase phase = Phase::Checking;
inline ota::Offer offer;
inline char message[96] = "";

inline constexpr int16_t kBtnY = 560;
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

inline void drawProgress(uint8_t pct) {
  constexpr int16_t x = 40, y = 420, w = Ui::W - 80, h = 36;
  ui.strokeRect(x, y, w, h, 2, 10);
  const int16_t fill = static_cast<int16_t>((w - 8) * pct / 100);
  if (fill > 0) ui.fillRect(static_cast<int16_t>(x + 4), static_cast<int16_t>(y + 4), fill, static_cast<int16_t>(h - 8), Color::Black, 8);
  char b[16];
  snprintf(b, sizeof(b), "%u%%", pct);
  ui.text(b, 0, static_cast<int16_t>(y + h + 16), Ui::W, 28, TextAlign::Center, Color::Black);
}

inline void draw(Rf r = Rf::Fast, uint8_t pct = 0, bool unattended = false) {
  ui.clear();
  drawStatusBar("Firmware update");
  char line[96];
  snprintf(line, sizeof(line), "This remote: %s", FIRMWARE_VERSION);
  ui.text(line, 0, 150, Ui::W, 28, TextAlign::Center, Color::DarkGray);
  switch (phase) {
    case Phase::Checking:
      ui.centered("Checking for an update...", 300, 32);
      break;
    case Phase::UpToDate:
      ui.centered("Up to date", 300, 40);
      ui.text("Nothing new on the server for this remote.", 0, 360, Ui::W, 28, TextAlign::Center, Color::DarkGray);
      drawButton(1, "BACK", false);
      break;
    case Phase::Offer:
      snprintf(line, sizeof(line), "%s is available", offer.version);
      ui.centered(line, 280, 40);
      snprintf(line, sizeof(line), "%.1f MB download, about a minute", offer.size / 1048576.0);
      ui.text(line, 0, 340, Ui::W, 28, TextAlign::Center, Color::DarkGray);
      ui.text("The remote restarts when it's done.", 0, 380, Ui::W, 28, TextAlign::Center, Color::DarkGray);
      drawButton(0, "INSTALL", true);
      drawButton(1, "NOT NOW", false);
      break;
    case Phase::Installing:
      snprintf(line, sizeof(line), "Installing %s", offer.version);
      ui.centered(line, 300, 40);
      ui.text(unattended ? "Scheduled update - don't unplug" : "Don't turn the remote off", 0, 350, Ui::W, 28,
              TextAlign::Center, Color::DarkGray);
      drawProgress(pct);
      break;
    case Phase::Failed:
      ui.centered("Update didn't install", 280, 40);
      ui.text(message, 20, 340, Ui::W - 40, 60, TextAlign::Center, Color::Black);
      ui.text("This remote is unchanged.", 0, 420, Ui::W, 28, TextAlign::Center, Color::DarkGray);
      drawButton(1, "BACK", false);
      break;
  }
  commitFrame(r);
}

inline void onProgress(uint8_t pct) { draw(Rf::Fast, pct); }
inline void onProgressUnattended(uint8_t pct) { draw(Rf::Fast, pct, /*unattended=*/true); }

// Install `offer` now. Restarts into it on success; otherwise shows why.
inline void install(bool unattended) {
  waitForBackgroundIdle();  // the network worker must not share the radio mid-download
  phase = Phase::Installing;
  draw(Rf::Clean, 0, unattended);
  pollBattery(/*force=*/true);
  if (ota::install(offer, g_battPct, unattended ? onProgressUnattended : onProgress, message, sizeof(message))) {
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

// A timer wake inside the update window (app/boot.h), nobody watching: the
// server's offer from the config just fetched, re-checked with the server.
inline void runScheduled() {
  char why[96];
  if (ota::fetchOffer(offer, why, sizeof(why)) != ota::Check::Offer) return;
  Serial.printf("[ota] scheduled update to %s\n", offer.version);
  install(/*unattended=*/true);
}

}  // namespace screen_ota
