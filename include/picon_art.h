#pragma once

// ===========================================================================
// picon_art — channel logos (picons) for the Receiver page, prepared by the
// server's /api/art like album art (server_art.h): scaled whole onto white
// and dithered to 1 bpp, so the device only blits them.
//
// Two sizes: the channel on now (kNowW x kNowH, beside its name) and each
// favourite button's (kFavW x kFavH). Each bitmap is kept on SD keyed by its
// src and size, so a wake paints the picons it had without the network, and
// a channel's picon is downloaded once — not once per wake. Fetched on the
// network worker only when a src changes (loadIfChanged()).
// ===========================================================================

#include <Arduino.h>
#include <SDCardManager.h>

#include "Icon.h"
#include "ha_client.h"
#include "server_art.h"
#include "persist.h"  // persist::g_ready

namespace piconart {

inline constexpr int16_t kNowW = 96, kNowH = 58;  // picons are ~5:3
inline constexpr int16_t kFavW = 60, kFavH = 36;
inline constexpr int kFavs = 6;
inline constexpr const char* kCacheDir = "/switchboard/picons";

struct Slot {
  char src[160] = "";
  uint8_t* bits = nullptr;  // PSRAM, null = none
};
inline Slot g_now;
inline Slot g_fav[kFavs];

inline size_t bytesFor(int16_t w, int16_t h) { return static_cast<size_t>((w + 7) / 8) * h; }

// FNV-1a of the src: a stable, filesystem-safe cache file name.
inline uint32_t hashOf(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
  return h;
}
inline void cachePath(const char* src, int16_t w, int16_t h, char* out, size_t cap) {
  snprintf(out, cap, "%s/%08lx_%dx%d.1bpp", kCacheDir, static_cast<unsigned long>(hashOf(src)), w, h);
}

inline uint8_t* loadFromSd(const char* src, int16_t w, int16_t h) {
  if (!persist::g_ready) return nullptr;
  char path[96];
  cachePath(src, w, h, path, sizeof(path));
  FsFile f;
  if (!SdMan.openFileForRead("picon", path, f)) return nullptr;
  const size_t want = bytesFor(w, h);
  uint8_t* buf = static_cast<uint8_t*>(ps_malloc(want));
  const int n = buf ? f.read(buf, want) : -1;
  f.close();
  if (n < 0 || static_cast<size_t>(n) != want) {
    free(buf);
    return nullptr;
  }
  return buf;
}
inline void saveToSd(const char* src, int16_t w, int16_t h, const uint8_t* bits) {
  if (!persist::g_ready || !SdMan.ensureDirectoryExists(kCacheDir)) return;
  char path[96];
  cachePath(src, w, h, path, sizeof(path));
  FsFile f;
  if (!SdMan.openFileForWrite("picon", path, f)) return;
  f.write(bits, bytesFor(w, h));
  f.close();
}

// Point `slot` at `src`: from SD, else (if `network`) from the server. Returns
// whether the slot changed. The old bitmap is freed only after the new one is
// in place.
inline bool setSlot(Slot& slot, const char* src, int16_t w, int16_t h, bool network) {
  if (!src) src = "";
  if (!strcmp(slot.src, src) && (slot.bits || !*src)) return false;
  uint8_t* bits = nullptr;
  if (*src) {
    bits = loadFromSd(src, w, h);
    if (!bits && network) {
      bits = serverart::fetchMask1(src, w, h, /*contain=*/true);
      if (bits) saveToSd(src, w, h, bits);
    }
    if (!bits && !network) return false;  // try again when the network's up
  }
  uint8_t* old = slot.bits;
  slot.bits = bits;
  snprintf(slot.src, sizeof(slot.src), "%s", bits ? src : (network ? src : ""));
  free(old);
  return true;
}

// Bring every slot in line with haclient::receiverInfo. `network` false =
// the SD cache only (a wake paint before Wi-Fi). Returns whether any changed.
inline bool load(bool network) {
  bool changed = setSlot(g_now, haclient::receiverInfo.picon, kNowW, kNowH, network);
  for (int i = 0; i < kFavs; ++i)
    changed |= setSlot(g_fav[i], haclient::receiverInfo.favPicon[i], kFavW, kFavH, network);
  return changed;
}

inline freeink::Icon nowIcon() {
  return freeink::Icon{static_cast<uint16_t>(kNowW), static_cast<uint16_t>(kNowH), static_cast<int16_t>(kNowH / 2),
                       g_now.bits};
}
inline freeink::Icon favIcon(int i) {
  return freeink::Icon{static_cast<uint16_t>(kFavW), static_cast<uint16_t>(kFavH), static_cast<int16_t>(kFavH / 2),
                       g_fav[i].bits};
}

}  // namespace piconart
