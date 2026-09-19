#pragma once

// ===========================================================================
// persist — the Standby screen's state, cached on the SD card so a wake can
// redraw the last-known screen instantly, before Wi-Fi has even joined.
//
// On a wake, setup() mounts the card (begin()), loads the cache (load()) and
// paints from it immediately, then reconnects and re-pulls in the background;
// a successful refresh calls save(). Unlike the RTC-memory blob this used to
// be, the file survives a full power loss (battery pull, not just deep
// sleep), and isn't capped by the ~8 KB of RTC slow memory on the ESP32-S3.
//
// The write is tmp-then-rename so a power loss mid-write can never leave a
// half-written cache.bin behind — load() would then see a bad magic (or a
// short read) and just fail, same as "no cache yet".
//
// Bump kMagicBase's version nibble whenever Blob's layout — or that of the
// haclient structs it embeds — changes, so a post-OTA wake ignores a stale
// cache file instead of misreading it as the new layout.
// ===========================================================================

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <SDCardManager.h>

#include "device_config_client.h"
#include "globals_client.h"
#include "ha_client.h"

namespace persist {

inline constexpr const char* kCacheDir  = "/switchboard";
inline constexpr const char* kCachePath = "/switchboard/cache.bin";
inline constexpr const char* kCacheTmp  = "/switchboard/cache.bin.tmp";

// 'WB' + a version nibble. The real magic (below, after Blob) XORs in
// sizeof(Blob) so ANY change to the blob layout — including the embedded
// haclient structs — auto-invalidates a stale cache file from a previous
// firmware. Reading old bytes as a new layout would otherwise poison
// globalsclient::ok and the entity ids -> "data not available" that never clears.
static constexpr uint32_t kMagicBase = 0x57420006u;  // .0006: moved off RTC onto the SD cache

struct Blob {
  uint32_t magic;

  // /api/globals — Home Assistant connection + shared Wi-Fi networks
  char haHost[64];
  uint16_t haPort;
  char haToken[320];
  bool globalsOk;
  globalsclient::WifiNetItem wifiNets[globalsclient::kMaxWifiNets];
  int wifiNetCount;

  // /api/devices/<slug>/config — Standby settings
  char devName[32];
  char weatherEntity[64];
  char climateEntity[64];
  char airQualityEntity[64];
  uint16_t refreshIntervalMin;
  bool deviceOk;
  bool lightGroupEnabled;
  char lightGroupName[32];
  char lightGroupEntity[64];
  bool lightGroupBrightness;
  bool lightGroupColorTemp;
  deviceconfig::LightItem lights[deviceconfig::kMaxLights];
  int lightCount;
  deviceconfig::LightItem scenes[deviceconfig::kMaxScenes];
  int sceneCount;
  bool blindsGroupEnabled;
  char blindsGroupName[32];
  char blindsGroupEntity[64];
  deviceconfig::LightItem blindsItems[6];
  int blindsItemCount;
  deviceconfig::LightItem climateSensors[6];
  int climateSensorCount;
  bool mediaEnabled;
  char mediaName[32];
  char mediaEntity[64];
  bool screenLighting, screenBlinds, screenMusic, screenTv, screenXbox, screenClimate, screenWifi;
  char activeSlug[32];
  char tvMediaEntity[64];
  char tvRemoteEntity[64];
  deviceconfig::AppItem tvApps[deviceconfig::kMaxTvApps];
  int tvAppCount;

  // last Home Assistant reads
  haclient::Weather weather;
  haclient::Climate climate;
  haclient::Air air;
  haclient::Light lightGroup;
  bool lightItemOn[24];
  float climateSensorValue[6];
  bool climateSensorOk[6];
  haclient::Cover cover;
  haclient::Cover coverItems[2];
  haclient::Forecast forecast;
  haclient::MediaPlayer media;
  struct tm clockUtc;
  bool clockValid;
};

static constexpr uint32_t kMagic = kMagicBase ^ static_cast<uint32_t>(sizeof(Blob));

// Plain RAM scratch — no longer needs to be RTC_DATA_ATTR since the actual
// persistence is the SD file, not this struct surviving deep sleep itself.
static Blob g_blob;

// Whether the card mounted this boot. false just means save()/load() quietly
// no-op — a missing/dead card degrades to "no cache", never a hang or crash.
inline bool g_ready = false;

// Mount the SD card cache. Call once, early in setup() — after the display
// is up (it can wait on nothing), before the first load(). Safe to call even
// if the board never got an SD card fitted; SdMan.begin() just fails fast.
inline bool begin() {
  g_ready = SdMan.begin() && SdMan.ensureDirectoryExists(kCacheDir);
  return g_ready;
}

// Unmount before deep sleep. On the X4 Pro's native SDMMC this floats the bus
// pads so their pull-ups stop back-feeding the card through sleep; a no-op on
// SPI-attached cards. Call only after the last save() of the session — a
// wake resets the MCU and remounts through begin() again.
inline void shutdown() {
  if (g_ready) SdMan.shutdown();
  g_ready = false;
}

// Settings -> Developer -> Hard reset: delete the cached room config/state so
// the next boot starts from nothing rather than replaying whatever got saved
// for the room being abandoned (paired with deviceconfig::resetSlug()).
inline void wipe() {
  if (!g_ready) return;
  SdMan.remove(kCachePath);
  SdMan.remove(kCacheTmp);
}

// Copy the live client state into g_blob (shared by save() below).
static void fillBlob() {
  Blob& b = g_blob;
  b.magic = kMagic;

  snprintf(b.haHost, sizeof(b.haHost), "%s", globalsclient::haHost);
  b.haPort = globalsclient::haPort;
  snprintf(b.haToken, sizeof(b.haToken), "%s", globalsclient::haToken);
  b.globalsOk = globalsclient::ok;
  memcpy(b.wifiNets, globalsclient::wifiNets, sizeof(b.wifiNets));
  b.wifiNetCount = globalsclient::wifiNetCount;

  snprintf(b.devName, sizeof(b.devName), "%s", deviceconfig::name);
  snprintf(b.weatherEntity, sizeof(b.weatherEntity), "%s", deviceconfig::weatherEntity);
  snprintf(b.climateEntity, sizeof(b.climateEntity), "%s", deviceconfig::climateEntity);
  snprintf(b.airQualityEntity, sizeof(b.airQualityEntity), "%s",
           deviceconfig::airQualityEntity);
  b.refreshIntervalMin = deviceconfig::refreshIntervalMin;
  b.deviceOk = deviceconfig::ok;
  b.lightGroupEnabled = deviceconfig::lightGroupEnabled;
  snprintf(b.lightGroupName, sizeof(b.lightGroupName), "%s", deviceconfig::lightGroupName);
  snprintf(b.lightGroupEntity, sizeof(b.lightGroupEntity), "%s",
           deviceconfig::lightGroupEntity);
  b.lightGroupBrightness = deviceconfig::lightGroupBrightness;
  b.lightGroupColorTemp = deviceconfig::lightGroupColorTemp;
  memcpy(b.lights, deviceconfig::lights, sizeof(b.lights));
  b.lightCount = deviceconfig::lightCount;
  memcpy(b.scenes, deviceconfig::scenes, sizeof(b.scenes));
  b.sceneCount = deviceconfig::sceneCount;
  b.blindsGroupEnabled = deviceconfig::blindsGroupEnabled;
  snprintf(b.blindsGroupName, sizeof(b.blindsGroupName), "%s",
           deviceconfig::blindsGroupName);
  snprintf(b.blindsGroupEntity, sizeof(b.blindsGroupEntity), "%s",
           deviceconfig::blindsGroupEntity);
  memcpy(b.blindsItems, deviceconfig::blindsItems, sizeof(b.blindsItems));
  b.blindsItemCount = deviceconfig::blindsItemCount;
  memcpy(b.climateSensors, deviceconfig::climateSensors, sizeof(b.climateSensors));
  b.climateSensorCount = deviceconfig::climateSensorCount;
  b.mediaEnabled = deviceconfig::mediaEnabled;
  snprintf(b.mediaName, sizeof(b.mediaName), "%s", deviceconfig::mediaName);
  snprintf(b.mediaEntity, sizeof(b.mediaEntity), "%s", deviceconfig::mediaEntity);
  b.screenLighting = deviceconfig::screenLighting;
  b.screenBlinds   = deviceconfig::screenBlinds;
  b.screenMusic    = deviceconfig::screenMusic;
  b.screenTv       = deviceconfig::screenTv;
  b.screenXbox     = deviceconfig::screenXbox;
  b.screenClimate  = deviceconfig::screenClimate;
  b.screenWifi     = deviceconfig::screenWifi;
  snprintf(b.activeSlug, sizeof(b.activeSlug), "%s", deviceconfig::activeSlug);
  snprintf(b.tvMediaEntity, sizeof(b.tvMediaEntity), "%s", deviceconfig::tvMediaEntity);
  snprintf(b.tvRemoteEntity, sizeof(b.tvRemoteEntity), "%s", deviceconfig::tvRemoteEntity);
  memcpy(b.tvApps, deviceconfig::tvApps, sizeof(b.tvApps));
  b.tvAppCount = deviceconfig::tvAppCount;

  b.weather = haclient::weather;
  b.climate = haclient::climate;
  b.air = haclient::air;
  b.lightGroup = haclient::lightGroup;
  memcpy(b.lightItemOn, haclient::lightItemOn, sizeof(b.lightItemOn));
  memcpy(b.climateSensorValue, haclient::climateSensorValue, sizeof(b.climateSensorValue));
  memcpy(b.climateSensorOk, haclient::climateSensorOk, sizeof(b.climateSensorOk));
  b.cover = haclient::cover;
  memcpy(b.coverItems, haclient::coverItems, sizeof(b.coverItems));
  b.forecast = haclient::forecast;
  b.media = haclient::media;
  b.clockUtc = haclient::clockUtc;
  b.clockValid = haclient::clockValid;
}

// Copy g_blob into the live client state (shared by load() below).
static void applyBlob() {
  const Blob& b = g_blob;

  snprintf(globalsclient::haHost, sizeof(globalsclient::haHost), "%s", b.haHost);
  globalsclient::haPort = b.haPort;
  snprintf(globalsclient::haToken, sizeof(globalsclient::haToken), "%s", b.haToken);
  globalsclient::ok = b.globalsOk;
  memcpy(globalsclient::wifiNets, b.wifiNets, sizeof(globalsclient::wifiNets));
  globalsclient::wifiNetCount = b.wifiNetCount;

  snprintf(deviceconfig::name, sizeof(deviceconfig::name), "%s", b.devName);
  snprintf(deviceconfig::weatherEntity, sizeof(deviceconfig::weatherEntity), "%s",
           b.weatherEntity);
  snprintf(deviceconfig::climateEntity, sizeof(deviceconfig::climateEntity), "%s",
           b.climateEntity);
  snprintf(deviceconfig::airQualityEntity, sizeof(deviceconfig::airQualityEntity), "%s",
           b.airQualityEntity);
  deviceconfig::refreshIntervalMin = b.refreshIntervalMin ? b.refreshIntervalMin : 30;
  deviceconfig::ok = b.deviceOk;
  deviceconfig::lightGroupEnabled = b.lightGroupEnabled;
  snprintf(deviceconfig::lightGroupName, sizeof(deviceconfig::lightGroupName), "%s",
           b.lightGroupName);
  snprintf(deviceconfig::lightGroupEntity, sizeof(deviceconfig::lightGroupEntity), "%s",
           b.lightGroupEntity);
  deviceconfig::lightGroupBrightness = b.lightGroupBrightness;
  deviceconfig::lightGroupColorTemp = b.lightGroupColorTemp;
  memcpy(deviceconfig::lights, b.lights, sizeof(deviceconfig::lights));
  deviceconfig::lightCount = b.lightCount;
  memcpy(deviceconfig::scenes, b.scenes, sizeof(deviceconfig::scenes));
  deviceconfig::sceneCount = b.sceneCount;
  deviceconfig::blindsGroupEnabled = b.blindsGroupEnabled;
  snprintf(deviceconfig::blindsGroupName, sizeof(deviceconfig::blindsGroupName), "%s",
           b.blindsGroupName);
  snprintf(deviceconfig::blindsGroupEntity, sizeof(deviceconfig::blindsGroupEntity), "%s",
           b.blindsGroupEntity);
  memcpy(deviceconfig::blindsItems, b.blindsItems, sizeof(deviceconfig::blindsItems));
  deviceconfig::blindsItemCount = b.blindsItemCount;
  memcpy(deviceconfig::climateSensors, b.climateSensors, sizeof(deviceconfig::climateSensors));
  deviceconfig::climateSensorCount = b.climateSensorCount;
  deviceconfig::mediaEnabled = b.mediaEnabled;
  snprintf(deviceconfig::mediaName, sizeof(deviceconfig::mediaName), "%s", b.mediaName);
  snprintf(deviceconfig::mediaEntity, sizeof(deviceconfig::mediaEntity), "%s", b.mediaEntity);
  deviceconfig::screenLighting = b.screenLighting;
  deviceconfig::screenBlinds   = b.screenBlinds;
  deviceconfig::screenMusic    = b.screenMusic;
  deviceconfig::screenTv       = b.screenTv;
  deviceconfig::screenXbox     = b.screenXbox;
  deviceconfig::screenClimate  = b.screenClimate;
  deviceconfig::screenWifi     = b.screenWifi;
  // NOTE: activeSlug is NOT restored here — NVS (loadSlug) is the authority; a
  // stale cache could otherwise revert a room the user just picked.
  snprintf(deviceconfig::tvMediaEntity, sizeof(deviceconfig::tvMediaEntity), "%s",
           b.tvMediaEntity);
  snprintf(deviceconfig::tvRemoteEntity, sizeof(deviceconfig::tvRemoteEntity), "%s",
           b.tvRemoteEntity);
  memcpy(deviceconfig::tvApps, b.tvApps, sizeof(deviceconfig::tvApps));
  deviceconfig::tvAppCount = b.tvAppCount;

  haclient::weather = b.weather;
  haclient::climate = b.climate;
  haclient::air = b.air;
  haclient::lightGroup = b.lightGroup;
  memcpy(haclient::lightItemOn, b.lightItemOn, sizeof(haclient::lightItemOn));
  memcpy(haclient::climateSensorValue, b.climateSensorValue, sizeof(haclient::climateSensorValue));
  memcpy(haclient::climateSensorOk, b.climateSensorOk, sizeof(haclient::climateSensorOk));
  haclient::cover = b.cover;
  memcpy(haclient::coverItems, b.coverItems, sizeof(haclient::coverItems));
  haclient::forecast = b.forecast;
  haclient::media = b.media;
  haclient::clockUtc = b.clockUtc;
  haclient::clockValid = b.clockValid;
}

// Write the live client state to the SD cache file. tmp-then-rename so a
// power loss mid-write can't corrupt the file a later load() would trust.
inline bool save() {
  if (!g_ready) return false;
  fillBlob();

  FsFile f;
  if (!SdMan.openFileForWrite("persist", kCacheTmp, f)) return false;
  const size_t n = f.write(reinterpret_cast<const uint8_t*>(&g_blob), sizeof(g_blob));
  f.close();
  if (n != sizeof(g_blob)) {
    SdMan.remove(kCacheTmp);
    return false;
  }

  SdMan.remove(kCachePath);  // SdFat's rename() won't overwrite an existing file
  if (!SdMan.rename(kCacheTmp, kCachePath)) {
    SdMan.remove(kCacheTmp);
    return false;
  }
  return true;
}

// Restore the SD cache into the live client state. Returns false if there's
// no card, no file yet (first-ever boot), or the file doesn't match this
// firmware's Blob layout (a stale file from an older build).
inline bool load() {
  if (!g_ready) return false;

  FsFile f;
  if (!SdMan.openFileForRead("persist", kCachePath, f)) return false;
  Blob b{};
  const int n = f.read(reinterpret_cast<uint8_t*>(&b), sizeof(b));
  f.close();
  if (n != static_cast<int>(sizeof(b)) || b.magic != kMagic) return false;

  g_blob = b;
  applyBlob();
  return true;
}

}  // namespace persist
