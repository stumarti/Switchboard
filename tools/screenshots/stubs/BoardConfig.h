#pragma once
#include "Arduino.h"
namespace BoardConfig {
struct Display { int sclk = 0, mosi = 0, cs = 0, dc = 0, rst = 0, busy = 0; };
struct Power { int8_t latch = 0, latch0 = -1; };
struct Board { Display display; Power power; };
inline Board ACTIVE;
inline void releaseSdRail() {}
inline void holdPowerRails() {}
}
