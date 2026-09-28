#pragma once

// ===========================================================================
// album_art — the Music page's artwork: the media_player's entity_picture,
// prepared by the Switchboard server (server_art.h) into a 1-bpp bitmap the
// size of the artwork square, in the Mask1 format every freeink::Icon uses,
// so it draws with the ordinary ui.icon(). No JPEG ever reaches the device.
//
// One image, held in PSRAM. fetch() is only worth calling when
// entity_picture actually changed (a new track): callers compare against
// g_sourceUrl first.
// ===========================================================================

#include <Arduino.h>

#include "Icon.h"
#include "server_art.h"

namespace albumart {

inline constexpr int16_t kArtSize = 280;  // the Music page's artwork square

// The current artwork — PSRAM, null until a fetch succeeds. g_sourceUrl is
// the entity_picture it was built from, so a caller can tell whether a
// re-fetch is needed.
inline uint8_t* g_bits = nullptr;
inline bool g_ok = false;
inline char g_sourceUrl[160] = "";

inline void clear() {
  if (g_bits) {
    free(g_bits);
    g_bits = nullptr;
  }
  g_ok = false;
  g_sourceUrl[0] = 0;
}

// The artwork for `path` (an entity_picture value). Leaves none (g_ok =
// false) on failure rather than showing the previous track's.
inline bool fetch(const char* path) {
  clear();
  if (!path || !*path) return false;
  g_bits = serverart::fetchMask1(path, kArtSize, kArtSize);
  if (!g_bits) return false;
  snprintf(g_sourceUrl, sizeof(g_sourceUrl), "%s", path);
  g_ok = true;
  return true;
}

inline freeink::Icon icon() {
  return freeink::Icon{static_cast<uint16_t>(kArtSize), static_cast<uint16_t>(kArtSize),
                       static_cast<int16_t>(kArtSize / 2), g_bits};
}

}  // namespace albumart
