#pragma once

// ===========================================================================
// font_pack — loads a compiled font theme from SD (/switchboard/fonts.pack,
// the "SBF1" format Switchboard-Server's lib/assets/pack-format.js writes)
// and downloads a fresh one over HTTP when the server has a newer version.
// Same shape as icon_pack.h - see that file's header comment for the shared
// design rationale (PSRAM budget, tmp-then-rename crash safety, xbox_art.h-
// style streaming download).
//
// One difference from icon_pack.h: a glyph's on-disk 7-byte record is NOT
// reinterpret_cast onto a `FontGlyph*` directly (the struct's in-memory
// layout/padding isn't a format guarantee), so each face's FontGlyph[] array
// is built field-by-field into its own small ps_malloc'd buffer. Bitmap
// blobs, being plain byte arrays with no struct layout involved, point
// directly into the one whole-file buffer, same as icon_pack.h's Icon.bits.
//
// `DisplayTarget::setFont(slot, const BitmapFont&)` stores the ADDRESS of
// whatever's passed (freeink-sdk's FreeInkUIDisplayTarget.h) - so every
// loaded BitmapFont here lives in g_fonts[], a static array, never a stack
// temporary; fonts.h hands out references into it.
// ===========================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <SDCardManager.h>
#include <FreeInkUIFont.h>
#include <string.h>

#include "config.h"
#include "http_json.h"
#include "persist.h"

namespace fontpack {

inline constexpr const char* kPath = "/switchboard/fonts.pack";
inline constexpr const char* kTmpPath = "/switchboard/fonts.pack.tmp";
inline constexpr uint32_t kMaxPackBytes = 4 * 1024 * 1024;
inline constexpr int kMaxFaces = 8;  // generous headroom over the 5 the firmware actually binds

struct LoadedFace {
  char slot[16];
  freeink::ui::BitmapFont font;
};

inline uint8_t* g_buf = nullptr;                 // ps_malloc'd whole-file buffer; bitmaps point into this
inline LoadedFace g_faces[kMaxFaces];             // static storage — setFont() keeps a pointer to these
inline int g_count = 0;
inline bool loaded = false;

inline uint16_t rdU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t rdU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void unload() {
  for (int i = 0; i < g_count; ++i) {
    if (g_faces[i].font.glyphs) {
      free(const_cast<freeink::ui::FontGlyph*>(g_faces[i].font.glyphs));
    }
  }
  memset(g_faces, 0, sizeof(g_faces));
  if (g_buf) { free(g_buf); g_buf = nullptr; }
  g_count = 0;
  loaded = false;
}

inline bool loadFromBuffer(uint8_t* buf, uint32_t len) {
  if (len < 8 || memcmp(buf, "SBF1", 4) != 0) {
    free(buf);
    return false;
  }
  const uint8_t faceCount = buf[4];
  if (faceCount > kMaxFaces) {
    free(buf);
    return false;
  }
  const uint32_t dirEntryLen = 39;
  const uint32_t dirStart = 8;
  if (dirStart + static_cast<uint32_t>(faceCount) * dirEntryLen > len) {
    free(buf);
    return false;
  }

  LoadedFace faces[kMaxFaces] = {};

  for (uint8_t i = 0; i < faceCount; ++i) {
    const uint8_t* e = buf + dirStart + i * dirEntryLen;
    memcpy(faces[i].slot, e, 16);
    faces[i].slot[15] = 0;
    const uint8_t* p = e + 16;
    const uint16_t first = rdU16(p); p += 2;
    const uint16_t last = rdU16(p); p += 2;
    const uint8_t yAdvance = *p++;
    const uint8_t ascent = *p++;
    const uint8_t maxW = *p++;
    const uint8_t maxH = *p++;
    const uint8_t bpp = *p++;
    const uint32_t glyphsOffset = rdU32(p); p += 4;
    const uint16_t glyphsCount = rdU16(p); p += 2;
    const uint32_t bitmapOffset = rdU32(p); p += 4;
    const uint32_t bitmapLength = rdU32(p);

    if (static_cast<uint64_t>(glyphsOffset) + static_cast<uint64_t>(glyphsCount) * 7 > len ||
        static_cast<uint64_t>(bitmapOffset) + bitmapLength > len) {
      for (uint8_t j = 0; j < i; ++j) free(const_cast<freeink::ui::FontGlyph*>(faces[j].font.glyphs));
      free(buf);
      return false;
    }

    auto* glyphs = static_cast<freeink::ui::FontGlyph*>(
        ps_malloc(sizeof(freeink::ui::FontGlyph) * glyphsCount));
    if (!glyphs) {
      for (uint8_t j = 0; j < i; ++j) free(const_cast<freeink::ui::FontGlyph*>(faces[j].font.glyphs));
      free(buf);
      return false;
    }
    for (uint16_t g = 0; g < glyphsCount; ++g) {
      const uint8_t* ge = buf + glyphsOffset + g * 7;
      glyphs[g].bitmapOffset = rdU16(ge);
      glyphs[g].width = ge[2];
      glyphs[g].height = ge[3];
      glyphs[g].xAdvance = ge[4];
      glyphs[g].xOffset = static_cast<int8_t>(ge[5]);
      glyphs[g].yOffset = static_cast<int8_t>(ge[6]);
    }

    faces[i].font = freeink::ui::BitmapFont{buf + bitmapOffset, glyphs,        first, last,
                                            yAdvance,           ascent,       maxW,  maxH,
                                            bpp};
  }

  unload();
  g_buf = buf;
  memcpy(g_faces, faces, sizeof(faces));
  g_count = faceCount;
  loaded = true;
  return true;
}

inline bool load() {
  if (!persist::g_ready) return false;

  FsFile f;
  if (!SdMan.openFileForRead("fontpack", kPath, f)) return false;
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

inline bool downloadAndLoad(const char* token) {
  const IPAddress ip = httpjson::resolveHost(SWITCHBOARD_SERVER_HOST);
  if (ip == IPAddress(0, 0, 0, 0)) return false;

  char url[96];
  snprintf(url, sizeof(url), "http://%s:%u/api/theme/fonts.pack", ip.toString().c_str(),
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
  if (!SdMan.openFileForWrite("fontpack", kTmpPath, f)) {
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

  SdMan.remove(kPath);
  if (!SdMan.rename(kTmpPath, kPath)) {
    SdMan.remove(kTmpPath);
    return false;
  }

  return load();
}

inline const freeink::ui::BitmapFont* find(const char* slot) {
  if (!loaded) return nullptr;
  for (int i = 0; i < g_count; ++i) {
    if (!strncmp(g_faces[i].slot, slot, sizeof(g_faces[i].slot))) return &g_faces[i].font;
  }
  return nullptr;
}

}  // namespace fontpack
