#pragma once

// ===========================================================================
// Pairing client — this device's identity (MAC address) and the bearer token
// it gets once an admin approves it in the Switchboard-Server web UI (see
// that repo's lib/pairing.js / POST /api/pairing/register).
//
// Flow: on first boot (no token in NVS), screen_pairing.h calls registerOnce()
// — once, then again each time the user presses a button after approving
// it — until the server reports "approved". The device shows up as
// "pending" in the admin UI in the meantime. The token is then stored in NVS
// (Preferences, namespace "switchboard" - same as device_config_client.h's
// activeSlug) and sent as the bearer on every subsequent request via
// device_config_client.h / room_list_client.h / globals_client.h, exactly the
// way ha_client.h already sends the Home Assistant token.
//
// Self-heal: if a previously-approved device loses its token (e.g. a factory
// reset wipes NVS), registerOnce() re-registering with the same MAC gets a
// freshly-rotated token back immediately, with no admin action needed - the
// server already made the trust decision the first time this MAC was
// approved. See lib/pairing.js's register() on the server side.
// ===========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "http_json.h"

namespace pairing {

inline char mac[18] = "";
// Server issues a 32-byte token as 64 lowercase hex chars (crypto.randomBytes
// (32).toString('hex') - see Switchboard-Server's lib/pairing.js); sized with
// margin.
inline char token[80] = "";
inline bool paired = false;
// Filled by a successful register()/poll response when the admin has
// assigned this device a room - screen_pairing.h applies it via
// deviceconfig::applyServerAssignedSlug() once paired, so a fresh device
// boots straight into its assigned room without a manual Settings -> Select
// room step.
inline char assignedSlug[32] = "";
inline char status[64] = "";

inline void ensureMac() {
  if (mac[0]) return;
  // WiFi.macAddress() returns e.g. "AA:BB:CC:DD:EE:FF" - the server's
  // normalizeMac() lowercases it, so casing here doesn't matter.
  snprintf(mac, sizeof(mac), "%s", WiFi.macAddress().c_str());
}

inline void loadFromNvs() {
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/true)) {
    paired = p.getBool("paired", false);
    const String t = p.getString("pairToken", "");
    if (t.length() > 0 && t.length() < sizeof(token)) {
      snprintf(token, sizeof(token), "%s", t.c_str());
    } else {
      paired = false;
    }
    p.end();
  }
}

inline void saveToNvs() {
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/false)) {
    p.putString("pairToken", token);
    p.putBool("paired", paired);
    p.end();
  }
}

// Forget the token for real — only once the server has actually said this
// device is no longer approved (pending / revoked / deleted). The next boot
// shows the pairing screen again.
inline void clear() {
  token[0] = 0;
  paired = false;
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/false)) {
    p.remove("pairToken");
    p.putBool("paired", false);
    p.end();
  }
}

// A 401 on an authenticated request (config/globals/room-list fetch) means
// the server no longer accepts this token — but NOT necessarily that this
// device was un-approved: the token may simply have been rotated, or the
// server's device list rewritten. Wiping the token on the spot (as this used
// to) sent an approved remote back to "waiting for approval" on its next
// boot. Instead, just flag it; the next refresh calls reauthorize(), which
// re-registers by MAC and — for a MAC the server still has approved — gets
// a fresh token straight back, with no admin action and no pairing screen.
inline volatile bool g_unauthorized = false;
inline void noteUnauthorized() { g_unauthorized = true; }
inline bool takeUnauthorized() {
  const bool u = g_unauthorized;
  g_unauthorized = false;
  return u;
}

// Whether the last registerOnce() reached the server and got an answer
// (approved or not) — false after a network/HTTP failure.
inline bool serverAnswered = false;

// One registration/poll attempt - non-blocking beyond the HTTP call itself.
// screen_pairing.h calls this in a loop with a delay between attempts.
// Returns true once the server reports "approved" (token + assignedSlug are
// then valid); false while still "pending" or on a network failure (status[]
// explains which).
inline bool registerOnce() {
  ensureMac();
  serverAnswered = false;

  char body[48];
  snprintf(body, sizeof(body), "{\"mac\":\"%s\"}", mac);

  JsonDocument doc;
  if (!httpjson::post(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, "/api/pairing/register",
                      /*bearer=*/nullptr, body, doc, status, sizeof(status))) {
    return false;
  }

  const char* st = doc["status"] | "pending";
  if (strcmp(st, "approved") != 0) {
    snprintf(status, sizeof(status), "%s", st);
    serverAnswered = true;
    return false;
  }
  serverAnswered = true;

  const char* tok = doc["token"] | "";
  if (*tok) {
    snprintf(token, sizeof(token), "%s", tok);
    paired = true;
    saveToNvs();
  }
  snprintf(assignedSlug, sizeof(assignedSlug), "%s", doc["slug"] | "");
  return true;
}

// After a 401: re-register by MAC. True if the server still has this device
// approved (a fresh token is now saved). If the server answered but no
// longer approves it, the token is forgotten for real (clear()); if it
// couldn't be reached, nothing changes and the next refresh tries again.
inline bool reauthorize() {
  if (registerOnce()) return true;
  if (serverAnswered) clear();
  return false;
}

}  // namespace pairing
