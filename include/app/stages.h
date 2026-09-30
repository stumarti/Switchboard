#pragma once

// ===========================================================================
// stages — what one loop() tick does on each screen. loop() reads one
// InFrame and hands it to tickStage(), which dispatches on `stage`.
//
// Cross-cutting rules applied here, once, instead of per screen:
//   - any input at all resets the idle timer;
//   - every screen without its own idle handling sleeps after the screen
//     timeout (falling back to the status page), so no screen can keep the
//     device awake until the battery is flat;
//   - a 10 s Power hold opens the power menu from any stage.
// ===========================================================================

#include <Arduino.h>

#include "screen_common.h"
#include "refresh_policy.h"
#include "globals_client.h"
#include "pairing_client.h"
#include "device_config_client.h"
#include "room_list_client.h"
#include "app/input.h"
#include "app/wifi_link.h"
#include "app/carousel.h"
#include "app/live.h"
#include "app/quick_access.h"
#include "app/data_refresh.h"
#include "app/power.h"
#include "screen_settings.h"
#include "screen_ota.h"
#include "screen_error.h"
#include "screen_debug.h"
#include "screen_power.h"


// ===========================================================================
// The carousel (Stage::Standby)
// ===========================================================================

// Left/Right: step through the carousel in this remote's page order
// (carouselSequence(): screens.order, hidden pages skipped), wrapping at
// either end. Status is always in the sequence, so it's never empty. A page
// turn is a screen switch — a full refresh.
static void turnCarouselPage(int dir) {
  uint8_t seq[kCarouselPages];
  const uint8_t n = carouselSequence(seq);
  uint8_t pos = 0;
  bool found = false;
  for (uint8_t k = 0; k < n; ++k) {
    if (seq[k] == carouselPage) {
      pos = k;
      found = true;
      break;
    }
  }
  if (!found) carouselPage = seq[0];  // current page was hidden: start over
  else carouselPage = seq[dir < 0 ? (pos + n - 1) % n : (pos + 1) % n];
  drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ScreenSwitch));
}

// A held drag in a page's slider (Lighting brightness, Music/TV volume)
// updates the level live on every frame. True if the tick was consumed.
static bool handlePageDrag(const InFrame& in) {
  switch (carouselPage) {
    case kPageLighting: return screen_lighting::handleDrag(in);
    case kPageMusic:    return screen_music::handleDrag(in);
    case kPageTv:       return screen_tv::handleDrag(in);
    case kPageReceiver: return screen_receiver::handleDrag(in);
    default:            return false;
  }
}

// A tap on the showing page's own controls. True if the tick was consumed.
static bool handlePageTap(const InFrame& in) {
  switch (carouselPage) {
    case kPageLighting: return screen_lighting::handleTap(in);
    case kPageClimate:  return screen_climate::handleTap(in);
    case kPageBlinds:   return screen_blinds::handleTap(in);
    case kPageMusic:    return screen_music::handleTap(in);
    case kPageTv:       return screen_tv::handleTap(in);
    case kPageXbox:     return screen_xbox::handleTap(in);
    case kPageReceiver: return screen_receiver::handleTap(in);
    case kPageWifi:     return screen_wifi_networks::handleTap(in);
    default:            return false;
  }
}

// Keeping the showing page current while it's live (app/live.h): a playing
// track or a running game is watched through one held-open request, renewed
// here every frame so it lapses the moment the page, the stage or the screen
// changes. A moving blind still re-reads until it stops. Every other page is
// passive: it waits for a press.
static void pollShowingPage() {
  live::Page watch = live::Page::None;
  switch (carouselPage) {
    case kPageBlinds: screen_blinds::pollWhileMoving(); break;
    case kPageMusic:
      if (live::g_unsupported) screen_music::pollWhilePlaying();
      else if (live::isLive(live::Page::Music)) watch = live::Page::Music;
      break;
    case kPageXbox:
      if (live::g_unsupported) screen_xbox::pollWhilePlaying();
      else if (live::isLive(live::Page::Xbox)) watch = live::Page::Xbox;
      break;
    default: break;
  }
  live::want(watch);
}

// A change the live watch brought: a new track or game is a full refresh
// (the art changed; clear its ghost), anything else a fast one. Changes to a
// page not showing are dropped — turning to it repaints in full anyway.
static bool repaintLiveChange() {
  uint8_t music = screen_music::g_liveChange, xbox = screen_xbox::g_liveChange;
  if (!music && !xbox) return false;
  screen_music::g_liveChange = 0;
  screen_xbox::g_liveChange = 0;
  const uint8_t change = carouselPage == kPageMusic ? music : carouselPage == kPageXbox ? xbox : 0;
  if (!change) return false;
  drawStandby(/*sleeping=*/false,
              refreshModeFor(change == 2 ? RefreshEvent::ScreenSwitch : RefreshEvent::DataLanding));
  return true;
}

// Every page's settleCheck() runs every tick (so each tracks its own busy
// edge continuously), but only the showing page can ask for a repaint —
// unless this frame already repainted (`draw` false).
static void settleShowingPage(bool draw) {
  bool repaint = false;
  repaint |= screen_climate::settleCheck(carouselPage == kPageClimate);
  repaint |= screen_blinds::settleCheck(carouselPage == kPageBlinds);
  repaint |= screen_lighting::settleCheck(carouselPage == kPageLighting);
  repaint |= screen_music::settleCheck(carouselPage == kPageMusic);
  repaint |= screen_tv::settleCheck(carouselPage == kPageTv);
  repaint |= screen_xbox::settleCheck(carouselPage == kPageXbox);
  repaint |= screen_receiver::settleCheck(carouselPage == kPageReceiver);
  if (repaint && draw) drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
}

// Keep the carousel in step with the background Wi-Fi link and data refresh:
//   - the link comes up    -> after a wake or a real outage, kick the refresh
//                             (its landing repaints, Wi-Fi glyph included);
//                             after an idle power-down the data is still
//                             fresh, so just the glyph repaints;
//   - the link goes down   -> the glyph repaints (the idle power-down too:
//                             the status bar always shows the true state);
//   - the refresh lands    -> drop the "updating" glyph and repaint the
//                             showing page, or divert to No-HA / No-room;
//   - the join gives up    -> drop the "updating" glyph.
// A repaint that only touches the status bar is a fast partial one
// (RefreshEvent::StatusGlyph); new data gets DataLanding.
// Returns true if it diverted to an error screen.
static bool syncCarouselWithNetwork() {
  const bool busy = g_weatherBusy;
  const bool wifi = wifilink::isUp();
  bool glyphRepaint = false, dataRepaint = false;

  if (wifi != standbyPrevWifi) {
    if (wifi && !wifilink::joinedFromIdle() && !busy) kickWeatherRefresh();
    // A refresh now running will repaint (glyph included) when it lands.
    if (!g_weatherBusy) glyphRepaint = true;
  }

  if (standbyPrevBusy && !busy) {
    g_wakeUpdating = false;
    standbyPrevBusy = false;
    standbyPrevWifi = wifi;
    // The server no longer approves this remote: restart into the pairing
    // screen (boot.h's first-run path) rather than sitting on No-HA.
    if (!pairing::paired) restartDevice();
    // Error screens only once the refresh has actually run and failed.
    if (wifi && !globalsclient::ok) { screen_no_ha::enter(); return true; }
    if (wifi && !deviceconfig::ok)  { screen_no_room::enter(); return true; }
    dataRepaint = true;
  }

  if (wifilink::takeJoinFailed() && g_wakeUpdating) {
    g_wakeUpdating = false;
    glyphRepaint = true;
  }

  standbyPrevBusy = g_weatherBusy;
  standbyPrevWifi = wifi;
  if (dataRepaint)
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::DataLanding));
  else if (glyphRepaint)
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::StatusGlyph));
  return false;
}

// Critically low battery -> the charge screen, once per wake.
static bool maybeShowLowBattery() {
  if (screen_low_battery::shown || g_battPct < 1 || g_battPct > kLowBatteryPct) return false;
  screen_low_battery::enter();
  return true;
}

static void tickCarousel(const InFrame& in) {
  if (screen_shade::open) { screen_shade::tick(in); return; }  // hold-Home control sheet
  if (jumpOpen) { tickQuickAccess(in); return; }                // Home-tap overlay
  if (handlePageDrag(in)) return;

  if (in.homeLong) { screen_shade::openSheet(); return; }
  if (in.homeTap)  { openQuickAccess(); return; }
  if (in.btnLeft || in.btnRight) { turnCarouselPage(in.btnLeft ? -1 : +1); return; }
  // Power -> sleep now (a no-op if Settings -> Developer -> Disable standby
  // is on, in which case this must not fall through below).
  if (in.btnPower) { standbySleepNow(); return; }
  if (in.tap && handlePageTap(in)) return;

  if (syncCarouselWithNetwork()) return;
  pollShowingPage();
  if (!in.tap) settleShowingPage(/*draw=*/!repaintLiveChange());
  if (maybeShowLowBattery()) return;
  if (idleTimedOut()) standbySleepNow();  // frontlight off, moon, deep sleep
}

// ===========================================================================
// Every other stage
// ===========================================================================

// No Home Assistant: any key retries (back to the carousel + a fresh fetch),
// Home opens Settings, hold Home opens the self-test. Idle sleeps and
// retries in 30 min.
static void tickNoHA(const InFrame& in) {
  if (in.homeLong) { screen_debug::enter(); return; }
  if (in.homeTap) { screen_settings::enter(); return; }  // "open configuration"
  if (in.btnLeft || in.btnRight || in.btnPower || in.tap || in.touchPress) {
    // RETRY: back to the carousel, re-run the fetch
    carouselPage = 0;
    kickWeatherRefresh();
    enterStandby();
    return;
  }
  // A finished retry that still can't reach HA keeps us here; a good one
  // returns to the carousel.
  {
    static bool prevBusy = false;
    const bool busy = g_weatherBusy;
    if (prevBusy && !busy) {
      if (wifilink::isUp() && globalsclient::ok) { carouselPage = 0; enterStandby(); }
      else screen_no_ha::draw(/*sleeping=*/false);
    }
    prevBusy = busy;
  }
  if (idleTimedOut()) noHASleepNow();  // noreturn — sleep, retry in 30 min
}

// No room config: same shape as No-HA, but Home goes straight to the room
// picker.
static void tickNoRoom(const InFrame& in) {
  if (in.homeLong) { screen_debug::enter(); return; }
  if (in.homeTap)  { screen_room_pick::enter(); return; }  // "open Settings" -> pick a room
  if (in.btnLeft || in.btnRight || in.btnPower || in.tap || in.touchPress) {
    carouselPage = 0;
    kickWeatherRefresh();
    enterStandby();
    return;
  }
  {
    static bool prevBusy = false;
    const bool busy = g_weatherBusy;
    if (prevBusy && !busy) {
      if (wifilink::isUp() && globalsclient::ok && deviceconfig::ok) {
        carouselPage = 0;
        enterStandby();
      } else if (wifilink::isUp() && !globalsclient::ok) {
        screen_no_ha::enter();
      } else {
        screen_no_room::draw(/*sleeping=*/false);
      }
    }
    prevBusy = busy;
  }
  if (idleTimedOut()) noHASleepNow();  // noreturn — sleep, retry in 30 min
}

// Charge screen: any key dismisses it; plugging in dismisses it too. Idle
// sleeps WITHOUT redrawing, so the charge screen stays on the panel.
static void tickLowBattery(const InFrame& in) {
  if (in.btnLeft || in.btnRight || in.btnPower || in.homeTap || in.homeLong || in.tap) {
    carouselPage = 0;
    enterStandby();
    return;
  }
  // Recovered while we were parked here (plugged in) -> back to the carousel.
  if (g_battPct > kLowBatteryPct + 1) {
    carouselPage = 0;
    enterStandby();
    return;
  }
  // Otherwise keep the charge screen on the panel and deep-sleep to save
  // power — never redraw over it here.
  if (idleTimedOut()) screen_low_battery::sleepNow();  // noreturn
}

// Debug page-through of every error screen (Settings -> Developer -> Error states).
static void tickErrPreview(const InFrame& in) {
  if (in.btnLeft) {
    screen_err_preview::prev();
  } else if (in.btnRight) {
    screen_err_preview::next();
  } else if (in.homeTap || in.homeLong || in.btnPower || in.tap) {
    carouselPage = 0;
    enterStandby();
  }
}

// Settings list: Left/Right move the cursor, Power or a tap activates a row.
static void tickSettings(const InFrame& in) {
  if (in.homeTap) { carouselPage = 0; enterStandby(); return; }
  if (in.btnLeft)  { screen_settings::sel = (screen_settings::sel + screen_settings::kCount - 1) % screen_settings::kCount; screen_settings::pressed = -1; screen_settings::draw(); return; }
  if (in.btnRight) { screen_settings::sel = (screen_settings::sel + 1) % screen_settings::kCount; screen_settings::pressed = -1; screen_settings::draw(); return; }
  // Flash the row black for one frame before acting on it, same as a
  // touch tap below — Power alone (no prior highlight change) reads as a
  // press on the cursor row too.
  if (in.btnPower) {
    screen_settings::pressed = screen_settings::sel;
    screen_settings::draw();
    screen_settings::activate(screen_settings::sel);
    return;
  }
  if (in.tap) {
    const int i = screen_settings::hitTest(in.ty);
    if (i >= 0) {
      screen_settings::pressed = i;
      screen_settings::draw();
      screen_settings::activate(i);
    }
  }
}

// Device info: any key goes back to Settings.
static void tickSettingsInfo(const InFrame& in) {
  if (in.tap && screen_settings_info::updateButtonHit(in.tx, in.ty)) { screen_ota::enter(); return; }
  if (in.homeTap || in.homeLong || in.btnLeft || in.btnPower || in.tap) screen_settings::enter();
}

// Firmware update: INSTALL / NOT NOW (or BACK); Home or Left leaves (not
// mid-install: that blocks in screen_ota::install() until it restarts).
static void tickOta(const InFrame& in) {
  using screen_ota::Phase;
  if (in.homeTap || in.homeLong || in.btnLeft) { screen_settings_info::enter(); return; }
  const int hit = in.tap ? screen_ota::buttonHit(in.tx, in.ty) : (in.btnPower ? 0 : -1);
  if (hit < 0) return;
  if (screen_ota::phase == Phase::Offer && hit == 0) { screen_ota::install(/*unattended=*/false); return; }
  if (hit == 1 || screen_ota::phase != Phase::Offer) screen_settings_info::enter();
}

// Select room: Left/Right move, tap picks, Home/Power back to Settings.
static void tickRoomPick(const InFrame& in) {
  if (in.homeTap || in.btnPower) { screen_settings::enter(); return; }
  if (!roomlist::ok) { if (in.btnLeft || in.tap) screen_settings::enter(); return; }
  if (in.btnLeft)  { screen_room_pick::sel = (screen_room_pick::sel + roomlist::count - 1) % roomlist::count; screen_room_pick::draw(); return; }
  if (in.btnRight) { screen_room_pick::sel = (screen_room_pick::sel + 1) % roomlist::count; screen_room_pick::draw(); return; }
  if (in.tap) {
    const int16_t top = static_cast<int16_t>(kStatusBarH + 12 + kPad);
    const int i = in.ty >= top ? (in.ty - top) / 82 : -1;
    if (i >= 0 && i < roomlist::count) screen_room_pick::pick(i);
  }
}

// Developer toggles: Left/Right move, Power or a tap toggles; last row = Back.
static void tickDeveloper(const InFrame& in) {
  if (in.homeTap) { screen_settings::enter(); return; }
  if (in.btnLeft)  { screen_developer::sel = (screen_developer::sel + screen_developer::kCount - 1) % screen_developer::kCount; screen_developer::pressed = -1; screen_developer::draw(); return; }
  if (in.btnRight) { screen_developer::sel = (screen_developer::sel + 1) % screen_developer::kCount; screen_developer::pressed = -1; screen_developer::draw(); return; }
  if (in.btnPower) {
    screen_developer::pressed = screen_developer::sel;
    screen_developer::draw();
    if (screen_developer::sel == screen_developer::kCount - 1) screen_settings::enter();
    else screen_developer::activate(screen_developer::sel);
    return;
  }
  if (in.tap) {
    const int i = screen_developer::hitTest(in.ty);
    if (i >= 0 && i < screen_developer::kCount - 1) {
      screen_developer::pressed = i;
      screen_developer::draw();
      screen_developer::activate(i);
    } else if (i == screen_developer::kCount - 1) {
      screen_settings::enter();
    }
  }
}

// Timeouts: Left/Right move, Power or a tap cycles a value; last row = Back.
static void tickTimeouts(const InFrame& in) {
  if (in.homeTap) { screen_settings::enter(); return; }
  if (in.btnLeft)  { screen_timeouts::sel = (screen_timeouts::sel + screen_timeouts::kCount - 1) % screen_timeouts::kCount; screen_timeouts::pressed = -1; screen_timeouts::draw(); return; }
  if (in.btnRight) { screen_timeouts::sel = (screen_timeouts::sel + 1) % screen_timeouts::kCount; screen_timeouts::pressed = -1; screen_timeouts::draw(); return; }
  if (in.btnPower) {
    screen_timeouts::pressed = screen_timeouts::sel;
    screen_timeouts::draw();
    if (screen_timeouts::sel < screen_timeouts::kBack) screen_timeouts::cycle(screen_timeouts::sel);
    else screen_settings::enter();
    return;
  }
  if (in.tap) {
    const int i = screen_timeouts::hitTest(in.ty);
    if (i >= 0 && i < screen_timeouts::kBack) {
      screen_timeouts::pressed = i;
      screen_timeouts::draw();
      screen_timeouts::cycle(i);
    } else if (i == screen_timeouts::kBack) {
      screen_settings::enter();
    }
  }
}

// Power menu (hold Power 10 s, from any stage). Cancel — or the auto-cancel
// timeout — goes back to the carousel on the page it was on: the menu can
// open over any stage, and the carousel is the one screen every stage can
// safely return to.
static void tickPowerMenu(const InFrame& in) {
  int act = -1;
  if (in.btnLeft || in.btnRight) {
    const int d = in.btnLeft ? screen_power::kCount - 1 : 1;
    screen_power::sel = (screen_power::sel + d) % screen_power::kCount;
    screen_power::openedMs = millis();
    screen_power::draw();
    return;
  }
  if (in.btnPower) act = screen_power::sel;
  else if (in.tap) act = screen_power::hitTest(in.ty);
  else if (in.homeTap) act = screen_power::kCancel;
  if (act < 0) {
    if (millis() - screen_power::openedMs > screen_power::kAutoCancelMs) enterStandby();
    return;
  }
  screen_power::pressed = act;
  screen_power::draw();
  if (act == screen_power::kRestart) restartDevice();    // noreturn
  if (act == screen_power::kShutdown) shutdown();        // noreturn
  enterStandby();
}

// A Wi-Fi network's join QR code: any key back to the Wifi carousel page.
static void tickWifiQr(const InFrame& in) {
  if (in.homeTap || in.homeLong || in.btnLeft || in.btnPower || in.tap) {
    carouselPage = kPageWifi;
    stage = Stage::Standby;
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ScreenSwitch));
  }
}

// Hardware self-test: every button/touch is logged on screen; a second
// Power press leaves it.
static void tickDebug(const InFrame& in) {
  bool dirty = false;

  if (in.btnLeft  && !screen_debug::btnSeen[screen_debug::TB_LEFT])  { screen_debug::btnSeen[screen_debug::TB_LEFT]  = true; dirty = true; }
  if (in.btnRight && !screen_debug::btnSeen[screen_debug::TB_RIGHT]) { screen_debug::btnSeen[screen_debug::TB_RIGHT] = true; dirty = true; }
  if (in.btnPower && !screen_debug::btnSeen[screen_debug::TB_POWER]) { screen_debug::btnSeen[screen_debug::TB_POWER] = true; dirty = true; }
  if (in.homeTap) {
    if (!screen_debug::btnSeen[screen_debug::TB_HOME]) { screen_debug::btnSeen[screen_debug::TB_HOME] = true; dirty = true; }
    screen_debug::stepBacklight();  // the Home key also steps the backlight
    dirty = true;
  }

  // --- touch: track position + count, redraw the crosshair -----------
  if (in.touchHeld) {
    screen_debug::lastTouchX = in.hx;
    screen_debug::lastTouchY = in.hy;
    dirty = true;
  }
  bool forceFull = false;  // set by the "Full refresh" button
  if (in.tap) {
    screen_debug::lastTouchX = in.tx;
    screen_debug::lastTouchY = in.ty;
    screen_debug::touchCount++;
    if (screen_debug::tapBacklight(in.tx, in.ty)) screen_debug::stepBacklight();
    if (screen_debug::tapWarmth(in.tx, in.ty)) screen_debug::stepWarmth();
    if (screen_debug::tapFullRefresh(in.tx, in.ty)) forceFull = true;
    dirty = true;
  }

  if (in.homeLong) {  // hold Home -> restart the self-test
    screen_debug::enter();
    return;
  }
  if (in.btnPower && screen_debug::btnSeen[screen_debug::TB_POWER]) {  // 2nd Power press -> leave the self-test
    carouselPage = 0;
    enterStandby();
    return;
  }

  if (dirty) {
    // Partial (fast) refresh keeps feedback snappy, but ghosting builds up.
    // Promote to a full refresh when the user asks (button) or once enough
    // partials have accumulated that the panel would start to look dirty.
    const bool autoFull = screen_debug::partialsSinceFull >= 40;
    screen_debug::draw(/*full=*/forceFull || autoFull);
  }
}

// ===========================================================================
// Radio
// ===========================================================================

// Wi-Fi off once nothing has needed it for Settings -> Timeouts -> Wi-Fi
// timeout: no input, and no network work (commands, re-reads, a refresh, a
// poll). The default, "Same as screen", never powers it down here — the
// radio goes off with the screen when the device sleeps. The next press
// posts a command, which rejoins fast, straight to the remembered AP
// (app/wifi_link.h); the status bar shows the true state throughout.
static void powerDownIdleRadio() {
  if (localsettings::wifiIdleOffMin == 0) return;  // same as the screen timeout
  if (wifilink::state != wifilink::State::Up || net::busy()) return;
  const uint32_t idleOffMs = static_cast<uint32_t>(localsettings::wifiIdleOffMin) * 60000u;
  const uint32_t now = millis();
  if (now - standbyIdleSinceMs < idleOffMs) return;
  if (now - net::g_lastActivityMs < idleOffMs) return;
  Serial.println("[wifi] idle -> radio off");
  wifilink::idleOff();
}

// ===========================================================================
// Dispatch
// ===========================================================================

// Stages with no idle handling of their own. The carousel sleeps from its
// own tick; No-HA / No-room / the charge screen sleep in their own way; the
// power menu auto-cancels back to the carousel.
static bool stageUsesGenericIdle(Stage s) {
  switch (s) {
    case Stage::Standby:
    case Stage::NoHA:
    case Stage::NoRoom:
    case Stage::LowBattery:
    case Stage::PowerMenu:
      return false;
    default:
      return true;
  }
}

static void tickStage(const InFrame& in) {
  if (in.any()) standbyIdleSinceMs = millis();

  // Power held 10 s -> the power menu, from any stage.
  if (in.powerMenu && stage != Stage::PowerMenu) {
    screen_power::enter();
    return;
  }

  if (stageUsesGenericIdle(stage) && idleTimedOut()) {
    sleepFromIdleScreen();
    return;
  }

  switch (stage) {
    case Stage::Standby:      tickCarousel(in); break;
    case Stage::NoHA:         tickNoHA(in); break;
    case Stage::NoRoom:       tickNoRoom(in); break;
    case Stage::LowBattery:   tickLowBattery(in); break;
    case Stage::ErrPreview:   tickErrPreview(in); break;
    case Stage::Settings:     tickSettings(in); break;
    case Stage::SettingsInfo: tickSettingsInfo(in); break;
    case Stage::Ota:          tickOta(in); break;
    case Stage::RoomPick:     tickRoomPick(in); break;
    case Stage::Developer:    tickDeveloper(in); break;
    case Stage::Timeouts:     tickTimeouts(in); break;
    case Stage::PowerMenu:    tickPowerMenu(in); break;
    case Stage::WifiQr:       tickWifiQr(in); break;
    case Stage::Debug:        tickDebug(in); break;
    default: break;
  }
}
