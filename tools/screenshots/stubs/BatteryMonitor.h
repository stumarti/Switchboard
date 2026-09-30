#pragma once
#include "Arduino.h"
// SB_BATTERY: the battery % it reports (the demo's Sofa remote has 64).
inline int g_hostBatteryPct = getenv("SB_BATTERY") ? atoi(getenv("SB_BATTERY")) : 64;
class BatteryMonitor {
 public:
  BatteryMonitor(int = 0) {}
  bool readPercentageChecked(uint8_t& p) { p = static_cast<uint8_t>(g_hostBatteryPct); return true; }
  bool readPercentageChecked(uint16_t& p) { p = static_cast<uint16_t>(g_hostBatteryPct); return true; }
  uint8_t readPercentage() { return static_cast<uint8_t>(g_hostBatteryPct); }
  uint16_t readMillivolts() { return 3900; }
};
