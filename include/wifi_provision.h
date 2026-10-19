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
// Or, from the list, "Set up with your phone": the remote opens its own
// hotspot (Switchboard-XXXX, a password of its own) and shows two QR codes,
// one that joins the hotspot and one that opens its page (most phones open it
// by themselves: the remote answers every name, as a captive portal). The
// page lists the networks it saw; pick one, type its password, save, and the
// remote joins it as in step 4.
//
// The keyboard is drawn with the same immediate-mode Ui primitives as the rest
// of the firmware (no dependency on the heavier FreeInkUI activity framework),
// hit-tested against InputManager tap coordinates.
// ===========================================================================

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <InputManager.h>
#include "qr_draw.h"
#include "ui.h"

namespace wifiprov {

using Color = freeink::ui::Color;
using TextAlign = freeink::ui::TextAlign;

// Result of the whole provisioning flow, surfaced to the debug screen.
inline char g_resultLine[64] = "";

// Give up after this long with no input on any provisioning screen (0 =
// never) — run() then returns false with g_timedOut set, and the boot path
// sleeps instead of sitting awake on a picker nobody is looking at.
inline uint32_t g_idleTimeoutMs = 0;
inline uint32_t g_lastInputMs = 0;
inline bool g_timedOut = false;
inline void noteInput() { g_lastInputMs = millis(); }
inline bool idleExpired() {
  if (g_idleTimeoutMs == 0 || millis() - g_lastInputMs <= g_idleTimeoutMs) return false;
  g_timedOut = true;
  return true;
}

// ---- small helpers --------------------------------------------------------

// Poll the join for up to timeoutMs, pumping a caller status callback every
// ~1.2 s so the screen can animate (a fast partial refresh each — the
// caller's first paint, tick 0, is the only full one). Returns true if
// connected.
template <typename PaintFn>
static bool joinWait(uint32_t timeoutMs, PaintFn paint) {
  const uint32_t start = millis();
  int tick = 0;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(1200);
    paint(++tick);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Status-screen commit for joinWait's paint callbacks: a full refresh for
// the first frame, a fast partial for each animation tick after it.
static void commitJoinFrame(Ui& ui, int tick) {
  if (tick == 0) ui.flushFull();
  else           ui.flushFast();
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

  // Just the password field + hint row, after a key press. A FULL refresh:
  // the field repaints on every key, and fast partial refreshes left it
  // heavily ghosted.
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
    ui.flushFull();
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
    // rotation used everywhere (Ui::touchToLogical).
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
  // open networks), or nullptr if it sat idle past g_idleTimeoutMs. Blocks,
  // pumping input.
  const char* run(const char* ssid) {
    draw(ssid);
    noteInput();
    for (;;) {
      input.update();
      if (idleExpired()) return nullptr;
      float nx, ny;
      if (input.wasTouchTap(nx, ny)) {
        noteInput();
        const int r = handleTap(nx, ny);
        if (r == 2) break;              // OK
        if (r == 1) drawField();        // letter/space/del -> just the field
        else if (r == 3) draw(ssid);    // layer change -> full
      }
      delay(8);
    }
    return buffer;
  }
};

// ---- AP scan + list -------------------------------------------------------

inline constexpr int16_t kPhoneBtnY = Ui::H - 156, kPhoneBtnH = 60;

// Scan and render a selectable AP list; returns the chosen SSID index, -4 for
// "Set up with your phone", or -3 if it sat idle past g_idleTimeoutMs. Fills
// ssidOut/openOut.
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
    const int maxShow = 7;
    if (n > maxShow) n = maxShow;

    // selection loop — the first paint after a scan is a full refresh;
    // moving the highlight is a fast partial one.
    int sel = 0;
    bool needRedraw = true;
    bool fullRedraw = true;
    noteInput();
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
        // footer buttons: Set up with your phone (highlighted as the entry
        // after the networks), Rescan
        const bool phoneOn = (sel == n);
        if (phoneOn) ui.fillRect(30, kPhoneBtnY, Ui::W - 60, kPhoneBtnH, Color::Black, 12);
        else         ui.strokeRect(30, kPhoneBtnY, Ui::W - 60, kPhoneBtnH, 2, 12);
        ui.text("Set up with your phone", 30, kPhoneBtnY + 18, Ui::W - 60, 26, TextAlign::Center,
                phoneOn ? Color::White : Color::Black);
        ui.strokeRect(30, Ui::H - 84, Ui::W - 60, 60, 2, 12);
        ui.text("Rescan", 30, Ui::H - 66, Ui::W - 60, 26, TextAlign::Center);
        ui.text("Left/Right: move    tap a row: choose", 30, kPhoneBtnY - 28,
                Ui::W - 60, 20, TextAlign::Center, Color::DarkGray);
        if (fullRedraw) ui.flushFull();
        else            ui.flushFast();
        needRedraw = false;
        fullRedraw = false;
      }

      input.update();
      if (idleExpired()) return -3;
      float nx, ny;
      // nav keys move the highlight
      if (input.wasAnyPressed()) noteInput();
      // (n + 1 entries: the networks, then the phone button)
      if (input.wasPressed(InputManager::BTN_UP) ||
          input.wasPressed(InputManager::BTN_LEFT)) {
        sel = (sel + n) % (n + 1); needRedraw = true;
      } else if (input.wasPressed(InputManager::BTN_DOWN) ||
                 input.wasPressed(InputManager::BTN_RIGHT)) {
        sel = (sel + 1) % (n + 1); needRedraw = true;
      } else if (input.wasPressed(InputManager::BTN_CONFIRM) && sel == n) {
        return -4;
      } else if (input.wasPressed(InputManager::BTN_CONFIRM) && n > 0) {
        strncpy(ssidOut, WiFi.SSID(sel).c_str(), ssidCap - 1);
        ssidOut[ssidCap - 1] = 0;
        *openOut = (WiFi.encryptionType(sel) == WIFI_AUTH_OPEN);
        return sel;
      } else if (input.wasTouchTap(nx, ny)) {
        noteInput();
        int16_t px, py;
        Ui::touchToLogical(nx, ny, px, py);
        (void)px;  // list is full-width; only the row (y) matters here
        // rescan button?
        if (py >= Ui::H - 84 && py < Ui::H - 24) { needRedraw = true; break; }  // rebreak -> rescan
        if (py >= kPhoneBtnY && py < kPhoneBtnY + kPhoneBtnH) return -4;
        const int16_t rowTop = 100, rowH = 72;
        if (py >= rowTop && py < kPhoneBtnY - 28) {
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

// ---- set up from a phone --------------------------------------------------
//
// The remote's own hotspot, a captive-portal DNS that answers every name with
// its address, and a page to choose the network on. As the viewport does it
// (Switchboard-Viewport's net/wifi.cpp), minus the server address: the remote
// only ever finds its server by mDNS.

namespace phone {

inline WebServer* g_web = nullptr;
inline bool g_saved = false;
inline char g_ssid[33] = "";
inline char g_pass[65] = "";
inline String g_options;  // the networks it saw, as <option>s

inline String escape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

// The networks in range, strongest first (as scanNetworks() sorts them),
// each name once.
inline void scan() {
  int n = 0;
  for (int attempt = 0; attempt < 3; ++attempt) {  // as pickNetwork does
    WiFi.scanDelete();
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    delay(120);
    n = WiFi.scanNetworks(/*async=*/false);
    if (n > 0) break;
    delay(300);
  }
  if (n < 0) n = 0;
  g_options = "";
  for (int i = 0; i < n; ++i) {
    const String name = escape(WiFi.SSID(i));
    if (!name.length() || g_options.indexOf("value=\"" + name + "\"") >= 0) continue;
    g_options += "<option value=\"" + name + "\">" + name + " (" + String((int)WiFi.RSSI(i)) + " dBm)</option>";
  }
  WiFi.scanDelete();
}

inline const char* kHead =
    "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Switchboard Remote</title><style>"
    "body{font-family:system-ui,sans-serif;margin:0;background:#f4f4f4;color:#111}"
    "main{max-width:420px;margin:0 auto;padding:24px}h1{font-size:22px;letter-spacing:.08em}"
    "label{display:block;margin:16px 0 6px;font-weight:600}select,input{width:100%;box-sizing:border-box;font-size:17px;padding:10px;"
    "border:1px solid #999;border-radius:8px;background:#fff}button{margin-top:22px;width:100%;font-size:18px;padding:12px;"
    "border:0;border-radius:8px;background:#111;color:#fff}p{color:#444}</style></head><body><main>"
    "<h1>SWITCHBOARD</h1>";

inline void page() {
  noteInput();
  String html = kHead;
  html += "<p>Choose the Wi-Fi network this remote should join.</p><form method=post action=/save>";
  html += "<label>Network</label><select name=pick><option value=''>Type one below</option>" + g_options + "</select>";
  html += "<label>Or its name</label><input name=ssid autocomplete=off maxlength=32>";
  html += "<label>Password</label><input name=pass type=password maxlength=64>";
  html += "<button>Save and connect</button></form><p><a href=/rescan>Look for networks again</a></p></main></body></html>";
  g_web->send(200, "text/html", html);
}

inline void save() {
  noteInput();
  String ssid = g_web->arg("ssid");
  ssid.trim();
  if (!ssid.length()) ssid = g_web->arg("pick");
  if (!ssid.length()) {
    g_web->send(400, "text/html", String(kHead) + "<p>Choose a network or type its name.</p><p><a href=/>Back</a></p></main></body></html>");
    return;
  }
  snprintf(g_ssid, sizeof(g_ssid), "%s", ssid.c_str());
  snprintf(g_pass, sizeof(g_pass), "%s", g_web->arg("pass").c_str());
  g_web->send(200, "text/html",
              String(kHead) + "<p>Saved. The remote is joining <b>" + escape(ssid) +
                  "</b> now.</p><p>If it can't join it, it shows its list of networks again.</p></main></body></html>");
  g_saved = true;
}

// Eight characters, no look-alikes.
inline void password(char* out) {
  static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (int i = 0; i < 8; ++i) out[i] = A[esp_random() % (sizeof(A) - 1)];
  out[8] = 0;
}

inline constexpr int16_t kQrBox = 232;
inline constexpr int16_t kKeyboardBtnY = Ui::H - 84;

// The setup screen: the hotspot's QR code (and its name and password, to
// type), then the page's (and its address).
inline void draw(Ui& ui, const char* apName, const char* apPass, const char* url) {
  ui.clear();
  ui.centered("Set up with your phone", 22, 32);
  ui.hline(30, 64, Ui::W - 60, 2);

  ui.centered("1. Scan to join the remote's Wi-Fi", 76, 24);
  char uri[96];
  qrdraw::wifiUri(uri, sizeof(uri), apName, apPass, false);
  qrdraw::draw(ui, uri, 106, kQrBox);
  char line[64];
  snprintf(line, sizeof(line), "%s   password %s", apName, apPass);
  ui.centered(line, 106 + kQrBox + 6, 24, Color::DarkGray);

  const int16_t y2 = 106 + kQrBox + 46;
  ui.centered("2. Then scan to open the setup page", y2, 24);
  qrdraw::draw(ui, url, y2 + 30, kQrBox - 40);
  ui.centered(url, y2 + 30 + kQrBox - 40 + 6, 24, Color::DarkGray);

  ui.strokeRect(30, kKeyboardBtnY, Ui::W - 60, 60, 2, 12);
  ui.text("Use the keyboard instead", 30, kKeyboardBtnY + 18, Ui::W - 60, 26, TextAlign::Center);
  ui.flushFull();  // a dense module grid ghosts badly under a partial refresh
}

}  // namespace phone

// Runs the phone setup until a network is saved on the page (1, ssidOut/passOut
// filled), "Use the keyboard instead" or a button (0: back to the list), or
// it sits idle past g_idleTimeoutMs (-3). The hotspot is gone again either way.
static int phoneSetup(Ui& ui, InputManager& input, char* ssidOut, size_t ssidCap, char* passOut,
                      size_t passCap) {
  ui.clear();
  ui.centered("Set up with your phone", 300, 32);
  ui.centered("starting...", 352, 26, Color::DarkGray);
  ui.flushFull();
  phone::scan();

  char apName[24], apPass[9];
  {
    uint8_t m[6];
    WiFi.macAddress(m);
    snprintf(apName, sizeof(apName), "Switchboard-%02X%02X", m[4], m[5]);
  }
  phone::password(apPass);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName, apPass);
  delay(200);
  const IPAddress ip = WiFi.softAPIP();
  char url[32];
  snprintf(url, sizeof(url), "http://%s", ip.toString().c_str());
  Serial.printf("[wifi] phone setup: %s at %s\n", apName, url);

  DNSServer dns;
  WebServer web(80);
  phone::g_web = &web;
  phone::g_saved = false;
  dns.start(53, "*", ip);
  web.on("/", HTTP_GET, phone::page);
  web.on("/save", HTTP_POST, phone::save);
  web.on("/rescan", HTTP_GET, [] {
    phone::scan();
    phone::g_web->sendHeader("Location", "/");
    phone::g_web->send(302, "text/plain", "");
  });
  // Every other address (a phone checking for a captive portal) lands here.
  const String home = String(url) + "/";
  web.onNotFound([home] {
    phone::g_web->sendHeader("Location", home);
    phone::g_web->send(302, "text/plain", "");
  });
  web.begin();
  phone::draw(ui, apName, apPass, url);

  int result = 0;
  uint32_t savedAt = 0;
  noteInput();
  for (;;) {
    dns.processNextRequest();
    web.handleClient();
    if (phone::g_saved && !savedAt) savedAt = millis();
    // Let the "Saved" page reach the phone, then join.
    if (savedAt && millis() - savedAt > 1500) {
      snprintf(ssidOut, ssidCap, "%s", phone::g_ssid);
      snprintf(passOut, passCap, "%s", phone::g_pass);
      result = 1;
      break;
    }
    input.update();
    if (idleExpired()) { result = -3; break; }
    float nx, ny;
    if (input.wasTouchTap(nx, ny)) {
      noteInput();
      int16_t px, py;
      Ui::touchToLogical(nx, ny, px, py);
      if (py >= phone::kKeyboardBtnY) break;
    } else if (input.wasAnyPressed()) {
      break;
    }
    delay(5);
  }
  web.stop();
  dns.stop();
  phone::g_web = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  return result;
}

// ---- public entry ---------------------------------------------------------
//
// Runs the whole flow. Returns true if connected; false only if a screen sat
// idle past `idleTimeoutMs` (g_timedOut is then set; 0 = wait forever).
// Fills g_resultLine for the debug screen either way.
static bool run(Ui& ui, InputManager& input, uint32_t savedTimeoutMs,
                uint32_t joinTimeoutMs, uint32_t idleTimeoutMs = 0) {
  g_idleTimeoutMs = idleTimeoutMs;
  g_timedOut = false;
  WiFi.mode(WIFI_STA);

  auto paintSaved = [&](int tick) {
    ui.clear();
    ui.centered("Wi-Fi", 300, 38);
    ui.centered("connecting to saved network", 362, 26, Color::DarkGray);
    char dots[4] = {0};
    for (int i = 0; i < (tick % 4); ++i) dots[i] = '.';
    ui.centered(dots, 410, 30);
    commitJoinFrame(ui, tick);
  };

  // --- 1. Try saved credentials silently --------------------------------
  // The last SSID/pass ESP32 stored in NVS, passed explicitly so a join
  // never inherits a BSSID/channel lock (see app/wifi_link.h's fast join).
  {
    wifi_config_t conf = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0])
      WiFi.begin(reinterpret_cast<const char*>(conf.sta.ssid),
                 reinterpret_cast<const char*>(conf.sta.password));
    else
      WiFi.begin();
  }
  paintSaved(0);
  if (joinWait(savedTimeoutMs, paintSaved)) {
    snprintf(g_resultLine, sizeof(g_resultLine), "%s  %s",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    return true;
  }

  // --- 2/3/4. Scan, pick, type password, connect ------------------------
  for (;;) {
    char ssid[33] = "";
    char pass[65] = "";
    bool open = false;
    const int picked = pickNetwork(ui, input, ssid, sizeof(ssid), &open);
    if (picked == -3) return false;  // idle
    if (picked == -4) {
      // From a phone: the page gives both the network and its password.
      const int r = phoneSetup(ui, input, ssid, sizeof(ssid), pass, sizeof(pass));
      if (r == -3) return false;  // idle
      if (r == 0) continue;       // back to the list
    } else {
      if (ssid[0] == 0) continue;  // (rescan path returns via loop)
      // Copied out of the Keyboard: its buffer dies with it at the end of the
      // if-block, before WiFi.begin() below reads the password.
      if (!open) {
        Keyboard kb(ui, input);
        const char* typed = kb.run(ssid);
        if (!typed) return false;  // idle
        snprintf(pass, sizeof(pass), "%s", typed);
      }
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
      commitJoinFrame(ui, tick);
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
    noteInput();
    for (;;) {
      input.update();
      if (idleExpired()) return false;
      float nx, ny;
      if (input.wasTouchTap(nx, ny) || input.wasAnyPressed()) break;
      delay(15);
    }
    snprintf(g_resultLine, sizeof(g_resultLine), "FAILED (%s)", ssid);
    // loop back to scan/pick
  }
}

}  // namespace wifiprov
