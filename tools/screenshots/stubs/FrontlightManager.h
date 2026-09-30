#pragma once
#include "Arduino.h"
class FrontlightManager {
 public:
  bool begin() { return true; }
  bool present() const { return true; }
  bool hasColorTemperature() const { return true; }
  void setBrightness(uint8_t b) { b_ = b; }
  void setColorTemperature(uint8_t) {}
  uint8_t brightness() const { return b_; }
  void off() { b_ = 0; }
 private:
  uint8_t b_ = 0;
};
