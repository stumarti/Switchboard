#pragma once
// Arduino-ESP32's captive-portal DNS, for the screenshots: answers nothing.
#include "ESPmDNS.h"
struct DNSServer {
  bool start(int, const char*, const IPAddress&) { return true; }
  void processNextRequest() {}
  void stop() {}
};
