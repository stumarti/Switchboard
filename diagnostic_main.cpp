// ===========================================================================
// DIAGNOSTIC firmware — proves the X4 Pro panel + power + pins work, using
// ONLY raw FreeInkDisplay primitives (no FreeInkUI). If this shows the three
// fills below, the hardware path is good and the blank-screen bug is in the
// FreeInkUI/DisplayTarget buffer handling of the main firmware.
//
// To use: temporarily rename src/main.cpp and drop this in as src/main.cpp
// (or set build_src_filter). Flash, watch serial, watch the panel.
//
// Expected on panel: full BLACK (3s) -> full WHITE (3s) -> half black / half
// white split (stays). Expected on serial: the [diag] lines below.
// ===========================================================================

#include <Arduino.h>
#include <EInkDisplay.h>
#include <BoardConfig.h>
#include <XteinkDetect.h>

static EInkDisplay display(
    BoardConfig::ACTIVE.display.sclk, BoardConfig::ACTIVE.display.mosi,
    BoardConfig::ACTIVE.display.cs, BoardConfig::ACTIVE.display.dc,
    BoardConfig::ACTIVE.display.rst, BoardConfig::ACTIVE.display.busy);

static void banner(const char* msg) {
  Serial.printf("[diag] %s\n", msg);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  banner("start");

  // Power the peripheral rails FIRST (GPIO1 master enable on the X4 Pro).
  BoardConfig::holdPowerRails();
  BoardConfig::releaseSdRail();
  delay(10);
  banner("rails held");

  // Detect the actual panel controller BEFORE begin(). Newer X4 Pro batches
  // carry a UC8179 (UltraChip) instead of the SSD1677; this bus probe promotes
  // ACTIVE.displayController SSD1677 -> UC8179 when it fingerprints the sibling
  // part, so begin() selects the matching driver. Without this the SSD1677
  // driver's BUSY polarity is wrong on a UC8179 unit → instant (0 ms) waits and
  // a blank panel. This is the step CrossPoint runs and our firmware was missing.
  bool promoted = freeink::applyXteinkDisplayController();
  Serial.printf("[diag] controller probe: %s (controller now = %d)\n",
                promoted ? "PROMOTED to UltraChip (UC8179)" : "kept default (SSD1677)",
                (int)BoardConfig::ACTIVE.displayController);

  display.begin();
  banner("display.begin done");
  Serial.printf("[diag] panel %ux%u, bufferSize=%u\n",
                display.getDisplayWidth(), display.getDisplayHeight(),
                (unsigned)display.getBufferSize());

  // --- Fill 1: BLACK ------------------------------------------------------
  // Framebuffer convention: 1 = white, 0 = black. So 0x00 = all black.
  display.clearScreen(0x00);
  banner("cleared to BLACK, refreshing (full)");
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  banner("black refresh complete");
  delay(3000);

  // --- Fill 2: WHITE ------------------------------------------------------
  display.clearScreen(0xFF);
  banner("cleared to WHITE, refreshing (full)");
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  banner("white refresh complete");
  delay(3000);

  // --- Fill 3: HALF/HALF via a black bitmap on the top half ---------------
  // Build a black block (all 0x00) covering the top half and blit it onto a
  // white field, so we can see orientation + partial addressing.
  display.clearScreen(0xFF);
  const uint16_t w = display.getDisplayWidth();
  const uint16_t h = display.getDisplayHeight() / 2;
  const size_t blockBytes = ((w + 7) / 8) * h;
  uint8_t* block = (uint8_t*)malloc(blockBytes);
  if (block) {
    memset(block, 0x00, blockBytes);  // all black
    display.drawImage(block, 0, 0, w, h);  // top half black
    free(block);
    banner("blitted top-half black");
  } else {
    banner("malloc failed for block");
  }
  display.displayBuffer(EInkDisplay::FULL_REFRESH);
  banner("half/half refresh complete — DONE");
}

void loop() {
  delay(1000);
}
