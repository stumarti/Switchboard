#pragma once

// ===========================================================================
// http_json — the one HTTP+JSON helper the Home/Standby fetches share.
//
// Resolves a host (an mDNS ".local" name or a literal IP), GETs/POSTs a path
// on it, and parses the JSON body into an ArduinoJson document. Optionally
// sends a bearer token and hands back the response's `Date:` header (the
// Standby screen uses the Home Assistant server's Date header as its clock).
//
// Plain lwip DNS on this core can't resolve ".local", so a hostname is looked
// up with MDNS.queryHost() on its first label. Requires MDNS.begin() first.
// ===========================================================================

#include <Arduino.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>

namespace httpjson {

// --- resolved-address cache -----------------------------------------------
// An mDNS lookup costs up to 3 s, and used to run before EVERY request — a
// data refresh is a dozen-plus requests, and every button press one more.
// The last few answers are kept (in RTC memory, so they survive deep sleep
// too); a request that can't connect to a cached address drops it and
// resolves afresh (request() below), so a host that moved is re-found on
// its own.
struct HostCacheEntry {
  char label[24];
  uint32_t ip;
};
inline constexpr int kHostCacheSize = 4;
RTC_DATA_ATTR inline HostCacheEntry g_hostCache[kHostCacheSize] = {};
inline portMUX_TYPE g_hostCacheMux = portMUX_INITIALIZER_UNLOCKED;

inline uint32_t cachedHost(const char* label) {
  uint32_t ip = 0;
  portENTER_CRITICAL(&g_hostCacheMux);
  for (const HostCacheEntry& e : g_hostCache)
    if (e.ip && !strcmp(e.label, label)) ip = e.ip;
  portEXIT_CRITICAL(&g_hostCacheMux);
  return ip;
}
inline void rememberHost(const char* label, uint32_t ip) {
  portENTER_CRITICAL(&g_hostCacheMux);
  int slot = -1;
  for (int i = 0; i < kHostCacheSize && slot < 0; ++i)
    if (!strcmp(g_hostCache[i].label, label) || !g_hostCache[i].ip) slot = i;
  if (slot < 0) {  // full: shift out the oldest
    for (int i = 1; i < kHostCacheSize; ++i) g_hostCache[i - 1] = g_hostCache[i];
    slot = kHostCacheSize - 1;
  }
  snprintf(g_hostCache[slot].label, sizeof(g_hostCache[slot].label), "%s", label);
  g_hostCache[slot].ip = ip;
  portEXIT_CRITICAL(&g_hostCacheMux);
}
inline void forgetHost(const char* label) {
  portENTER_CRITICAL(&g_hostCacheMux);
  for (HostCacheEntry& e : g_hostCache)
    if (!strcmp(e.label, label)) e.ip = 0;
  portEXIT_CRITICAL(&g_hostCacheMux);
}

// The mDNS label for `host` — its first label, minus any ".local" — or ""
// if it's a literal address (or empty).
inline void mdnsLabel(const char* host, char* out, size_t cap) {
  out[0] = 0;
  IPAddress literal;
  if (!host || !*host || literal.fromString(host)) return;
  snprintf(out, cap, "%s", host);
  if (char* dot = strchr(out, '.')) *dot = 0;
}

// Resolve `host` to an IP. Accepts a literal dotted-quad ("192.168.1.4"), a
// bare mDNS label ("switchboard"), or a ".local" name ("homeassistant.local").
// Returns 0.0.0.0 on failure. `fresh` skips the cache.
inline IPAddress resolveHost(const char* host, bool fresh = false) {
  if (!host || !*host) return IPAddress(0, 0, 0, 0);

  IPAddress literal;
  if (literal.fromString(host)) return literal;

  char label[24];
  mdnsLabel(host, label, sizeof(label));
  if (!*label) return IPAddress(0, 0, 0, 0);

  if (!fresh) {
    if (const uint32_t ip = cachedHost(label)) return IPAddress(ip);
  }
  const IPAddress ip = MDNS.queryHost(label, 3000);
  if (ip != IPAddress(0, 0, 0, 0)) rememberHost(label, static_cast<uint32_t>(ip));
  return ip;
}

// Resolve + request + parse. `body` non-null -> POST that JSON body, else GET.
//   bearer   : if non-empty, sent as "Authorization: Bearer <bearer>".
//   status   : filled with a short failure reason when the call returns false.
//   dateOut  : if non-null, receives the response's Date header (may be "").
//   filter   : if non-null, a DeserializationOption::Filter document — only
//              matching keys are kept (keeps big HA responses off the heap).
// Returns true only on resolve + HTTP 2xx + successful JSON parse.
// Conditional-request options (all optional):
//   ifNoneMatch : sent as If-None-Match — the ETag of the copy we already
//                 hold. A 304 reply is a success with `notModified` set and
//                 `doc` left empty: keep using that copy.
//   etagOut     : receives the response's ETag (may be "").
//   timeoutMs   : read timeout, 0 = the usual 6 s (a held-open request —
//                 app/live.h — needs longer than the server holds it).
struct Conditional {
  const char* ifNoneMatch = nullptr;
  char* etagOut = nullptr;
  size_t etagCap = 0;
  bool notModified = false;
  uint32_t timeoutMs = 0;
};

inline bool request(const char* host, uint16_t port, const char* path, const char* bearer,
                    const char* body, JsonDocument& doc, char* status, size_t statusCap,
                    char* dateOut = nullptr, size_t dateCap = 0,
                    const JsonDocument* filter = nullptr, Conditional* cond = nullptr) {
  if (dateOut && dateCap) dateOut[0] = 0;
  if (cond) {
    cond->notModified = false;
    if (cond->etagOut && cond->etagCap) cond->etagOut[0] = 0;
  }

  HTTPClient http;
  int code = 0;
  // Try 1 uses the cached address; if it can't even connect (code < 0) and
  // the host is an mDNS name, the cached address is dropped and try 2
  // resolves afresh.
  for (int attempt = 0; attempt < 2; ++attempt) {
    const IPAddress ip = resolveHost(host, /*fresh=*/attempt == 1);
    if (ip == IPAddress(0, 0, 0, 0)) {
      snprintf(status, statusCap, "can't resolve %s", host && *host ? host : "(empty)");
      return false;
    }

    char url[192];
    snprintf(url, sizeof(url), "http://%s:%u%s", ip.toString().c_str(), port, path);

    http.setTimeout(cond && cond->timeoutMs ? cond->timeoutMs : 6000);
    if (!http.begin(url)) {
      snprintf(status, statusCap, "bad URL");
      return false;
    }
    if (bearer && *bearer) {
      char auth[320];
      snprintf(auth, sizeof(auth), "Bearer %s", bearer);
      http.addHeader("Authorization", auth);
    }
    if (body) http.addHeader("Content-Type", "application/json");
    if (cond && cond->ifNoneMatch && *cond->ifNoneMatch)
      http.addHeader("If-None-Match", cond->ifNoneMatch);
    const char* collect[] = {"Date", "ETag"};
    http.collectHeaders(collect, 2);

    code = body ? http.POST(String(body)) : http.GET();
    char label[24];
    mdnsLabel(host, label, sizeof(label));
    if (code >= 0 || !*label || attempt == 1) break;
    http.end();
    forgetHost(label);  // stale address — resolve again
  }
  if (cond && code == 304 && cond->ifNoneMatch && *cond->ifNoneMatch) {
    cond->notModified = true;  // what we hold is current
    if (cond->etagOut && cond->etagCap)
      snprintf(cond->etagOut, cond->etagCap, "%s", cond->ifNoneMatch);
    http.end();
    status[0] = 0;
    return true;
  }
  if (code < 200 || code >= 300) {
    snprintf(status, statusCap, "HTTP %d", code);
    http.end();
    return false;
  }

  if (dateOut && dateCap) {
    const String d = http.header("Date");
    snprintf(dateOut, dateCap, "%s", d.c_str());
  }
  if (cond && cond->etagOut && cond->etagCap) {
    const String e = http.header("ETag");
    snprintf(cond->etagOut, cond->etagCap, "%s", e.c_str());
  }

  const DeserializationError err =
      filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
             : deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    snprintf(status, statusCap, "parse: %s", err.c_str());
    return false;
  }

  status[0] = 0;
  return true;
}

inline bool get(const char* host, uint16_t port, const char* path, const char* bearer,
                JsonDocument& doc, char* status, size_t statusCap, char* dateOut = nullptr,
                size_t dateCap = 0, const JsonDocument* filter = nullptr,
                Conditional* cond = nullptr) {
  return request(host, port, path, bearer, /*body=*/nullptr, doc, status, statusCap, dateOut,
                 dateCap, filter, cond);
}

inline bool post(const char* host, uint16_t port, const char* path, const char* bearer,
                 const char* body, JsonDocument& doc, char* status, size_t statusCap,
                 const JsonDocument* filter = nullptr) {
  return request(host, port, path, bearer, body, doc, status, statusCap, nullptr, 0, filter);
}

}  // namespace httpjson
