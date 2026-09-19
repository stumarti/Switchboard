#pragma once

// ===========================================================================
// Device-config client — GETs /api/devices/<slug>/config and pulls out the
// Standby screen's settings: which Home Assistant entities to show and how
// often to refresh.
//
//   standby.weatherEntity     e.g. "weather.forecast_home_2"  (also the daily
//                             forecast source, via weather.get_forecasts)
//   standby.climateEntity     e.g. "climate.room_climate_office"  (indoor temp)
//   standby.airQualityEntity  e.g. "sensor.air_quality_index"  (optional)
//   standby.refreshIntervalMin
//   climate.additionalSensors[]  extra temperature sensors (e.g. window/wall/
//                             thermostat), shown at the bottom of the
//                             Climate carousel page
//
// The slug is SWITCHBOARD_DEVICE_SLUG in config.h. HA connection details and
// the shared Wi-Fi networks list are taken from /api/globals
// (globals_client.h), not from here.
// ===========================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "http_json.h"
#include "local_settings.h"

namespace deviceconfig {

// The room/device slug this remote drives. Defaults to SWITCHBOARD_DEVICE_SLUG
// but is overridable at runtime (Settings -> Select room) and stored in NVS.
inline char activeSlug[32] = SWITCHBOARD_DEVICE_SLUG;

inline void loadSlug() {
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/true)) {
    const String s = p.getString("slug", SWITCHBOARD_DEVICE_SLUG);
    p.end();
    if (s.length() > 0 && s.length() < sizeof(activeSlug))
      snprintf(activeSlug, sizeof(activeSlug), "%s", s.c_str());
  }
}
inline void saveSlug(const char* s) {
  if (!s || !*s) return;
  snprintf(activeSlug, sizeof(activeSlug), "%s", s);
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.putString("slug", s);
    p.end();
  }
}
// Settings -> Developer -> Hard reset: forget the picked room and go back to
// SWITCHBOARD_DEVICE_SLUG — the rescue path out of a room whose server config
// is bad enough to wedge the device on every boot (a config that crashes the
// weather-fetch task crashes the whole chip, since it's one address space —
// that reads as a boot loop, and by the time it's picked a slug is already
// wrong, so Settings -> Select room may not be reachable to fix it by hand).
inline void resetSlug() {
  snprintf(activeSlug, sizeof(activeSlug), "%s", SWITCHBOARD_DEVICE_SLUG);
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.remove("slug");
    p.end();
  }
}

inline char name[32] = "";
inline char weatherEntity[64] = "";
inline char climateEntity[64] = "";
inline char airQualityEntity[64] = "";
inline uint16_t refreshIntervalMin = 30;

// lighting.group — the room's main light (Lighting carousel page).
inline bool lightGroupEnabled = false;
inline char lightGroupName[32] = "";
inline char lightGroupEntity[64] = "";
inline bool lightGroupBrightness = false;
inline bool lightGroupColorTemp = false;

// lighting.lights[] / lighting.scenes[] — individual lights and scenes.
struct LightItem {
  char name[20] = "";
  char entity[52] = "";
};
// Lights and scenes both get a bigger cap than the other item lists — the
// Lighting page's grid fits 12 (2x6) chips per page and pages through the
// rest, on both the Lights and Scenes tabs independently (see
// screen_lighting.h's kListPageSize).
inline constexpr int kMaxLights = 24;
inline LightItem lights[kMaxLights];
inline int lightCount = 0;
inline constexpr int kMaxScenes = 24;
inline LightItem scenes[kMaxScenes];
inline int sceneCount = 0;

// blinds.group / blinds.items[] — the room's cover(s) (Blinds carousel page).
inline bool blindsGroupEnabled = false;
inline char blindsGroupName[32] = "";
inline char blindsGroupEntity[64] = "";
inline LightItem blindsItems[6];
inline int blindsItemCount = 0;

// climate.additionalSensors[] — extra temperature sensors shown at the
// bottom of the Climate carousel page (e.g. a window/wall/thermostat sensor,
// alongside the main climateEntity's own reading).
inline LightItem climateSensors[6];
inline int climateSensorCount = 0;

// media.* — the room's media_player entity (Music carousel page).
inline bool mediaEnabled = false;
inline char mediaName[32] = "";
inline char mediaEntity[64] = "";

// tv.* — the room's TV (TV carousel page): a `remote.*` entity for dpad/back/
// home/power/volume, a `media_player.*` entity for app launching, and the
// app-name -> package-name map (tv.apps, a JSON object) shown as a chip grid.
inline char tvMediaEntity[64] = "";
inline char tvRemoteEntity[64] = "";
struct AppItem {
  char name[20] = "";
  char pkg[52] = "";
};
// Capped at 4 (not the other lists' 6) — the RTC-memory blob (persist.h) that
// mirrors this config is already tight against the ESP32-S3's small RTC slow
// memory region; see kMaxWifiNets below for the same constraint.
inline constexpr int kMaxTvApps = 4;
inline AppItem tvApps[kMaxTvApps];
inline int tvAppCount = 0;

// screens.* — per-page carousel visibility for this room. Missing/absent
// defaults to true (an older config with no "screens" key still shows
// everything), so only an explicit `false` hides a page. Status has no
// toggle — it's always shown.
inline bool screenLighting = true;
inline bool screenBlinds   = true;
inline bool screenMusic    = true;
inline bool screenTv       = true;
inline bool screenXbox     = true;
inline bool screenClimate  = true;
inline bool screenWifi     = true;

inline bool ok = false;
inline char status[64] = "";

inline bool fetch() {
  ok = false;
  name[0] = weatherEntity[0] = climateEntity[0] = airQualityEntity[0] = 0;
  refreshIntervalMin = localsettings::refreshOverrideMin ? localsettings::refreshOverrideMin : 30;
  lightGroupEnabled = lightGroupBrightness = lightGroupColorTemp = false;
  lightGroupName[0] = lightGroupEntity[0] = 0;
  lightCount = sceneCount = 0;
  blindsGroupEnabled = false;
  blindsGroupName[0] = blindsGroupEntity[0] = 0;
  blindsItemCount = 0;
  climateSensorCount = 0;
  mediaEnabled = false;
  mediaName[0] = mediaEntity[0] = 0;
  screenLighting = screenBlinds = screenMusic = screenTv = screenXbox = screenClimate = screenWifi =
      true;
  tvMediaEntity[0] = tvRemoteEntity[0] = 0;
  tvAppCount = 0;

  char cfgPath[96];
  snprintf(cfgPath, sizeof(cfgPath), "/api/devices/%s/config", activeSlug);

  JsonDocument doc;
  if (!httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, cfgPath, /*bearer=*/nullptr,
                     doc, status, sizeof(status))) {
    return false;
  }

  snprintf(name, sizeof(name), "%s", doc["name"] | SWITCHBOARD_DEVICE_SLUG);

  JsonObjectConst sb = doc["standby"].as<JsonObjectConst>();
  snprintf(weatherEntity, sizeof(weatherEntity), "%s", sb["weatherEntity"] | "");
  snprintf(climateEntity, sizeof(climateEntity), "%s", sb["climateEntity"] | "");
  snprintf(airQualityEntity, sizeof(airQualityEntity), "%s", sb["airQualityEntity"] | "");
  // Settings -> Timeouts -> Refresh interval, when set, wins over the
  // server's per-room value — a local override, not a fallback for a missing
  // server field (that's the "| 30" below, unaffected by this).
  if (localsettings::refreshOverrideMin) {
    refreshIntervalMin = localsettings::refreshOverrideMin;
  } else {
    const int mins = sb["refreshIntervalMin"] | 30;
    refreshIntervalMin = mins < 1 ? 1 : static_cast<uint16_t>(mins);
  }

  JsonObjectConst lig = doc["lighting"].as<JsonObjectConst>();
  JsonObjectConst lg = lig["group"].as<JsonObjectConst>();
  lightGroupEnabled = lg["enabled"] | false;
  snprintf(lightGroupName, sizeof(lightGroupName), "%s", lg["name"] | "");
  snprintf(lightGroupEntity, sizeof(lightGroupEntity), "%s", lg["entity"] | "");
  lightGroupBrightness = lg["controls"]["brightness"] | false;
  lightGroupColorTemp = lg["controls"]["colorTemp"] | false;

  const auto fillItems = [](JsonArrayConst arr, LightItem* out, int& n, int cap) {
    for (JsonObjectConst o : arr) {
      if (n >= cap) break;
      const char* e = o["entity"] | "";
      if (!*e) continue;
      snprintf(out[n].entity, sizeof(out[n].entity), "%s", e);
      snprintf(out[n].name, sizeof(out[n].name), "%s", o["name"] | e);
      ++n;
    }
  };
  fillItems(lig["lights"].as<JsonArrayConst>(), lights, lightCount, kMaxLights);
  fillItems(lig["scenes"].as<JsonArrayConst>(), scenes, sceneCount, kMaxScenes);

  JsonObjectConst bl = doc["blinds"].as<JsonObjectConst>();
  JsonObjectConst bg = bl["group"].as<JsonObjectConst>();
  blindsGroupEnabled = bg["enabled"] | false;
  snprintf(blindsGroupName, sizeof(blindsGroupName), "%s", bg["name"] | "");
  snprintf(blindsGroupEntity, sizeof(blindsGroupEntity), "%s", bg["entity"] | "");
  fillItems(bl["items"].as<JsonArrayConst>(), blindsItems, blindsItemCount, 6);

  JsonObjectConst cl = doc["climate"].as<JsonObjectConst>();
  fillItems(cl["additionalSensors"].as<JsonArrayConst>(), climateSensors, climateSensorCount, 6);

  JsonObjectConst med = doc["media"].as<JsonObjectConst>();
  mediaEnabled = med["enabled"] | false;
  snprintf(mediaName, sizeof(mediaName), "%s", med["name"] | "");
  snprintf(mediaEntity, sizeof(mediaEntity), "%s", med["entity"] | "");

  JsonObjectConst scr = doc["screens"].as<JsonObjectConst>();
  screenLighting = scr["lighting"] | true;
  screenBlinds   = scr["blinds"] | true;
  screenMusic    = scr["music"] | true;
  screenTv       = scr["tv"] | true;
  screenXbox     = scr["xbox"] | true;
  screenClimate  = scr["climate"] | true;
  screenWifi     = scr["wifi"] | true;

  JsonObjectConst tv = doc["tv"].as<JsonObjectConst>();
  snprintf(tvMediaEntity, sizeof(tvMediaEntity), "%s", tv["mediaPlayerEntity"] | "");
  snprintf(tvRemoteEntity, sizeof(tvRemoteEntity), "%s", tv["remoteEntity"] | "");
  for (JsonPairConst kv : tv["apps"].as<JsonObjectConst>()) {
    if (tvAppCount >= kMaxTvApps) break;
    const char* pkg = kv.value().as<const char*>();
    if (!pkg || !*pkg) continue;
    AppItem& app = tvApps[tvAppCount];
    snprintf(app.name, sizeof(app.name), "%s", kv.key().c_str());
    snprintf(app.pkg, sizeof(app.pkg), "%s", pkg);
    ++tvAppCount;
  }

  ok = true;
  return true;
}

}  // namespace deviceconfig
