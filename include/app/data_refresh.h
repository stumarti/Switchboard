#pragma once

// ===========================================================================
// data_refresh — the shared Switchboard-server + Home Assistant pull that
// feeds every carousel page in one pass, run on a background task
// (kickWeatherRefresh()) so the UI never blocks on it. Owned here rather
// than by any one screen, since it drives all of them at once.
// ===========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"
#include "persist.h"
#include "pairing_client.h"
#include "theme_client.h"
#include "mdi_icon.h"
#include "screen_common.h"
#include "screen_xbox.h"
#include "screen_climate.h"
#include "screen_lighting.h"
#include "screen_blinds.h"
#include "screen_music.h"
#include "app/wifi_link.h"
#include "app/net.h"
#include "app/carousel.h"  // ensureCarouselPageEnabled()

// See wifilink::ensureMdns() — kept as the free function screen_fwd.h
// declares, since every screen's background task calls it by this name.
static void ensureMdns() { wifilink::ensureMdns(); }

// A server 401 (flagged by the clients, see pairing::noteUnauthorized())
// means the token was rejected, not necessarily that this remote was
// un-approved: re-register by MAC, which for a still-approved device hands
// back a fresh token at once. True if it did (the caller retries its
// fetches). If the server really has un-approved it, pairing::paired goes
// false and the carousel restarts into the pairing screen
// (syncCarouselWithNetwork()).
static bool recoverFromUnauthorized() {
  if (!pairing::takeUnauthorized()) return false;
  const bool ok = pairing::reauthorize();
  Serial.printf("[pairing] token rejected -> re-register: %s (%s)\n", ok ? "approved" : "not approved",
                pairing::status);
  return ok;
}

// A fingerprint of what the Status page shows (weather, air, forecast, the
// indoor temperature) — compared across a refresh to tell whether the
// page's data actually changed (haclient::dataChangedUtc).
static uint32_t statusSignature() {
  uint32_t crc = 0;
  crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&haclient::weather), sizeof(haclient::weather));
  crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&haclient::air), sizeof(haclient::air));
  crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&haclient::forecast), sizeof(haclient::forecast));
  const float indoor = haclient::climate.hasTemp ? haclient::climate.temp : -1000.0f;
  crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t*>(&indoor), sizeof(indoor));
  return crc;
}

// Pull everything the carousel shows, then save what changed to the SD cache
// (persist.h) so the next wake can paint it before Wi-Fi is up. On any
// failure the last good cached state is restored (a network blip must not
// blank the screen). Returns true if the weather fetch succeeded. Drives
// every carousel page in one pass, so it lives here rather than being owned
// by any one screen. Only ever runs once Wi-Fi is up — with no link there's
// nothing to do (the clients still hold what boot loaded from the cache).
static bool refreshStandby() {
  if (!wifilink::isUp()) return false;

  // TEMP DEBUG — confirm whether globals are being (re)fetched or served from
  // the SD cache. Remove once the blank-Wi-Fi-names issue is resolved.
  Serial.printf("[globals] refreshStandby: ok=%d (%s)\n", globalsclient::ok,
                globalsclient::ok ? "skipping fetch, using cached/persisted globals"
                                   : "will fetch");
  if (!globalsclient::ok) globalsclient::fetch();  // HA host/token — rarely changes
  deviceconfig::fetch();                           // entity ids + refresh interval
  if (recoverFromUnauthorized()) {                 // token rotated -> retry with the new one
    if (!globalsclient::ok) globalsclient::fetch();
    deviceconfig::fetch();
  }
  // Icons: nothing already on SD is re-pulled unless Settings -> Refresh now
  // asked for it. A theme version bump still downloads the new pack, and any
  // per-item MDI icon the config just introduced is fetched right here.
  const bool forceIcons = themeclient::takeRefreshRequest();
  themeclient::checkForUpdate(pairing::token, forceIcons);
  mdiicon::resolveAll(pairing::token, forceIcons ? mdiicon::Fetch::Force : mdiicon::Fetch::IfMissing);

  const uint32_t statusBefore = statusSignature();
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
    // Between each group of requests, send any press that came in meanwhile
    // (net::serviceCommands()) — a tap never waits for the refresh. And a
    // page with a command still in flight (its g_busy) is skipped: its own
    // re-read will land shortly, and fetching it now could overwrite what
    // the user just changed on screen with the pre-command state.
    gotWeather = haclient::fetchWeather(h, p, t, deviceconfig::weatherEntity);
    haclient::fetchAir(h, p, t, deviceconfig::airQualityEntity);
    haclient::fetchForecast(h, p, t, deviceconfig::weatherEntity);
    net::serviceCommands();
    if (!screen_climate::g_busy) haclient::fetchClimate(h, p, t, deviceconfig::climateEntity);
    // Climate page's additional-sensor footer (kitchen: Window/Wall/Thermostat).
    for (int i = 0; i < deviceconfig::climateSensorCount && i < 6; ++i)
      haclient::climateSensorOk[i] = haclient::fetchSensorValue(
          h, p, t, deviceconfig::climateSensors[i].entity, haclient::climateSensorValue[i]);
    net::serviceCommands();
    if (deviceconfig::lightGroupEnabled && !screen_lighting::g_busy) {
      haclient::fetchLight(h, p, t, deviceconfig::lightGroupEntity);
      // Individual on/off for the Lighting page's Lights tab — one small GET
      // per configured light (kitchen: 4), so this does add to the refresh's
      // total time; acceptable at the normal 15+ minute refresh cadence.
      for (int i = 0; i < deviceconfig::lightCount && i < deviceconfig::kMaxLights; ++i)
        haclient::fetchLightOn(h, p, t, deviceconfig::lights[i].entity, haclient::lightItemOn[i]);
    }
    net::serviceCommands();
    if (deviceconfig::blindsGroupEnabled && !screen_blinds::g_busy)
      haclient::fetchCover(h, p, t, deviceconfig::blindsGroupEntity);
    // Two individual blinds, no group entity to read a combined position from
    // -> the Blinds page's side-by-side panel layout needs each one's own state.
    if (deviceconfig::blindsItemCount == 2)
      for (int i = 0; i < 2; ++i)
        if (!screen_blinds::g_itemBusy[i])
          haclient::fetchCoverItem(h, p, t, deviceconfig::blindsItems[i].entity, i);
    net::serviceCommands();
    if (deviceconfig::mediaEnabled && !screen_music::g_busy)
      haclient::fetchMedia(h, p, t, deviceconfig::mediaEntity);
    if (deviceconfig::xboxMediaEntity[0] && !screen_xbox::g_busy) {
      haclient::fetchXboxMedia(h, p, t, deviceconfig::xboxMediaEntity);
      screen_xbox::loadVisibleArt();  // catches a game/track change even off-page
    }
    net::serviceCommands();
  }

  if (gotWeather && globalsclient::ok && deviceconfig::ok) {
    if (!haclient::dataChangedValid || statusSignature() != statusBefore) {
      haclient::dataChangedUtc = haclient::clockUtc;
      haclient::dataChangedValid = haclient::clockValid;
    }
    ensureCarouselPageEnabled();  // a config change may have hidden the current page
    persist::save();
  } else {
    persist::load();  // roll back any half-updated / cleared client state
    mdiicon::resolveAll(nullptr, mdiicon::Fetch::CacheOnly);  // re-point at the rolled-back config's icons
  }
  return gotWeather;
}

// Ask the network worker (app/net.h) for a refresh — it runs there, so a
// wake never blocks on the HA calls. g_weatherBusy stays set until it's done.
static void kickWeatherRefresh() { net::requestRefresh(); }

// The worker's refresh job (registered by startNetwork()).
static void runRefreshJob() { refreshStandby(); }

// Start the network worker. Every interactive boot path calls this once,
// after the cached config is loaded; the unattended timer paths don't need
// it (they call refreshStandby() directly and go back to sleep).
static void startNetwork() { net::start(runRefreshJob); }
