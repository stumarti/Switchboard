#pragma once
#include "Arduino.h"
namespace freeink {
struct PowerManager {
  template <class... A> static void deepSleep(A...) {}
  template <class... A> static void startDeepSleep(A...) {}
  template <class... A> static void armWakeOnPins(A...) {}
  template <class... A> static void powerDownRailsForSleep(A...) {}
};
}
