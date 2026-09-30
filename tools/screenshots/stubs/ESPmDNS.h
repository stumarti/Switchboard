#pragma once
#include <cstdint>
#include <string>
#include "Arduino.h"
struct IPAddress {
  uint32_t v = 0;
  IPAddress() {}
  IPAddress(int a, int b, int c, int d) : v((a << 24) | (b << 16) | (c << 8) | d) {}
  explicit IPAddress(uint32_t x) : v(x) {}
  bool fromString(const char* s) {
    int a, b, c, d;
    if (sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
    *this = IPAddress(a, b, c, d);
    return true;
  }
  bool operator==(const IPAddress& o) const { return v == o.v; }
  bool operator!=(const IPAddress& o) const { return v != o.v; }
  operator uint32_t() const { return v; }
  String toString() const { char b[16]; snprintf(b, sizeof(b), "%u.%u.%u.%u", (v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255); return String(b); }
};
// Every name is this machine, where tools/screenshots runs a Switchboard Server.
struct HostMDNS {
  IPAddress queryHost(const char*, int = 0) { return IPAddress(127, 0, 0, 1); }
  bool begin(const char*) { return true; }
  void end() {}
};
inline HostMDNS MDNS;
