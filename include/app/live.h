#pragma once

// ===========================================================================
// live — keeping an active page (Music, Xbox) current while it is on screen,
// without polling. Everything else on the remote is passive: it shows what
// the last refresh brought and waits for a press.
//
// The server labels each page "live" or "passive" in every state response
// (doc["live"]): a track playing, a game running. While the showing page is
// live — or its own state says it's playing, e.g. just after PLAY — the UI
// tick calls want(page) every frame, and this task holds one request open on
// the server:
//
//   GET /api/devices/<slug>/state?page=music&wait=20   If-None-Match: <etag>
//
// The server answers the moment that page's state changes (a new track, a
// pause, the volume) or with 304 after 20 s. So the radio does nothing but
// keep one idle socket open, and the screen repaints only on a real change.
//
// Battery comes first:
//   - only while the screen is awake and showing that page: want() must be
//     renewed every frame, so leaving the page, another stage or sleep
//     simply lets it lapse;
//   - only while Wi-Fi is already up: this never starts the radio and never
//     counts as network activity, so Settings -> Timeouts -> Wi-Fi timeout
//     still powers the radio down on schedule, and that ends the watch;
//   - an older server (no "live" in its answer) is never held open: the
//     pages fall back to their old 30 s re-read.
//
// The answer is parsed here, then handed to the network worker (app/net.h),
// which applies it with the same parsers as the refresh — the one task that
// writes page state — and fetches new art only when the track changed.
// ===========================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "device_config_client.h"
#include "globals_client.h"
#include "ha_client.h"
#include "http_json.h"
#include "pairing_client.h"
#include "screen_music.h"
#include "screen_xbox.h"
#include "app/net.h"
#include "app/wifi_link.h"

namespace live {

enum class Page : uint8_t { None, Music, Xbox };
// The server's label for a page; Unknown until a state response says.
enum class Label : uint8_t { Unknown, Passive, Live };

inline constexpr uint32_t kWaitSec = 20;         // what we ask the server to hold
inline constexpr uint32_t kReadTimeoutMs = 27000; // longer than the hold
inline constexpr uint32_t kBackoffMs = 10000;    // after a failed request
inline constexpr uint32_t kWantLapseMs = 3000;   // want() not renewed -> stop

inline volatile Label g_music = Label::Unknown;
inline volatile Label g_xbox = Label::Unknown;
// The server has no held-open request (an older one): never try again this
// boot, the pages poll the old way.
inline volatile bool g_unsupported = false;

inline volatile Page g_want = Page::None;
inline volatile uint32_t g_wantMs = 0;
inline TaskHandle_t g_task = nullptr;

// A parsed answer waiting for the worker (guarded by g_mux).
inline portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
inline JsonDocument* g_pending = nullptr;
inline Page g_pendingPage = Page::None;

inline Label labelOf(JsonVariantConst v) {
  if (v.isNull()) return Label::Unknown;
  return strcmp(v | "", "live") == 0 ? Label::Live : Label::Passive;
}

// doc["live"] from any state response (the refresh's too).
inline void applyLabels(JsonObjectConst labels) {
  if (labels.isNull()) return;
  g_music = labelOf(labels["music"]);
  g_xbox = labelOf(labels["xbox"]);
}

// Is `page` worth watching right now? The server's label, or the page's own
// state (an optimistic PLAY, or an older server that sends no labels).
inline bool isLive(Page page) {
  if (page == Page::Music)
    return g_music == Label::Live || !strcmp(haclient::media.state, "playing");
  if (page == Page::Xbox)
    return g_xbox == Label::Live || !strcmp(haclient::xboxMedia.state, "playing");
  return false;
}

// UI thread, every carousel frame: the page to watch, or None.
inline void want(Page page) {
  const Page before = g_want;
  g_want = page;
  g_wantMs = millis();
  if (page != before && page != Page::None && g_task) xTaskNotifyGive(g_task);
}

inline bool wanted(Page page) {
  return page != Page::None && g_want == page && millis() - g_wantMs < kWantLapseMs;
}

// --- applying an answer (network worker) --------------------------------
inline void applyMusic(JsonObjectConst states) {
  const char* e = deviceconfig::mediaEntity;
  if (!deviceconfig::mediaEnabled || !e[0] || screen_music::g_busy) return;
  const haclient::MediaPlayer before = haclient::media;
  haclient::applyMedia(states[e]);
  const haclient::MediaPlayer& now = haclient::media;
  const bool track = strcmp(before.title, now.title) != 0 || strcmp(before.artist, now.artist) != 0 ||
                     strcmp(before.picture, now.picture) != 0;
  if (track) screen_music::syncArt();
  if (track) screen_music::g_liveChange = 2;
  else if (memcmp(&before, &now, sizeof(now)) != 0 && screen_music::g_liveChange < 1)
    screen_music::g_liveChange = 1;
}

inline void applyXbox(JsonObjectConst states) {
  const char* e = deviceconfig::xboxMediaEntity;
  if (!e[0] || screen_xbox::g_busy) return;
  const haclient::MediaPlayer before = haclient::xboxMedia;
  haclient::applyXboxMedia(states[e]);
  const haclient::MediaPlayer& now = haclient::xboxMedia;
  const bool game = strcmp(before.title, now.title) != 0 || strcmp(before.picture, now.picture) != 0;
  if (game) {
    screen_xbox::fetchVisibleArt(0);
    screen_xbox::g_artDirty = false;  // the full repaint below covers the art
    screen_xbox::g_liveChange = 2;
  } else if (memcmp(&before, &now, sizeof(now)) != 0 && screen_xbox::g_liveChange < 1) {
    screen_xbox::g_liveChange = 1;
  }
}

// net::g_idleHook: take a waiting answer and apply it. Not network activity
// of its own, beyond a new track's art.
inline void applyPending() {
  portENTER_CRITICAL(&g_mux);
  JsonDocument* doc = g_pending;
  const Page page = g_pendingPage;
  g_pending = nullptr;
  portEXIT_CRITICAL(&g_mux);
  if (!doc) return;
  applyLabels((*doc)["live"].as<JsonObjectConst>());
  JsonObjectConst states = (*doc)["states"].as<JsonObjectConst>();
  if (wanted(page)) {
    if (page == Page::Music) applyMusic(states);
    if (page == Page::Xbox) applyXbox(states);
  }
  delete doc;
}

// --- the held-open request (its own task) ---------------------------------
inline bool ready(Page page) {
  return wanted(page) && !g_unsupported && wifilink::isUp() && globalsclient::ok && pairing::token[0] &&
         deviceconfig::activeSlug[0];
}

inline void taskFn(void*) {
  static char etag[64] = "";
  Page lastPage = Page::None;
  for (;;) {
    const Page page = g_want;
    if (!ready(page)) {
      lastPage = Page::None;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    if (page != lastPage) etag[0] = 0;  // a different page's copy is no use
    lastPage = page;

    char path[128];
    snprintf(path, sizeof(path), "/api/devices/%s/state?page=%s&wait=%u", deviceconfig::activeSlug,
             page == Page::Music ? "music" : "xbox", static_cast<unsigned>(kWaitSec));
    JsonDocument* doc = new JsonDocument;
    char status[48];
    char newEtag[64];
    httpjson::Conditional cond;
    cond.ifNoneMatch = etag;
    cond.etagOut = newEtag;
    cond.etagCap = sizeof(newEtag);
    cond.timeoutMs = kReadTimeoutMs;
    const bool ok = httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, path, pairing::token,
                                  *doc, status, sizeof(status), nullptr, 0, nullptr, &cond);
    if (!ok) {
      delete doc;
      Serial.printf("[live] %s\n", status);
      if (!strcmp(status, "HTTP 401")) pairing::noteUnauthorized();
      if (!strcmp(status, "HTTP 400") || !strcmp(status, "HTTP 404")) g_unsupported = true;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kBackoffMs));
      continue;
    }
    if (cond.notModified) {  // held the whole time, nothing changed: ask again
      delete doc;
      continue;
    }
    // An older server ignores ?wait and answers at once, every time: stop.
    // (No ETag either would mean the same: nothing to hold the request on.)
    if ((*doc)["live"].isNull() || !newEtag[0]) {
      g_unsupported = true;
      delete doc;
      continue;
    }
    snprintf(etag, sizeof(etag), "%s", newEtag);
    if (g_want != page) {  // left the page meanwhile
      delete doc;
      continue;
    }
    portENTER_CRITICAL(&g_mux);
    JsonDocument* stale = g_pending;
    g_pending = doc;
    g_pendingPage = page;
    portEXIT_CRITICAL(&g_mux);
    delete stale;
    net::wake();
  }
}

// Start with the network worker (app/data_refresh.h's startNetwork()).
inline void start() {
  net::g_idleHook = applyPending;
  if (g_task) return;
  xTaskCreatePinnedToCore(taskFn, "sb_live", 8192, nullptr, 1, &g_task, 1);
}

}  // namespace live
