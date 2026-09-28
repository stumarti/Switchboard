#pragma once

// ===========================================================================
// screen_pairing — the "approve this remote" gate, run on a first-run boot
// right after Wi-Fi connects (app/boot.h), before anything else touches the
// server. A boot that's already paired (NVS has a token) is a no-op
// fast-path — no network round trip, no screen shown.
//
// Flow — it registers ONCE, then leaves the server alone:
//
//   register ──(server unreachable)──> "Can't reach the server"
//      │                                 retries every 30 s, or on a key press
//      ├──(approved)──> done
//      └──(pending)───> "Approve this remote on the server, then press any
//                        button" — no polling; each key press checks once
//
// Every state change is one full refresh (no ghosting, and nothing refreshes
// while it waits). The device used to re-register every 4 s for as long as
// it sat here, each poll a server-side write and a full panel refresh.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"  // ensureMdns()
#include "pairing_client.h"
#include "device_config_client.h"

namespace screen_pairing {

// Automatic retries while the server can't be reached at all.
inline constexpr uint32_t kRetryMs = 30000;

enum class View : uint8_t { Checking, Unreachable, Pending, NotYet };

// One multi-line message in the large (28 px) face, centred.
inline void bigLine(const char* s, int16_t y) {
  ui.text(s, 0, y, Ui::W, 36, TextAlign::Center, Color::Black, 1, Ui::kFont28);
}
inline void smallLine(const char* s, int16_t y, Color c = Color::DarkGray) {
  ui.text(s, 0, y, Ui::W, 24, TextAlign::Center, c, 1, Ui::kFontSmall);
}

inline void draw(View v) {
  ui.clear();
  ui.centered("Switchboard", 150, 38);
  ui.hline(Ui::W / 2 - 120, 200, 240, 2);

  switch (v) {
    case View::Checking:
      bigLine("Checking with the", 300);
      bigLine("Switchboard server...", 340);
      break;
    case View::Unreachable:
      bigLine("Can't reach the", 290);
      bigLine("Switchboard server", 330);
      if (pairing::status[0]) {
        char why[80];
        snprintf(why, sizeof(why), "%s", pairing::status);
        smallLine(why, 390);
      }
      // The server's mDNS name only resolves from the same subnet (or across
      // a reflector), so this device's own address is half the diagnosis.
      {
        char ipLine[40];
        snprintf(ipLine, sizeof(ipLine), "this device: %s", WiFi.localIP().toString().c_str());
        smallLine(ipLine, 416);
      }
      bigLine("Retrying every 30 s,", 480);
      bigLine("or press any button", 520);
      break;
    case View::Pending:
    case View::NotYet:
      if (v == View::NotYet) {
        bigLine("Not approved yet.", 260);
      }
      bigLine("Approve this remote on", 310);
      bigLine("the Switchboard server's", 350);
      bigLine("Remotes page, then", 390);
      bigLine("press any button.", 430);
      break;
  }

  char macLine[40];
  snprintf(macLine, sizeof(macLine), "MAC %s", pairing::mac);
  smallLine(macLine, 620);
  ui.flushFull();
}

// Block until any physical button or a screen tap (InputManager polled
// directly — the input task isn't running during first-run setup), up to
// `maxWaitMs` (0 = no limit). Returns false if it timed out.
inline bool waitForPress(uint32_t maxWaitMs) {
  const uint32_t start = millis();
  float nx, ny;
  for (;;) {
    input.update();
    if (input.wasAnyPressed() || input.wasHomeKeyTapped() ||
        (input.hasTouch() && input.wasTouchTap(nx, ny)))
      return true;
    if (maxWaitMs && millis() - start >= maxWaitMs) return false;
    delay(20);
  }
}

// One registration attempt. SWITCHBOARD_SERVER_HOST is an mDNS name, and
// MDNS.queryHost() fails outright until MDNS.begin() has run — hence
// ensureMdns() first (a guarded no-op once up).
inline bool attempt() {
  ensureMdns();
  const bool approved = pairing::registerOnce();
  Serial.printf("[pairing] register %s (ip %s): %s\n", pairing::mac,
                WiFi.localIP().toString().c_str(), approved ? "approved" : pairing::status);
  return approved;
}

// Returns true once approved (the server-assigned room, if the admin picked
// one at approval time and no room was picked on the device by hand, is
// applied — see deviceconfig::applyServerAssignedSlug). Returns false if it
// sat with no button press for `idleTimeoutMs` (0 = never) — the boot path
// then sleeps ("Setup paused"; any button resumes it).
inline bool run(uint32_t idleTimeoutMs = 0) {
  pairing::loadFromNvs();
  if (pairing::paired) return true;
  pairing::ensureMac();

  uint32_t idleSince = millis();
  bool everPending = false;
  View view = View::Checking;
  draw(view);
  for (;;) {
    if (attempt()) break;

    bool pressed = false;
    if (!pairing::serverAnswered) {
      // Couldn't reach the server: retry on our own every 30 s, sooner on a
      // key press. The screen only repaints when something changes.
      if (view != View::Unreachable) draw(view = View::Unreachable);
      pressed = waitForPress(kRetryMs);
      if (pressed) idleSince = millis();
      else if (idleTimeoutMs && millis() - idleSince > idleTimeoutMs) return false;
    } else {
      // Registered — the server has it as pending. Leave the server alone
      // until the user says it's been approved.
      draw(view = everPending ? View::NotYet : View::Pending);
      everPending = true;
      const uint32_t left =
          idleTimeoutMs ? (millis() - idleSince < idleTimeoutMs ? idleTimeoutMs - (millis() - idleSince) : 1)
                        : 0;
      if (!waitForPress(left)) return false;
      idleSince = millis();
      pressed = true;
    }
    // A key press gets instant "Checking..." feedback; a silent 30 s retry
    // leaves the screen alone.
    if (pressed) draw(view = View::Checking);
  }

  if (pairing::assignedSlug[0]) {
    deviceconfig::applyServerAssignedSlug(pairing::assignedSlug);
  }
  return true;
}

}  // namespace screen_pairing
