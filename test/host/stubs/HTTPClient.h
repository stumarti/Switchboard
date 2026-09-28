#pragma once
#include "Arduino.h"
// Every request fails to connect: host tests exercise parsing, not the network.
struct Stream {
  int read() { return -1; }
  size_t readBytes(char*, size_t) { return 0; }
};
struct HTTPClient {
  void setTimeout(int) {}
  bool begin(const char*) { return false; }
  void addHeader(const char*, const char*) {}
  void collectHeaders(const char**, int) {}
  int GET() { return -1; }
  int POST(String) { return -1; }
  int getSize() { return -1; }
  String header(const char*) { return String(); }
  Stream& getStream() { static Stream s; return s; }
  void end() {}
};
