#pragma once

// ===========================================================================
// The carousel — paging through the resting screens. Page 0 is the
// weather/status page (header = room name); pages 1/2/6 are the Lighting/
// Blinds/Climate control screens; the rest are scaffolds ("coming soon").
// The carousel stays awake and interactive for idleToSleepMs() (Settings ->
// Timeouts -> Screen timeout); then it powers the frontlight down, stamps a
// moon on the status bar, and deep-sleeps until the refresh timer or a
// physical button.
// ===========================================================================

#include <Arduino.h>

#include "screen_common.h"
#include "screen_fwd.h"
#include "refresh_policy.h"
#include "device_config_client.h"
#include "local_settings.h"
#include "app/frontlight.h"
#include "app/rtc_state.h"
#include "app/wifi_link.h"
#include "screen_shade.h"
#include "screen_status.h"
#include "screen_climate.h"
#include "screen_lighting.h"
#include "screen_blinds.h"
#include "screen_music.h"
#include "screen_tv.h"
#include "screen_xbox.h"
#include "screen_wifi_networks.h"

static constexpr uint8_t kCarouselPages = 8;
// Climate last (not Wifi) so it's one Left-press away from Status, wrapping
// around the end of the carousel — the page swapped in most often, per request.
static const char* const kCarouselNames[kCarouselPages] = {
    "Status", "Lighting", "Blinds", "Music", "TV", "Xbox", "Wifi", "Climate"};
static constexpr uint8_t kPageLighting = 1;
static constexpr uint8_t kPageBlinds   = 2;
static constexpr uint8_t kPageMusic    = 3;
static constexpr uint8_t kPageTv       = 4;
static constexpr uint8_t kPageXbox     = 5;
static constexpr uint8_t kPageWifi     = 6;
static constexpr uint8_t kPageClimate  = 7;

// Per-room config (deviceconfig::screens.*) can hide any page but Status
// (page 0, which has no toggle — it's always shown).
static bool pageEnabled(uint8_t page) {
  switch (page) {
    case kPageLighting: return deviceconfig::screenLighting;
    case kPageBlinds:   return deviceconfig::screenBlinds;
    case kPageMusic:    return deviceconfig::screenMusic;
    case kPageTv:       return deviceconfig::screenTv;
    case kPageXbox:     return deviceconfig::screenXbox;
    case kPageClimate:  return deviceconfig::screenClimate;
    case kPageWifi:     return deviceconfig::screenWifi;
    default:            return true;  // Status, and any future page with no toggle
  }
}
// If the current page just got hidden (a config refresh disabled it while
// the carousel was sitting on it), step forward to the nearest enabled one.
// Status is always enabled, so this can never spin forever.
static void ensureCarouselPageEnabled() {
  for (uint8_t tries = 0; tries < kCarouselPages && !pageEnabled(carouselPage); ++tries)
    carouselPage = static_cast<uint8_t>((carouselPage + 1) % kCarouselPages);
}

// Was a hardcoded 120000 (2 min); now Settings -> Timeouts -> Screen timeout
// (localsettings::idleToSleepMin, persisted in NVS).
static inline uint32_t idleToSleepMs() {
  return static_cast<uint32_t>(localsettings::idleToSleepMin) * 60000u;
}

// The carousel page the device fell asleep on is saved in the RTC record
// (rtcstate::g.carouselPage). A button wake restores it; a timer wake
// (nobody's there) reverts to the status page — and when we sleep on a
// non-status page we arm a wake (Settings -> Timeouts -> Control page
// timeout, localsettings::controlPageRevertMin) just to make that revert happen.
static inline uint32_t revertToStatusSec() {
  return static_cast<uint32_t>(localsettings::controlPageRevertMin) * 60u;
}

// The battery percentage at/below which the charge screen takes over.
static constexpr uint8_t kLowBatteryPct = 5;

static const freeink::Icon* carouselIcon(uint8_t page) {
  switch (page) {
    case 0: return &icons::get("nav_status");
    case 1: return &icons::get("nav_lighting");
    case 2: return &icons::get("nav_blinds");
    case 3: return &icons::get("nav_music");
    case 4: return &icons::get("nav_tv");
    case 5: return &icons::get("nav_xbox");
    case 6: return &icons::get("nav_wifi");
    case 7: return &icons::get("nav_climate");
    default: return nullptr;
  }
}

// A row of position dots along the very bottom — the carousel affordance.
// Only enabled pages get a dot, so a hidden page doesn't leave a "gap" dot
// nobody can land on.
static void drawCarouselDots() {
  uint8_t visible = 0;
  for (uint8_t i = 0; i < kCarouselPages; ++i) if (pageEnabled(i)) ++visible;
  if (visible == 0) return;

  const int16_t sp = 20, rad = 4, d = 8;
  const int16_t total = static_cast<int16_t>((visible - 1) * sp);
  int16_t x = static_cast<int16_t>(Ui::W / 2 - total / 2);
  const int16_t y = static_cast<int16_t>(Ui::H - 20);
  for (uint8_t i = 0; i < kCarouselPages; ++i) {
    if (!pageEnabled(i)) continue;
    if (i == carouselPage)
      ui.fillRect(static_cast<int16_t>(x - rad), static_cast<int16_t>(y - rad), d, d, Color::Black,
                  rad);
    else
      ui.strokeRect(static_cast<int16_t>(x - rad), static_cast<int16_t>(y - rad), d, d, 1, rad);
    x = static_cast<int16_t>(x + sp);
  }
}

// The Home-tap overlay (jump list / Quick Access hub, app/quick_access.h) —
// declared here because entering the carousel always closes it.
static bool jumpOpen = false;

// Set while a post-wake reconnect + refresh is in flight, so the status bar
// carries a small "updating" glyph over the still-cached page instead of a
// blocking modal card — cleared in the SAME repaint that shows the fresh
// data (the g_weatherBusy busy->idle edge, see syncCarouselWithNetwork()),
// so a wake costs one repaint at wake + one when the refresh lands.
static bool g_wakeUpdating = false;

// Just the carousel pixels (status bar + page body + dots), no commit — split
// out so the wake path can paint the real page underneath and stack a popup
// on top before a single commitFrame(). `sleeping` only affects the moon
// glyph here; drawStandby() below also uses it to force Rf::Full.
static void drawStandbyContent(bool sleeping, int pressed) {
  ui.clear();
  const bool statusPage = carouselPage == 0;
  drawStatusBar(statusPage ? (deviceconfig::name[0] ? deviceconfig::name : deviceconfig::activeSlug)
                           : kCarouselNames[carouselPage],
                /*showMoon=*/sleeping, carouselIcon(carouselPage), g_wakeUpdating);

  if (carouselPage == 0) {
    screen_status::draw();
  } else if (carouselPage == kPageClimate) {
    screen_climate::draw(pressed);
  } else if (carouselPage == kPageLighting) {
    screen_lighting::draw(pressed);
  } else if (carouselPage == kPageBlinds) {
    screen_blinds::draw(pressed);
  } else if (carouselPage == kPageMusic) {
    screen_music::draw(pressed);
  } else if (carouselPage == kPageTv) {
    screen_tv::draw(pressed);
  } else if (carouselPage == kPageXbox) {
    screen_xbox::draw(pressed);
  } else if (carouselPage == kPageWifi) {
    screen_wifi_networks::draw();
  } else {
    ui.text(kCarouselNames[carouselPage], 0, static_cast<int16_t>(Ui::H / 2 - 60), Ui::W, 64,
            TextAlign::Center, Color::Black);
    ui.text("coming soon", 0, static_cast<int16_t>(Ui::H / 2 + 16), Ui::W, 26, TextAlign::Center,
            Color::DarkGray, 1, Ui::kFontSmall);
  }

  drawCarouselDots();
}


// The carousel. `sleeping` adds the moon + forces a clean frame for deep sleep.
// `pressed` inverts one action-bar button (Climate/Lighting) for tap feedback.
// (Default arguments are on the declaration in screen_fwd.h.)
static void drawStandby(bool sleeping, Rf r, int pressed) {
  if (sleeping) r = Rf::Full;
  drawStandbyContent(sleeping, pressed);
  commitFrame(r);
}


// Wi-Fi / weather state the Standby loop watched at its last repaint, so it
// only repaints on an actual change (seeded on entry so entry doesn't count).
static bool standbyPrevBusy = false;
static bool standbyPrevWifi = false;

// Enter the awake carousel. NOTHING here blocks: draw once from the
// last-known (cached) data, light the frontlight, start the idle clock, make
// sure Wi-Fi is (re)joining, and kick an async weather refresh if it's up.
// syncCarouselWithNetwork() repaints when the fetch lands or Wi-Fi changes.
static void enterStandby() {
  stage = Stage::Standby;
  screen_shade::open = false;
  jumpOpen = false;
  if (frontlight.present() && !frontlightIsOn()) frontlightSetOn(true);
  // The wake's "updating" glyph only means something while its join/refresh
  // is still pending — it may have finished while another screen was up.
  if (!g_weatherBusy && wifilink::state != wifilink::State::Joining) g_wakeUpdating = false;
  // Entering the carousel (from a sub-screen, room pick, or boot) is
  // navigation, not control feedback.
  drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ScreenSwitch));
  standbyIdleSinceMs = millis();
  wifilink::ensureStarted();
  standbyPrevBusy = g_weatherBusy;
  standbyPrevWifi = wifilink::isUp();
  if (wifilink::isUp() && !g_weatherBusy) kickWeatherRefresh();
}

// How long the carousel sleeps before its timer wake. On a control page we
// arm a wake (Settings -> Timeouts -> Control page timeout) so the device
// reverts to the status page while nobody's looking; on the status page we
// use the normal weather-refresh interval. 0 = only a button wakes it.
static uint32_t carouselSleepTimerSec() {
  const uint32_t refreshSec = static_cast<uint32_t>(deviceconfig::refreshIntervalMin) * 60u;
  if (carouselPage == 0) return refreshSec;
  return (refreshSec > 0 && refreshSec < revertToStatusSec()) ? refreshSec : revertToStatusSec();
}

// Idle timeout / Power on the carousel: let any in-flight network work land,
// kill the frontlight, stamp the moon, and deep-sleep (power::sleepFor()
// saves the page to the RTC record).
//
// Settings -> Developer -> Disable standby short-circuits this to a no-op (a
// bench-flashing aid — see localsettings::standbyDisabled) — the one reason
// this can't be [[noreturn]]. Every caller returns right after it, so a
// returning call is handled the same as a sleeping one.
static void standbySleepNow() {
  if (localsettings::standbyDisabled) {
    standbyIdleSinceMs = millis();  // don't re-trigger next loop() tick
    return;
  }
  // Lamp off first — instant feedback — then let any in-flight refresh land
  // BEFORE drawing the sleep frame, so the panel can't be left showing older
  // data than the SD cache.
  frontlightOff();
  waitForBackgroundIdle();
  drawStandby(/*sleeping=*/true);
  sleepFor(rtcstate::SleepReason::Carousel, carouselSleepTimerSec());  // noreturn
}

// Idle timeout on any non-carousel screen (Settings, Debug, a QR code, ...):
// fall back to the status page and sleep exactly like the carousel does, so
// no screen can keep the device awake until the battery is flat.
static void sleepFromIdleScreen() {
  if (localsettings::standbyDisabled) {  // bench aid: stay on this screen, awake
    standbyIdleSinceMs = millis();
    return;
  }
  carouselPage = 0;
  stage = Stage::Standby;
  screen_shade::open = false;
  jumpOpen = false;
  standbySleepNow();
}

// True once the current stage has seen no input for the screen timeout.
static bool idleTimedOut() { return millis() - standbyIdleSinceMs > idleToSleepMs(); }
