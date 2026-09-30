#pragma once
#include "Arduino.h"
// No buttons: nothing is ever pressed.
class InputManager {
 public:
  enum Button { BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_POWER };
  enum SwipeDir { SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN };
  void begin() {}
  void update() {}
  bool wasPressed(int) const { return false; }
  bool wasReleased(int) const { return false; }
  bool isPressed(int) const { return false; }
  bool wasAnyPressed() const { return false; }
  bool wasTouchTap(int16_t* = nullptr, int16_t* = nullptr) const { return false; }
  bool wasTouchTap(float&, float&) const { return false; }
  bool wasSwipe(float&, float&, float&, float&) const { return false; }
  bool hasTouch() const { return true; }
  void suppressTouchContact() {}
  bool wasHomeKeyTapped() const { return false; }
  bool wasHomeKeyLongPressed() const { return false; }
  bool isTouchHeldAt(float&, float&) const { return false; }
  unsigned long getPowerButtonHeldTime() const { return 0; }
};
