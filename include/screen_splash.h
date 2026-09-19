#pragma once

// ===========================================================================
// screen_splash — the boot splash: Switchboard logo + name + slogan. Drawn
// once, straight to a FULL refresh, before Wi-Fi provisioning runs.
// ===========================================================================

#include "screen_common.h"
#include "config.h"

namespace screen_splash {

inline void draw() {
  ui.clear();
  // Hero logo, upper-middle of the tall portrait canvas.
  ui.iconCentered(kLogoRemote, 236);
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

}  // namespace screen_splash
