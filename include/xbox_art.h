#pragma once

// ===========================================================================
// xbox_art — fetches Xbox box art / hero art (a JPEG: the media_player's
// entity_picture for the hero, or a game's configured art URL for a library
// row), decodes it with TJpg_Decoder, and dithers it down to a 1-bpp bitmap
// — the same Mask1 format every baked freeink::Icon and album_art.h's output
// use, so it draws with the ordinary ui.icon()/iconScaled().
//
// Unlike album_art.h (one image, refetched every track change, RAM-only),
// the Xbox screen needs several images live at once (a hero + up to 6 row
// thumbnails) and the same game's art is shown again every time its row
// scrolls back into view, so this module:
//   - is a plain function, not a singleton: the caller owns each returned
//     buffer (ps_malloc'd) and frees it when that slot's content changes.
//   - caches the decoded, already-dithered bitmap to SD, keyed by product ID
//     + size, so a game's art is fetched and decoded once ever, then just
//     read back on every later page view / re-entry. Best-effort: a missing
//     or unmountable card just means every view re-fetches (get() still
//     works, it just never short-circuits through the cache).
//   - shares album_art.h's TJpg_Decoder singleton, so decode calls go
//     through screen_common.h's g_jpegDecodeBusy guard.
// ===========================================================================

#include <Arduino.h>
#include <ctype.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <TJpg_Decoder.h>
#include <SDCardManager.h>

#include "Icon.h"
#include "http_json.h"  // httpjson::resolveHost()
#include "screen_common.h"  // g_jpegDecodeBusy guard
#include "persist.h"  // persist::g_ready — the card is already mounted (or not) by boot

namespace xboxart {

// Sized to what screen_xbox.h's vertical budget actually fits (portrait
// 480x800, minus the header/footer/6 rows) rather than a round number —
// see its kHeroArtSize/kThumbSz, which must match these exactly (the
// placeholder glyph and the real art are drawn at the same slot size).
inline constexpr int16_t kHeroSize = 140;  // the hero art square
inline constexpr int16_t kRowSize  = 48;   // a library row's thumbnail square

inline constexpr const char* kCacheDir = "/switchboard/xboxart";

inline const uint8_t kBayer4x4[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

inline uint8_t luminance(uint16_t rgb565) {
  const uint8_t r5 = (rgb565 >> 11) & 0x1F, g6 = (rgb565 >> 5) & 0x3F, b5 = rgb565 & 0x1F;
  const uint8_t r8 = static_cast<uint8_t>(r5 * 255 / 31);
  const uint8_t g8 = static_cast<uint8_t>(g6 * 255 / 63);
  const uint8_t b8 = static_cast<uint8_t>(b5 * 255 / 31);
  return static_cast<uint8_t>((r8 * 30 + g8 * 59 + b8 * 11) / 100);
}

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

// Decode-in-progress state — file-scope because TJpg_Decoder's callback is a
// plain function pointer, not something that can capture (same constraint
// as album_art.h's detail namespace; this is a SEPARATE buffer so a
// concurrent Music decode can't collide with it — g_jpegDecodeBusy is what
// keeps the two from running at literally the same time).
namespace detail {
inline uint16_t* g_rgb = nullptr;
inline int g_decW = 0, g_decH = 0;

inline bool onBlock(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* data) {
  if (!g_rgb) return false;
  for (uint16_t row = 0; row < h; ++row) {
    const int py = y + row;
    if (py < 0 || py >= g_decH) continue;
    for (uint16_t col = 0; col < w; ++col) {
      const int px = x + col;
      if (px < 0 || px >= g_decW) continue;
      g_rgb[py * g_decW + px] = data[row * w + col];
    }
  }
  return true;
}
}  // namespace detail

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

// Fetch `url` over HTTP, decode as JPEG, and dither into a freshly
// ps_malloc'd `size`x`size` 1bpp buffer (caller frees). nullptr on any
// network/decode failure — never partial output.
inline uint8_t* fetchAndDecode(const char* host, uint16_t port, const char* token, const char* url,
                               int16_t size) {
  if (!url || !*url) return nullptr;

  const IPAddress ip = httpjson::resolveHost(host);
  if (ip == IPAddress(0, 0, 0, 0)) return nullptr;

  char full[224];
  // A fully-qualified art URL (browse_media thumbnails are sometimes
  // absolute) is used as-is; a relative one (entity_picture, or a
  // configured path) is resolved against the HA host, same convention as
  // album_art.h.
  if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
    snprintf(full, sizeof(full), "%s", url);
  } else {
    snprintf(full, sizeof(full), "http://%s:%u%s", ip.toString().c_str(), port, url);
  }

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(full)) return nullptr;
  if (token && *token) {
    char auth[320];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    http.addHeader("Authorization", auth);
  }
  const int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return nullptr;
  }
  const int len = http.getSize();
  if (len <= 0 || len > 3 * 1024 * 1024) {
    http.end();
    return nullptr;
  }
  uint8_t* jpg = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(len)));
  if (!jpg) {
    http.end();
    return nullptr;
  }
  WiFiClient* stream = http.getStreamPtr();
  int got = 0;
  const uint32_t deadline = millis() + 8000;
  while (got < len && millis() < deadline) {
    const int avail = stream->available();
    if (avail <= 0) {
      delay(5);
      continue;
    }
    const int want = avail < (len - got) ? avail : (len - got);
    const int n = stream->readBytes(jpg + got, want);
    if (n <= 0) break;
    got += n;
  }
  http.end();
  if (got < len) {
    free(jpg);
    return nullptr;
  }

  if (!acquireJpegDecoder()) {  // serialize against album_art.h's decoder use
    free(jpg);
    return nullptr;
  }
  uint16_t srcW = 0, srcH = 0;
  if (TJpgDec.getJpgSize(&srcW, &srcH, jpg, len) != JDR_OK || srcW == 0 || srcH == 0) {
    free(jpg);
    releaseJpegDecoder();
    return nullptr;
  }
  uint8_t scale = 1;
  while (scale < 8 && (srcW / (scale * 2)) >= size && (srcH / (scale * 2)) >= size) scale *= 2;
  TJpgDec.setJpgScale(scale);

  detail::g_decW = srcW / scale;
  detail::g_decH = srcH / scale;
  detail::g_rgb = static_cast<uint16_t*>(
      ps_malloc(static_cast<size_t>(detail::g_decW) * detail::g_decH * sizeof(uint16_t)));
  if (!detail::g_rgb) {
    free(jpg);
    releaseJpegDecoder();
    return nullptr;
  }
  TJpgDec.setCallback(detail::onBlock);
  const JRESULT jr = TJpgDec.drawJpg(0, 0, jpg, len);
  free(jpg);
  releaseJpegDecoder();
  if (jr != JDR_OK) {
    free(detail::g_rgb);
    detail::g_rgb = nullptr;
    return nullptr;
  }

  const size_t rowBytes = (size + 7) / 8;
  uint8_t* bits = static_cast<uint8_t*>(ps_malloc(rowBytes * size));
  if (!bits) {
    free(detail::g_rgb);
    detail::g_rgb = nullptr;
    return nullptr;
  }
  memset(bits, 0xFF, rowBytes * size);  // all-white

  for (int16_t y = 0; y < size; ++y) {
    const int sy = y * detail::g_decH / size;
    for (int16_t x = 0; x < size; ++x) {
      const int sx = x * detail::g_decW / size;
      const uint8_t lum = luminance(detail::g_rgb[sy * detail::g_decW + sx]);
      const uint8_t threshold = static_cast<uint8_t>(kBayer4x4[y & 3][x & 3] * 17);
      if (lum < threshold) bits[y * rowBytes + (x >> 3)] &= static_cast<uint8_t>(~(0x80 >> (x & 7)));
    }
  }
  free(detail::g_rgb);
  detail::g_rgb = nullptr;
  return bits;
}

// Top-level entry: SD cache first (keyed by `slug`+`size`), then network on
// a miss — caching the result for next time. `slug` should already be
// filesystem-safe (see slugify()); callers that key by product ID can pass
// it directly if the integration's IDs are already alnum, or run it through
// slugify() first if not. Returns a ps_malloc'd size x size 1bpp buffer the
// caller owns, or nullptr — a null result means "draw the placeholder
// glyph," never a stall.
inline uint8_t* get(const char* host, uint16_t port, const char* token, const char* slug,
                    const char* url, int16_t size) {
  if (uint8_t* cached = loadFromSd(slug, size)) return cached;
  uint8_t* fresh = fetchAndDecode(host, port, token, url, size);
  if (fresh) saveToSd(slug, size, fresh);
  return fresh;
}

inline freeink::Icon icon(const uint8_t* bits, int16_t size) {
  return freeink::Icon{static_cast<uint16_t>(size), static_cast<uint16_t>(size),
                       static_cast<int16_t>(size / 2), bits};
}

}  // namespace xboxart
