#pragma once
// NVS on the device; here, one in-memory map shared by every namespace.
#include <map>
#include <string>
#include "Arduino.h"
struct Preferences {
  static std::map<std::string, std::string>& store() { static std::map<std::string, std::string> m; return m; }
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  String getString(const char* k, const String& d = String()) { auto it = store().find(k); return it == store().end() ? d : String(it->second); }
  size_t putString(const char* k, const String& v) { store()[k] = v; return v.size(); }
  bool getBool(const char* k, bool d = false) { auto it = store().find(k); return it == store().end() ? d : it->second == "1"; }
  size_t putBool(const char* k, bool v) { store()[k] = v ? "1" : "0"; return 1; }
  uint16_t getUShort(const char* k, uint16_t d = 0) { auto it = store().find(k); return it == store().end() ? d : static_cast<uint16_t>(std::stoi(it->second)); }
  size_t putUShort(const char* k, uint16_t v) { store()[k] = std::to_string(v); return 2; }
  size_t getString(const char* k, char* out, size_t n) { auto it = store().find(k); if (it == store().end() || !n) return 0; snprintf(out, n, "%s", it->second.c_str()); return strlen(out); }
  uint8_t getUChar(const char* k, uint8_t d = 0) { auto it = store().find(k); return it == store().end() ? d : static_cast<uint8_t>(std::stoi(it->second)); }
  size_t putUChar(const char* k, uint8_t v) { store()[k] = std::to_string(v); return 1; }
  uint32_t getUInt(const char* k, uint32_t d = 0) { auto it = store().find(k); return it == store().end() ? d : static_cast<uint32_t>(std::stoul(it->second)); }
  size_t putUInt(const char* k, uint32_t v) { store()[k] = std::to_string(v); return 4; }
  int32_t getInt(const char* k, int32_t d = 0) { auto it = store().find(k); return it == store().end() ? d : std::stoi(it->second); }
  size_t putInt(const char* k, int32_t v) { store()[k] = std::to_string(v); return 4; }
  size_t getBytes(const char*, void*, size_t) { return 0; }
  size_t putBytes(const char*, const void*, size_t n) { return n; }
  bool isKey(const char* k) { return store().count(k) > 0; }
  bool clear() { return true; }
  bool remove(const char* k) { return store().erase(k) > 0; }
};
