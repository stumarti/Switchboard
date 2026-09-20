#pragma once

// ---------------------------------------------------------------------------
// Ui — a thin, self-contained drawing surface over the FreeInk SDK.
//
// Owns the 800x480 framebuffer, wraps EInkDisplay for refreshes, and exposes
// a handful of immediate-mode primitives (text / rect / line / icon) built on
// FreeInk's DisplayTarget. Deliberately minimal: the boot sequence only needs
// to paint a few full screens, not a retained widget tree.
//
// Coordinates are LOGICAL. The panel is 800x480 landscape-native; we render in
// landscape (the X4 Pro's buttons sit on the long edge), so logical == panel
// here and touch maps straight through.
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <EInkDisplay.h>
#include <BoardConfig.h>
#include <FreeInkUIDisplayTarget.h>
#include <Icon.h>

#include "atkinson_font.h"

// The FreeInkUI drawing types (Rect, Paint, Color, TextStyle, DisplayTarget,
// Orientation, ...) live in freeink::ui.
namespace fu = freeink::ui;

class Ui {
 public:
  // Every text face is Atkinson Hyperlegible (atkinson_font.h), bound below
  // in begin(). Font slot 0 (unnamed — pass nothing) is the 24px face every
  // screen already uses as its default. Slot 1 is a smaller 14px face for
  // compact chrome like the status bar, where the default size runs too
  // large (bumped 10px -> 12px -> 14px over two rounds of "still too small
  // to read on the panel"). Slot 2 is the large (~68px) digits-only face for
  // the Standby/Climate pages' temperature readout. Slot 3 is the plain 12px
  // face, for any call site that wants exactly that size rather than "the
  // small chrome face" (which is 14px now). Slot 4 is the 28px face — not
  // yet bound to a specific use.
  static constexpr fu::FontId kFontSmall = 1;
  static constexpr fu::FontId kFontTemp = 2;
  static constexpr fu::FontId kFont12 = 3;
  static constexpr fu::FontId kFont28 = 4;

  // Portrait: the X4 Pro panel is 800x480 landscape-native, but this is a
  // hand-held remote, so we render portrait (held tall). In portrait the
  // logical frame is 480 wide x 800 tall; Orientation::Portrait rotates the
  // panel 90° CW (matching the SDK/CrossPoint convention) and the touch
  // transform is inverted to match, so taps map correctly.
  static constexpr int16_t W = 480;
  static constexpr int16_t H = 800;

  Ui()
      : display_(BoardConfig::ACTIVE.display.sclk, BoardConfig::ACTIVE.display.mosi,
                 BoardConfig::ACTIVE.display.cs, BoardConfig::ACTIVE.display.dc,
                 BoardConfig::ACTIVE.display.rst, BoardConfig::ACTIVE.display.busy) {}

  void begin() {
    display_.begin();  // allocates + owns the framebuffer
    // Construct the drawing target ONCE, after begin(), bound to the panel's
    // framebuffer — exactly the pattern the FreeInk SDK reference uses. In
    // single-buffer mode (EINK_DISPLAY_SINGLE_BUFFER_MODE=1) the display never
    // swaps buffers, so this pointer stays valid for the life of the program.
    target_ = new fu::DisplayTarget(
        display_.getFrameBuffer(), display_.getDisplayWidth(), display_.getDisplayHeight(),
        display_.getDisplayWidthBytes(), fu::Orientation::Portrait);
    target_->setFont(0, fu::kAtkinsonHL24Font);  // override the SDK's bundled default (Noto Sans)
    target_->setFont(kFontSmall, fu::kAtkinsonHL14Font);
    target_->setFont(kFontTemp, fu::kAtkinsonHLDigits68Font);
    target_->setFont(kFont12, fu::kAtkinsonHL12Font);
    target_->setFont(kFont28, fu::kAtkinsonHL28Font);
  }

  // Rendered size of `s` in a given font slot (width x line height), for
  // laying out something right next to a variable-width string.
  fu::Size measure(const char* s, fu::FontId font = 0) {
    return target_->measureText(font, s, fu::TextStyle{});
  }

  // Map a normalized touch point (0..1, the panel-native frame InputManager
  // reports) to logical screen pixels. We render Portrait (panel rotated
  // 90 CW), so this is the inverse rotation — ported from CrossPoint's
  // GfxRenderer::tapToLogical (Portrait case). Panel-native long axis is our
  // logical height, short axis our logical width.
  static void touchToLogical(float nx, float ny, int16_t& lx, int16_t& ly) {
    int px = static_cast<int>(nx * H);
    int py = static_cast<int>(ny * W);
    px = px < 0 ? 0 : (px > H - 1 ? H - 1 : px);
    py = py < 0 ? 0 : (py > W - 1 ? W - 1 : py);
    lx = static_cast<int16_t>(W - 1 - py);
    ly = static_cast<int16_t>(px);
  }

  // ---- frame lifecycle --------------------------------------------------
  void clear() {
    display_.clearScreen(0xFF);  // white paper (0xFF = white; 0x00 = black)
  }

  // Full refresh: clean, no ghosting — use for whole-screen transitions and to
  // scrub accumulated ghosting after a run of partial refreshes. Blocking:
  // waits out any in-flight async refresh, then runs (~1-1.5 s). Only fired on
  // content switches/sleep, never from rapid repeated input, so there's no
  // pipelining win worth the extra shadow buffer here.
  void flushFull() {
    display_.waitRefreshComplete();
    display_.displayBuffer(freeink::FreeInkDisplay::FULL_REFRESH);
  }
  // Half (balanced) refresh: cleaner than fast, faster than full — reserved
  // for commitFrame()'s periodic kCleanEvery ghost-scrub and the manual
  // "Full refresh" control-panel tile (screen_shade.h), which DO fire from
  // (or shortly after) a run of rapid taps even though no single control's
  // own feedback ever requests Half directly — Climate's +/-0.5 step and a
  // volume drag/tap run are Fast/DU, same as every other control-feedback
  // redraw (see main.cpp's RefreshEvent table). NON-BLOCKING, same shape as
  // flushFast() below: pushes the frame and returns once the waveform has
  // started (~waveform is still developing, ~0.5 s) so the caller can compose
  // the next frame immediately. Uses the SHADOWED async path (unlike
  // flushFast's no-shadow one): the UC8279 driver's displayFinish() re-reads
  // the just-displayed frame to resync its OLD plane, and by then the live
  // framebuffer may already hold the next composed frame — the shadow keeps a
  // stable copy of what was actually sent so that resync stays correct
  // (see FreeInkDisplay::displayAsyncImpl's noShadow-vs-shadow contract).
  void flushHalf() {
    display_.waitRefreshComplete();
    display_.displayBufferAsync(freeink::FreeInkDisplay::HALF_REFRESH);
  }
  // Fast / partial refresh, NON-BLOCKING: pushes the frame and returns in
  // ~25 ms while the panel develops it (~0.3 s). The framebuffer is free to
  // redraw immediately. Call refreshBusy() before firing another, or
  // syncDisplay() to block until it lands. Uses the no-shadow async path
  // (matches CrossPoint's HalDisplay) — the panel keeps its own diff baseline.
  void flushFast() {
    display_.waitRefreshComplete();
    display_.displayBufferAsyncNoShadow(freeink::FreeInkDisplay::FAST_REFRESH);
  }
  bool refreshBusy() { return display_.refreshBusy(); }
  void syncDisplay() { display_.waitRefreshComplete(); }

  // Wake helpers (mirror CrossPoint's HalDisplay::begin). After a deep-sleep
  // wake the controller's diff baseline is gone; skipInitialResync() tells the
  // driver our own next paint is a self-contained scrub (HALF), so it needn't
  // force a slow FULL. requestResync() does the opposite — force one clean FULL.
  void skipInitialResync() { display_.skipInitialResync(); }
  void requestResync() { display_.requestResync(); }

  // ---- primitives -------------------------------------------------------
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                fu::Color c = fu::Color::Black, uint8_t radius = 0) {
    target_->fill(fu::Rect{x, y, w, h}, fu::Paint::solid(c), radius);
  }

  void strokeRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t weight = 2,
                  uint8_t radius = 0, fu::Color c = fu::Color::Black) {
    target_->stroke(fu::Rect{x, y, w, h}, fu::Paint::solid(c), weight, radius);
  }

  void hline(int16_t x, int16_t y, int16_t w, uint8_t weight = 1,
             fu::Color c = fu::Color::Black) {
    target_->line(fu::Point{x, y}, fu::Point{static_cast<int16_t>(x + w), y},
                  weight, fu::Paint::solid(c));
  }

  // Text in a box, aligned. maxLines>1 wraps. `font` selects a slot bound via
  // setFont() — 0 (default) is the bundled 24px face, kFontSmall the 14px one.
  void text(const char* s, int16_t x, int16_t y, int16_t w, int16_t h,
            fu::TextAlign align = fu::TextAlign::Left,
            fu::Color c = fu::Color::Black, uint8_t maxLines = 1,
            fu::FontId font = 0) {
    fu::TextStyle st;
    st.align = align;
    st.color = c;
    st.maxLines = maxLines;
    st.font = font;
    target_->text(fu::Rect{x, y, w, h}, s, st);
  }

  // Full-width centered line of text.
  void centered(const char* s, int16_t y, int16_t h,
                fu::Color c = fu::Color::Black) {
    text(s, 0, y, W, h, fu::TextAlign::Center, c);
  }

  // Draw a FreeInk Icon (1-bpp, bit 0 = ink) with its top-left at (x,y).
  void icon(const freeink::Icon& ic, int16_t x, int16_t y,
            fu::Color c = fu::Color::Black) {
    fu::BitmapRef ref;
    ref.data = ic.bits;
    ref.width = ic.w;
    ref.height = ic.h;
    ref.format = fu::BitmapFormat::Mask1;  // bit 0 = draw, bit 1 = skip
    ref.progmem = false;
    target_->bitmap(fu::Rect{x, y, static_cast<int16_t>(ic.w), static_cast<int16_t>(ic.h)},
                    ref, fu::BitmapMode::Center, fu::Paint::solid(c));
  }

  // Center an icon horizontally at a given top y.
  void iconCentered(const freeink::Icon& ic, int16_t y,
                    fu::Color c = fu::Color::Black) {
    icon(ic, static_cast<int16_t>((W - ic.w) / 2), y, c);
  }

  // Draw an Icon scaled to fit a w x h box (nearest-neighbor). Use integer
  // multiples of the source size for a clean result.
  void iconScaled(const freeink::Icon& ic, int16_t x, int16_t y, int16_t w, int16_t h,
                  fu::Color c = fu::Color::Black) {
    fu::BitmapRef ref;
    ref.data = ic.bits;
    ref.width = ic.w;
    ref.height = ic.h;
    ref.format = fu::BitmapFormat::Mask1;
    ref.progmem = false;
    target_->bitmap(fu::Rect{x, y, w, h}, ref, fu::BitmapMode::Stretch, fu::Paint::solid(c));
  }

  EInkDisplay& display() { return display_; }
  fu::DisplayTarget& gfx() { return *target_; }

 private:
  EInkDisplay display_;
  fu::DisplayTarget* target_ = nullptr;
};
