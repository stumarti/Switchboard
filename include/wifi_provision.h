#pragma once

// ===========================================================================
// Wi-Fi provisioning for the Switchboard boot sequence.
//
// Flow (all on-device, portrait, touch-driven):
//   1. Try the ESP32's own saved credentials silently (WiFi.begin() with no
//      args reuses the last SSID/pass it stored in NVS). If that connects,
//      we're done — no list, no prompt.
//   2. On failure (or no saved creds), scan for APs and show a selectable list.
//   3. Tap an AP -> an on-screen QWERTY keyboard collects the password.
//   4. Connect with the chosen SSID + typed password. ESP32 persists them, so
//      next boot step 1 reuses them.
//
// The keyboard is drawn with the same immediate-mode Ui primitives as the rest
// of the firmware (no dependency on the heavier FreeInkUI activity framework),
// hit-tested against InputManager tap coordinates.
// ===========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <InputManager.h>
#include "ui.h"

namespace wifiprov {

using Color = freeink::ui::Color;
using TextAlign = freeink::ui::TextAlign;

// Result of the whole provisioning flow, surfaced to the debug screen.
inline char g_resultLine[64] = "";

// ---- small helpers --------------------------------------------------------

// Poll the join for up to timeoutMs, pumping a caller status callback ~every
// 400 ms so the screen can animate. Returns true if connected.
template <typename PaintFn>
static bool joinWait(uint32_t timeoutMs, PaintFn paint) {
  const uint32_t start = millis();
  int tick = 0;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(400);
    paint(++tick);
  }
  return WiFi.status() == WL_CONNECTED;
}

// ---- on-screen keyboard ---------------------------------------------------
//
// A fixed QWERTY grid + a shift/symbols layer. Rows are strings; each visible
// char is one key. Special keys are appended as wide keys on the bottom row.

struct Keyboard {
  Ui& ui;
  InputManager& input;
  char buffer[65] = "";
  uint8_t len = 0;
  bool shift = false;     // caps for next letter
  bool symbols = false;   // symbol layer

  Keyboard(Ui& u, InputManager& in) : ui(u), input(in) {}

  // Layer rows. Lowercase / uppercase / symbols.
  const char* rowsLower[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const char* rowsUpper[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  const char* rowsSym[3]   = {"1234567890", "@#$%&-_+()", "*\"':;!?/"};

  const char* const* rows() const {
    return symbols ? rowsSym : (shift ? rowsUpper : rowsLower);
  }

  // Geometry (portrait 480 wide). Keyboard occupies the lower part of screen.
  static constexpr int16_t kbTop = 470;
  static constexpr int16_t keyH = 62;
  static constexpr int16_t gap = 6;
  static constexpr int16_t sideMargin = 8;

  // Key width for a row of n keys spanning the full width.
  int16_t keyW(int n) const { return (Ui::W - 2 * sideMargin - (n - 1) * gap) / n; }

  void append(char c) {
    if (len < sizeof(buffer) - 1) {
      buffer[len++] = c;
      buffer[len] = 0;
    }
    if (shift) shift = false;  // one-shot shift
  }
  void backspace() {
    if (len > 0) buffer[--len] = 0;
  }

  // Just the password field + hint row. Key presses do not redraw the keyboard.
  void drawField() {
    ui.fillRect(0, 100, Ui::W, kbTop - 100 - 4, Color::White);  // clear the strip
    ui.strokeRect(24, 110, Ui::W - 48, 56, 2, 8);
    char shown[80];
    snprintf(shown, sizeof(shown), "%s_", buffer);
    ui.text(shown, 40, 126, Ui::W - 80, 30, TextAlign::Left);
    char hint[48];
    snprintf(hint, sizeof(hint), "%u chars   %s", len,
             symbols ? "symbols" : (shift ? "SHIFT" : "abc"));
    ui.text(hint, 24, 176, Ui::W - 48, 22, TextAlign::Left, Color::DarkGray);
    ui.flushFast();
  }

  // The whole password-entry screen (first draw + shift/symbols layer changes).
  void draw(const char* ssid) {
    ui.clear();
    ui.centered("Enter password", 24, 34);
    char sub[48];
    snprintf(sub, sizeof(sub), "for \"%s\"", ssid);
    ui.centered(sub, 66, 26, Color::DarkGray);

    ui.strokeRect(24, 110, Ui::W - 48, 56, 2, 8);
    char shown[80];
    snprintf(shown, sizeof(shown), "%s_", buffer);
    ui.text(shown, 40, 126, Ui::W - 80, 30, TextAlign::Left);

    char hint[48];
    snprintf(hint, sizeof(hint), "%u chars   %s", len, symbols ? "symbols" : (shift ? "SHIFT" : "abc"));
    ui.text(hint, 24, 176, Ui::W - 48, 22, TextAlign::Left, Color::DarkGray);

    drawKeys();
    ui.flushFull();
  }

  void drawKeys() {
    const char* const* r = rows();
    for (int row = 0; row < 3; ++row) {
      const int n = strlen(r[row]);
      const int16_t w = keyW(10);  // align to a 10-wide grid
      const int16_t rowW = n * w + (n - 1) * gap;
      int16_t x = (Ui::W - rowW) / 2;
      const int16_t y = kbTop + row * (keyH + gap);
      for (int i = 0; i < n; ++i) {
        drawKey(x, y, w, keyH, r[row][i]);
        x += w + gap;
      }
    }
    // Bottom row: [shift] [123/abc] [space] [del] [OK]
    const int16_t y = kbTop + 3 * (keyH + gap);
    const int16_t unit = keyW(10);
    int16_t x = sideMargin;
    drawWideKey(x, y, unit * 2 + gap, keyH, shift ? "SHIFT*" : "shift"); x += unit * 2 + gap * 2;
    drawWideKey(x, y, unit * 2 + gap, keyH, symbols ? "abc" : "?123");  x += unit * 2 + gap * 2;
    drawWideKey(x, y, unit * 2 + gap, keyH, "space");                   x += unit * 2 + gap * 2;
    drawWideKey(x, y, unit + gap, keyH, "del");                         x += unit + gap * 2;
    // OK filled
    ui.fillRect(x, y, Ui::W - sideMargin - x, keyH, Color::Black, 8);
    ui.text("OK", x, y + 18, Ui::W - sideMargin - x, 26, TextAlign::Center, Color::White);
  }

  void drawKey(int16_t x, int16_t y, int16_t w, int16_t h, char label) {
    ui.strokeRect(x, y, w, h, 2, 8);
    char s[2] = {label, 0};
    ui.text(s, x, y + h / 2 - 13, w, 26, TextAlign::Center);
  }
  void drawWideKey(int16_t x, int16_t y, int16_t w, int16_t h, const char* label) {
    ui.strokeRect(x, y, w, h, 2, 8);
    ui.text(label, x, y + h / 2 - 11, w, 24, TextAlign::Center);
  }

  // Map a tap to a key action. Returns:
  //  0 = nothing, 1 = field-only redraw (letter/space/del), 2 = OK,
  //  3 = full redraw (shift/symbols layer change).
  int handleTap(float nx, float ny) {
    // InputManager reports taps in the panel-native (landscape) frame; the
    // keyboard is drawn in logical portrait pixels, so apply the same inverse
    // rotation main.cpp uses everywhere.
    int16_t px, py;
    Ui::touchToLogical(nx, ny, px, py);

    // Letter rows
    const char* const* r = rows();
    for (int row = 0; row < 3; ++row) {
      const int n = strlen(r[row]);
      const int16_t w = keyW(10);
      const int16_t rowW = n * w + (n - 1) * gap;
      int16_t x = (Ui::W - rowW) / 2;
      const int16_t y = kbTop + row * (keyH + gap);
      if (py >= y && py < y + keyH) {
        for (int i = 0; i < n; ++i) {
          if (px >= x && px < x + w) {
            append(r[row][i]);
            return 1;
          }
          x += w + gap;
        }
      }
    }

    // Bottom row
    const int16_t y = kbTop + 3 * (keyH + gap);
    if (py >= y && py < y + keyH) {
      const int16_t unit = keyW(10);
      int16_t x = sideMargin;
      const int16_t shiftW = unit * 2 + gap;
      const int16_t symW = unit * 2 + gap;
      const int16_t spaceW = unit * 2 + gap;
      const int16_t delW = unit + gap;
      if (px < x + shiftW) { shift = !shift; return 3; } x += shiftW + gap;
      if (px < x + symW)   { symbols = !symbols; shift = false; return 3; } x += symW + gap;
      if (px < x + spaceW) { append(' '); return 1; } x += spaceW + gap;
      if (px < x + delW)   { backspace(); return 1; } x += delW + gap;
      return 2;  // OK region (rest of the row)
    }
    return 0;
  }

  // Run the keyboard until OK. Returns the typed password (may be empty for
  // open networks). Blocks, pumping input.
  const char* run(const char* ssid) {
    draw(ssid);
    for (;;) {
      input.update();
      float nx, ny;
      if (input.wasTouchTap(nx, ny)) {
        const int r = handleTap(nx, ny);
        if (r == 2) break;              // OK
        if (r == 1) drawField();        // letter/space/del -> just the field (fast)
        else if (r == 3) draw(ssid);    // layer change -> full
      }
      delay(8);
    }
    return buffer;
  }
};

// ---- AP scan + list -------------------------------------------------------

// Scan and render a selectable AP list; returns the chosen SSID index, or -1 if
// the user asked to rescan (we loop), or -2 to skip. Fills ssidOut/openOut.
static int pickNetwork(Ui& ui, InputManager& input, char* ssidOut, size_t ssidCap,
                       bool* openOut) {
  for (;;) {
    // scanning screen
    ui.clear();
    ui.centered("Wi-Fi", 40, 38);
    ui.centered("scanning for networks...", 92, 26, Color::DarkGray);
    ui.flushFull();

    // A prior failed WiFi.begin() (saved creds, or a bad password) leaves the
    // station auto-reconnecting in the background; scanNetworks() then returns
    // 0 / WIFI_SCAN_FAILED. Drop any pending association first (CrossPoint does
    // the same before every scan), then retry a couple of times.
    int n = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
      WiFi.scanDelete();
      WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
      delay(120);
      n = WiFi.scanNetworks(/*async=*/false);
      if (n > 0) break;
      delay(300);
    }
    if (n < 0) n = 0;

    // Build a de-duplicated, signal-sorted view (scanNetworks already sorts by
    // RSSI on ESP32). Cap the list to what fits.
    const int maxShow = 8;
    if (n > maxShow) n = maxShow;

    // selection loop
    int sel = 0;
    bool needRedraw = true;
    for (;;) {
      if (needRedraw) {
        ui.clear();
        ui.centered("Select a network", 34, 34);
        ui.hline(30, 80, Ui::W - 60, 2);
        if (n == 0) {
          ui.centered("no networks found", 300, 28, Color::DarkGray);
        }
        const int16_t rowTop = 100, rowH = 72;
        for (int i = 0; i < n; ++i) {
          const int16_t y = rowTop + i * rowH;
          const bool on = (i == sel);
          if (on) ui.fillRect(30, y, Ui::W - 60, rowH - 10, Color::Black, 10);
          else    ui.strokeRect(30, y, Ui::W - 60, rowH - 10, 2, 10);
          const Color fg = on ? Color::White : Color::Black;
          const Color sub = on ? Color::LightGray : Color::DarkGray;
          ui.text(WiFi.SSID(i).c_str(), 48, y + 10, Ui::W - 150, 26, TextAlign::Left, fg);
          char meta[40];
          const bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
          snprintf(meta, sizeof(meta), "%ld dBm  %s", (long)WiFi.RSSI(i), open ? "open" : "secured");
          ui.text(meta, 48, y + 40, Ui::W - 150, 22, TextAlign::Left, sub);
          if (on) ui.text("select", Ui::W - 130, y + 20, 96, 24, TextAlign::Right, fg);
        }
        // footer buttons: Rescan
        ui.strokeRect(30, Ui::H - 84, Ui::W - 60, 60, 2, 12);
        ui.text("Rescan", 30, Ui::H - 66, Ui::W - 60, 26, TextAlign::Center);
        ui.text("Left/Right: move    tap a row: choose", 30, Ui::H - 108,
                Ui::W - 60, 20, TextAlign::Center, Color::DarkGray);
        ui.flushFull();
        needRedraw = false;
      }

      input.update();
      float nx, ny;
      // nav keys move the highlight
      if ((input.wasPressed(InputManager::BTN_UP) ||
           input.wasPressed(InputManager::BTN_LEFT)) && n > 0) {
        sel = (sel + n - 1) % n; needRedraw = true;
      } else if ((input.wasPressed(InputManager::BTN_DOWN) ||
                  input.wasPressed(InputManager::BTN_RIGHT)) && n > 0) {
        sel = (sel + 1) % n; needRedraw = true;
      } else if (input.wasPressed(InputManager::BTN_CONFIRM) && n > 0) {
        strncpy(ssidOut, WiFi.SSID(sel).c_str(), ssidCap - 1);
        ssidOut[ssidCap - 1] = 0;
        *openOut = (WiFi.encryptionType(sel) == WIFI_AUTH_OPEN);
        return sel;
      } else if (input.wasTouchTap(nx, ny)) {
        int16_t px, py;
        Ui::touchToLogical(nx, ny, px, py);
        (void)px;  // list is full-width; only the row (y) matters here
        // rescan button?
        if (py >= Ui::H - 84 && py < Ui::H - 24) { needRedraw = true; break; }  // rebreak -> rescan
        const int16_t rowTop = 100, rowH = 72;
        if (py >= rowTop) {
          int idx = (py - rowTop) / rowH;
          if (idx >= 0 && idx < n) {
            strncpy(ssidOut, WiFi.SSID(idx).c_str(), ssidCap - 1);
            ssidOut[ssidCap - 1] = 0;
            *openOut = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
            return idx;
          }
        }
      }
      delay(12);
    }
    // fell out of selection loop -> rescan
  }
}

// ---- public entry ---------------------------------------------------------
//
// Runs the whole flow. Returns true if connected. Fills g_resultLine for the
// debug screen either way.
static bool run(Ui& ui, InputManager& input, uint32_t savedTimeoutMs,
                uint32_t joinTimeoutMs) {
  WiFi.mode(WIFI_STA);

  auto paintSaved = [&](int tick) {
    ui.clear();
    ui.centered("Wi-Fi", 300, 38);
    ui.centered("connecting to saved network", 362, 26, Color::DarkGray);
    char dots[4] = {0};
    for (int i = 0; i < (tick % 4); ++i) dots[i] = '.';
    ui.centered(dots, 410, 30);
    ui.flushFull();
  };

  // --- 1. Try saved credentials silently --------------------------------
  // WiFi.begin() with no args reuses the last SSID/pass ESP32 stored in NVS.
  WiFi.begin();
  paintSaved(0);
  if (joinWait(savedTimeoutMs, paintSaved)) {
    snprintf(g_resultLine, sizeof(g_resultLine), "%s  %s",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    return true;
  }

  // --- 2/3/4. Scan, pick, type password, connect ------------------------
  for (;;) {
    char ssid[33] = "";
    bool open = false;
    pickNetwork(ui, input, ssid, sizeof(ssid), &open);
    if (ssid[0] == 0) continue;  // (rescan path returns via loop)

    const char* pass = "";
    if (!open) {
      Keyboard kb(ui, input);
      pass = kb.run(ssid);
    }

    // connecting screen
    auto paintJoin = [&](int tick) {
      ui.clear();
      ui.centered("Wi-Fi", 300, 38);
      char l[48]; snprintf(l, sizeof(l), "connecting to %s", ssid);
      ui.centered(l, 362, 26, Color::DarkGray);
      char dots[4] = {0};
      for (int i = 0; i < (tick % 4); ++i) dots[i] = '.';
      ui.centered(dots, 410, 30);
      ui.flushFull();
    };

    WiFi.begin(ssid, pass);   // ESP32 persists these for next boot
    paintJoin(0);
    if (joinWait(joinTimeoutMs, paintJoin)) {
      snprintf(g_resultLine, sizeof(g_resultLine), "%s  %s",
               WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      return true;
    }

    // failed -> offer retry / back to list
    ui.clear();
    ui.centered("Connection failed", 300, 34);
    ui.centered(ssid, 350, 26, Color::DarkGray);
    ui.centered("tap to choose another network", 420, 24, Color::DarkGray);
    ui.flushFull();
    // wait for a tap, then loop back to the picker
    for (;;) {
      input.update();
      float nx, ny;
      if (input.wasTouchTap(nx, ny) || input.wasAnyPressed()) break;
      delay(15);
    }
    snprintf(g_resultLine, sizeof(g_resultLine), "FAILED (%s)", ssid);
    // loop back to scan/pick
  }
}

}  // namespace wifiprov
