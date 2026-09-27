#pragma once

// ===========================================================================
// icon_pack — loads a compiled icon theme from SD (/switchboard/icons.pack,
// the "SBI1" format Switchboard-Server's lib/assets/pack-format.js writes)
// and downloads a fresh one over HTTP when the server has a newer version.
//
// The whole file is read into one ps_malloc'd PSRAM buffer (icon bitmap
// payloads are tens of KB total - see the Switchboard-Server plan's memory-
// budget note - PSRAM capacity is a non-issue); a small parallel array of
// {name, freeink::Icon} is materialized once at load time, with each Icon's
// `.bits` pointing directly into that buffer (Ui::icon()/DisplayTarget::
// bitmap() already dereference Icon.bits with a plain pointer, no PROGMEM/
// flash-only assumption - see ui.h - so this is a drop-in swap for a
// compile-time Icon's address).
//
// The download itself mirrors xbox_art.h's fetchAndDecode(): resolve the
// host, GET with a known Content-Length, stream into a buffer/file with a
// bounded wait loop. Written tmp-then-rename (persist.h's same crash-safety
// pattern) so a power loss mid-download can never leave a half-written pack
// file that a later load() would misread.
// ===========================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SDCardManager.h>
#include <string.h>

#include "Icon.h"
#include "config.h"
#include "http_json.h"
#include "persist.h"  // persist::g_ready — the card is already mounted (or not) by boot

namespace iconpack {

inline constexpr const char* kPath = "/switchboard/icons.pack";
inline constexpr const char* kTmpPath = "/switchboard/icons.pack.tmp";
inline constexpr uint32_t kMaxPackBytes = 4 * 1024 * 1024;  // sanity cap, real packs are ~50KB

struct LoadedIcon {
  char name[24];
  freeink::Icon icon;
};

inline uint8_t* g_buf = nullptr;      // ps_malloc'd whole-file buffer; icons' .bits point into this
inline LoadedIcon* g_icons = nullptr;  // ps_malloc'd directory, parsed once at load()
inline int g_count = 0;
inline bool loaded = false;

inline void unload() {
  if (g_buf) { free(g_buf); g_buf = nullptr; }
  if (g_icons) { free(g_icons); g_icons = nullptr; }
  g_count = 0;
  loaded = false;
}

inline uint16_t rdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t rdU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Parse an already-read-into-RAM pack buffer (used by load() below, and
// directly by a future in-place-verify step if ever needed). Takes ownership
// of `buf` on success (stored in g_buf); frees it on failure.
inline bool loadFromBuffer(uint8_t* buf, uint32_t len) {
  if (len < 8 || memcmp(buf, "SBI1", 4) != 0) {
    free(buf);
    return false;
  }
  const uint16_t count = rdU16(buf + 4);
  const uint32_t dirEntryLen = 38;
  const uint32_t dirStart = 8;
  if (dirStart + static_cast<uint32_t>(count) * dirEntryLen > len) {
    free(buf);
    return false;
  }

  LoadedIcon* icons = static_cast<LoadedIcon*>(ps_malloc(sizeof(LoadedIcon) * count));
  if (!icons) {
    free(buf);
    return false;
  }

  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t* e = buf + dirStart + i * dirEntryLen;
    memcpy(icons[i].name, e, 24);
    icons[i].name[23] = 0;
    const uint16_t w = rdU16(e + 24);
    const uint16_t h = rdU16(e + 26);
    const int16_t cy = static_cast<int16_t>(rdU16(e + 28));
    const uint32_t bitsOffset = rdU32(e + 30);
    const uint32_t bitsLength = rdU32(e + 34);
    if (static_cast<uint64_t>(bitsOffset) + bitsLength > len) {
      free(buf);
      free(icons);
      return false;
    }
    icons[i].icon = freeink::Icon{w, h, cy, buf + bitsOffset};
  }

  unload();
  g_buf = buf;
  g_icons = icons;
  g_count = count;
  loaded = true;
  return true;
}

// (Re)load the pack file already on SD, if any. false (with `loaded` left
// unset) just means "no custom pack" — icons.h falls back to the compiled-in
// defaults, never a crash or a hang.
inline bool load() {
  if (!persist::g_ready) return false;

  FsFile f;
  if (!SdMan.openFileForRead("iconpack", kPath, f)) return false;
  const uint32_t len = f.size();
  if (len < 8 || len > kMaxPackBytes) {
    f.close();
    return false;
  }
  uint8_t* buf = static_cast<uint8_t*>(ps_malloc(len));
  if (!buf) {
    f.close();
    return false;
  }
  const int n = f.read(buf, len);
  f.close();
  if (n < 0 || static_cast<uint32_t>(n) != len) {
    free(buf);
    return false;
  }

  return loadFromBuffer(buf, len);
}

// GETs /api/theme/icons.pack, streams it to a tmp file on SD, renames into
// place, then reloads. false on any failure along the way — the previously
// loaded pack (if any) is left untouched, so a bad download never regresses
// what's already showing.
inline bool downloadAndLoad(const char* token) {
  const IPAddress ip = httpjson::resolveHost(SWITCHBOARD_SERVER_HOST);
  if (ip == IPAddress(0, 0, 0, 0)) return false;

  char url[96];
  snprintf(url, sizeof(url), "http://%s:%u/api/theme/icons.pack", ip.toString().c_str(),
           SWITCHBOARD_SERVER_PORT);

  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(url)) return false;
  if (token && *token) {
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    http.addHeader("Authorization", auth);
  }
  const int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return false;
  }
  const int len = http.getSize();
  if (len <= 0 || static_cast<uint32_t>(len) > kMaxPackBytes) {
    http.end();
    return false;
  }

  if (!persist::g_ready || !SdMan.ensureDirectoryExists("/switchboard")) {
    http.end();
    return false;
  }
  FsFile f;
  if (!SdMan.openFileForWrite("iconpack", kTmpPath, f)) {
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t chunk[512];
  int got = 0;
  bool ok = true;
  const uint32_t deadline = millis() + 20000;
  while (got < len && millis() < deadline) {
    const int avail = stream->available();
    if (avail <= 0) {
      delay(5);
      continue;
    }
    int want = avail < static_cast<int>(sizeof(chunk)) ? avail : static_cast<int>(sizeof(chunk));
    if (want > len - got) want = len - got;
    const int n = stream->readBytes(chunk, want);
    if (n <= 0) { ok = false; break; }
    if (f.write(chunk, n) != static_cast<size_t>(n)) { ok = false; break; }
    got += n;
  }
  f.close();
  http.end();

  if (!ok || got < len) {
    SdMan.remove(kTmpPath);
    return false;
  }

  SdMan.remove(kPath);  // SdFat's rename() won't overwrite an existing file
  if (!SdMan.rename(kTmpPath, kPath)) {
    SdMan.remove(kTmpPath);
    return false;
  }

  return load();
}

inline const freeink::Icon* find(const char* name) {
  if (!loaded) return nullptr;
  for (int i = 0; i < g_count; ++i) {
    if (!strncmp(g_icons[i].name, name, sizeof(g_icons[i].name))) return &g_icons[i].icon;
  }
  return nullptr;
}

}  // namespace iconpack
