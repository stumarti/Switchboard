#pragma once
#include "Arduino.h"
// A response body, read from memory (HTTPClient fetched the whole reply).
struct WiFiClient {
  std::string buf;
  size_t pos = 0;
  int available() { return static_cast<int>(buf.size() - pos); }
  int read() { return pos < buf.size() ? static_cast<uint8_t>(buf[pos++]) : -1; }
  int read(uint8_t* p, size_t n) { return static_cast<int>(readBytes(p, n)); }
  size_t readBytes(char* p, size_t n) { n = std::min(n, buf.size() - pos); memcpy(p, buf.data() + pos, n); pos += n; return n; }
  size_t readBytes(uint8_t* p, size_t n) { return readBytes(reinterpret_cast<char*>(p), n); }
  bool connected() { return pos < buf.size(); }
  void setTimeout(int) {}
  void stop() {}
};
