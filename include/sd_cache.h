#pragma once

// ===========================================================================
// sd_cache — small, crash-safe files on the SD card, for the caches that let
// a wake paint real data before Wi-Fi is even up (persist.h decides WHAT is
// cached; this is just HOW).
//
//   /switchboard/config/<name>.json   raw server JSON (globals, each room's
//                                     config), re-parsed by the same code a
//                                     live fetch uses
//   /switchboard/state/<name>.bin     one page's last-known Home Assistant
//                                     state, as a checked binary record
//
// Every write goes to <file>.tmp and is renamed over the real file, so a
// power loss mid-write never leaves a torn file (SdFat's rename() won't
// replace an existing file, so the old one is removed first; a read that
// finds only the .tmp — power lost in that gap — uses it). Binary records
// carry a magic, a per-record schema version, the payload size and a CRC, so
// a stale file from an older firmware, or a damaged one, reads as "no cache"
// instead of garbage — and only THAT page's cache is lost, not everything.
//
// A missing or dead card degrades every call to a quiet no-op/false: the
// device just starts without a cache, it never hangs or crashes.
// ===========================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SDCardManager.h>
#include <esp_rom_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

namespace sdcache {

inline constexpr const char* kRoot      = "/switchboard";
inline constexpr const char* kConfigDir = "/switchboard/config";
inline constexpr const char* kStateDir  = "/switchboard/state";

// Whether the card mounted this boot.
inline bool g_ready = false;

// The refresh task and the main loop can both reach the cache; one mutex
// keeps their file operations from interleaving on the card.
inline SemaphoreHandle_t g_lock = nullptr;
struct Lock {
  Lock() { if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { if (g_lock) xSemaphoreGive(g_lock); }
};

// Mount the card and make sure the cache folders exist. Call once, early in
// boot, after the display is up.
inline bool begin() {
  if (!g_lock) g_lock = xSemaphoreCreateMutex();
  g_ready = SdMan.begin() && SdMan.ensureDirectoryExists(kRoot) &&
            SdMan.ensureDirectoryExists(kConfigDir) && SdMan.ensureDirectoryExists(kStateDir);
  return g_ready;
}

// Unmount before deep sleep. On the X4 Pro's native SDMMC this floats the
// bus pads so their pull-ups stop back-feeding the card through sleep.
inline void shutdown() {
  Lock lock;
  if (g_ready) SdMan.shutdown();
  g_ready = false;
}

inline void configPath(const char* name, char* out, size_t cap) {
  snprintf(out, cap, "%s/%s.json", kConfigDir, name);
}
inline void statePath(const char* name, char* out, size_t cap) {
  snprintf(out, cap, "%s/%s.bin", kStateDir, name);
}

// Open `path` for reading, falling back to its .tmp (a write that lost power
// between removing the old file and renaming the new one into place).
inline bool openForRead(const char* path, FsFile& f) {
  // exists() first: a plain miss (nothing cached yet) is normal, and
  // openFileForRead() would log it as an error.
  if (SdMan.exists(path) && SdMan.openFileForRead("cache", path, f)) return true;
  char tmp[96];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  return SdMan.exists(tmp) && SdMan.openFileForRead("cache", tmp, f);
}

// Rename the fully written `tmp` over `path`.
inline bool commit(const char* tmp, const char* path) {
  if (SdMan.exists(path)) SdMan.remove(path);  // SdFat's rename() won't overwrite
  if (SdMan.rename(tmp, path)) return true;
  SdMan.remove(tmp);
  return false;
}

// --- JSON documents ----------------------------------------------------------

inline bool writeJson(const char* name, JsonVariantConst doc) {
  Lock lock;
  if (!g_ready) return false;
  char path[80], tmp[96];
  configPath(name, path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FsFile f;
  if (!SdMan.openFileForWrite("cache", tmp, f)) return false;
  const size_t expected = measureJson(doc);
  const size_t n = serializeJson(doc, f);
  f.close();
  if (n != expected) {
    SdMan.remove(tmp);
    return false;
  }
  return commit(tmp, path);
}

inline bool readJson(const char* name, JsonDocument& doc) {
  Lock lock;
  if (!g_ready) return false;
  char path[80];
  configPath(name, path, sizeof(path));
  FsFile f;
  if (!openForRead(path, f)) return false;
  const DeserializationError err = deserializeJson(doc, f);
  f.close();
  return !err;
}

// --- Binary state records ----------------------------------------------------

struct RecordHeader {
  uint32_t magic;
  uint16_t schema;  // bump a record's schema when its payload layout changes
  uint16_t reserved;
  uint32_t size;    // payload bytes
  uint32_t crc;     // CRC-32 of the payload
};
inline constexpr uint32_t kRecordMagic = 0x54534253u;  // 'SBST'

inline uint32_t crcOf(const void* data, size_t len) {
  return esp_rom_crc32_le(0, static_cast<const uint8_t*>(data), static_cast<uint32_t>(len));
}

inline bool writeRecord(const char* name, uint16_t schema, const void* data, size_t len) {
  Lock lock;
  if (!g_ready) return false;
  char path[80], tmp[96];
  statePath(name, path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  RecordHeader h{kRecordMagic, schema, 0, static_cast<uint32_t>(len), crcOf(data, len)};
  FsFile f;
  if (!SdMan.openFileForWrite("cache", tmp, f)) return false;
  const size_t n = f.write(reinterpret_cast<const uint8_t*>(&h), sizeof(h)) +
                   f.write(static_cast<const uint8_t*>(data), len);
  f.close();
  if (n != sizeof(h) + len) {
    SdMan.remove(tmp);
    return false;
  }
  return commit(tmp, path);
}

// Fills `data` only if the whole record checks out (magic, schema, size,
// CRC); on any mismatch `data` is left untouched and false is returned.
inline bool readRecord(const char* name, uint16_t schema, void* data, size_t len,
                       uint32_t* crcOut = nullptr) {
  Lock lock;
  if (!g_ready) return false;
  char path[80];
  statePath(name, path, sizeof(path));
  FsFile f;
  if (!openForRead(path, f)) return false;
  RecordHeader h{};
  bool ok = f.read(reinterpret_cast<uint8_t*>(&h), sizeof(h)) == static_cast<int>(sizeof(h)) &&
            h.magic == kRecordMagic && h.schema == schema && h.size == len;
  if (ok) {
    // Read into a scratch buffer first, so a short read or bad CRC can't
    // leave `data` half-overwritten.
    uint8_t* buf = static_cast<uint8_t*>(malloc(len));
    ok = buf && f.read(buf, len) == static_cast<int>(len) && crcOf(buf, len) == h.crc;
    if (ok) {
      memcpy(data, buf, len);
      if (crcOut) *crcOut = h.crc;
    }
    free(buf);
  }
  f.close();
  return ok;
}

// --- Maintenance ---------------------------------------------------------------

inline void removeConfig(const char* name) {
  Lock lock;
  if (!g_ready) return;
  char path[80], tmp[96];
  configPath(name, path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  SdMan.remove(path);
  SdMan.remove(tmp);
}

// Settings -> Developer -> Hard reset: every cached config and page state.
inline void wipe() {
  Lock lock;
  if (!g_ready) return;
  SdMan.removeDir(kConfigDir);
  SdMan.removeDir(kStateDir);
  SdMan.ensureDirectoryExists(kConfigDir);
  SdMan.ensureDirectoryExists(kStateDir);
}

}  // namespace sdcache
