#pragma once

// ===========================================================================
// persist — what's cached on the SD card so a wake paints real data before
// Wi-Fi is even up, and what reloads it. (sd_cache.h does the file I/O.)
//
//   config/globals.json          /api/globals as the server sent it
//   config/room-<slug>.json      this room's /api/devices/<slug>/config
//   state/<slug>-status.bin      weather, air quality, forecast, clock
//   state/<slug>-climate.bin     thermostat + the extra temperature sensors
//   state/<slug>-lighting.bin    the group light + each light's on/off
//   state/<slug>-blinds.bin      the group cover + the two row-layout covers
//   state/<slug>-music.bin       the Music page's media player
//   state/<slug>-xbox.bin        the Xbox page's console
//   state/<slug>-hub.bin         the Quick Access hub's toggle strips
//
// Everything but the globals is per room (<slug> = the active room), so
// switching rooms (Settings -> Select room) paints that room's own last
// state straight away — never the previous room's.
//
// Config is cached as the server's raw JSON and re-parsed by exactly the
// code a live fetch uses, so a firmware update never invalidates it, and
// every section a page needs comes back (the old single blob silently left
// out the Xbox library and the Quick Access hub). The config files are
// written by the clients themselves the moment a fetch succeeds.
//
// Page state is one small checked record per page (see sd_cache.h): a
// layout change in one page's structs only drops THAT page's cache, and a
// record is only rewritten when its contents actually changed — most
// refreshes and sleeps write nothing at all.
//
// Flow: boot mounts the card and load()s everything (no network) -> the
// first paint uses it -> once Wi-Fi is up the background refresh re-pulls,
// and on success save()s whatever changed; on failure load() rolls the
// half-updated clients back to the cache. sleepFor() also save()s, so a
// state change made by the user (a light toggled) is what the next wake
// shows, not the state from the last refresh.
// ===========================================================================

#include <Arduino.h>
#include <string.h>
#include <time.h>

#include "sd_cache.h"
#include "device_config_client.h"
#include "globals_client.h"
#include "ha_client.h"

namespace persist {

// Whether the card mounted this boot (theme_client.h checks it before
// downloading a pack to SD).
inline bool& g_ready = sdcache::g_ready;

// ---------------------------------------------------------------------------
// Page state records. Each is a plain struct, captured from / restored to
// the live haclient state. Bump kSchema if a record's meaning changes
// without its size changing (a size change is caught automatically).
// ---------------------------------------------------------------------------
inline constexpr uint16_t kSchema = 1;

struct StatusState {
  haclient::Weather weather;
  haclient::Air air;
  haclient::Forecast forecast;
  struct tm clockUtc;
  bool clockValid;
  struct tm dataChangedUtc;
  bool dataChangedValid;
  static constexpr const char* kName = "status";
  void capture() {
    weather = haclient::weather;
    air = haclient::air;
    forecast = haclient::forecast;
    clockUtc = haclient::clockUtc;
    clockValid = haclient::clockValid;
    dataChangedUtc = haclient::dataChangedUtc;
    dataChangedValid = haclient::dataChangedValid;
  }
  void restore() const {
    haclient::weather = weather;
    haclient::air = air;
    haclient::forecast = forecast;
    haclient::clockUtc = clockUtc;
    haclient::clockValid = clockValid;
    haclient::dataChangedUtc = dataChangedUtc;
    haclient::dataChangedValid = dataChangedValid;
  }
};

struct ClimateState {
  haclient::Climate climate;
  float sensorValue[6];
  bool sensorOk[6];
  static constexpr const char* kName = "climate";
  void capture() {
    climate = haclient::climate;
    memcpy(sensorValue, haclient::climateSensorValue, sizeof(sensorValue));
    memcpy(sensorOk, haclient::climateSensorOk, sizeof(sensorOk));
  }
  void restore() const {
    haclient::climate = climate;
    memcpy(haclient::climateSensorValue, sensorValue, sizeof(sensorValue));
    memcpy(haclient::climateSensorOk, sensorOk, sizeof(sensorOk));
  }
};
static_assert(sizeof(ClimateState::sensorValue) == sizeof(haclient::climateSensorValue), "");

struct LightingState {
  haclient::Light group;
  bool itemOn[24];
  static constexpr const char* kName = "lighting";
  void capture() {
    group = haclient::lightGroup;
    memcpy(itemOn, haclient::lightItemOn, sizeof(itemOn));
  }
  void restore() const {
    haclient::lightGroup = group;
    memcpy(haclient::lightItemOn, itemOn, sizeof(itemOn));
  }
};
static_assert(sizeof(LightingState::itemOn) == sizeof(haclient::lightItemOn), "");

struct BlindsState {
  haclient::Cover group;
  haclient::Cover items[2];
  static constexpr const char* kName = "blinds";
  void capture() {
    group = haclient::cover;
    memcpy(items, haclient::coverItems, sizeof(items));
  }
  void restore() const {
    haclient::cover = group;
    memcpy(haclient::coverItems, items, sizeof(items));
  }
};

struct MusicState {
  haclient::MediaPlayer media;
  static constexpr const char* kName = "music";
  void capture() { media = haclient::media; }
  void restore() const { haclient::media = media; }
};

struct XboxState {
  haclient::MediaPlayer media;
  static constexpr const char* kName = "xbox";
  void capture() { media = haclient::xboxMedia; }
  void restore() const { haclient::xboxMedia = media; }
};

struct ReceiverState {
  haclient::Receiver receiver;
  haclient::ReceiverInfo info;  // now / next and the picon srcs
  static constexpr const char* kName = "receiver";
  void capture() {
    receiver = haclient::receiver;
    info = haclient::receiverInfo;
  }
  void restore() const {
    haclient::receiver = receiver;
    haclient::receiverInfo = info;
  }
};

struct HubState {
  bool toggleOn[haclient::kMaxHubToggles];
  static constexpr const char* kName = "hub";
  void capture() { memcpy(toggleOn, haclient::hubToggleOn, sizeof(toggleOn)); }
  void restore() const { memcpy(haclient::hubToggleOn, toggleOn, sizeof(toggleOn)); }
};

// Remembers the CRC of what's on the card for record T, so save() can skip
// an unchanged page without touching the card.
template <typename T>
struct Record {
  uint32_t savedCrc = 0;
  bool haveCrc = false;

  // "<slug>-<page>" — this page's file for the active room.
  static void fileName(char* out, size_t cap) {
    snprintf(out, cap, "%s-%s", deviceconfig::activeSlug, T::kName);
  }

  bool load() {
    T t;
    uint32_t crc = 0;
    char name[48];
    fileName(name, sizeof(name));
    if (!sdcache::readRecord(name, kSchema, &t, sizeof(T), &crc)) return false;
    t.restore();
    savedCrc = crc;
    haveCrc = true;
    return true;
  }

  // Returns true if the record is on the card (written now or unchanged).
  bool save() {
    T t;
    memset(static_cast<void*>(&t), 0, sizeof(T));  // stable padding -> stable CRC
    t.capture();
    const uint32_t crc = sdcache::crcOf(&t, sizeof(T));
    if (haveCrc && crc == savedCrc) return true;
    char name[48];
    fileName(name, sizeof(name));
    if (!sdcache::writeRecord(name, kSchema, &t, sizeof(T))) return false;
    savedCrc = crc;
    haveCrc = true;
    Serial.printf("[persist] saved %s (%u bytes)\n", T::kName, static_cast<unsigned>(sizeof(T)));
    return true;
  }
};

inline Record<StatusState> g_status;
inline Record<ClimateState> g_climate;
inline Record<LightingState> g_lighting;
inline Record<BlindsState> g_blinds;
inline Record<MusicState> g_music;
inline Record<XboxState> g_xbox;
inline Record<ReceiverState> g_receiver;
inline Record<HubState> g_hub;

// ---------------------------------------------------------------------------

// Mount the card (and clean up the pre-per-page single cache file, which no
// firmware reads any more). Call once, early in boot, after the display is up.
inline bool begin() {
  if (!sdcache::begin()) return false;
  SdMan.remove("/switchboard/cache.bin");
  SdMan.remove("/switchboard/cache.bin.tmp");
  return true;
}

// The cached server config (globals + this room's config). True if both
// came back — enough to know which entities each page shows.
inline bool loadConfig() {
  const bool globalsOk = globalsclient::loadCached();
  const bool roomOk = deviceconfig::loadCached();
  return globalsOk && roomOk;
}

// Every page's cached state. A page with no (valid) record keeps whatever
// its client holds — at boot, the "unavailable" defaults.
inline int loadState() {
  int n = 0;
  n += g_status.load();
  n += g_climate.load();
  n += g_lighting.load();
  n += g_blinds.load();
  n += g_music.load();
  n += g_xbox.load();
  n += g_receiver.load();
  n += g_hub.load();
  return n;
}

// Config + state. Also the refresh's rollback after a failed fetch: every
// client goes back to the last good cached copy. True if the config came
// back (there's something real to paint).
inline bool load() {
  const bool configOk = loadConfig();
  const int pages = loadState();
  Serial.printf("[persist] load: config=%d pages=%d\n", configOk, pages);
  return configOk;
}

// Write every page whose state changed since it was last saved/loaded.
// (Config files are written by the clients as each fetch succeeds.)
inline void save() {
  g_status.save();
  g_climate.save();
  g_lighting.save();
  g_blinds.save();
  g_music.save();
  g_xbox.save();
  g_receiver.save();
  g_hub.save();
}

// Every page's live state back to its "never fetched" defaults.
inline void resetState() {
  haclient::weather = haclient::Weather{};
  haclient::air = haclient::Air{};
  haclient::forecast = haclient::Forecast{};
  haclient::clockUtc = {};
  haclient::clockValid = false;
  haclient::dataChangedUtc = {};
  haclient::dataChangedValid = false;
  haclient::climate = haclient::Climate{};
  for (int i = 0; i < 6; ++i) {
    haclient::climateSensorValue[i] = 0;
    haclient::climateSensorOk[i] = false;
  }
  haclient::lightGroup = haclient::Light{};
  memset(haclient::lightItemOn, 0, sizeof(haclient::lightItemOn));
  haclient::cover = haclient::Cover{};
  haclient::coverItems[0] = haclient::Cover{};
  haclient::coverItems[1] = haclient::Cover{};
  haclient::media = haclient::MediaPlayer{};
  haclient::xboxMedia = haclient::MediaPlayer{};
  memset(haclient::hubToggleOn, 0, sizeof(haclient::hubToggleOn));
}

// Forget which versions of the page files are on the card (they're another
// room's now, or gone).
inline void forgetSaved() {
  g_status = {};
  g_climate = {};
  g_lighting = {};
  g_blinds = {};
  g_music = {};
  g_xbox = {};
  g_hub = {};
}

// Settings -> Select room just changed deviceconfig::activeSlug: show the
// new room's cached config + state (or blank "unavailable" pages if it has
// never been cached) until the refresh lands.
inline bool switchRoom() {
  forgetSaved();
  resetState();
  return load();
}

// Unmount before deep sleep. Call only after the last save() of the session.
inline void shutdown() { sdcache::shutdown(); }

// Settings -> Developer -> Hard reset: forget every cached config and page
// state, so the next boot starts from nothing rather than replaying the room
// being abandoned (paired with deviceconfig::resetSlug()).
inline void wipe() {
  sdcache::wipe();
  forgetSaved();
}

}  // namespace persist
