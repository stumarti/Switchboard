#pragma once

// ===========================================================================
// ha_client — reads the two Home Assistant entities the Standby screen needs,
// straight from HA's REST API (GET /api/states/<entity>, bearer auth):
//
//   weather.*  -> condition + outdoor temp / wind / humidity
//   climate.*  -> current_temperature (shown as "Indoor")
//
// HA's connection (host / port / token) comes from globals_client.h. HA also
// serves the clock: every REST response carries a `Date:` header, which we
// parse into the date line at the top of the screen.
// ===========================================================================

#include <Arduino.h>
#include <stdlib.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "http_json.h"

namespace haclient {

struct Weather {
  char condition[24] = "";   // raw HA state, e.g. "partlycloudy"
  char label[24] = "";       // human label, e.g. "Partly cloudy"
  float temp = 0;    bool hasTemp = false;
  float wind = 0;    bool hasWind = false;
  char windUnit[10] = "km/h";
  int humidity = 0;  bool hasHumidity = false;
  float uv = 0;      bool hasUv = false;
  bool ok = false;
  char status[48] = "";
};

struct Climate {
  float temp = 0;    bool hasTemp = false;       // current_temperature -> "Indoor"
  float target = 0;  bool hasTarget = false;     // attributes.temperature (setpoint)
  float minTemp = 7, maxTemp = 35, tempStep = 0.5f;
  char mode[16] = "";                            // hvac state: heat / cool / off / auto / ...
  char modes[8][16] = {};   int modeCount = 0;   // attributes.hvac_modes
  bool ok = false;
  char status[48] = "";
};

struct Air {
  int index = 0;     bool has = false;   // numeric AQI
  char category[16] = "";                // "Good" / "Moderate" / ...
  bool ok = false;
  char status[48] = "";
};

struct Light {
  bool on = false;
  int brightnessPct = 0;   bool hasBrightness = false;  // from attributes.brightness (0..255)
  bool ok = false;
  char status[48] = "";
};

struct Cover {
  int position = 0;   bool hasPosition = false;   // 0..100, 100 = fully open
  char state[12] = "";                            // open / closed / opening / closing
  bool ok = false;
  char status[48] = "";
};

struct ForecastDay {
  char dow[4] = "";   // "MON"
  float hi = 0;       bool hasHi = false;
  float lo = 0;       bool hasLo = false;
  char condition[24] = "";
};

struct Forecast {
  ForecastDay day[3];
  int count = 0;
  bool ok = false;
  char status[48] = "";
};

struct MediaPlayer {
  char state[16] = "";     // playing / paused / idle / off / unavailable / buffering
  char title[64] = "";     // attributes.media_title
  char artist[64] = "";    // attributes.media_artist
  char picture[160] = "";  // attributes.entity_picture — a relative HA URL (album
                           // art), e.g. "/api/media_player_proxy/media_player.x?..."
  int volumePct = 0;   bool hasVolume = false;  // attributes.volume_level (0..1) -> 0..100
  bool muted = false;  bool hasMuted = false;   // attributes.is_volume_muted
  bool ok = false;
  char status[48] = "";
};

inline Weather weather;
inline Climate climate;
inline Air air;
inline Light lightGroup;
// Individual-light on/off, indexed like deviceconfig::lights[] (24 slots,
// deviceconfig::kMaxLights) — the Lighting page's Lights tab shows a bulb
// icon per chip. Lighter-weight than a full Light per item (see
// fetchLightOn()): just the one bit each needs.
inline bool lightItemOn[24] = {};
inline Cover cover;
// Per-blind state when a room has exactly two individual covers and no
// group entity — the Blinds page's side-by-side panel layout, one entry per
// deviceconfig::blindsItems[]. Only indices 0/1 are ever used.
inline Cover coverItems[2];
inline Forecast forecast;
inline MediaPlayer media;
// The Xbox carousel page's console — a separate MediaPlayer instance (not
// media[]) since Music and Xbox are two independently-configured
// media_player entities that can both be active in the same room, same
// convention as cover vs coverItems[].
inline MediaPlayer xboxMedia;
// climate.additionalSensors[] readings, indexed like deviceconfig::
// climateSensors[] — the Climate page's "area temperatures" footer.
inline float climateSensorValue[6] = {0, 0, 0, 0, 0, 0};
inline bool climateSensorOk[6] = {false, false, false, false, false, false};

// Quick Access hub: live on/off for each configured Toggle item, indexed
// like deviceconfig::hubItems[] (app/quick_access.h reads/writes it).
inline constexpr int kMaxHubToggles = 20;
inline bool hubToggleOn[kMaxHubToggles] = {};

// UTC wall clock from the last HA `Date:` header. valid=false until a fetch
// has parsed one.
inline struct tm clockUtc = {};
inline bool clockValid = false;

// When what the Status page shows last actually CHANGED (the HA clock at
// that refresh) — its "Updated" footer. Not simply the last fetch time: a
// refresh that brings back identical data then draws an identical frame,
// which commitFrame() skips instead of refreshing the panel just to move a
// timestamp (every unattended timer wake used to).
inline struct tm dataChangedUtc = {};
inline bool dataChangedValid = false;

// --- condition -> display label -----------------------------------------
inline const char* conditionLabel(const char* c) {
  struct Row { const char* key; const char* label; };
  static const Row kRows[] = {
      {"clear-night", "Clear night"},   {"cloudy", "Cloudy"},
      {"exceptional", "Exceptional"},   {"fog", "Fog"},
      {"hail", "Hail"},                 {"lightning", "Lightning"},
      {"lightning-rainy", "Thunder, rain"}, {"partlycloudy", "Partly cloudy"},
      {"pouring", "Pouring"},           {"rainy", "Rainy"},
      {"snowy", "Snowy"},               {"snowy-rainy", "Sleet"},
      {"sunny", "Sunny"},               {"windy", "Windy"},
      {"windy-variant", "Windy"},
  };
  if (c && *c)
    for (const Row& r : kRows)
      if (strcmp(r.key, c) == 0) return r.label;
  return c && *c ? c : "Unknown";
}

// Day of week (0 = Sunday) for a Gregorian date — Sakamoto's algorithm. Used
// instead of a libc timegm/gmtime round-trip so this stays portable.
inline int dayOfWeek(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 7) % 7;
}

// Parse an HTTP Date header ("Wed, 20 Aug 2025 14:07:23 GMT") into clockUtc.
inline void parseHttpDate(const char* s) {
  if (!s || !*s) return;
  static const char* kMon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char mon[8] = "";
  int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
  // Skip the leading "Www, "
  const char* p = strchr(s, ',');
  p = p ? p + 1 : s;
  if (sscanf(p, " %d %7s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6) return;

  int monIdx = -1;
  for (int i = 0; i < 12; ++i)
    if (strncmp(mon, kMon[i], 3) == 0) monIdx = i;
  if (monIdx < 0 || year < 1970 || day < 1 || day > 31) return;

  struct tm t = {};
  t.tm_mday = day;
  t.tm_mon = monIdx;
  t.tm_year = year - 1900;
  t.tm_hour = hh;
  t.tm_min = mm;
  t.tm_sec = ss;
  t.tm_wday = dayOfWeek(year, monIdx + 1, day);
  clockUtc = t;
  clockValid = true;
}

// --- fetch helpers ------------------------------------------------------
// Both take the resolved HA host/port/token from globals_client.

// Parse one weather entity's state object into `weather`. Shared by the direct fetch below and the
// server's /state response (app/data_refresh.h).
inline bool applyWeather(JsonVariantConst doc) {
  weather = Weather{};
  snprintf(weather.condition, sizeof(weather.condition), "%s", doc["state"] | "");
  snprintf(weather.label, sizeof(weather.label), "%s", conditionLabel(weather.condition));

  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  if (a["temperature"].is<float>() || a["temperature"].is<int>()) {
    weather.temp = a["temperature"].as<float>();
    weather.hasTemp = true;
  }
  if (a["wind_speed"].is<float>() || a["wind_speed"].is<int>()) {
    weather.wind = a["wind_speed"].as<float>();
    weather.hasWind = true;
  }
  snprintf(weather.windUnit, sizeof(weather.windUnit), "%s", a["wind_speed_unit"] | "km/h");
  if (a["humidity"].is<float>() || a["humidity"].is<int>()) {
    weather.humidity = a["humidity"].as<int>();
    weather.hasHumidity = true;
  }
  if (a["uv_index"].is<float>() || a["uv_index"].is<int>()) {
    weather.uv = a["uv_index"].as<float>();
    weather.hasUv = true;
  }

  weather.ok = weather.condition[0] != 0;
  if (!weather.ok) snprintf(weather.status, sizeof(weather.status), "no state");
  return weather.ok;
}

inline bool fetchWeather(const char* host, uint16_t port, const char* token,
                         const char* entity) {
  weather = Weather{};
  if (!entity || !*entity) {
    snprintf(weather.status, sizeof(weather.status), "no weather entity");
    return false;
  }

  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);

  // Weather entities carry a big `forecast` array we don't need — filter the
  // parse down to the current-conditions fields.
  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["temperature"] = true;
  filter["attributes"]["humidity"] = true;
  filter["attributes"]["wind_speed"] = true;
  filter["attributes"]["wind_speed_unit"] = true;
  filter["attributes"]["uv_index"] = true;

  JsonDocument doc;
  char date[40] = "";
  if (!httpjson::get(host, port, path, token, doc, weather.status, sizeof(weather.status),
                     date, sizeof(date), &filter)) {
    return false;
  }
  if (date[0]) parseHttpDate(date);
  return applyWeather(doc.as<JsonVariantConst>());
}

// Parse one climate entity's state object into `climate`. Shared by the direct fetch below and the
// server's /state response (app/data_refresh.h).
inline bool applyClimate(JsonVariantConst doc) {
  climate = Climate{};

  snprintf(climate.mode, sizeof(climate.mode), "%s", doc["state"] | "");

  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  if (a["current_temperature"].is<float>() || a["current_temperature"].is<int>()) {
    climate.temp = a["current_temperature"].as<float>();
    climate.hasTemp = true;
  }
  if (a["temperature"].is<float>() || a["temperature"].is<int>()) {
    climate.target = a["temperature"].as<float>();
    climate.hasTarget = true;
  }
  if (a["min_temp"].is<float>() || a["min_temp"].is<int>()) climate.minTemp = a["min_temp"].as<float>();
  if (a["max_temp"].is<float>() || a["max_temp"].is<int>()) climate.maxTemp = a["max_temp"].as<float>();
  if (a["target_temp_step"].is<float>() || a["target_temp_step"].is<int>())
    climate.tempStep = a["target_temp_step"].as<float>();
  for (JsonVariantConst m : a["hvac_modes"].as<JsonArrayConst>()) {
    const char* s = m.as<const char*>();
    if (s && *s && climate.modeCount < 8)
      snprintf(climate.modes[climate.modeCount++], 16, "%s", s);
  }

  climate.ok = climate.hasTemp || climate.hasTarget;
  if (!climate.ok) snprintf(climate.status, sizeof(climate.status), "no temperature data");
  return climate.ok;
}

inline bool fetchClimate(const char* host, uint16_t port, const char* token,
                         const char* entity) {
  climate = Climate{};
  if (!entity || !*entity) {
    snprintf(climate.status, sizeof(climate.status), "no climate entity");
    return false;
  }

  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);

  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["current_temperature"] = true;
  filter["attributes"]["temperature"] = true;
  filter["attributes"]["min_temp"] = true;
  filter["attributes"]["max_temp"] = true;
  filter["attributes"]["target_temp_step"] = true;
  filter["attributes"]["hvac_modes"] = true;

  JsonDocument doc;
  if (!httpjson::get(host, port, path, token, doc, climate.status, sizeof(climate.status),
                     nullptr, 0, &filter)) {
    return false;
  }
  return applyClimate(doc.as<JsonVariantConst>());
}

// --- climate service calls (POST /api/services/climate/*) --------------
// The response is a state list we don't need; HTTP 2xx == the call landed.
inline bool setClimateTemperature(const char* host, uint16_t port, const char* token,
                                  const char* entity, float temp) {
  if (!entity || !*entity) return false;
  char body[96];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"temperature\":%.1f}", entity,
           static_cast<double>(temp));
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/climate/set_temperature", token, body, doc, st,
                        sizeof(st), &keepNothing);
}
inline bool setClimateHvacMode(const char* host, uint16_t port, const char* token,
                               const char* entity, const char* mode) {
  if (!entity || !*entity || !mode || !*mode) return false;
  char body[96];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"hvac_mode\":\"%s\"}", entity, mode);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/climate/set_hvac_mode", token, body, doc, st,
                        sizeof(st), &keepNothing);
}

// --- lights ------------------------------------------------------------
// Parse the group light's state object into `lightGroup`. Shared by the direct fetch below and the
// server's /state response (app/data_refresh.h).
inline bool applyLight(JsonVariantConst doc) {
  lightGroup = Light{};
  const char* st = doc["state"] | "";
  lightGroup.on = strcmp(st, "on") == 0;
  JsonVariantConst b = doc["attributes"]["brightness"];
  if (b.is<int>() || b.is<float>()) {
    lightGroup.brightnessPct = static_cast<int>((b.as<float>() * 100.0f / 255.0f) + 0.5f);
    lightGroup.hasBrightness = true;
  }
  lightGroup.ok = st[0] != 0;
  if (!lightGroup.ok) snprintf(lightGroup.status, sizeof(lightGroup.status), "no state");
  return lightGroup.ok;
}

inline bool fetchLight(const char* host, uint16_t port, const char* token, const char* entity) {
  lightGroup = Light{};
  if (!entity || !*entity) {
    snprintf(lightGroup.status, sizeof(lightGroup.status), "no light entity");
    return false;
  }
  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);

  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["brightness"] = true;

  JsonDocument doc;
  if (!httpjson::get(host, port, path, token, doc, lightGroup.status, sizeof(lightGroup.status),
                     nullptr, 0, &filter)) {
    return false;
  }
  return applyLight(doc.as<JsonVariantConst>());
}

// `on` from a state object ("on" -> true). False (and `on` untouched) if it
// carries no state. Shared by the direct fetches and the server's /state.
inline bool applyOnOff(JsonVariantConst doc, bool& on) {
  const char* s = doc["state"] | "";
  if (!s[0]) return false;
  on = strcmp(s, "on") == 0;
  return true;
}

// Just the on/off state of one individual light entity — for the Lighting
// page's Lights tab, which shows a bulb icon per chip. Deliberately lighter
// than fetchLight(): no brightness, so a whole individual-lights tab (up to
// six entities) costs one small GET each rather than the full state parse.
inline bool fetchLightOn(const char* host, uint16_t port, const char* token, const char* entity,
                         bool& on) {
  on = false;
  if (!entity || !*entity) return false;
  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);
  JsonDocument filter;
  filter["state"] = true;
  JsonDocument doc;
  char st[48];
  if (!httpjson::get(host, port, path, token, doc, st, sizeof(st), nullptr, 0, &filter)) return false;
  return applyOnOff(doc.as<JsonVariantConst>(), on);
}

// A numeric sensor state (HA sends it as a string) -> `value`. False if
// missing/unknown/unavailable or not numeric.
inline bool applySensorValue(JsonVariantConst doc, float& value) {
  value = 0;
  const char* s = doc["state"] | "";
  if (!s[0] || !strcmp(s, "unknown") || !strcmp(s, "unavailable")) return false;
  char* end = nullptr;
  const float v = strtod(s, &end);
  if (end == s) return false;  // not numeric
  value = v;
  return true;
}

// A plain numeric sensor reading (e.g. a temperature sensor's own entity) —
// for the Climate page's additional-sensors footer. Home Assistant reports a
// sensor's `state` as a JSON string even when it's numeric, so this parses
// it with strtod rather than expecting ArduinoJson to coerce it.
inline bool fetchSensorValue(const char* host, uint16_t port, const char* token, const char* entity,
                             float& value) {
  value = 0;
  if (!entity || !*entity) return false;
  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);
  JsonDocument filter;
  filter["state"] = true;
  JsonDocument doc;
  char st[48];
  if (!httpjson::get(host, port, path, token, doc, st, sizeof(st), nullptr, 0, &filter)) return false;
  return applySensorValue(doc.as<JsonVariantConst>(), value);
}

// Generic "call this HA service on this entity" (scene.turn_on, light.toggle).
inline bool callService(const char* host, uint16_t port, const char* token, const char* domain,
                        const char* service, const char* entity) {
  if (!entity || !*entity) return false;
  char path[80];
  snprintf(path, sizeof(path), "/api/services/%s/%s", domain, service);
  char body[80];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, path, token, body, doc, st, sizeof(st), &keepNothing);
}

inline bool setLightOn(const char* host, uint16_t port, const char* token, const char* entity,
                       bool on) {
  if (!entity || !*entity) return false;
  char body[80];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, on ? "/api/services/light/turn_on" : "/api/services/light/turn_off",
                        token, body, doc, st, sizeof(st), &keepNothing);
}
inline bool setLightColorTempKelvin(const char* host, uint16_t port, const char* token,
                                    const char* entity, int kelvin) {
  if (!entity || !*entity) return false;
  char body[110];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"color_temp_kelvin\":%d}", entity, kelvin);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/light/turn_on", token, body, doc, st, sizeof(st),
                        &keepNothing);
}
inline bool setLightBrightness(const char* host, uint16_t port, const char* token, const char* entity,
                               int pct) {
  if (!entity || !*entity) return false;
  if (pct < 1) return setLightOn(host, port, token, entity, false);
  if (pct > 100) pct = 100;
  char body[96];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"brightness_pct\":%d}", entity, pct);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/light/turn_on", token, body, doc, st, sizeof(st),
                        &keepNothing);
}

// --- covers (blinds) -------------------------------------------------
// A cover's state object -> `out`. Shared by the direct fetches and the
// server's /state response.
inline bool applyCover(JsonVariantConst doc, Cover& out) {
  out = Cover{};
  const char* st = doc["state"] | "";
  snprintf(out.state, sizeof(out.state), "%s", st);
  JsonVariantConst pos = doc["attributes"]["current_position"];
  if (pos.is<int>() || pos.is<float>()) {
    int v = static_cast<int>(pos.as<float>() + 0.5f);
    out.position = v < 0 ? 0 : (v > 100 ? 100 : v);
    out.hasPosition = true;
  }
  out.ok = st[0] != 0;
  if (!out.ok) snprintf(out.status, sizeof(out.status), "no state");
  return out.ok;
}

inline bool fetchCoverInto(const char* host, uint16_t port, const char* token, const char* entity,
                           Cover& out) {
  out = Cover{};
  if (!entity || !*entity) {
    snprintf(out.status, sizeof(out.status), "no cover entity");
    return false;
  }
  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);

  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["current_position"] = true;

  JsonDocument doc;
  if (!httpjson::get(host, port, path, token, doc, out.status, sizeof(out.status), nullptr, 0,
                     &filter)) {
    return false;
  }
  return applyCover(doc.as<JsonVariantConst>(), out);
}
inline bool fetchCover(const char* host, uint16_t port, const char* token, const char* entity) {
  return fetchCoverInto(host, port, token, entity, cover);
}
// One entry of coverItems[] (0 or 1) — the Blinds page's two-panel layout.
inline bool fetchCoverItem(const char* host, uint16_t port, const char* token, const char* entity,
                           int idx) {
  if (idx < 0 || idx >= 2) return false;
  return fetchCoverInto(host, port, token, entity, coverItems[idx]);
}

inline bool setCoverPosition(const char* host, uint16_t port, const char* token, const char* entity,
                             int pct) {
  if (!entity || !*entity) return false;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  char body[96];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"position\":%d}", entity, pct);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/cover/set_cover_position", token, body, doc, st,
                        sizeof(st), &keepNothing);
}

// --- media player (Music page) ------------------------------------------
// Play/pause/next/previous all go through the generic callService() above
// (media_player.media_play_pause / media_next_track / media_previous_track);
// only volume and mute need their own bodies (a level / a bool), so those two
// get dedicated helpers, same split as the light brightness/on-off pair.
// The Music player's state object -> `media`, keeping the last known
// volume/mute when this report omits them (see fetchMedia()). Shared by the
// direct fetch and the server's /state response.
inline bool applyMedia(JsonVariantConst doc) {
  const bool prevHasVolume = media.hasVolume, prevHasMuted = media.hasMuted, prevMuted = media.muted;
  const int prevVolumePct = media.volumePct;
  media = MediaPlayer{};
  media.hasVolume = prevHasVolume;
  media.volumePct = prevVolumePct;
  media.hasMuted = prevHasMuted;
  media.muted = prevMuted;
  const char* st = doc["state"] | "";
  snprintf(media.state, sizeof(media.state), "%s", st);
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  snprintf(media.title, sizeof(media.title), "%s", a["media_title"] | "");
  snprintf(media.artist, sizeof(media.artist), "%s", a["media_artist"] | "");
  snprintf(media.picture, sizeof(media.picture), "%s", a["entity_picture"] | "");
  JsonVariantConst vol = a["volume_level"];
  if (vol.is<float>() || vol.is<int>()) {
    media.volumePct = static_cast<int>(vol.as<float>() * 100.0f + 0.5f);
    media.hasVolume = true;
  }
  if (a["is_volume_muted"].is<bool>()) {
    media.muted = a["is_volume_muted"].as<bool>();
    media.hasMuted = true;
  }
  media.ok = st[0] != 0;
  if (!media.ok) snprintf(media.status, sizeof(media.status), "no state");
  return media.ok;
}

inline bool fetchMedia(const char* host, uint16_t port, const char* token, const char* entity) {
  // Carry the last known volume/mute across fetches: some media_player
  // integrations only report volume_level/is_volume_muted while actively
  // playing, and hiding the volume row just because playback paused (rather
  // than showing the last-known level) would be more confusing, not less.
  const bool prevHasVolume = media.hasVolume, prevHasMuted = media.hasMuted, prevMuted = media.muted;
  const int prevVolumePct = media.volumePct;
  media = MediaPlayer{};
  if (!entity || !*entity) {
    snprintf(media.status, sizeof(media.status), "no media entity");
    return false;
  }
  media.hasVolume = prevHasVolume;
  media.volumePct = prevVolumePct;
  media.hasMuted = prevHasMuted;
  media.muted = prevMuted;
  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);

  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["media_title"] = true;
  filter["attributes"]["media_artist"] = true;
  filter["attributes"]["entity_picture"] = true;
  filter["attributes"]["volume_level"] = true;
  filter["attributes"]["is_volume_muted"] = true;

  JsonDocument doc;
  if (!httpjson::get(host, port, path, token, doc, media.status, sizeof(media.status), nullptr, 0,
                     &filter)) {
    return false;
  }
  return applyMedia(doc.as<JsonVariantConst>());
}

// Same body as fetchMedia() above, into xboxMedia instead of media — kept as
// its own copy rather than a shared helper taking an output ref, matching
// this file's existing convention for a second instance of a similar entity
// (fetchCover/fetchCoverItem). No volume/mute carry-over here: the Xbox
// page's hero doesn't expose a volume row, so there's nothing to preserve
// across a state where the console stops reporting it.
// Parse the Xbox media_player's state object into `xboxMedia`. Shared by the direct fetch below and the
// server's /state response (app/data_refresh.h).
inline bool applyXboxMedia(JsonVariantConst doc) {
  xboxMedia = MediaPlayer{};
  const char* st = doc["state"] | "";
  snprintf(xboxMedia.state, sizeof(xboxMedia.state), "%s", st);
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  snprintf(xboxMedia.title, sizeof(xboxMedia.title), "%s", a["media_title"] | "");
  snprintf(xboxMedia.picture, sizeof(xboxMedia.picture), "%s", a["entity_picture"] | "");
  xboxMedia.ok = st[0] != 0;
  if (!xboxMedia.ok) snprintf(xboxMedia.status, sizeof(xboxMedia.status), "no state");
  return xboxMedia.ok;
}

inline bool fetchXboxMedia(const char* host, uint16_t port, const char* token, const char* entity) {
  xboxMedia = MediaPlayer{};
  if (!entity || !*entity) {
    snprintf(xboxMedia.status, sizeof(xboxMedia.status), "no media entity");
    return false;
  }
  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);

  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["media_title"] = true;
  filter["attributes"]["entity_picture"] = true;

  JsonDocument doc;
  if (!httpjson::get(host, port, path, token, doc, xboxMedia.status, sizeof(xboxMedia.status), nullptr,
                     0, &filter)) {
    return false;
  }
  return applyXboxMedia(doc.as<JsonVariantConst>());
}

inline bool setMediaVolume(const char* host, uint16_t port, const char* token, const char* entity,
                           int pct) {
  if (!entity || !*entity) return false;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  char body[96];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"volume_level\":%.2f}", entity,
           static_cast<double>(pct) / 100.0);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/media_player/volume_set", token, body, doc, st,
                        sizeof(st), &keepNothing);
}
inline bool setMediaMute(const char* host, uint16_t port, const char* token, const char* entity,
                         bool mute) {
  if (!entity || !*entity) return false;
  char body[96];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"is_volume_muted\":%s}", entity,
           mute ? "true" : "false");
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/media_player/volume_mute", token, body, doc, st,
                        sizeof(st), &keepNothing);
}

// --- TV remote (TV carousel page) ---------------------------------------
// A `remote.*` entity's send_command (dpad/back/home/power/volume/mute — the
// Android TV Remote integration's own keyevent vocabulary, e.g. "DPAD_UP",
// "HOME", "VOLUME_UP") and a `media_player.*` entity's select_source (app
// launch, source = the app's package name, per deviceconfig::tvApps[]).
inline bool sendRemoteCommand(const char* host, uint16_t port, const char* token,
                              const char* entity, const char* command) {
  if (!entity || !*entity || !command || !*command) return false;
  char body[96];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"command\":\"%s\"}", entity, command);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/remote/send_command", token, body, doc, st,
                        sizeof(st), &keepNothing);
}
inline bool selectMediaSource(const char* host, uint16_t port, const char* token,
                              const char* entity, const char* source) {
  if (!entity || !*entity || !source || !*source) return false;
  char body[128];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"source\":\"%s\"}", entity, source);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/media_player/select_source", token, body, doc,
                        st, sizeof(st), &keepNothing);
}

// Launch an app by package name — the TV page's app row. The Android TV
// Remote integration (the one this file's remote.send_command keyevents
// target) launches apps via media_player.play_media with content type
// "app", NOT media_player.select_source (that's the older ADB-based
// "Android Debug Bridge" integration's mechanism, which expects a source
// name rather than a raw package id).
inline bool launchApp(const char* host, uint16_t port, const char* token, const char* entity,
                      const char* pkg) {
  if (!entity || !*entity || !pkg || !*pkg) return false;
  char body[192];
  snprintf(body, sizeof(body),
          "{\"entity_id\":\"%s\",\"media_content_id\":\"%s\",\"media_content_type\":\"app\"}", entity,
          pkg);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/media_player/play_media", token, body, doc, st,
                        sizeof(st), &keepNothing);
}

// --- Xbox (Xbox carousel page) -------------------------------------------
// remote.turn_on / remote.turn_off power the console — the Xbox integration's
// remote entity supports real on/off (unlike TV's Android remote, which only
// exposes a POWER keyevent toggle via send_command).
inline bool setXboxPower(const char* host, uint16_t port, const char* token, const char* entity,
                         bool on) {
  return callService(host, port, token, "remote", on ? "turn_on" : "turn_off", entity);
}

// Launch a game by product ID. Content type "app" mirrors launchApp() above
// (the closest confirmed precedent in this codebase for "launch by id via
// play_media") — NOT independently confirmed against the Xbox integration's
// own browse_media response, which is the reliable way to get the exact
// media_content_type it expects; there's no browse_media client here to
// check it against (HA's browse_media is a WebSocket-only call, and this
// app only speaks the REST API). If launches don't register on real
// hardware, capturing one real browse_media child (e.g. via HA's dev tools)
// and comparing its media_content_type is the first thing to check.
inline bool launchXboxGame(const char* host, uint16_t port, const char* token, const char* entity,
                           const char* productId) {
  if (!entity || !*entity || !productId || !*productId) return false;
  char body[192];
  snprintf(body, sizeof(body),
          "{\"entity_id\":\"%s\",\"media_content_id\":\"%s\",\"media_content_type\":\"app\"}", entity,
          productId);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, "/api/services/media_player/play_media", token, body, doc, st,
                        sizeof(st), &keepNothing);
}

// Short air-quality band for a US-EPA-style AQI number.
inline const char* aqiCategory(int aqi) {
  if (aqi <= 50) return "Good";
  if (aqi <= 100) return "Moderate";
  if (aqi <= 150) return "Poor";
  if (aqi <= 200) return "Unhealthy";
  if (aqi <= 300) return "Very poor";
  return "Hazardous";
}

// Air-quality index from a plain sensor entity. The state is usually the AQI
// number; some sensors report a word ("Good") or stash the number in an
// attribute. Optional — a blank entity just returns false.
// Parse an air-quality sensor's state object into `air`. Shared by the direct fetch below and the
// server's /state response (app/data_refresh.h).
inline bool applyAir(JsonVariantConst doc) {
  air = Air{};

  JsonVariantConst st = doc["state"];
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  if (st.is<int>() || st.is<float>()) {
    air.index = st.as<int>();
    air.has = true;
  } else if (a["air_quality_index"].is<int>() || a["air_quality_index"].is<float>()) {
    air.index = a["air_quality_index"].as<int>();
    air.has = true;
  } else if (a["aqi"].is<int>() || a["aqi"].is<float>()) {
    air.index = a["aqi"].as<int>();
    air.has = true;
  }

  const char* word = st.is<const char*>() ? st.as<const char*>() : nullptr;
  if (air.has) {
    snprintf(air.category, sizeof(air.category), "%s", aqiCategory(air.index));
    air.ok = true;
  } else if (word && *word && strcmp(word, "unknown") != 0 && strcmp(word, "unavailable") != 0) {
    snprintf(air.category, sizeof(air.category), "%s", word);
    air.ok = true;
  } else {
    snprintf(air.status, sizeof(air.status), "no reading");
  }
  return air.ok;
}

inline bool fetchAir(const char* host, uint16_t port, const char* token, const char* entity) {
  air = Air{};
  if (!entity || !*entity) {
    snprintf(air.status, sizeof(air.status), "no air entity");
    return false;
  }

  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);

  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["air_quality_index"] = true;
  filter["attributes"]["aqi"] = true;

  JsonDocument doc;
  if (!httpjson::get(host, port, path, token, doc, air.status, sizeof(air.status), nullptr, 0,
                     &filter)) {
    return false;
  }
  return applyAir(doc.as<JsonVariantConst>());
}

// 3-day outlook via the weather.get_forecasts service (the current HA way —
// forecast data left the entity attributes in 2024). `weatherEntity` is the
// same entity fetchWeather() uses.
inline bool applyForecast(JsonArrayConst days);  // below

inline bool fetchForecast(const char* host, uint16_t port, const char* token,
                          const char* weatherEntity) {
  forecast = Forecast{};
  if (!weatherEntity || !*weatherEntity) {
    snprintf(forecast.status, sizeof(forecast.status), "no weather entity");
    return false;
  }

  char body[128];
  snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"type\":\"daily\"}", weatherEntity);

  JsonDocument filter;
  JsonArray fArr =
      filter["service_response"][weatherEntity]["forecast"].to<JsonArray>();
  JsonObject fe = fArr.add<JsonObject>();
  fe["datetime"] = true;
  fe["condition"] = true;
  fe["temperature"] = true;
  fe["templow"] = true;

  JsonDocument doc;
  if (!httpjson::post(host, port, "/api/services/weather/get_forecasts?return_response", token,
                      body, doc, forecast.status, sizeof(forecast.status), &filter)) {
    return false;
  }

  return applyForecast(doc["service_response"][weatherEntity]["forecast"].as<JsonArrayConst>());
}

// A daily forecast array (weather.get_forecasts' per-entity `forecast`) ->
// `forecast`, first three days. Shared by the direct service call above and
// the server's /state response.
inline bool applyForecast(JsonArrayConst days) {
  forecast = Forecast{};
  static const char* kAbbr[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  for (JsonVariantConst d : days) {
    if (forecast.count >= 3) break;
    ForecastDay& fd = forecast.day[forecast.count];

    int y = 0, m = 0, dd = 0;
    if (sscanf(d["datetime"] | "", "%d-%d-%d", &y, &m, &dd) == 3 && y > 1970)
      snprintf(fd.dow, sizeof(fd.dow), "%s", kAbbr[dayOfWeek(y, m, dd)]);

    snprintf(fd.condition, sizeof(fd.condition), "%s", d["condition"] | "");
    if (d["temperature"].is<float>() || d["temperature"].is<int>()) {
      fd.hi = d["temperature"].as<float>();
      fd.hasHi = true;
    }
    if (d["templow"].is<float>() || d["templow"].is<int>()) {
      fd.lo = d["templow"].as<float>();
      fd.hasLo = true;
    }
    forecast.count++;
  }

  forecast.ok = forecast.count > 0;
  if (!forecast.ok) snprintf(forecast.status, sizeof(forecast.status), "no forecast data");
  return forecast.ok;
}

// --- Quick Access hub (app/quick_access.h's Home-key jump list) --------------------
// Generic, entity-domain-driven service calls for hub.items[]'s per-button
// quick action — unlike every other page here, the hub doesn't know its
// entities' domains ahead of time (they're server config, not a fixed
// per-page entity), so these infer the service from the entity_id prefix
// instead of a page-specific hand-written call.
inline void domainOf(const char* entity, char* out, size_t outCap) {
  out[0] = 0;
  if (!entity) return;
  const char* dot = strchr(entity, '.');
  const size_t n = dot ? static_cast<size_t>(dot - entity) : strlen(entity);
  snprintf(out, outCap, "%.*s", static_cast<int>(n < outCap - 1 ? n : outCap - 1), entity);
}

// A hub toggle button's live state, keyed generically off `state == "on"` —
// covers light/switch/input_boolean/fan/media_player, the common toggleable
// domains. `out` is left unchanged on failure (caller keeps showing the last
// known state rather than flipping to "off" on a transient fetch error).
inline bool fetchHubToggleState(const char* host, uint16_t port, const char* token,
                                const char* entity, bool& out) {
  if (!entity || !*entity) return false;
  char path[96];
  snprintf(path, sizeof(path), "/api/states/%s", entity);
  JsonDocument filter;
  filter["state"] = true;
  JsonDocument doc;
  char status[48];
  if (!httpjson::get(host, port, path, token, doc, status, sizeof(status), nullptr, 0, &filter))
    return false;
  return applyOnOff(doc.as<JsonVariantConst>(), out);
}

// Toggle-type quick action: always <domain>.turn_on / <domain>.turn_off —
// every common toggleable domain (light, switch, input_boolean, fan,
// media_player, humidifier, siren) follows this pair, so there's no
// per-direction override to plumb through hub.items[]'s single `service`
// field (that field is for `run` actions below, a single explicit call).
inline bool hubToggle(const char* host, uint16_t port, const char* token, const char* entity, bool on) {
  if (!entity || !*entity) return false;
  char domain[24];
  domainOf(entity, domain, sizeof(domain));
  if (!domain[0]) return false;
  return callService(host, port, token, domain, on ? "turn_on" : "turn_off", entity);
}

// Run-type quick action: the explicit `service` ("domain.service") if the
// config gave one, else scene.turn_on / script.turn_on inferred from the
// entity's own domain (the two run-style domains this covers without a
// config-side override) — anything else falls back to <domain>.turn_on,
// the same guess launchXboxGame's neighbor helpers make elsewhere in this
// file when there's no better information. `dataFields`, if non-empty, is a
// raw `"key":value,...` fragment (device_config_client.h's HubItem::
// actionData) spliced into the body alongside entity_id.
inline bool hubRun(const char* host, uint16_t port, const char* token, const char* entity,
                   const char* service, const char* dataFields) {
  if (!entity || !*entity) return false;
  char domain[24], svc[32];
  domainOf(entity, domain, sizeof(domain));
  if (service && *service) {
    const char* dot = strchr(service, '.');
    if (dot) {
      snprintf(domain, sizeof(domain), "%.*s", static_cast<int>(dot - service), service);
      snprintf(svc, sizeof(svc), "%s", dot + 1);
    } else {
      snprintf(svc, sizeof(svc), "%s", service);
    }
  } else if (!strcmp(domain, "scene") || !strcmp(domain, "script")) {
    snprintf(svc, sizeof(svc), "turn_on");
  } else {
    snprintf(svc, sizeof(svc), "turn_on");
  }
  if (!domain[0]) return false;
  char path[80];
  snprintf(path, sizeof(path), "/api/services/%s/%s", domain, svc);
  char body[192];
  if (dataFields && *dataFields)
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",%s}", entity, dataFields);
  else
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity);
  JsonDocument doc, keepNothing;
  char st[48];
  return httpjson::post(host, port, path, token, body, doc, st, sizeof(st), &keepNothing);
}

}  // namespace haclient
