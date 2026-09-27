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
#include "app/quick_access.h"  // hubDirty / hubActionBusy

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

// ===========================================================================
// Config: the room's config + the globals (HA connection, Wi-Fi networks)
// + the theme pack versions.
// ===========================================================================

// The server's /bundle ETag for the room it was fetched for, kept across
// deep sleep: an unchanged bundle then comes back as a bodyless 304.
RTC_DATA_ATTR static char g_bundleEtag[48] = "";
RTC_DATA_ATTR static char g_bundleSlug[32] = "";
// This session's server lacks /bundle or /state (an older Switchboard-
// Server): use the per-endpoint / direct-to-HA paths instead.
static bool g_noBundleEndpoint = false;
static bool g_noStateEndpoint = false;

// Icons: nothing already on SD is re-pulled unless Settings -> Refresh now
// asked for it (`force`); a theme version bump still downloads the new pack,
// and any per-item MDI icon the config just introduced is fetched here.
static void resolveIcons(bool force) {
  mdiicon::resolveAll(pairing::token, force ? mdiicon::Fetch::Force : mdiicon::Fetch::IfMissing);
}

enum class ServerResult : uint8_t { Ok, NoEndpoint, Failed };

// GET /api/devices/<slug>/bundle — config + globals + theme versions in one
// request, conditional on the copy we already hold.
static ServerResult fetchBundle(bool forceIcons) {
  char path[96];
  snprintf(path, sizeof(path), "/api/devices/%s/bundle", deviceconfig::activeSlug);
  const bool haveCopy = globalsclient::ok && deviceconfig::ok && !forceIcons &&
                        !strcmp(g_bundleSlug, deviceconfig::activeSlug) && g_bundleEtag[0];
  char etag[48] = "";
  httpjson::Conditional cond;
  cond.ifNoneMatch = haveCopy ? g_bundleEtag : nullptr;
  cond.etagOut = etag;
  cond.etagCap = sizeof(etag);

  JsonDocument doc;
  char status[64];
  if (!httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, path, pairing::token, doc,
                     status, sizeof(status), nullptr, 0, nullptr, &cond)) {
    Serial.printf("[bundle] %s\n", status);
    if (!strcmp(status, "HTTP 401")) pairing::noteUnauthorized();
    return !strcmp(status, "HTTP 404") ? ServerResult::NoEndpoint : ServerResult::Failed;
  }
  if (cond.notModified) {
    Serial.println("[bundle] unchanged (304)");
    resolveIcons(false);
    return ServerResult::Ok;
  }

  globalsclient::applyJson(doc["globals"]);
  sdcache::writeJson(globalsclient::kCacheName, doc["globals"]);
  deviceconfig::applyJson(doc["config"]);
  char cache[48];
  deviceconfig::cacheName(cache, sizeof(cache));
  sdcache::writeJson(cache, doc["config"]);
  themeclient::applyVersions(doc["theme"]["iconsVersion"] | "", doc["theme"]["fontsVersion"] | "",
                             pairing::token, forceIcons);
  resolveIcons(forceIcons);
  snprintf(g_bundleEtag, sizeof(g_bundleEtag), "%s", etag);
  snprintf(g_bundleSlug, sizeof(g_bundleSlug), "%s", deviceconfig::activeSlug);
  Serial.println("[bundle] updated");
  return ServerResult::Ok;
}

// The per-endpoint path (/api/globals, /config, /api/theme) — for an older
// server, or when /bundle failed.
static void fetchConfigLegacy(bool forceIcons) {
  if (!globalsclient::ok) globalsclient::fetch();  // HA host/token — rarely changes
  deviceconfig::fetch();                           // entity ids + refresh interval
  if (recoverFromUnauthorized()) {                 // token rotated -> retry with the new one
    if (!globalsclient::ok) globalsclient::fetch();
    deviceconfig::fetch();
  }
  themeclient::checkForUpdate(pairing::token, forceIcons);
  resolveIcons(forceIcons);
}

static void refreshConfig(bool forceIcons) {
  if (!g_noBundleEndpoint) {
    ServerResult r = fetchBundle(forceIcons);
    if (r == ServerResult::Failed && recoverFromUnauthorized()) r = fetchBundle(forceIcons);
    if (r == ServerResult::Ok) return;
    if (r == ServerResult::NoEndpoint) {
      // A 404 is either an older server (no /bundle) or no such room. The
      // per-endpoint fetch tells which: if it finds the room, the server
      // just lacks the endpoint — stop asking for it this session.
      fetchConfigLegacy(forceIcons);
      if (deviceconfig::ok) g_noBundleEndpoint = true;
      return;
    }
  }
  fetchConfigLegacy(forceIcons);
}

// ===========================================================================
// Data: every page's live Home Assistant state.
// ===========================================================================

// GET /api/devices/<slug>/state — the server fetches the room's entities
// from HA in parallel and returns them trimmed; applied here with the same
// parsers the direct path uses. A page with a command still in flight is
// skipped, same as the direct path (its own re-read lands shortly).
static ServerResult fetchServerState() {
  char path[96];
  snprintf(path, sizeof(path), "/api/devices/%s/state", deviceconfig::activeSlug);
  JsonDocument doc;
  char status[64], date[40] = "";
  if (!httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, path, pairing::token, doc,
                     status, sizeof(status), date, sizeof(date))) {
    Serial.printf("[state] %s\n", status);
    if (!strcmp(status, "HTTP 401")) pairing::noteUnauthorized();
    return !strcmp(status, "HTTP 404") ? ServerResult::NoEndpoint : ServerResult::Failed;
  }
  if (date[0]) haclient::parseHttpDate(date);

  JsonObjectConst states = doc["states"].as<JsonObjectConst>();
  JsonObjectConst errors = doc["errors"].as<JsonObjectConst>();
  // The state object for `entity`, or null (not configured / HA had none).
  const auto stateOf = [&](const char* entity) -> JsonVariantConst {
    return (entity && *entity) ? states[entity] : JsonVariantConst();
  };
  // Why an entity has no state — for the page's "unavailable" placeholder.
  const auto whyMissing = [&](const char* entity, char* out, size_t cap) {
    if (!entity || !*entity) snprintf(out, cap, "no entity configured");
    else snprintf(out, cap, "%s", errors[entity] | "no state");
  };

  const char* we = deviceconfig::weatherEntity;
  if (!haclient::applyWeather(stateOf(we)))
    whyMissing(we, haclient::weather.status, sizeof(haclient::weather.status));
  if (!haclient::applyAir(stateOf(deviceconfig::airQualityEntity)))
    whyMissing(deviceconfig::airQualityEntity, haclient::air.status, sizeof(haclient::air.status));
  if (!haclient::applyForecast(doc["forecast"][we].as<JsonArrayConst>()))
    whyMissing(we, haclient::forecast.status, sizeof(haclient::forecast.status));

  if (!screen_climate::g_busy && !haclient::applyClimate(stateOf(deviceconfig::climateEntity)))
    whyMissing(deviceconfig::climateEntity, haclient::climate.status, sizeof(haclient::climate.status));
  for (int i = 0; i < deviceconfig::climateSensorCount && i < 6; ++i)
    haclient::climateSensorOk[i] = haclient::applySensorValue(
        stateOf(deviceconfig::climateSensors[i].entity), haclient::climateSensorValue[i]);

  if (deviceconfig::lightGroupEnabled && !screen_lighting::g_busy) {
    if (!haclient::applyLight(stateOf(deviceconfig::lightGroupEntity)))
      whyMissing(deviceconfig::lightGroupEntity, haclient::lightGroup.status,
                 sizeof(haclient::lightGroup.status));
    for (int i = 0; i < deviceconfig::lightCount && i < deviceconfig::kMaxLights; ++i) {
      haclient::lightItemOn[i] = false;
      haclient::applyOnOff(stateOf(deviceconfig::lights[i].entity), haclient::lightItemOn[i]);
    }
  }

  if (deviceconfig::blindsGroupEnabled && !screen_blinds::g_busy &&
      !haclient::applyCover(stateOf(deviceconfig::blindsGroupEntity), haclient::cover))
    whyMissing(deviceconfig::blindsGroupEntity, haclient::cover.status, sizeof(haclient::cover.status));
  if (deviceconfig::blindsItemCount == 2)
    for (int i = 0; i < 2; ++i)
      if (!screen_blinds::g_itemBusy[i] &&
          !haclient::applyCover(stateOf(deviceconfig::blindsItems[i].entity), haclient::coverItems[i]))
        whyMissing(deviceconfig::blindsItems[i].entity, haclient::coverItems[i].status,
                   sizeof(haclient::coverItems[i].status));

  if (deviceconfig::mediaEnabled && !screen_music::g_busy &&
      !haclient::applyMedia(stateOf(deviceconfig::mediaEntity)))
    whyMissing(deviceconfig::mediaEntity, haclient::media.status, sizeof(haclient::media.status));
  if (deviceconfig::xboxMediaEntity[0] && !screen_xbox::g_busy) {
    if (!haclient::applyXboxMedia(stateOf(deviceconfig::xboxMediaEntity)))
      whyMissing(deviceconfig::xboxMediaEntity, haclient::xboxMedia.status,
                 sizeof(haclient::xboxMedia.status));
    screen_xbox::loadVisibleArt();  // catches a game/track change even off-page
  }

  if (!hubActionBusy) {
    bool changed = false;
    for (int i = 0; i < deviceconfig::hubItemCount; ++i) {
      if (deviceconfig::hubItems[i].actionType != deviceconfig::HubAction::Toggle) continue;
      const bool before = haclient::hubToggleOn[i];
      haclient::applyOnOff(stateOf(deviceconfig::hubItems[i].actionEntity), haclient::hubToggleOn[i]);
      changed |= before != haclient::hubToggleOn[i];
    }
    if (changed) hubDirty = true;
  }
  return ServerResult::Ok;
}

// The direct path: every entity straight from Home Assistant, one request
// each — for an older server, or when the server can't reach HA itself.
// Returns whether the weather came back.
static bool fetchDataDirect() {
  bool gotWeather = false;
  for (int attempt = 0; attempt < 2 && !gotWeather; ++attempt) {
    // 2nd pass: the HA host/token we had was stale (config changed, or a bad
    // cache poisoned it) — re-pull /api/globals and try once more.
    if (attempt == 1) {
      globalsclient::fetch();
      deviceconfig::fetch();
      resolveIcons(false);  // new names in the re-pulled config
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
      // per configured light.
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
  return gotWeather;
}

// Server first (one request); the direct path if the server lacks /state,
// or can't reach Home Assistant itself right now. Returns whether the
// weather came back.
static bool refreshData() {
  if (!g_noStateEndpoint && deviceconfig::ok) {
    net::serviceCommands();
    ServerResult r = fetchServerState();
    if (r == ServerResult::Failed && recoverFromUnauthorized()) r = fetchServerState();
    if (r == ServerResult::Ok) return haclient::weather.ok;
    if (r == ServerResult::NoEndpoint) g_noStateEndpoint = true;
  }
  return fetchDataDirect();
}

// ===========================================================================
// The refresh
// ===========================================================================

// Pull everything the carousel shows — normally two requests to the server
// (/bundle, then /state), falling back to the per-endpoint and direct-to-HA
// paths on an older server — then save what changed to the SD cache
// (persist.h) so the next wake can paint it before Wi-Fi is up. On failure
// the last good cached state is restored (a network blip must not blank the
// screen). Returns true if the weather came back. Only ever runs once Wi-Fi
// is up — with no link there's nothing to do (the clients still hold what
// boot loaded from the cache).
static bool refreshStandby() {
  if (!wifilink::isUp()) return false;

  const bool forceIcons = themeclient::takeRefreshRequest();
  refreshConfig(forceIcons);

  const uint32_t statusBefore = statusSignature();
  const bool gotWeather = refreshData();

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
