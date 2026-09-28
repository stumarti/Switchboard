#pragma once

// ===========================================================================
// xbox_art — Xbox box art / hero art (the media_player's entity_picture for
// the hero, a game's configured art URL for a library row), prepared by the
// Switchboard server (server_art.h) into a 1-bpp Mask1 bitmap — the format
// every freeink::Icon and album_art.h use, so it draws with the ordinary
// ui.icon()/iconScaled(). No JPEG ever reaches the device.
//
// Unlike album_art.h (one image, refetched every track change, RAM-only),
// the Xbox screen needs several images live at once (a hero + up to 6 row
// thumbnails) and the same game's art is shown again every time its row
// scrolls back into view, so this module:
//   - is a plain function, not a singleton: the caller owns each returned
//     buffer (ps_malloc'd) and frees it when that slot's content changes.
//   - caches each bitmap to SD, keyed by product ID + size, so a game's art
//     is fetched once ever, then just read back on every later page view /
//     re-entry. Best-effort: a missing or unmountable card just means every
//     view re-fetches (get() still works, it just never short-circuits
//     through the cache).
// ===========================================================================

#include <Arduino.h>
#include <ctype.h>
#include <SDCardManager.h>

#include "Icon.h"
#include "server_art.h"
#include "persist.h"  // persist::g_ready — the card is already mounted (or not) by boot

namespace xboxart {

// Sized to what screen_xbox.h's vertical budget actually fits (portrait
// 480x800, minus the header/footer/6 rows) rather than a round number —
// see its kHeroArtSize/kThumbSz, which must match these exactly (the
// placeholder glyph and the real art are drawn at the same slot size).
inline constexpr int16_t kHeroSize = 140;  // the hero art square
inline constexpr int16_t kRowSize  = 48;   // a library row's thumbnail square

inline constexpr const char* kCacheDir = "/switchboard/xboxart";

inline size_t bytesFor(int16_t size) {
  return static_cast<size_t>((size + 7) / 8) * size;
}

// Filesystem-safe cache key from a product ID / URL: keep alnum, fold
// everything else to '_', capped well under SdFat's path limits.
inline void slugify(const char* raw, char* out, size_t outCap) {
  size_t o = 0;
  for (const char* p = raw; *p && o + 1 < outCap; ++p) {
    const char c = *p;
    out[o++] = isalnum(static_cast<unsigned char>(c)) ? c : '_';
  }
  out[o] = 0;
  if (o == 0 && outCap > 3) snprintf(out, outCap, "art");
}

inline void cachePath(const char* slug, int16_t size, char* out, size_t outCap) {
  snprintf(out, outCap, "%s/%s_%d.1bpp", kCacheDir, slug, static_cast<int>(size));
}

// Read a cached bitmap for `slug`/`size` from SD. Returns a ps_malloc'd
// buffer (caller frees), or nullptr if the card isn't mounted, there's no
// cache entry, or the file is the wrong size (a stale/corrupt entry — the
// caller re-fetches rather than trusting a partial read).
inline uint8_t* loadFromSd(const char* slug, int16_t size) {
  if (!persist::g_ready) return nullptr;
  char path[96];
  cachePath(slug, size, path, sizeof(path));
  const size_t want = bytesFor(size);
  FsFile f;
  if (!SdMan.openFileForRead("xboxart", path, f)) return nullptr;
  uint8_t* buf = static_cast<uint8_t*>(ps_malloc(want));
  if (!buf) {
    f.close();
    return nullptr;
  }
  const int n = f.read(buf, want);
  f.close();
  if (n < 0 || static_cast<size_t>(n) != want) {
    free(buf);
    return nullptr;
  }
  return buf;
}

// Best-effort write-back; failures are silent (the in-RAM bitmap the caller
// already has is still good, it just won't short-circuit next time).
inline void saveToSd(const char* slug, int16_t size, const uint8_t* bits) {
  if (!persist::g_ready) return;
  if (!SdMan.ensureDirectoryExists(kCacheDir)) return;
  char path[96];
  cachePath(slug, size, path, sizeof(path));
  FsFile f;
  if (!SdMan.openFileForWrite("xboxart", path, f)) return;
  f.write(bits, bytesFor(size));
  f.close();
}

// Top-level entry: SD cache first (keyed by `slug`+`size`), then network on
// a miss — caching the result for next time. `slug` should already be
// filesystem-safe (see slugify()); callers that key by product ID can pass
// it directly if the integration's IDs are already alnum, or run it through
// slugify() first if not. Returns a ps_malloc'd size x size 1bpp buffer the
// caller owns, or nullptr — a null result means "draw the placeholder
// glyph," never a stall.
inline uint8_t* get(const char* slug, const char* url, int16_t size) {
  if (uint8_t* cached = loadFromSd(slug, size)) return cached;
  uint8_t* fresh = serverart::fetchMask1(url, size, size);
  if (fresh) saveToSd(slug, size, fresh);
  return fresh;
}

inline freeink::Icon icon(const uint8_t* bits, int16_t size) {
  return freeink::Icon{static_cast<uint16_t>(size), static_cast<uint16_t>(size),
                       static_cast<int16_t>(size / 2), bits};
}

}  // namespace xboxart
