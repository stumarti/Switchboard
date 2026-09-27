#pragma once

// ===========================================================================
// screen_splash — the first-run splash: Switchboard logo + name + slogan.
// Drawn once, straight to a FULL refresh, before Wi-Fi provisioning runs.
// Only a device that isn't set up yet (no saved Wi-Fi, not paired, or no
// cache after a power loss) ever shows it — every other boot and wake goes
// straight to the cached carousel (app/boot.h).
// ===========================================================================

#include "screen_common.h"
#include "config.h"

namespace screen_splash {

inline void draw() {
  ui.clear();
  // Hero logo, upper-middle of the tall portrait canvas.
  ui.iconCentered(icons::get("logoremote"), 236);
  // Wordmark.
  ui.centered(SWITCHBOARD_NAME, 392, 40);
  // Rule under the wordmark.
  ui.hline(Ui::W / 2 - 120, 444, 240, 2);
  // Slogan.
  ui.centered(SWITCHBOARD_SLOGAN, 456, 34);
  // Footer hint.
  ui.centered("starting up", 560, 26, Color::DarkGray);
  ui.flushFull();
}

// Hold the splash for up to `ms`, or until any key/tap skips it. Polls the
// InputManager directly — the input task isn't running yet this early.
inline void holdFor(uint32_t ms) {
  const uint32_t start = millis();
  float nx, ny;
  while (millis() - start < ms) {
    input.update();
    if (input.wasAnyPressed() || (input.hasTouch() && input.wasTouchTap(nx, ny))) break;
    delay(20);
  }
}

// First-run setup (Wi-Fi or pairing) sat idle long enough that the device is
// going to sleep rather than wait on it all night. Any button wakes it and
// setup picks up where it left off (saved Wi-Fi is reused, pairing re-polls).
inline void drawPaused(const char* why) {
  ui.clear();
  ui.iconCentered(icons::get("logoremote"), 236);
  ui.centered(SWITCHBOARD_NAME, 392, 40);
  ui.hline(Ui::W / 2 - 120, 444, 240, 2);
  ui.centered("Setup paused", 470, 30);
  ui.centered(why, 512, 26, Color::DarkGray);
  ui.centered("press any button to continue", 560, 26, Color::DarkGray);
  ui.flushFull();
}

}  // namespace screen_splash
