#pragma once

// ===========================================================================
// Globals client — GETs the Switchboard server's /api/globals for the shared
// Home Assistant connection (host / port / long-lived token) and the shared
// list of Wi-Fi networks. The Standby screen talks to HA directly using the
// former; the Wi-Fi networks carousel page (screen_wifi_networks.h) shows a
// join QR code for each of the latter — unlike everything in
// device_config_client.h, these are the same across every room, not per-device.
// ===========================================================================

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "http_json.h"

namespace globalsclient {

// Home Assistant connection from the server's global config.
inline char haHost[64] = "";
inline uint16_t haPort = 8123;
inline char haToken[320] = "";

// wifiNetworks[] — named Wi-Fi networks any room can show a join QR code for
// (Settings -> Wi-Fi networks), independent of the credentials this device
// itself is joined with.
struct WifiNetItem {
  char name[32] = "";
  char ssid[33] = "";
  char password[65] = "";
  bool open = false;
};
// Capped at 3 (Home/Guest/+1) — RTC slow memory (persist.h mirrors this so a
// wake can redraw without a network round trip) is only ~8 KB total on the
// ESP32-S3, already shared with every other per-room list.
inline constexpr int kMaxWifiNets = 3;
inline WifiNetItem wifiNets[kMaxWifiNets];
inline int wifiNetCount = 0;

inline bool ok = false;
inline char status[64] = "";

// Resolve SWITCHBOARD_SERVER_HOST, GET /api/globals, pull homeAssistant.* and
// wifiNetworks[].
inline bool fetch() {
  ok = false;
  haHost[0] = haToken[0] = 0;
  haPort = 8123;
  wifiNetCount = 0;

  JsonDocument doc;
  if (!httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, SWITCHBOARD_GLOBALS_PATH,
                     /*bearer=*/nullptr, doc, status, sizeof(status))) {
    Serial.printf("[globals] fetch FAILED: %s\n", status);
    return false;
  }

  // TEMP DEBUG — dump the raw /api/globals body so a device that shows blank
  // Wi-Fi network names can be diagnosed over serial. Remove once resolved.
  Serial.print("[globals] raw body: ");
  serializeJson(doc, Serial);
  Serial.println();

  JsonObjectConst ha = doc["homeAssistant"].as<JsonObjectConst>();
  snprintf(haHost, sizeof(haHost), "%s", ha["host"] | "");
  snprintf(haToken, sizeof(haToken), "%s", ha["token"] | "");
  haPort = ha["port"] | 8123;

  for (JsonObjectConst n : doc["wifiNetworks"].as<JsonArrayConst>()) {
    if (wifiNetCount >= kMaxWifiNets) break;
    // The server's entries carry {id, name, password} — no separate "ssid"
    // key, "name" doubles as the SSID (e.g. "Stu-Home"). Prefer an explicit
    // "ssid" if a future server version adds one, else fall back to "name".
    const char* name = n["name"] | "";
    const char* ssid = n["ssid"] | name;
    if (!*ssid) {
      Serial.println("[globals] wifiNetworks entry skipped: no name/ssid");
      continue;
    }
    WifiNetItem& w = wifiNets[wifiNetCount];
    snprintf(w.ssid, sizeof(w.ssid), "%s", ssid);
    snprintf(w.name, sizeof(w.name), "%s", *name ? name : ssid);
    snprintf(w.password, sizeof(w.password), "%s", n["password"] | "");
    w.open = w.password[0] == 0;
    Serial.printf("[globals] wifiNetworks[%d]: name=\"%s\" ssid=\"%s\" open=%d\n", wifiNetCount,
                  w.name, w.ssid, w.open);
    ++wifiNetCount;
  }
  Serial.printf("[globals] wifiNetCount=%d\n", wifiNetCount);

  if (!haHost[0] || !haToken[0]) {
    snprintf(status, sizeof(status), "globals: no HA host/token");
    Serial.printf("[globals] fetch FAILED: %s\n", status);
    return false;
  }

  ok = true;
  Serial.println("[globals] fetch OK");
  return true;
}

}  // namespace globalsclient
