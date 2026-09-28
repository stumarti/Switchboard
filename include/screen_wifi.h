#pragma once

// ===========================================================================
// screen_wifi — runs the on-device Wi-Fi provisioning flow (scan / pick /
// keyboard, or a silent reuse of saved credentials) once, on a first-run
// boot (app/boot.h). No live "screen" of its own beyond what
// wifi_provision.h already draws; this is just the thin wrapper the boot
// sequence calls, plus the one-line result the Debug screen shows.
// ===========================================================================

#include "screen_common.h"
#include "config.h"
#include "wifi_provision.h"

namespace screen_wifi {

// Filled by run(), read by screen_debug.
inline char result[48] = "";

// 1. Try the ESP32's saved credentials silently.
// 2. On failure, scan for APs, show a selectable list, collect the password on
//    an on-screen keyboard, and connect. ESP32 persists the new creds so a
//    later boot goes straight through step 1.
//
// Returns true once connected; false if a provisioning screen sat idle past
// `idleTimeoutMs` (0 = wait forever) — the boot path then sleeps.
inline bool run(uint32_t idleTimeoutMs = 0) {
  const bool ok = wifiprov::run(ui, input, /*savedTimeoutMs=*/8000, WIFI_JOIN_TIMEOUT_MS,
                                idleTimeoutMs);
  snprintf(result, sizeof(result), "%s", wifiprov::g_resultLine);
  // No confirmation screen — the carousel draws next.
  return ok;
}

}  // namespace screen_wifi
