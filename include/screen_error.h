#pragma once

// ===========================================================================
// screen_error — the error-screen family: No Home Assistant, No room config,
// Charge the device (critically low battery), and a debug preview that pages
// through all of them. They share one visual template (drawErrorScreen) and
// a deep-sleep-and-retry helper, so they live together in one file rather
// than duplicating that shared plumbing across four.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"

// The shared error-screen format: status bar, ringed icon, bold title, up to
// three dim body lines, a filled action button, and a dim footer.
inline void drawErrorScreen(const char* barLabel, const char* title, const char* l1, const char* l2,
                            const char* l3, const char* button, const char* subButton,
                            const char* footer, bool sleeping, const freeink::Icon* icon = nullptr) {
  ui.clear();
  drawStatusBar(barLabel, /*showMoon=*/sleeping);
  const int16_t cx = Ui::W / 2;
  const int16_t W2 = static_cast<int16_t>(Ui::W - kShPad * 2);

  ui.strokeRect(static_cast<int16_t>(cx - 52), 148 + kPad, 104, 104, 3, 52);
  if (icon)
    ui.iconScaled(*icon, static_cast<int16_t>(cx - 27), static_cast<int16_t>(148 + kPad + 25), 54,
                  54);
  else
    ui.text("!", static_cast<int16_t>(cx - 16), 168 + kPad, 32, 64, TextAlign::Center, Color::Black);

  ui.text(title, 0, 286 + kPad, Ui::W, 40, TextAlign::Center, Color::Black);
  // Body: promoted from the tiny chrome font to the full body face so it reads
  // in proportion to the title and button.
  const char* body[3] = {l1, l2, l3};
  for (int i = 0; i < 3; ++i)
    if (body[i] && *body[i])
      ui.text(body[i], kShPad, static_cast<int16_t>(338 + kPad + i * 32), W2, 30, TextAlign::Center,
              Color::DarkGray);

  ui.fillRect(kShPad, 470 + kPad, W2, 76, Color::Black, 16);
  ui.text(button, kShPad, 480 + kPad, W2, 30, TextAlign::Center, Color::White);
  if (subButton && *subButton)
    ui.text(subButton, kShPad, 514 + kPad, W2, 22, TextAlign::Center, Color::LightGray, 1,
            Ui::kFontSmall);

  if (footer && *footer)
    ui.text(footer, 0, 576 + kPad, Ui::W, 24, TextAlign::Center, Color::DarkGray);

  // Always Full: every draw here is a whole-screen swap (a different error
  // entirely in screen_err_preview's next()/prev()) with no interactive
  // control to give feedback for, so there's no "sleeping" special case left.
  commitFrame(Rf::Full);
}

// ===========================================================================
// Stage — NO HOME ASSISTANT
// ===========================================================================
// Shown when Wi-Fi is up but the Switchboard/HA server never answered. Any key
// retries; the Home key opens Settings. On idle it deep-sleeps and retries in
// 30 min (rtcNoHA routes the timer wake).
namespace screen_no_ha {

inline void draw(bool sleeping = false) {
  const bool wifi = WiFi.status() == WL_CONNECTED;
  drawErrorScreen("Home Assistant", "No Home Assistant",
                  "Wi-Fi is up, but the Switchboard",
                  "server isn't answering. Check it's",
                  "online and the room token is set.", "RETRY",
                  "any key  -  Home opens configuration",
                  sleeping ? "retrying in 30 min"
                           : (wifi ? "connected  -  HA unreachable" : "Wi-Fi down"),
                  sleeping, &kErr_server);
}

inline void enter() {
  stage = Stage::NoHA;
  standbyIdleSinceMs = millis();
  draw(/*sleeping=*/false);
}

}  // namespace screen_no_ha

// ===========================================================================
// Stage — NO ROOM CONFIG
// ===========================================================================
namespace screen_no_room {

inline void draw(bool sleeping = false) {
  drawErrorScreen("Setup", "No room config",
                  "The server has no config for this",
                  "device. Add one, or choose another",
                  "room in Settings.", "SELECT ROOM",
                  "any key retries  -  Home opens Settings",
                  sleeping ? "retrying in 30 min" : "connected  -  no device config",
                  sleeping, &kWx_ui_cog);
}

inline void enter() {
  stage = Stage::NoRoom;
  standbyIdleSinceMs = millis();
  draw(/*sleeping=*/false);
}

}  // namespace screen_no_room

// Idle on No-HA / No-room: deep-sleep, keeping the SAME screen on wake
// instead of reverting to the carousel — a button wake still returns through
// the fast wake path (rtcStandbyActive), and setup() re-checks rtcNoHA to
// redraw the right one of the two before retrying.
[[noreturn]] inline void noHASleepNow() {
  if (frontlight.present()) frontlight.off();
  if (stage == Stage::NoRoom) screen_no_room::draw(/*sleeping=*/true);
  else                        screen_no_ha::draw(/*sleeping=*/true);
  rtcNoHA = true;
  rtcStandbyActive = true;  // a button wake still returns through the fast path
  deepSleepWithWake(30ULL * 60ULL * 1000000ULL);  // retry in 30 minutes
}

// ===========================================================================
// Stage — CHARGE THE DEVICE (critically low battery)
// ===========================================================================
namespace screen_low_battery {

// Shown once per wake when the battery reads critically low.
inline bool shown = false;

inline void enter() {
  stage = Stage::LowBattery;
  shown = true;
  standbyIdleSinceMs = millis();
  char foot[32];
  snprintf(foot, sizeof(foot), "battery %u%%", g_battPct);
  drawErrorScreen("Battery", "Charge the device", "Battery very low. Plug in a USB-C",
                  "cable to keep the screen updating.", "", "DISMISS", "any key",
                  foot, /*sleeping=*/false, &kErr_battery);
}

// Idle on the charge screen: deep-sleep WITHOUT redrawing, so the "Charge the
// device" screen stays on the panel through power save. A button wake or the
// 10-minute timer re-checks the battery (setup()'s rtcLowBattery path).
[[noreturn]] inline void sleepNow() {
  rtcLowBattery = true;
  if (frontlight.present()) frontlight.off();
  deepSleepWithWake(10ULL * 60ULL * 1000000ULL);
}

}  // namespace screen_low_battery

// ===========================================================================
// error-screen preview (jump list -> "Error states") — a debug page-through
// of every error screen's look, without the real conditions that trigger them.
// ===========================================================================
namespace screen_err_preview {

struct Entry {
  const char* bar; const char* title; const char* l1; const char* l2; const char* l3;
  const char* button; const char* sub; const char* foot;
  const freeink::Icon* icon;
};
inline const Entry kEntries[] = {
    {"Wi-Fi", "No Wi-Fi", "Couldn't join a Wi-Fi network.",
     "Check the router and the saved", "password.", "SET UP WI-FI",
     "any key to retry", "Wi-Fi disconnected", &kWifiEmpty},
    {"Home Assistant", "No Home Assistant", "Wi-Fi is up, but the Switchboard",
     "server isn't answering. Check it's", "online and the room token is set.",
     "RETRY", "Home opens configuration", "connected  -  HA unreachable", &kErr_server},
    {"Setup", "No room config", "The server has no config for this",
     "device. Add one, or choose another", "room in Settings.", "SELECT ROOM", "",
     "connected  -  no device config", &kWx_ui_cog},
    {"Weather", "Weather unavailable", "No weather entity is set, or HA",
     "returned no reading for it.", "", "OPEN CONFIGURATION", "",
     "connected  -  no weather", &kErr_cloud},
    {"Battery", "Charge the device", "Battery very low. Plug in a USB-C",
     "cable to keep the screen updating.", "", "OK", "any key to dismiss",
     "battery critically low", &kErr_battery},
};
inline constexpr int kCount = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));
inline int idx = 0;

inline void draw() {
  const Entry& e = kEntries[idx];
  drawErrorScreen(e.bar, e.title, e.l1, e.l2, e.l3, e.button, e.sub, e.foot, /*sleeping=*/false,
                  e.icon);
}
inline void enter() {
  stage = Stage::ErrPreview;
  idx = 0;
  standbyIdleSinceMs = millis();
  draw();
}
inline void next() { idx = (idx + 1) % kCount; draw(); }
inline void prev() { idx = (idx + kCount - 1) % kCount; draw(); }

}  // namespace screen_err_preview
