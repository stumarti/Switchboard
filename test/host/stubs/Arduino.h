#pragma once
// Just enough of the Arduino core for the firmware's pure headers to compile
// on a PC (test/host). Nothing here talks to hardware.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
struct String : std::string {
  using std::string::string;
  String() {}
  String(const std::string& s) : std::string(s) {}
};
struct HostSerial {
  template <class... A> void printf(const char* f, A... a) { ::printf(f, a...); }
  void println(const char* s = "") { ::puts(s); }
  void print(const char* s) { ::fputs(s, stdout); }
};
inline HostSerial Serial;
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
