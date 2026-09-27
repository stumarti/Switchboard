#pragma once

// ===========================================================================
// data_refresh — the shared Switchboard-server + Home Assistant pull that
// feeds every carousel page in one pass, run on a background task
// (kickWeatherRefresh()) so the UI never blocks on it. Owned here rather
// than by any one screen, since it drives all of them at once.
// ===========================================================================

#include <Arduino.h>
#include <WiFi.h>

#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"
#include "persist.h"
#include "pairing_client.h"
#include "theme_client.h"
#include "mdi_icon.h"
#include "screen_common.h"
#include "screen_xbox.h"
#include "app/wifi_link.h"
#include "app/carousel.h"  // ensureCarouselPageEnabled()

// See wifilink::ensureMdns() — kept as the free function screen_fwd.h
// declares, since every screen's background task calls it by this name.
static void ensureMdns() { wifilink::ensureMdns(); }

// Pull everything the carousel shows, then save it to the SD cache
// (persist.h) so the next wake can paint it before Wi-Fi is up. On any
// failure the last good cached state is restored (a network blip must not
// blank the screen). Returns true if the
// weather fetch succeeded. Drives ALL FOUR carousel pages in one HA round
// trip, so it lives here rather than being owned by screen_status alone.
static bool refreshStandby() {
  if (WiFi.status() != WL_CONNECTED) {
    persist::load();
    mdiicon::resolveAll(nullptr, mdiicon::Fetch::CacheOnly);
    return false;
  }

  // TEMP DEBUG — confirm whether globals are being (re)fetched or served from
  // the RTC-persisted cache. Remove once the blank-Wi-Fi-names issue is resolved.
  Serial.printf("[globals] refreshStandby: ok=%d (%s)\n", globalsclient::ok,
                globalsclient::ok ? "skipping fetch, using cached/persisted globals"
                                   : "will fetch");
  if (!globalsclient::ok) globalsclient::fetch();  // HA host/token — rarely changes
  deviceconfig::fetch();                           // entity ids + refresh interval
  // Icons: nothing already on SD is re-pulled unless Settings -> Refresh now
  // asked for it. A theme version bump still downloads the new pack, and any
  // per-item MDI icon the config just introduced is fetched right here.
  const bool forceIcons = themeclient::takeRefreshRequest();
  themeclient::checkForUpdate(pairing::token, forceIcons);
  mdiicon::resolveAll(pairing::token, forceIcons ? mdiicon::Fetch::Force : mdiicon::Fetch::IfMissing);

  bool gotWeather = false;
  for (int attempt = 0; attempt < 2 && !gotWeather; ++attempt) {
    // 2nd pass: the HA host/token we had was stale (config changed, or a bad
    // persist blob poisoned it) — re-pull /api/globals and try once more.
    if (attempt == 1) {
      globalsclient::fetch();
      deviceconfig::fetch();
      mdiicon::resolveAll(pairing::token, mdiicon::Fetch::IfMissing);  // new names in the re-pulled config
    }
    if (!globalsclient::ok) break;
    const char* h = globalsclient::haHost;
    const uint16_t p = globalsclient::haPort;
    const char* t = globalsclient::haToken;
    gotWeather = haclient::fetchWeather(h, p, t, deviceconfig::weatherEntity);
    haclient::fetchClimate(h, p, t, deviceconfig::climateEntity);
    haclient::fetchAir(h, p, t, deviceconfig::airQualityEntity);
    haclient::fetchForecast(h, p, t, deviceconfig::weatherEntity);
    if (deviceconfig::lightGroupEnabled) {
      haclient::fetchLight(h, p, t, deviceconfig::lightGroupEntity);
      // Individual on/off for the Lighting page's Lights tab — one small GET
      // per configured light (kitchen: 4), so this does add to the refresh's
      // total time; acceptable at the normal 15+ minute refresh cadence.
      for (int i = 0; i < deviceconfig::lightCount && i < deviceconfig::kMaxLights; ++i)
        haclient::fetchLightOn(h, p, t, deviceconfig::lights[i].entity, haclient::lightItemOn[i]);
    }
    if (deviceconfig::blindsGroupEnabled)
      haclient::fetchCover(h, p, t, deviceconfig::blindsGroupEntity);
    // Two individual blinds, no group entity to read a combined position from
    // -> the Blinds page's side-by-side panel layout needs each one's own state.
    if (deviceconfig::blindsItemCount == 2)
      for (int i = 0; i < 2; ++i)
        haclient::fetchCoverItem(h, p, t, deviceconfig::blindsItems[i].entity, i);
    // Climate page's additional-sensor footer (kitchen: Window/Wall/Thermostat).
    for (int i = 0; i < deviceconfig::climateSensorCount && i < 6; ++i)
      haclient::climateSensorOk[i] = haclient::fetchSensorValue(
          h, p, t, deviceconfig::climateSensors[i].entity, haclient::climateSensorValue[i]);
    if (deviceconfig::mediaEnabled)
      haclient::fetchMedia(h, p, t, deviceconfig::mediaEntity);
    if (deviceconfig::xboxMediaEntity[0]) {
      haclient::fetchXboxMedia(h, p, t, deviceconfig::xboxMediaEntity);
      screen_xbox::loadVisibleArt();  // catches a game/track change even off-page
    }
  }

  if (gotWeather && globalsclient::ok && deviceconfig::ok) {
    ensureCarouselPageEnabled();  // a config change may have hidden the current page
    persist::save();
  } else {
    persist::load();  // roll back any half-updated / cleared client state
    mdiicon::resolveAll(nullptr, mdiicon::Fetch::CacheOnly);  // re-point at the rolled-back config's icons
  }
  return gotWeather;
}

// Background weather refresh — so a wake never blocks on the HA calls.
// (g_weatherBusy itself lives in screen_common.h — every screen's own kick()
// gates on it too, so an action's background task never races the weather
// task over shared mDNS/HTTP resources.)
static void weatherTask(void*) {
  g_weatherBusy = true;
  ensureMdns();
  refreshStandby();
  g_weatherBusy = false;
  vTaskDelete(nullptr);
}

static void kickWeatherRefresh() {
  if (g_weatherBusy) return;
  g_weatherBusy = true;  // set before create so a racing caller can't double-spawn
  if (xTaskCreatePinnedToCore(weatherTask, "sb_wx", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    g_weatherBusy = false;
}
