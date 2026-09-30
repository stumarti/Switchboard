#pragma once
// The panel, as a framebuffer only: 800x480, 1 bit a pixel, set = white.
#include "Arduino.h"
#include <vector>
namespace freeink {
class FreeInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  static constexpr int16_t DISPLAY_WIDTH = 800, DISPLAY_HEIGHT = 480;
  FreeInkDisplay(int, int, int, int, int, int) {}
  void begin() { fb_.assign(getBufferSize(), 0xFF); }
  uint8_t* getFrameBuffer() const { return const_cast<uint8_t*>(fb_.data()); }
  int16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  int16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
  int16_t getDisplayWidthBytes() const { return DISPLAY_WIDTH / 8; }
  uint32_t getBufferSize() const { return DISPLAY_WIDTH / 8 * DISPLAY_HEIGHT; }
  void clearScreen(uint8_t v) { std::fill(fb_.begin(), fb_.end(), v); }
  void waitRefreshComplete() {}
  void displayBuffer(RefreshMode) { ++frames; }
  void displayBufferAsync(RefreshMode) { ++frames; }
  void displayBufferAsyncNoShadow(RefreshMode) { ++frames; }
  bool refreshBusy() { return false; }
  void skipInitialResync() {}
  void requestResync() {}
  void deepSleep() {}
  int frames = 0;
 private:
  std::vector<uint8_t> fb_;
};
}  // namespace freeink
using EInkDisplay = freeink::FreeInkDisplay;
