#pragma once
#include "Arduino.h"
enum { WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
struct HostWiFi {
  int status() { return WL_DISCONNECTED; }
  int RSSI() { return -60; }
  String macAddress() { return String("AA:BB:CC:DD:EE:FF"); }
};
inline HostWiFi WiFi;
