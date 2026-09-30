#pragma once
#include "Arduino.h"
#define U_FLASH 0
#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF
struct HostUpdate {
  bool begin(size_t, int = 0) { return false; }
  size_t write(uint8_t*, size_t n) { return n; }
  bool end(bool = false) { return false; }
  void abort() {}
  const char* errorString() { return "no flash on the host"; }
};
inline HostUpdate Update;
