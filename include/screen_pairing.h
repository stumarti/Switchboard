#pragma once

// ===========================================================================
// screen_pairing — blocking "waiting for approval" gate, run once at boot
// right after Wi-Fi connects (main.cpp), before anything else touches the
// server. Modeled directly on wifi_provision.h's join loop: sits here polling
// until satisfied, since there's nothing useful to show without a paired
// device's config. A warm boot that's already paired (NVS has a token) is a
// no-op fast-path - no network round trip, no screen shown.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"  // ensureMdns()
#include "pairing_client.h"
#include "device_config_client.h"

namespace screen_pairing {

inline void draw(const char* dots) {
  ui.clear();
  ui.centered("Switchboard", 300, 38);
  ui.centered("waiting for approval", 362, 26, Color::DarkGray);
  char macLine[40];
  snprintf(macLine, sizeof(macLine), "MAC %s", pairing::mac);
  ui.centered(macLine, 410, 24, Color::DarkGray);
  ui.centered(dots, 450, 22, Color::DarkGray);
  // Why the last attempt didn't get through — "can't resolve switchboard",
  // "HTTP -1", ... Blank while the server has the request and it's merely
  // pending, so this line only appears when the device isn't reaching it.
  if (pairing::status[0] && strcmp(pairing::status, "pending") != 0) {
    char why[80];
    snprintf(why, sizeof(why), "server: %s", pairing::status);
    ui.centered(why, 480, 22, Color::DarkGray);
    // This device's own address — the server's mDNS name only resolves from
    // the same subnet (or across a reflector), so seeing it is half the
    // diagnosis.
    char ipLine[40];
    snprintf(ipLine, sizeof(ipLine), "this device: %s", WiFi.localIP().toString().c_str());
    ui.centered(ipLine, 506, 22, Color::DarkGray);
  }
  ui.text("Approve this device from the admin UI's", 0, 540, Ui::W, 22, TextAlign::Center,
          Color::DarkGray, 1, Ui::kFontSmall);
  ui.text("Devices page - it continues on its own.", 0, 564, Ui::W, 22, TextAlign::Center,
          Color::DarkGray, 1, Ui::kFontSmall);
  ui.flushFull();
}

// Blocks until the server approves this device. Applies the server-assigned
// room (if the admin picked one at approval time, and this device hasn't had
// a room manually picked already - see deviceconfig::applyServerAssignedSlug)
// before returning.
inline void run() {
  pairing::loadFromNvs();
  if (pairing::paired) return;

  pairing::ensureMac();
  for (int tick = 0;; ++tick) {
    char dots[4] = {0};
    for (int i = 0; i < (tick % 4); ++i) dots[i] = '.';
    draw(dots);
    // SWITCHBOARD_SERVER_HOST is an mDNS name, and MDNS.queryHost() fails
    // outright until MDNS.begin() has run — without this every attempt died
    // on the device ("can't resolve switchboard") and never reached the
    // server, so the device never appeared as pending. Retried each tick
    // (a guarded no-op once up) in case Wi-Fi was still settling.
    ensureMdns();
    if (pairing::registerOnce()) break;
    Serial.printf("[pairing] register %s (ip %s): %s\n", pairing::mac,
                  WiFi.localIP().toString().c_str(), pairing::status);
    delay(4000);
  }

  if (pairing::assignedSlug[0]) {
    deviceconfig::applyServerAssignedSlug(pairing::assignedSlug);
  }
}

}  // namespace screen_pairing
