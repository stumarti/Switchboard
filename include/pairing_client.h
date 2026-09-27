#pragma once

// ===========================================================================
// Pairing client — this device's identity (MAC address) and the bearer token
// it gets once an admin approves it in the Switchboard-Server web UI (see
// that repo's lib/pairing.js / POST /api/pairing/register).
//
// Flow: on first boot (no token in NVS), screen_pairing.h calls registerOnce()
// in a loop until the server reports "approved" - the device shows up as
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

// A 401 on any authenticated request (config/globals/room-list fetch, or a
// theme pack download) means the token was revoked/deleted server-side -
// forget it so the next boot (or an immediate re-entry main.cpp triggers)
// shows the pairing screen again instead of looping on 401s forever.
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

// One registration/poll attempt - non-blocking beyond the HTTP call itself.
// screen_pairing.h calls this in a loop with a delay between attempts.
// Returns true once the server reports "approved" (token + assignedSlug are
// then valid); false while still "pending" or on a network failure (status[]
// explains which).
inline bool registerOnce() {
  ensureMac();

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
    return false;
  }

  const char* tok = doc["token"] | "";
  if (*tok) {
    snprintf(token, sizeof(token), "%s", tok);
    paired = true;
    saveToNvs();
  }
  snprintf(assignedSlug, sizeof(assignedSlug), "%s", doc["slug"] | "");
  return true;
}

}  // namespace pairing
