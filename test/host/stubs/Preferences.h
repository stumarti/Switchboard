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
  bool remove(const char* k) { return store().erase(k) > 0; }
};
