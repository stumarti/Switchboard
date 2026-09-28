#pragma once

// ===========================================================================
// server_art — a picture (album art, box art) as a ready-to-draw bitmap,
// prepared by the Switchboard server's GET /api/art: resized and dithered
// there into the Mask1 format every freeink::Icon uses (1 bpp, rows padded
// to whole bytes, MSB first, 0 = ink). So the device never downloads or
// decodes a JPEG — just w*h/8 bytes it can hand straight to ui.icon().
//
// `src` is what Home Assistant gives as an entity_picture (a path on HA,
// which the server fetches with its own token) or any http(s) URL.
// ===========================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include "config.h"
#include "http_json.h"       // httpjson::resolveHost()
#include "pairing_client.h"  // the bearer token

namespace serverart {

inline size_t maskBytes(int16_t w, int16_t h) {
  return static_cast<size_t>((w + 7) / 8) * static_cast<size_t>(h);
}

// Percent-encode `in` for a query string.
inline void urlEncode(const char* in, char* out, size_t cap) {
  static const char* hex = "0123456789ABCDEF";
  size_t o = 0;
  for (const char* p = in; *p && o + 4 < cap; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out[o++] = static_cast<char>(c);
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
}

// Fetch `src` as a w x h Mask1 bitmap. Returns a ps_malloc'd buffer the
// caller owns (free()), or nullptr on any failure — never partial output.
// `contain`: the whole picture on white (a logo, e.g. a picon) rather than
// filling the box and cropping (album art).
inline uint8_t* fetchMask1(const char* src, int16_t w, int16_t h, bool contain = false) {
  if (!src || !*src || !pairing::token[0]) return nullptr;
  const IPAddress ip = httpjson::resolveHost(SWITCHBOARD_SERVER_HOST);
  if (ip == IPAddress(0, 0, 0, 0)) return nullptr;

  char enc[480];
  urlEncode(src, enc, sizeof(enc));
  char url[600];
  snprintf(url, sizeof(url), "http://%s:%u/api/art?src=%s&w=%d&h=%d&fmt=mask1%s", ip.toString().c_str(),
           SWITCHBOARD_SERVER_PORT, enc, w, h, contain ? "&fit=contain" : "");

  HTTPClient http;
  http.setTimeout(10000);  // the server may be fetching + converting the original
  if (!http.begin(url)) return nullptr;
  char auth[112];
  snprintf(auth, sizeof(auth), "Bearer %s", pairing::token);
  http.addHeader("Authorization", auth);
  const int code = http.GET();
  const size_t want = maskBytes(w, h);
  if (code != 200 || http.getSize() != static_cast<int>(want)) {
    if (code == 401) pairing::noteUnauthorized();
    http.end();
    return nullptr;
  }
  uint8_t* bits = static_cast<uint8_t*>(ps_malloc(want));
  if (!bits) {
    http.end();
    return nullptr;
  }
  WiFiClient* stream = http.getStreamPtr();
  size_t got = 0;
  const uint32_t deadline = millis() + 10000;
  while (got < want && millis() < deadline) {
    const int avail = stream->available();
    if (avail <= 0) {
      delay(5);
      continue;
    }
    const size_t n = stream->readBytes(bits + got, min(static_cast<size_t>(avail), want - got));
    if (n == 0) break;
    got += n;
  }
  http.end();
  if (got != want) {
    free(bits);
    return nullptr;
  }
  return bits;
}

}  // namespace serverart
