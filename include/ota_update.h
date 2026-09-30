#pragma once

// ===========================================================================
// ota_update — over-the-air firmware updates from the Switchboard server.
//
// The server decides (its Settings -> Remote updates page): which version
// remotes should run, the pilot remotes that get it first, whether a remote
// may update from its own Settings and on what schedule. This file only:
//
//   fetchOffer()   GET /api/firmware/offer — what to install now (204: none)
//   install()      GET /api/firmware/image/<version> into the spare app slot,
//                  hashing as it goes; switches to it only if the size and the
//                  SHA-256 match the offer. Needs fwMinBattery % battery.
//   confirm()      called once this boot has reached the server (the bundle
//                  came back): marks new firmware good, and reports how the
//                  last update went (POST /api/firmware/report)
//
// Rollback: the bootloader is built with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
// and src/main.cpp keeps the Arduino core from marking an update good by
// itself (verifyRollbackLater()). New firmware starts "pending"; if it resets
// — crashes, or sleeps — before confirm(), the bootloader goes back to the
// previous firmware. So a build that can't reach the server on its first
// run can't strand a remote.
// ===========================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "device_config_client.h"
#include "ha_client.h"
#include "http_json.h"
#include "ota_policy.h"
#include "pairing_client.h"

namespace ota {

struct Offer {
  char version[48] = "";
  uint32_t size = 0;
  char sha256[65] = "";
};

enum class Check : uint8_t { Offer, UpToDate, Failed };

// NVS: the install in progress (so the next boot can say how it went) and
// failed scheduled tries per version.
inline constexpr const char* kNs = "sb-ota";

inline void setStatus(char* status, size_t cap, const char* msg) { snprintf(status, cap, "%s", msg); }

// The server's current offer for this remote.
inline Check fetchOffer(Offer& out, char* status, size_t statusCap) {
  out = Offer{};
  if (WiFi.status() != WL_CONNECTED) {
    setStatus(status, statusCap, "Not connected to Wi-Fi");
    return Check::Failed;
  }
  const IPAddress ip = httpjson::resolveHost(SWITCHBOARD_SERVER_HOST);
  if (ip == IPAddress(0, 0, 0, 0)) {
    setStatus(status, statusCap, "Can't find the server");
    return Check::Failed;
  }
  char url[128];
  snprintf(url, sizeof(url), "http://%s:%u/api/firmware/offer", ip.toString().c_str(), SWITCHBOARD_SERVER_PORT);
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) {
    setStatus(status, statusCap, "Bad server address");
    return Check::Failed;
  }
  char auth[320];
  snprintf(auth, sizeof(auth), "Bearer %s", pairing::token);
  http.addHeader("Authorization", auth);
  http.addHeader("X-Firmware", FIRMWARE_VERSION);
  const int code = http.GET();
  if (code == 204) {
    http.end();
    return Check::UpToDate;
  }
  if (code != 200) {
    snprintf(status, statusCap, "Server answered HTTP %d", code);
    http.end();
    return Check::Failed;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  const char* ver = doc["version"] | "";
  const char* sha = doc["sha256"] | "";
  const uint32_t size = doc["size"] | 0u;
  if (err || !ver[0] || strlen(ver) >= sizeof(out.version) || strlen(sha) != 64 || size == 0) {
    setStatus(status, statusCap, "The server's offer was incomplete");
    return Check::Failed;
  }
  snprintf(out.version, sizeof(out.version), "%s", ver);
  snprintf(out.sha256, sizeof(out.sha256), "%s", sha);
  out.size = size;
  return Check::Offer;
}

inline void report(const char* version, const char* from, bool ok, const char* error) {
  JsonDocument body;
  body["version"] = version;
  body["from"] = from;
  body["ok"] = ok;
  body["error"] = error ? error : "";
  char buf[320];
  serializeJson(body, buf, sizeof(buf));
  JsonDocument resp;
  char status[48];
  httpjson::post(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, "/api/firmware/report", pairing::token, buf,
                 resp, status, sizeof(status));
}

inline uint8_t triesAt(const char* version) {
  Preferences p;
  if (!p.begin(kNs, true)) return 0;
  char v[48];
  const size_t n = p.getString("failVer", v, sizeof(v));
  const uint8_t tries = n && !strcmp(v, version) ? p.getUChar("failN", 0) : 0;
  p.end();
  return tries;
}
inline void noteFailure(const char* version) {
  const uint8_t tries = triesAt(version);
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putString("failVer", version);
  p.putUChar("failN", static_cast<uint8_t>(tries < 250 ? tries + 1 : tries));
  p.end();
}

using Progress = void (*)(uint8_t pct);

// Download, check and switch to `offer`. On success the next boot runs it
// (the caller restarts); on failure the running firmware is untouched and
// `status` says why. Reports a failure to the server itself.
inline bool install(const Offer& offer, uint8_t battPct, Progress progress, char* status, size_t statusCap) {
  auto fail = [&](const char* why) {
    setStatus(status, statusCap, why);
    report(offer.version, FIRMWARE_VERSION, false, why);
    noteFailure(offer.version);
    return false;
  };
  if (!otapolicy::batteryOk(battPct, deviceconfig::fwMinBattery)) {
    snprintf(status, statusCap, "Battery below %u%%: charge it first", deviceconfig::fwMinBattery);
    return false;  // not a failed install: nothing reported
  }
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next || offer.size > next->size) return fail("The firmware doesn't fit this remote");

  const IPAddress ip = httpjson::resolveHost(SWITCHBOARD_SERVER_HOST);
  if (ip == IPAddress(0, 0, 0, 0)) return fail("Can't find the server");
  char url[160];
  snprintf(url, sizeof(url), "http://%s:%u/api/firmware/image/%s", ip.toString().c_str(), SWITCHBOARD_SERVER_PORT,
           offer.version);
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(url)) return fail("Bad server address");
  char auth[320];
  snprintf(auth, sizeof(auth), "Bearer %s", pairing::token);
  http.addHeader("Authorization", auth);
  http.addHeader("X-Firmware", FIRMWARE_VERSION);
  const int code = http.GET();
  if (code != 200) {
    http.end();
    char why[48];
    snprintf(why, sizeof(why), "Download failed (HTTP %d)", code);
    return fail(why);
  }
  if (http.getSize() != static_cast<int>(offer.size)) {
    http.end();
    return fail("The download isn't the size the server announced");
  }
  if (!Update.begin(offer.size, U_FLASH)) {
    http.end();
    return fail(Update.errorString());
  }

  // Remember the attempt: the next boot reports how it went (confirm()).
  {
    Preferences p;
    if (p.begin(kNs, false)) {
      p.putString("pendVer", offer.version);
      p.putString("pendFrom", FIRMWARE_VERSION);
      p.end();
    }
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  WiFiClient* in = http.getStreamPtr();
  static uint8_t buf[4096];
  uint32_t done = 0;
  uint8_t lastPct = 255;
  uint32_t lastByteMs = millis();
  bool ok = true;
  const char* why = "";
  while (done < offer.size) {
    const size_t avail = in->available();
    if (!avail) {
      if (!http.connected() || millis() - lastByteMs > 15000) {
        ok = false;
        why = "The download stopped";
        break;
      }
      delay(2);
      continue;
    }
    const size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
    const size_t n = in->readBytes(buf, want < offer.size - done ? want : offer.size - done);
    if (!n) continue;
    lastByteMs = millis();
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != n) {
      ok = false;
      why = Update.errorString();
      break;
    }
    done += n;
    const uint8_t pct = static_cast<uint8_t>((static_cast<uint64_t>(done) * 100) / offer.size);
    if (progress && pct / 10 != lastPct / 10) progress(pct);
    lastPct = pct;
  }
  http.end();
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (ok) {
    char hex[65];
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (strcasecmp(hex, offer.sha256) != 0) {
      ok = false;
      why = "Checksum mismatch: the download was damaged";
    }
  }
  if (!ok) {
    Update.abort();
    Preferences p;
    if (p.begin(kNs, false)) {
      p.remove("pendVer");
      p.remove("pendFrom");
      p.end();
    }
    return fail(why);
  }
  if (!Update.end(/*evenIfRemaining=*/false)) return fail(Update.errorString());
  setStatus(status, statusCap, "");
  return true;  // the new firmware boots next, pending until confirm()
}

// This boot reached the server: mark the running firmware good (ending any
// rollback window) and report the update that led here, if any. Idempotent.
inline void confirm() {
  static bool done = false;
  if (done) return;
  done = true;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[ota] new firmware confirmed");
  }
  Preferences p;
  if (!p.begin(kNs, false)) return;
  char pend[48] = "", from[48] = "";
  p.getString("pendVer", pend, sizeof(pend));
  p.getString("pendFrom", from, sizeof(from));
  if (pend[0]) {
    p.remove("pendVer");
    p.remove("pendFrom");
  }
  p.end();
  if (!pend[0]) return;
  if (!strcmp(pend, FIRMWARE_VERSION)) {
    report(pend, from, true, "");
  } else {
    // Still (or again) on the old firmware: the new one never reached the
    // server on its first run, so the bootloader went back.
    char why[120];
    snprintf(why, sizeof(why), "Went back to %s: the new firmware didn't reach the server", FIRMWARE_VERSION);
    report(pend, from, false, why);
    noteFailure(pend);
  }
}

// The local hour now, or -1 without a clock this boot.
inline int localHour() {
  int64_t now = 0;
  if (!haclient::nowUtc(now)) return -1;
  int64_t local = now + static_cast<int64_t>(deviceconfig::utcOffsetMin) * 60;
  local %= 86400;
  if (local < 0) local += 86400;
  return static_cast<int>(local / 3600);
}

// "02:13", local time now; "" without a clock this boot.
inline void localTimeText(char* out, size_t cap) {
  out[0] = 0;
  int64_t now = 0;
  if (!haclient::nowUtc(now)) return;
  int64_t local = (now + static_cast<int64_t>(deviceconfig::utcOffsetMin) * 60) % 86400;
  if (local < 0) local += 86400;
  snprintf(out, cap, "%02d:%02d", static_cast<int>(local / 3600), static_cast<int>((local % 3600) / 60));
}

// A timer wake that should install the server's offer now.
inline bool scheduledDue(uint8_t battPct) {
  return otapolicy::scheduledDue(deviceconfig::fwEnabled && deviceconfig::fwFromHour >= 0, deviceconfig::fwOfferVersion,
                                 localHour(), deviceconfig::fwFromHour, deviceconfig::fwToHour, battPct,
                                 deviceconfig::fwMinBattery, triesAt(deviceconfig::fwOfferVersion));
}

}  // namespace ota
