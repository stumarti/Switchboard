#pragma once
#include "Arduino.h"
#include "ESPmDNS.h"
enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 };
enum { WIFI_MODE_NULL = 0, WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2 };
enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };
typedef int wl_status_t;
struct HostWiFi {
  int st = WL_CONNECTED;
  int status() { return st; }
  int RSSI(int = 0) { return -58; }
  String SSID(int = 0) { return String("Home"); }
  int encryptionType(int) { return WIFI_AUTH_WPA2_PSK; }
  // SB_MAC: which of the demo's remotes this is.
  static const char* mac() { const char* m = getenv("SB_MAC"); return m && *m ? m : "A0:B1:C2:00:00:02"; }
  String macAddress() { return String(mac()); }
  void macAddress(uint8_t* m) { unsigned v[6] = {}; sscanf(mac(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]); for (int i = 0; i < 6; ++i) m[i] = static_cast<uint8_t>(v[i]); }
  IPAddress localIP() { return IPAddress(192, 168, 1, 42); }
  bool begin(const char* = nullptr, const char* = nullptr, int = 0, const uint8_t* = nullptr, bool = true) { return true; }
  bool mode(int) { return true; }
  int getMode() { return WIFI_STA; }
  bool disconnect(bool = false, bool = false) { return true; }
  int scanNetworks(bool = false, bool = false) { return 0; }
  void scanDelete() {}
  int channel(int = 0) { return 6; }
  uint8_t* BSSID(int = 0) { static uint8_t b[6]; return b; }
  bool setSleep(bool) { return true; }
  bool setAutoReconnect(bool) { return true; }
  void persistent(bool) {}
  void setHostname(const char*) {}
  void setTxPower(int) {}
};
inline HostWiFi WiFi;
