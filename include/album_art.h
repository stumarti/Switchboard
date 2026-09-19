#pragma once

// ===========================================================================
// album_art — fetches a Home Assistant media_player's entity_picture (a
// JPEG, usually served from /api/media_player_proxy/...), decodes it with
// TJpg_Decoder, and dithers it down to a 1-bpp bitmap sized to the Music
// page's artwork square — the same Mask1 format (bits.h: bit 0 = draw, bit 1
// = skip) every baked freeink::Icon uses, so it draws with the ordinary
// ui.icon() — no new drawing primitive needed.
//
// Runtime-built, not baked at compile time like weather_icons.h's icons: the
// source image changes with whatever's playing. fetch() allocates its
// buffers from PSRAM and is only worth calling when entity_picture's URL
// actually changed (a new track) — screen_music.h's background task does
// that comparison before calling in.
// ===========================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <TJpg_Decoder.h>

#include "Icon.h"
#include "http_json.h"  // httpjson::resolveHost()

namespace albumart {

inline constexpr int16_t kArtSize = 280;  // the Music page's artwork square

// The current artwork, once decoded — bits is PSRAM-allocated, null until a
// fetch succeeds. g_sourceUrl is the entity_picture path it was built from,
// so the caller can tell whether a re-fetch is actually needed.
inline uint8_t* g_bits = nullptr;
inline bool g_ok = false;
inline char g_sourceUrl[160] = "";

inline const uint8_t kBayer4x4[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

inline uint8_t luminance(uint16_t rgb565) {
  const uint8_t r5 = (rgb565 >> 11) & 0x1F, g6 = (rgb565 >> 5) & 0x3F, b5 = rgb565 & 0x1F;
  const uint8_t r8 = static_cast<uint8_t>(r5 * 255 / 31);
  const uint8_t g8 = static_cast<uint8_t>(g6 * 255 / 63);
  const uint8_t b8 = static_cast<uint8_t>(b5 * 255 / 31);
  return static_cast<uint8_t>((r8 * 30 + g8 * 59 + b8 * 11) / 100);
}

// Decode-in-progress state — file-scope because TJpg_Decoder's callback is a
// plain function pointer (SketchCallback), not something that can capture.
namespace detail {
inline uint16_t* g_rgb = nullptr;  // the full decoded image, RGB565, g_decW x g_decH
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

// Fetch + decode + dither `path` (an entity_picture value) into g_bits.
// Frees any previous artwork first; leaves none (g_ok=false) on any
// network/decode failure rather than showing something stale or wrong.
inline bool fetch(const char* host, uint16_t port, const char* token, const char* path) {
  g_ok = false;
  if (g_bits) { free(g_bits); g_bits = nullptr; }
  g_sourceUrl[0] = 0;
  if (!path || !*path) return false;

  const IPAddress ip = httpjson::resolveHost(host);
  if (ip == IPAddress(0, 0, 0, 0)) return false;

  char url[224];
  snprintf(url, sizeof(url), "http://%s:%u%s", ip.toString().c_str(), port, path);

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(url)) return false;
  if (token && *token) {
    char auth[320];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    http.addHeader("Authorization", auth);
  }
  const int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return false;
  }

  const int len = http.getSize();
  if (len <= 0 || len > 3 * 1024 * 1024) {  // sanity cap — a thumbnail, not a mixtape
    http.end();
    return false;
  }
  uint8_t* jpg = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(len)));
  if (!jpg) {
    http.end();
    return false;
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
    return false;
  }

  uint16_t srcW = 0, srcH = 0;
  if (TJpgDec.getJpgSize(&srcW, &srcH, jpg, len) != JDR_OK || srcW == 0 || srcH == 0) {
    free(jpg);
    return false;
  }
  // Smallest power-of-2 decode scale that still leaves BOTH dimensions >=
  // kArtSize, so the resize pass below only ever downsamples.
  uint8_t scale = 1;
  while (scale < 8 && (srcW / (scale * 2)) >= kArtSize && (srcH / (scale * 2)) >= kArtSize) scale *= 2;
  TJpgDec.setJpgScale(scale);

  detail::g_decW = srcW / scale;
  detail::g_decH = srcH / scale;
  detail::g_rgb = static_cast<uint16_t*>(
      ps_malloc(static_cast<size_t>(detail::g_decW) * detail::g_decH * sizeof(uint16_t)));
  if (!detail::g_rgb) {
    free(jpg);
    return false;
  }

  TJpgDec.setCallback(detail::onBlock);
  const JRESULT jr = TJpgDec.drawJpg(0, 0, jpg, len);
  free(jpg);
  if (jr != JDR_OK) {
    free(detail::g_rgb);
    detail::g_rgb = nullptr;
    return false;
  }

  // Resize (nearest-neighbor) + ordered-dither straight into the final 1bpp
  // buffer — MSB-first, bit 0 = ink, same packing every baked Icon uses.
  const int rowBytes = (kArtSize + 7) / 8;
  g_bits = static_cast<uint8_t*>(ps_malloc(static_cast<size_t>(rowBytes) * kArtSize));
  if (!g_bits) {
    free(detail::g_rgb);
    detail::g_rgb = nullptr;
    return false;
  }
  memset(g_bits, 0xFF, static_cast<size_t>(rowBytes) * kArtSize);  // start all-white

  for (int y = 0; y < kArtSize; ++y) {
    const int sy = y * detail::g_decH / kArtSize;
    for (int x = 0; x < kArtSize; ++x) {
      const int sx = x * detail::g_decW / kArtSize;
      const uint8_t lum = luminance(detail::g_rgb[sy * detail::g_decW + sx]);
      const uint8_t threshold = static_cast<uint8_t>(kBayer4x4[y & 3][x & 3] * 17);  // 0..255
      if (lum < threshold) g_bits[y * rowBytes + (x >> 3)] &= static_cast<uint8_t>(~(0x80 >> (x & 7)));
    }
  }
  free(detail::g_rgb);
  detail::g_rgb = nullptr;

  snprintf(g_sourceUrl, sizeof(g_sourceUrl), "%s", path);
  g_ok = true;
  return true;
}

inline void clear() {
  if (g_bits) { free(g_bits); g_bits = nullptr; }
  g_ok = false;
  g_sourceUrl[0] = 0;
}

inline freeink::Icon icon() {
  return freeink::Icon{static_cast<uint16_t>(kArtSize), static_cast<uint16_t>(kArtSize),
                       static_cast<int16_t>(kArtSize / 2), g_bits};
}

}  // namespace albumart
