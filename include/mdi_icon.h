#pragma once

// ===========================================================================
// mdi_icon — fetches+caches individual MDI icons chosen per-item in the admin
// UI (Quick Access hub buttons, Lighting's individual lights/scenes, Blinds'
// individual items — see Switchboard-Server's shared icon-picker widget and
// GET /api/icons/mdi/:name). Distinct from icon_pack.h/icons.h (the ~107
// fixed, named, household-wide chrome slots re-skinned from the Theme page):
// this is per-room, per-item, an unbounded set of arbitrary MDI names, so it
// doesn't fit that pack model — each icon is fetched and SD-cached
// individually instead, keyed by name+size, the same fetch-then-cache shape
// xbox_art.h already uses for box art.
//
// The wire format is deliberately NOT icon_pack.h's multi-entry "SBI1" pack -
// there's no directory to build for a single icon: {w:u16, h:u16,
// opticalCenterY:i16} (6 bytes) followed by the packed 1bpp bits (see
// Switchboard-Server's lib/assets/icons.js::compileSingleIcon).
//
// Resolution never happens from a draw path — a network fetch can block
// briefly, which is fine in the background refresh task but would stall
// rendering if called from drawHubGrid()/drawChips() directly. Draw code only
// ever reads the resolved pointers below (hubIcons[]/lightIcons[]/
// sceneIcons[]/blindIcons[]). resolveAll() runs:
//   * at boot, CacheOnly — straight from SD, so a wake paint (or an offline
//     session) shows the custom icons without waiting on Wi-Fi;
//   * right after every deviceconfig::fetch(), IfMissing — an icon already
//     on SD is NEVER re-downloaded; only a name the config just introduced
//     is fetched, immediately, inside that same background task;
//   * after Settings -> Refresh now, Force — every configured icon is
//     re-downloaded and its SD copy overwritten;
//   * after persist::load() rolls a config back, CacheOnly again, so the
//     pointers match the config actually in effect.
// ===========================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SDCardManager.h>
#include <string.h>

#include "Icon.h"
#include "config.h"
#include "http_json.h"
#include "persist.h"
#include "device_config_client.h"

namespace mdiicon {

// Sizes match how big each context actually draws its icon: hub tiles use
// the same size as the fixed jump-grid icons (JUMP_SIZE in Switchboard-
// Server's icon-slots.js); lighting/scenes/blinds chips match the existing
// bulb-on/off glyph size (BULB_SIZE).
inline constexpr int kHubIconSize = 40;
inline constexpr int kChipIconSize = 36;

inline void cachePath(const char* name, int size, char* out, size_t outCap) {
  // MDI names are already filesystem-safe (lowercase alnum + hyphens only -
  // enforced server-side too, see Switchboard-Server's isValidMdiName()).
  snprintf(out, outCap, "/switchboard/mdiicons/%s_%d.icon", name, size);
}

inline bool parseBuf(uint8_t* buf, uint32_t len, freeink::Icon& out) {
  if (len < 6) return false;
  const uint16_t w = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
  const uint16_t h = static_cast<uint16_t>(buf[2] | (buf[3] << 8));
  const int16_t cy = static_cast<int16_t>(buf[4] | (buf[5] << 8));
  const uint32_t rowBytes = (static_cast<uint32_t>(w) + 7u) / 8u;
  if (static_cast<uint64_t>(rowBytes) * h + 6 > len) return false;
  out = freeink::Icon{w, h, cy, buf + 6};
  return true;
}

// Returns a ps_malloc'd buffer (caller owns it; `out` points inside it) on
// success, or nullptr.
inline uint8_t* loadFromSd(const char* name, int size, freeink::Icon& out) {
  if (!persist::g_ready) return nullptr;
  char path[96];
  cachePath(name, size, path, sizeof(path));
  FsFile f;
  if (!SdMan.openFileForRead("mdiicon", path, f)) return nullptr;
  const uint32_t len = f.size();
  if (len < 6 || len > 16384) { f.close(); return nullptr; }
  uint8_t* buf = static_cast<uint8_t*>(ps_malloc(len));
  if (!buf) { f.close(); return nullptr; }
  const int n = f.read(buf, len);
  f.close();
  if (n < 0 || static_cast<uint32_t>(n) != len || !parseBuf(buf, len, out)) {
    free(buf);
    return nullptr;
  }
  return buf;
}

inline uint8_t* fetchAndCache(const char* name, int size, const char* token, freeink::Icon& out) {
  if (!token || !*token) return nullptr;  // CacheOnly callers pass no token — never hit the network
  const IPAddress ip = httpjson::resolveHost(SWITCHBOARD_SERVER_HOST);
  if (ip == IPAddress(0, 0, 0, 0)) return nullptr;

  char url[144];
  snprintf(url, sizeof(url), "http://%s:%u/api/icons/mdi/%s?size=%d", ip.toString().c_str(),
           SWITCHBOARD_SERVER_PORT, name, size);

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) return nullptr;
  char auth[96];
  snprintf(auth, sizeof(auth), "Bearer %s", token);
  http.addHeader("Authorization", auth);
  const int code = http.GET();
  if (code < 200 || code >= 300) { http.end(); return nullptr; }
  const int len = http.getSize();
  if (len < 6 || len > 16384) { http.end(); return nullptr; }

  uint8_t* buf = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(len)));
  if (!buf) { http.end(); return nullptr; }

  WiFiClient* stream = http.getStreamPtr();
  int got = 0;
  bool ok = true;
  const uint32_t deadline = millis() + 8000;
  while (got < len && millis() < deadline) {
    const int avail = stream->available();
    if (avail <= 0) { delay(5); continue; }
    int want = avail < (len - got) ? avail : (len - got);
    const int n = stream->readBytes(buf + got, want);
    if (n <= 0) { ok = false; break; }
    got += n;
  }
  http.end();
  if (!ok || got < len) { free(buf); return nullptr; }

  if (persist::g_ready && SdMan.ensureDirectoryExists("/switchboard/mdiicons")) {
    char path[96], tmp[104];
    cachePath(name, size, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FsFile f;
    if (SdMan.openFileForWrite("mdiicon", tmp, f)) {
      f.write(buf, static_cast<size_t>(len));
      f.close();
      SdMan.remove(path);
      SdMan.rename(tmp, path);
    }
  }

  if (!parseBuf(buf, static_cast<uint32_t>(len), out)) { free(buf); return nullptr; }
  return buf;
}

// A small fixed-capacity cache of resolved icons, keyed by name+size, shared
// by every list (hub items, lighting lights/scenes, blinds items) - the same
// icon picked in two different places (or two different rooms) is fetched
// once. Ring-buffer eviction: simple, and a room's total distinct icons is
// small (well under kMaxCached) in every realistic config.
inline constexpr int kMaxCached = 48;
struct Slot {
  char name[48] = "";
  int size = 0;
  freeink::Icon icon{};
  uint8_t* buf = nullptr;
  bool valid = false;
  uint32_t forcedPass = 0;  // g_forcePass this slot was last re-downloaded in
};
inline Slot g_slots[kMaxCached];
inline int g_nextEvict = 0;

// How hard get() tries — see the header comment for who uses which.
enum class Fetch : uint8_t { CacheOnly, IfMissing, Force };

// Bumped once per Force resolveAll(), so an icon picked in two places is
// re-downloaded once per pass, not once per use.
inline uint32_t g_forcePass = 0;

// Buffers a Force pass replaced. The main loop may be mid-draw from one
// (resolveAll runs on the background refresh task), so they're only freed at
// the start of the NEXT Force pass, long after any draw that saw them.
inline uint8_t* g_retired[kMaxCached] = {};

inline Slot& claimSlot(const char* name, int size, const freeink::Icon& icon, uint8_t* buf) {
  Slot& s = g_slots[g_nextEvict];
  if (s.valid && s.buf) free(s.buf);
  s = Slot{};
  s.valid = true;
  snprintf(s.name, sizeof(s.name), "%s", name);
  s.size = size;
  s.icon = icon;
  s.buf = buf;
  g_nextEvict = (g_nextEvict + 1) % kMaxCached;
  return s;
}

// nullptr on any failure (no card, no network, unknown icon, server
// unreachable) — callers treat that as "no custom icon, use the existing
// fallback," never a crash.
inline const freeink::Icon* get(const char* name, int size, const char* token, Fetch mode) {
  if (!name || !*name) return nullptr;

  Slot* hit = nullptr;
  for (auto& s : g_slots) {
    if (s.valid && s.size == size && !strcmp(s.name, name)) { hit = &s; break; }
  }

  if (mode == Fetch::Force && !(hit && hit->forcedPass == g_forcePass)) {
    freeink::Icon icon{};
    if (uint8_t* buf = fetchAndCache(name, size, token, icon)) {
      if (!hit) {
        Slot& s = claimSlot(name, size, icon, buf);
        s.forcedPass = g_forcePass;
        return &s.icon;
      }
      for (auto& r : g_retired)
        if (!r) { r = hit->buf; break; }  // full -> leak it; bounded by kMaxCached per pass
      hit->icon = icon;  // the old buffer stays valid until it's retired
      hit->buf = buf;
      hit->forcedPass = g_forcePass;
      return &hit->icon;
    }
    // Server unreachable -> keep whatever we already had (fall through).
  }
  if (hit) return &hit->icon;

  freeink::Icon icon{};
  uint8_t* buf = loadFromSd(name, size, icon);
  if (!buf && mode != Fetch::CacheOnly) buf = fetchAndCache(name, size, token, icon);
  if (!buf) return nullptr;
  return &claimSlot(name, size, icon, buf).icon;
}

// ---- resolved-per-item pointers, refreshed once per config fetch --------

inline const freeink::Icon* hubIcons[deviceconfig::kMaxHubItems] = {};
inline const freeink::Icon* lightIcons[deviceconfig::kMaxLights] = {};
inline const freeink::Icon* sceneIcons[deviceconfig::kMaxScenes] = {};
inline const freeink::Icon* blindIcons[6] = {};

// `token` may be nullptr for CacheOnly (no network use at all).
inline void resolveAll(const char* token, Fetch mode) {
  if (mode == Fetch::Force) {
    for (auto& r : g_retired) { free(r); r = nullptr; }
    ++g_forcePass;
  }
  for (int i = 0; i < deviceconfig::hubItemCount; ++i)
    hubIcons[i] = get(deviceconfig::hubItems[i].icon, kHubIconSize, token, mode);
  for (int i = 0; i < deviceconfig::lightCount; ++i)
    lightIcons[i] = get(deviceconfig::lights[i].icon, kChipIconSize, token, mode);
  for (int i = 0; i < deviceconfig::sceneCount; ++i)
    sceneIcons[i] = get(deviceconfig::scenes[i].icon, kChipIconSize, token, mode);
  for (int i = 0; i < deviceconfig::blindsItemCount && i < 6; ++i)
    blindIcons[i] = get(deviceconfig::blindsItems[i].icon, kChipIconSize, token, mode);
}

}  // namespace mdiicon
