#pragma once

// ---------------------------------------------------------------------------
// Switchboard boot sequence — user configuration
// ---------------------------------------------------------------------------
// Everything you'd normally tweak lives here so you don't have to touch the
// state machine in main.cpp.

// ---- Version ----------------------------------------------------------
// Set by tools/gen_version.py at build time from `git describe` (e.g.
// "v0.3.0", or "v0.3.0-4-gabc1234-dirty" between tags) — see that script's
// header comment. This fallback only fires when building outside PlatformIO
// (e.g. clangd/IDE parsing), where the -D flag from the script isn't
// present, so the editor doesn't flag it as undefined.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// ---- Wi-Fi ----------------------------------------------------------------
// Set these before flashing. Leave WIFI_SSID empty ("") to make the Wi-Fi
// stage a visible no-op (it renders the screen, waits briefly, then moves on)
// so you can watch the whole sequence on a bench without a network.
#define WIFI_SSID ""
#define WIFI_PASS ""

// How long to keep trying the join before the Wi-Fi screen reports failure
// and the sequence advances anyway (ms).
#define WIFI_JOIN_TIMEOUT_MS 15000

// ---- Brand ----------------------------------------------------------------
#define SWITCHBOARD_NAME    "SWITCHBOARD"
#define SWITCHBOARD_SLOGAN  "One tap. Every room."

// ---- Server -----------------------------------------------------------
// The Switchboard server component, resolved via mDNS once Wi-Fi is up. Host
// is the mDNS name with no ".local" suffix and no scheme — plain lwip DNS
// can't resolve ".local" on this core, so it's looked up explicitly with
// MDNS.queryHost() instead of being passed straight to HTTPClient.
//
//   /api/globals                 shared Home Assistant host / port / token
//   /api/devices/<slug>/config   this device's Standby screen settings
//                                (which weather + climate entities to show)
#define SWITCHBOARD_SERVER_HOST "switchboard"
#define SWITCHBOARD_SERVER_PORT 45678
#define SWITCHBOARD_GLOBALS_PATH "/api/globals"

// This device's slug as the server knows it (see /api/devices).
#define SWITCHBOARD_DEVICE_SLUG "office"
#define SWITCHBOARD_DEVICE_CONFIG_PATH "/api/devices/" SWITCHBOARD_DEVICE_SLUG "/config"
