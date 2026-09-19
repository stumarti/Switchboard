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
#include <stdio.h>
#include <string.h>

namespace httpjson {

// Resolve `host` to an IP. Accepts a literal dotted-quad ("192.168.1.4"), a
// bare mDNS label ("switchboard"), or a ".local" name ("homeassistant.local").
// Returns 0.0.0.0 on failure.
inline IPAddress resolveHost(const char* host) {
  if (!host || !*host) return IPAddress(0, 0, 0, 0);

  IPAddress literal;
  if (literal.fromString(host)) return literal;

  // Take the first label, minus any trailing ".local".
  char label[48];
  snprintf(label, sizeof(label), "%s", host);
  if (char* dot = strchr(label, '.')) *dot = 0;
  if (!*label) return IPAddress(0, 0, 0, 0);

  return MDNS.queryHost(label, 3000);
}

// Resolve + request + parse. `body` non-null -> POST that JSON body, else GET.
//   bearer   : if non-empty, sent as "Authorization: Bearer <bearer>".
//   status   : filled with a short failure reason when the call returns false.
//   dateOut  : if non-null, receives the response's Date header (may be "").
//   filter   : if non-null, a DeserializationOption::Filter document — only
//              matching keys are kept (keeps big HA responses off the heap).
// Returns true only on resolve + HTTP 2xx + successful JSON parse.
inline bool request(const char* host, uint16_t port, const char* path, const char* bearer,
                    const char* body, JsonDocument& doc, char* status, size_t statusCap,
                    char* dateOut = nullptr, size_t dateCap = 0,
                    const JsonDocument* filter = nullptr) {
  if (dateOut && dateCap) dateOut[0] = 0;

  const IPAddress ip = resolveHost(host);
  if (ip == IPAddress(0, 0, 0, 0)) {
    snprintf(status, statusCap, "can't resolve %s", host && *host ? host : "(empty)");
    return false;
  }

  char url[192];
  snprintf(url, sizeof(url), "http://%s:%u%s", ip.toString().c_str(), port, path);

  HTTPClient http;
  http.setTimeout(6000);
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
  const char* collect[] = {"Date"};
  http.collectHeaders(collect, 1);

  const int code = body ? http.POST(String(body)) : http.GET();
  if (code < 200 || code >= 300) {
    snprintf(status, statusCap, "HTTP %d", code);
    http.end();
    return false;
  }

  if (dateOut && dateCap) {
    const String d = http.header("Date");
    snprintf(dateOut, dateCap, "%s", d.c_str());
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
                size_t dateCap = 0, const JsonDocument* filter = nullptr) {
  return request(host, port, path, bearer, /*body=*/nullptr, doc, status, statusCap, dateOut,
                 dateCap, filter);
}

inline bool post(const char* host, uint16_t port, const char* path, const char* bearer,
                 const char* body, JsonDocument& doc, char* status, size_t statusCap,
                 const JsonDocument* filter = nullptr) {
  return request(host, port, path, bearer, body, doc, status, statusCap, nullptr, 0, filter);
}

}  // namespace httpjson
