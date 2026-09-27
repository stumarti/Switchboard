#pragma once

// ===========================================================================
// wifi_link — the station link, managed as a small non-blocking state machine
// so no boot or wake path ever waits on the join with the UI frozen:
//
//   Off ──beginJoin()──> Joining ──(associated)──> Up
//                           │                       │ (link lost)
//                           └──(timeout)──> Failed  └──> Joining
//
// Timing out powers the radio down (it would otherwise keep retrying in the
// background and burn battery). loop() calls poll() every tick; the carousel
// reads isUp() / takeJoinFailed() to decide when to kick a refresh or clear
// its status-bar "updating" glyph.
//
// joinBlocking() is the one blocking variant, for the unattended timer wake
// where there's no UI to keep responsive.
// ===========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>

#include "config.h"

namespace wifilink {

enum class State : uint8_t { Off, Joining, Up, Failed };

inline State state = State::Off;
inline uint32_t g_joinStartMs = 0;
inline uint32_t g_joinTimeoutMs = WIFI_JOIN_TIMEOUT_MS;
inline bool g_joinFailed = false;  // one-shot, read by takeJoinFailed()

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

// Start joining the saved network in the background. Returns immediately.
inline void beginJoin(uint32_t timeoutMs = WIFI_JOIN_TIMEOUT_MS) {
  if (isUp()) {
    state = State::Up;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // reuse the SSID/pass persisted in NVS
  g_joinStartMs = millis();
  g_joinTimeoutMs = timeoutMs;
  g_joinFailed = false;
  state = State::Joining;
}

// Join unless already up or mid-join — e.g. on returning to the carousel
// after an earlier attempt gave up (the user is back, so try again).
inline void ensureStarted() {
  if (state == State::Off || state == State::Failed) beginJoin();
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
}

// Advance the state machine. Call every loop() tick.
inline void poll() {
  switch (state) {
    case State::Joining:
      if (isUp()) {
        state = State::Up;
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
