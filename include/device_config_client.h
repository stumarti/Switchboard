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
#include "pairing_client.h"
#include "sd_cache.h"

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
// Settings -> Select room (screen_room_pick.h) is an explicit user choice -
// once made, it wins over anything the server assigns by MAC (see
// applyServerAssignedSlug below), same "local override beats server default"
// precedent as localsettings::refreshOverrideMin elsewhere in this file.
inline bool slugUserPicked = false;
inline void loadSlugUserPicked() {
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/true)) {
    slugUserPicked = p.getBool("slugPicked", false);
    p.end();
  }
}
inline void saveSlug(const char* s) {
  if (!s || !*s) return;
  snprintf(activeSlug, sizeof(activeSlug), "%s", s);
  slugUserPicked = true;
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.putString("slug", s);
    p.putBool("slugPicked", true);
    p.end();
  }
}
// Called once, right after a successful pairing approval (screen_pairing.h),
// with the room the admin assigned this device's MAC on the Remotes page.
// A no-op once the user has picked a room by hand on the device itself.
inline void applyServerAssignedSlug(const char* s) {
  if (!s || !*s || slugUserPicked) return;
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
  slugUserPicked = false;
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.remove("slug");
    p.remove("slugPicked");
    p.end();
  }
}

inline char name[32] = "";
inline char weatherEntity[64] = "";
inline char climateEntity[64] = "";
inline char airQualityEntity[64] = "";
inline uint16_t refreshIntervalMin = 30;
// standby.refreshAligned: timer wakes land on the clock (every 30 min =
// :00 and :30 local time) instead of 30 min after the last sleep, each
// remote standby.refreshStaggerSec later than the mark so a houseful of
// remotes doesn't ask the server at once. standby.utcOffsetMin is the
// server's local time zone right now (the remote's clock is UTC).
// See refresh_schedule.h.
inline bool refreshAligned = false;
inline int16_t utcOffsetMin = 0;
inline uint16_t refreshStaggerSec = 0;

// firmware.* — over-the-air updates, from the server's Settings -> Remote
// updates (off unless switched on there). fwOffer* is what the server wants
// this remote to install now (empty = nothing); ota_update.h asks again
// before installing. fwFromHour/fwToHour: the scheduled window in local
// time, -1 = no schedule.
inline bool fwEnabled = false;
inline bool fwButton = false;
inline int8_t fwFromHour = -1;
inline int8_t fwToHour = -1;
inline uint8_t fwMinBattery = 30;
inline char fwOfferVersion[48] = "";
inline uint32_t fwOfferSize = 0;
inline char fwOfferSha[65] = "";

// lighting.group — the room's main light (Lighting carousel page).
inline bool lightGroupEnabled = false;
inline char lightGroupName[32] = "";
inline char lightGroupEntity[64] = "";
inline bool lightGroupBrightness = false;
inline bool lightGroupColorTemp = false;
inline bool lightGroupColor = false;    // RGB colour presets (the Colour tab)
inline bool lightGroupEffects = false;  // the light's own effects (the Colour tab)

// lighting.lights[] / lighting.scenes[] — individual lights and scenes.
// Also reused as-is by blinds.items[] and climate.additionalSensors[] below
// (same name+entity shape) — `icon` is meaningful for the first three
// (chip icon override, resolved via mdi_icon.h) and simply unused/blank for
// climate sensors. Sized generously (47 usable chars) for the longest real
// MDI icon names (e.g. "microsoft-xbox-controller-battery-alert").
struct LightItem {
  char name[20] = "";
  char entity[52] = "";
  char icon[48] = "";
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
// app row: tv.appList (name, launch value, optional MDI icon) from a current
// server, else the older name -> package map tv.apps.
inline char tvMediaEntity[64] = "";
inline char tvRemoteEntity[64] = "";
struct AppItem {
  char name[20] = "";
  char pkg[52] = "";
  char icon[32] = "";  // an MDI name (mdi_icon.h), blank = the built-in glyph
};
// Capped at 4 (not the other lists' 6) — the TV page's app grid fits four.
inline constexpr int kMaxTvApps = 4;
inline AppItem tvApps[kMaxTvApps];
inline int tvAppCount = 0;

// receiver.* — an Enigma2 satellite/cable box (Receiver carousel page),
// through Home Assistant's enigma2 media_player: channel up/down are its
// next/previous track, a favourite channel is a select_source, plus volume,
// mute and power. Up to six favourites, each with an optional MDI icon.
inline char receiverName[24] = "Receiver";
inline char receiverEntity[64] = "";
struct ReceiverChannel {
  char name[24] = "";    // the button's label
  char source[48] = "";  // the channel as the box lists it (select_source)
  char icon[32] = "";    // an MDI name, blank = none
};
inline constexpr int kMaxReceiverChannels = 6;
inline ReceiverChannel receiverChannels[kMaxReceiverChannels];
inline int receiverChannelCount = 0;

// xbox.* — the room's Xbox console (Xbox carousel page): a `media_player.*`
// entity for the running-game hero + launching titles, a `remote.*` entity
// for power (remote.turn_on/turn_off — unlike TV's Android remote, the Xbox
// integration's remote entity supports real on/off, not just keyevents), and
// the configured game library (xbox.games[], a JSON array — the reliable
// list; a future browse_media-sourced list would replace this array without
// changing anything below screen_xbox.h reads it).
inline char xboxMediaEntity[64] = "";
inline char xboxRemoteEntity[64] = "";
struct XboxGame {
  char name[40] = "";
  char productId[48] = "";  // media_content_id for media_player.play_media
  char art[128] = "";       // box-art URL (relative to the HA host, or absolute)
};
// Capped at 36 — six pages of the Xbox library (6 rows/page); a "Browse"
// library from the server can list a whole console's installed games.
inline constexpr int kMaxXboxGames = 36;
inline XboxGame xboxGames[kMaxXboxGames];
inline int xboxGameCount = 0;

// hub.* — the Quick Access hub (app/quick_access.h's Home-key jump list): an ordered,
// paged grid of launcher buttons, each optionally paired with a bottom-strip
// quick action. Only `target` strings this app has a real screen for do
// anything on tap — 'media'->Music, 'climate'->Climate, 'lighting'->
// Lighting, 'blinds'->Blinds, 'tv'->TV, 'xbox'->Xbox, 'guestwifi'->the Wifi
// networks page. Anything else (e.g. 'vacuum' — no vacuum screen exists in
// this app) falls back to the jump list's own "Settings" destination rather
// than silently doing nothing on tap; see hubTargetPage() in app/quick_access.h.
// hubItemCount == 0 (no "hub" key, or an empty items[]) keeps app/quick_access.h's
// fixed, hardcoded kJumpItems grid — this is additive, not a replacement,
// for any server config that hasn't been migrated to send hub.items[] yet.
enum class HubAction : uint8_t { None, Toggle, Run };
struct HubItem {
  char name[24] = "";
  // Optional MDI icon name (admin UI's icon picker), resolved via
  // mdi_icon.h. Empty -> app/quick_access.h falls back to the fixed jump-grid icon for
  // this item's `target` (hubIconFor()), same look as before this field
  // existed.
  char icon[48] = "";
  char target[16] = "";
  HubAction actionType = HubAction::None;
  char actionEntity[64] = "";
  // Optional explicit "domain.service" (e.g. "cover.open_cover"); empty ->
  // inferred from actionEntity's domain (see ha_client.h's hubToggle/hubRun).
  char actionService[32] = "";
  // Optional extra service-call fields as a raw JSON object, e.g.
  // {"brightness_pct":50} — spliced into the call body verbatim. Only
  // string/number/bool members round-trip (ArduinoJson's serializeJson of
  // whatever the source sent); anything nested is dropped rather than risk
  // sending a malformed body.
  char actionData[96] = "";
};
// Two pages' worth (10/page) — matches the jump grid's own fixed layout.
inline constexpr int kMaxHubItems = 20;
inline HubItem hubItems[kMaxHubItems];
inline int hubItemCount = 0;
// hub.quickActionsEnabled — the room's switch for the buttons' bottom-third
// quick action. Off: the whole button opens its page. (Settings -> Developer
// can still turn quick actions off on one remote, never back on.)
inline bool hubQuickActions = true;

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
inline bool screenReceiver = false;  // off unless the server says otherwise

// screens.order — the carousel's page order as set per remote in the admin
// UI (only the enabled pages, e.g. ["status","climate","lighting"]), as
// carousel page indices (kPageIds' positions, which match app/carousel.h's
// page numbering). Empty = the built-in order.
inline constexpr const char* kPageIds[] = {"status", "lighting", "blinds", "music", "tv",
                                           "xbox",   "wifi",     "climate", "receiver"};
inline constexpr uint8_t kPageIdCount = sizeof(kPageIds) / sizeof(kPageIds[0]);
inline uint8_t pageOrder[kPageIdCount];
inline uint8_t pageOrderCount = 0;

// A hub action's service data -> the raw `"key":value,...` fragment
// ha_client.h's hubRun() splices in beside entity_id. The server stores it
// as JSON text (what was typed in the editor); an object is taken as-is.
// Anything that isn't a JSON object, or doesn't fit, leaves it empty.
inline void parseActionData(JsonVariantConst data, char* out, size_t cap) {
  out[0] = 0;
  JsonDocument d;
  if (data.is<const char*>()) {
    if (deserializeJson(d, data.as<const char*>()) != DeserializationError::Ok) return;
  } else {
    d.set(data);
  }
  if (!d.is<JsonObjectConst>() || d.as<JsonObjectConst>().size() == 0) return;
  char buf[256];
  const size_t n = serializeJson(d, buf, sizeof(buf));
  if (n < 2 || n >= sizeof(buf) - 1 || n - 2 >= cap) return;  // "{...}" -> "..."
  memcpy(out, buf + 1, n - 2);
  out[n - 2] = 0;
}

inline void parsePageOrder(JsonArrayConst order) {
  pageOrderCount = 0;
  for (JsonVariantConst v : order) {
    const char* id = v.as<const char*>();
    if (!id) continue;
    for (uint8_t i = 0; i < kPageIdCount; ++i) {
      if (strcmp(id, kPageIds[i]) != 0) continue;
      bool dup = false;
      for (uint8_t j = 0; j < pageOrderCount; ++j) dup |= pageOrder[j] == i;
      if (!dup && pageOrderCount < kPageIdCount) pageOrder[pageOrderCount++] = i;
      break;
    }
  }
}

inline bool ok = false;
inline char status[64] = "";

// Every field back to its "no config" default.
inline void reset() {
  ok = false;
  name[0] = weatherEntity[0] = climateEntity[0] = airQualityEntity[0] = 0;
  refreshIntervalMin = localsettings::refreshOverrideMin ? localsettings::refreshOverrideMin : 30;
  refreshAligned = false;
  utcOffsetMin = 0;
  refreshStaggerSec = 0;
  fwEnabled = fwButton = false;
  fwFromHour = fwToHour = -1;
  fwMinBattery = 30;
  fwOfferVersion[0] = fwOfferSha[0] = 0;
  fwOfferSize = 0;
  lightGroupEnabled = lightGroupBrightness = lightGroupColorTemp = false;
  lightGroupColor = lightGroupEffects = false;
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
  screenReceiver = false;
  pageOrderCount = 0;
  tvMediaEntity[0] = tvRemoteEntity[0] = 0;
  tvAppCount = 0;
  xboxMediaEntity[0] = xboxRemoteEntity[0] = 0;
  xboxGameCount = 0;
  snprintf(receiverName, sizeof(receiverName), "%s", "Receiver");
  receiverEntity[0] = 0;
  receiverChannelCount = 0;
  hubItemCount = 0;
  hubQuickActions = true;
}

// Fill every field from a /api/devices/<slug>/config document — the same
// parse for a live fetch and for the copy cached on SD.
inline void applyJson(JsonVariantConst doc) {
  reset();
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
  refreshAligned = sb["refreshAligned"] | false;
  const int off = sb["utcOffsetMin"] | 0;
  utcOffsetMin = static_cast<int16_t>(off < -14 * 60 ? -14 * 60 : (off > 14 * 60 ? 14 * 60 : off));
  JsonObjectConst fw = doc["firmware"].as<JsonObjectConst>();
  fwEnabled = fw["enabled"] | false;
  if (fwEnabled) {
    fwButton = fw["button"] | false;
    JsonObjectConst win = fw["schedule"].as<JsonObjectConst>();
    if (!win.isNull()) {
      const int from = win["fromHour"] | -1, to = win["toHour"] | -1;
      if (from >= 0 && from <= 23 && to >= 0 && to <= 23) {
        fwFromHour = static_cast<int8_t>(from);
        fwToHour = static_cast<int8_t>(to);
      }
    }
    const int minBatt = fw["minBattery"] | 30;
    fwMinBattery = static_cast<uint8_t>(minBatt < 10 ? 10 : (minBatt > 90 ? 90 : minBatt));
    JsonObjectConst offer = fw["offer"].as<JsonObjectConst>();
    const char* ver = offer["version"] | "";
    const char* sha = offer["sha256"] | "";
    const uint32_t size = offer["size"] | 0u;
    // Only a complete offer: a version, a size and a 64-hex SHA-256.
    if (ver[0] && strlen(ver) < sizeof(fwOfferVersion) && size > 0 && strlen(sha) == 64) {
      snprintf(fwOfferVersion, sizeof(fwOfferVersion), "%s", ver);
      snprintf(fwOfferSha, sizeof(fwOfferSha), "%s", sha);
      fwOfferSize = size;
    }
  }
  const int stagger = sb["refreshStaggerSec"] | 0;
  // Any length: refresh_schedule.h wraps one longer than the interval.
  refreshStaggerSec = static_cast<uint16_t>(stagger < 0 ? 0 : (stagger > 65535 ? 65535 : stagger));

  JsonObjectConst lig = doc["lighting"].as<JsonObjectConst>();
  JsonObjectConst lg = lig["group"].as<JsonObjectConst>();
  lightGroupEnabled = lg["enabled"] | false;
  snprintf(lightGroupName, sizeof(lightGroupName), "%s", lg["name"] | "");
  snprintf(lightGroupEntity, sizeof(lightGroupEntity), "%s", lg["entity"] | "");
  lightGroupBrightness = lg["controls"]["brightness"] | false;
  lightGroupColorTemp = lg["controls"]["colorTemp"] | false;
  lightGroupColor = lg["controls"]["color"] | false;
  lightGroupEffects = lg["controls"]["effects"] | false;

  const auto fillItems = [](JsonArrayConst arr, LightItem* out, int& n, int cap) {
    for (JsonObjectConst o : arr) {
      if (n >= cap) break;
      const char* e = o["entity"] | "";
      if (!*e) continue;
      snprintf(out[n].entity, sizeof(out[n].entity), "%s", e);
      snprintf(out[n].name, sizeof(out[n].name), "%s", o["name"] | e);
      snprintf(out[n].icon, sizeof(out[n].icon), "%s", o["icon"] | "");
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
  screenReceiver = scr["receiver"] | false;
  parsePageOrder(scr["order"].as<JsonArrayConst>());

  JsonObjectConst tv = doc["tv"].as<JsonObjectConst>();
  snprintf(tvMediaEntity, sizeof(tvMediaEntity), "%s", tv["mediaPlayerEntity"] | "");
  snprintf(tvRemoteEntity, sizeof(tvRemoteEntity), "%s", tv["remoteEntity"] | "");
  if (tv["appList"].is<JsonArrayConst>()) {
    for (JsonObjectConst a : tv["appList"].as<JsonArrayConst>()) {
      if (tvAppCount >= kMaxTvApps) break;
      const char* pkg = a["launch"] | "";
      if (!*pkg) continue;
      AppItem& app = tvApps[tvAppCount];
      snprintf(app.name, sizeof(app.name), "%s", a["name"] | "");
      snprintf(app.pkg, sizeof(app.pkg), "%s", pkg);
      snprintf(app.icon, sizeof(app.icon), "%s", a["icon"] | "");
      ++tvAppCount;
    }
  } else {
    for (JsonPairConst kv : tv["apps"].as<JsonObjectConst>()) {
      if (tvAppCount >= kMaxTvApps) break;
      const char* pkg = kv.value().as<const char*>();
      if (!pkg || !*pkg) continue;
      AppItem& app = tvApps[tvAppCount];
      snprintf(app.name, sizeof(app.name), "%s", kv.key().c_str());
      snprintf(app.pkg, sizeof(app.pkg), "%s", pkg);
      app.icon[0] = 0;
      ++tvAppCount;
    }
  }

  JsonObjectConst rx = doc["receiver"].as<JsonObjectConst>();
  snprintf(receiverName, sizeof(receiverName), "%s", rx["name"] | "Receiver");
  snprintf(receiverEntity, sizeof(receiverEntity), "%s", rx["mediaPlayerEntity"] | "");
  for (JsonObjectConst c : rx["channels"].as<JsonArrayConst>()) {
    if (receiverChannelCount >= kMaxReceiverChannels) break;
    const char* src = c["source"] | "";
    if (!*src) continue;
    ReceiverChannel& ch = receiverChannels[receiverChannelCount];
    snprintf(ch.name, sizeof(ch.name), "%s", c["name"] | src);
    if (!ch.name[0]) snprintf(ch.name, sizeof(ch.name), "%s", src);
    snprintf(ch.source, sizeof(ch.source), "%s", src);
    snprintf(ch.icon, sizeof(ch.icon), "%s", c["icon"] | "");
    ++receiverChannelCount;
  }

  JsonObjectConst xbox = doc["xbox"].as<JsonObjectConst>();
  snprintf(xboxMediaEntity, sizeof(xboxMediaEntity), "%s", xbox["mediaPlayerEntity"] | "");
  snprintf(xboxRemoteEntity, sizeof(xboxRemoteEntity), "%s", xbox["remoteEntity"] | "");
  for (JsonObjectConst g : xbox["games"].as<JsonArrayConst>()) {
    if (xboxGameCount >= kMaxXboxGames) break;
    const char* pid = g["productId"] | "";
    if (!*pid) continue;
    XboxGame& game = xboxGames[xboxGameCount];
    snprintf(game.name, sizeof(game.name), "%s", g["name"] | pid);
    snprintf(game.productId, sizeof(game.productId), "%s", pid);
    snprintf(game.art, sizeof(game.art), "%s", g["art"] | "");
    ++xboxGameCount;
  }

  JsonObjectConst hub = doc["hub"].as<JsonObjectConst>();
  hubQuickActions = hub["quickActionsEnabled"] | true;
  for (JsonObjectConst it : hub["items"].as<JsonArrayConst>()) {
    if (hubItemCount >= kMaxHubItems) break;
    const char* nm = it["name"] | "";
    if (!*nm) continue;
    HubItem& h = hubItems[hubItemCount];
    snprintf(h.name, sizeof(h.name), "%s", nm);
    snprintf(h.icon, sizeof(h.icon), "%s", it["icon"] | "");
    snprintf(h.target, sizeof(h.target), "%s", it["target"] | "");
    JsonObjectConst act = it["action"].as<JsonObjectConst>();
    const char* atype = act["type"] | "none";
    h.actionType = !strcmp(atype, "toggle") ? HubAction::Toggle
                  : !strcmp(atype, "run")   ? HubAction::Run
                                            : HubAction::None;
    snprintf(h.actionEntity, sizeof(h.actionEntity), "%s", act["entity"] | "");
    snprintf(h.actionService, sizeof(h.actionService), "%s", act["service"] | "");
    parseActionData(act["data"], h.actionData, sizeof(h.actionData));
    ++hubItemCount;
  }

  ok = true;
}

// The SD cache file this room's config lives in ("room-<slug>" — slugs are
// a-z0-9- only, so always a safe filename). Per room, so switching rooms
// never paints another room's config.
inline void cacheName(char* out, size_t cap) { snprintf(out, cap, "room-%s", activeSlug); }

// GET this room's config. On success it's applied AND cached on SD (so the
// next wake paints it before Wi-Fi is up); on failure every field is reset —
// the caller rolls back to the cache (persist::load()).
inline bool fetch() {
  reset();
  char cfgPath[96];
  snprintf(cfgPath, sizeof(cfgPath), "/api/devices/%s/config", activeSlug);

  JsonDocument doc;
  if (!httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, cfgPath, pairing::token,
                     doc, status, sizeof(status))) {
    if (!strcmp(status, "HTTP 401")) pairing::noteUnauthorized();
    return false;
  }
  applyJson(doc);
  char cache[48];
  cacheName(cache, sizeof(cache));
  sdcache::writeJson(cache, doc);
  return true;
}

// Apply this room's config from the SD cache (no network). False if there's
// no cached copy for the current room.
inline bool loadCached() {
  JsonDocument doc;
  char cache[48];
  cacheName(cache, sizeof(cache));
  if (!sdcache::readJson(cache, doc)) {
    reset();
    return false;
  }
  applyJson(doc);
  return true;
}

}  // namespace deviceconfig
