#pragma once
#include <cstdint>
#include <string>
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
  std::string toString() const { return "0.0.0.0"; }
};
// Host tests never resolve anything: mDNS always misses.
struct HostMDNS {
  IPAddress queryHost(const char*, int) { return IPAddress(); }
};
inline HostMDNS MDNS;
