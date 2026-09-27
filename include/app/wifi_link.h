#pragma once

// ===========================================================================
// wifi_link — the station link, managed as a small non-blocking state machine
// so no boot or wake path ever waits on the join with the UI frozen:
//
//   Off ──beginJoin()──> Joining ──(associated)──> Up ──idleOff()──> IdleOff
//                           │                       │ (link lost)        │
//                           └──(timeout)──> Failed  └──> Joining <───────┘
//                                                         ensureStarted() (a press)
//
// Timing out powers the radio down (it would otherwise keep retrying in the
// background and burn battery). loop() calls poll() every tick, and turns
// the radio off (idleOff()) once nothing — the user or the network worker —
// has needed it for a while; the next command brings it straight back. The
// carousel reads isUp() / takeJoinFailed() to decide when to kick a refresh
// or clear its status-bar "updating" glyph.
//
// Fast reconnect: the access point (BSSID) and channel of the last good
// link are kept in RTC memory, so a rejoin — after a wake, or after an idle
// power-down — goes straight to that AP instead of scanning every channel
// first (the slow part of a join). If that doesn't associate within
// kFastJoinMs, it falls back to a normal scan-and-join.
//
// joinBlocking() is the one blocking variant, for the unattended timer wake
// where there's no UI to keep responsive.
// ===========================================================================

#include <Arduino.h>
#include <esp_attr.h>
#include <string.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>

#include "config.h"

namespace wifilink {

enum class State : uint8_t { Off, Joining, Up, Failed, IdleOff };

inline State state = State::Off;
inline uint32_t g_joinStartMs = 0;
inline uint32_t g_joinTimeoutMs = WIFI_JOIN_TIMEOUT_MS;
inline bool g_joinFailed = false;  // one-shot, read by takeJoinFailed()
// Rejoining after an idle power-down: the UI keeps showing the link as
// available meanwhile (it was only off to save power).
inline bool g_resumingFromIdle = false;

// --- fast reconnect memory (survives deep sleep) ---
struct FastJoin {
  uint32_t magic;
  uint8_t bssid[6];
  int32_t channel;
};
inline constexpr uint32_t kFastJoinMagic = 0x4653574Au;  // 'FSWJ'
RTC_DATA_ATTR inline FastJoin g_fast = {};
inline constexpr uint32_t kFastJoinMs = 3500;
inline bool g_fastAttempt = false;  // the current join is a direct-to-AP attempt

inline bool isUp() { return WiFi.status() == WL_CONNECTED; }

// mDNS resolves SWITCHBOARD_SERVER_HOST. begin() re-inits the responder every
// call, so guard it — one success per link is enough, and racing task calls
// must not tear it down under each other. off() drops the guard so the next
// link brings the responder back up.
inline volatile bool g_mdnsUp = false;
inline void ensureMdns() {
  if (g_mdnsUp || !isUp()) return;
  if (MDNS.begin("switchboard-remote")) g_mdnsUp = true;
}

// Whether the ESP32 has station credentials saved in NVS (a previous
// provisioning run stored them). Brings the station interface up to read
// the config, which every caller is about to do anyway.
inline bool hasSavedCredentials() {
  if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
  return conf.sta.ssid[0] != 0;
}

// Remember the AP we're associated with, for the next fast rejoin.
inline void rememberAp() {
  const uint8_t* b = WiFi.BSSID();
  if (!b) return;
  memcpy(g_fast.bssid, b, sizeof(g_fast.bssid));
  g_fast.channel = WiFi.channel();
  g_fast.magic = kFastJoinMagic;
}

// The saved station credentials (from the last provisioning).
inline bool savedCredentials(wifi_config_t& conf) {
  conf = {};
  return esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0];
}

// A normal scan-and-join of the saved network. Passes the SSID/password
// explicitly (rather than a bare WiFi.begin()) so it never inherits a
// BSSID/channel lock left in the config by a fast join.
inline void beginSavedJoin() {
  wifi_config_t conf;
  if (savedCredentials(conf))
    WiFi.begin(reinterpret_cast<const char*>(conf.sta.ssid),
               reinterpret_cast<const char*>(conf.sta.password));
  else
    WiFi.begin();
}

// Straight to the remembered AP + channel, with the saved credentials.
// False if there's nothing remembered (or no saved credentials).
inline bool beginFastJoin() {
  if (g_fast.magic != kFastJoinMagic || g_fast.channel <= 0) return false;
  wifi_config_t conf;
  if (!savedCredentials(conf)) return false;
  WiFi.begin(reinterpret_cast<const char*>(conf.sta.ssid),
             reinterpret_cast<const char*>(conf.sta.password), g_fast.channel, g_fast.bssid);
  return true;
}

// Start joining the saved network in the background. Returns immediately.
inline void beginJoin(uint32_t timeoutMs = WIFI_JOIN_TIMEOUT_MS) {
  if (isUp()) {
    state = State::Up;
    return;
  }
  WiFi.mode(WIFI_STA);
  g_fastAttempt = beginFastJoin();
  if (!g_fastAttempt) beginSavedJoin();
  g_joinStartMs = millis();
  g_joinTimeoutMs = timeoutMs;
  g_joinFailed = false;
  state = State::Joining;
}

// Join unless already up or mid-join — e.g. on returning to the carousel
// after an earlier attempt gave up (the user is back, so try again).
inline void ensureStarted() {
  if (state == State::IdleOff) g_resumingFromIdle = true;
  if (state == State::Off || state == State::Failed || state == State::IdleOff) beginJoin();
}

// Radio off. Also what a join timeout does, so a missing network doesn't
// keep the modem retrying in the background.
inline void off() {
  if (g_mdnsUp) {
    MDNS.end();
    g_mdnsUp = false;
  }
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  state = State::Off;
  g_resumingFromIdle = false;
}

// Radio off to save power while nothing needs it; the next ensureStarted()
// (any command) rejoins, fast, and the UI never shows the link as lost.
inline void idleOff() {
  off();
  state = State::IdleOff;
}

// What the status bar's Wi-Fi glyph should say: connected, or only off to
// save power (and coming straight back when needed).
inline bool showsConnected() {
  return isUp() || state == State::IdleOff || (state == State::Joining && g_resumingFromIdle);
}

// Advance the state machine. Call every loop() tick.
inline void poll() {
  switch (state) {
    case State::Joining:
      if (isUp()) {
        state = State::Up;
        g_resumingFromIdle = false;
        rememberAp();
      } else if (g_fastAttempt && millis() - g_joinStartMs > kFastJoinMs) {
        // The remembered AP/channel didn't answer (moved, or a different
        // AP of the same network is closer now): forget it and do a normal
        // scan-and-join, within the same overall timeout.
        g_fastAttempt = false;
        g_fast.magic = 0;
        WiFi.disconnect(false);
        beginSavedJoin();
      } else if (millis() - g_joinStartMs > g_joinTimeoutMs) {
        off();
        state = State::Failed;
        g_joinFailed = true;
      }
      break;
    case State::Up:
      // The driver auto-reconnects on its own; give it a fresh join window
      // (and the same give-up-and-power-down) rather than spinning forever.
      if (!isUp()) {
        g_joinStartMs = millis();
        state = State::Joining;
      }
      break;
    case State::Off:
    case State::Failed:
    case State::IdleOff:
      break;
  }
}

// True once per failed join — the carousel drops its "updating" glyph.
inline bool takeJoinFailed() {
  const bool f = g_joinFailed;
  g_joinFailed = false;
  return f;
}

// Blocking join, for the unattended timer wake only (nobody to keep the UI
// responsive for). Powers the radio back down on failure.
inline bool joinBlocking(uint32_t timeoutMs) {
  beginJoin(timeoutMs);
  while (state == State::Joining) {
    poll();
    delay(50);
  }
  return state == State::Up;
}

}  // namespace wifilink
