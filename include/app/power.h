#pragma once

// ===========================================================================
// power — every way the device sleeps or restarts, in one place:
//
//   sleepFor(reason, timerSec)  the ONLY deep-sleep entry point. Waits for
//                               in-flight network/SD work, stamps the RTC
//                               record's reason + page (boot.h routes the
//                               next wake on them), then sleeps.
//   shutdown()                  power menu -> Shut down (Power-only wake).
//   restartDevice()             Restart / Wi-Fi reset / hard reset.
//
// Before this, each screen called the raw deep-sleep function itself and
// set (or forgot to set) its own RTC flags — the low-battery sleep forgot,
// and every wake from it came back through the splash screen. Nothing also
// waited for the background refresh, so a Power click mid-refresh could
// unmount the SD card under a cache write.
// ===========================================================================

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <BoardConfig.h>
#include <PowerManager.h>

#include "screen_common.h"
#include "persist.h"
#include "app/rtc_state.h"
#include "app/frontlight.h"
#include "app/wifi_link.h"
#include "app/net.h"
#include "screen_power.h"

// Set by boot.h once ui.begin() has run. The low-battery timer check never
// brings the panel up (it only reads the gauge), so sleeping must not send
// the panel commands then.
static bool g_panelBegun = false;


// The physical buttons, all active-LOW: Left=GPIO0, Right=GPIO7, Power=GPIO3.
// (Home is a GT911 capacitive key — it can't wake the chip from deep sleep.)
static constexpr gpio_num_t kPowerPin = GPIO_NUM_3;
static const gpio_num_t kWakePins[] = {GPIO_NUM_0, GPIO_NUM_7, kPowerPin};

// Deep-sleep the chip, waking on any button in `wakeMask` (a full chip reset
// back through setup()) and optionally after `timerUs` microseconds (0 = none).
// Never returns. Only sleepFor() calls this — it arms every physical button,
// or Power alone for a shutdown.
//
// Mirrors CrossPoint's enterDeepSleep() + HalPowerManager::startDeepSleep():
// tear down Wi-Fi, panel deep-sleep command, arm ext1 wake, HOLD the master
// peripheral-rail latch (GPIO1) HIGH — PowerManager::deepSleep() runs
// esp_sleep_config_gpio_isolate() which otherwise lets the latch float, the
// rail drops on battery, and the next press cold-boots instead of fast-waking.
[[noreturn]] static void deepSleepOnPins(uint64_t wakeMask, uint64_t timerUs) {
  // 0. Remember the frontlight state — its rail is cut below, so the wake path
  //    has to put it back deliberately. Let any in-flight async refresh land
  //    first so the panel keeps a complete frame through sleep. Tear down
  //    USB-CDC (CrossPoint does the same) so the host sees a clean disconnect
  //    and the peripheral isn't holding a power domain across the GPIO wake.
  saveFrontlightToRtc();
  if (g_panelBegun) ui.syncDisplay();
  // Unmount the SD cache — on the X4 Pro's native SDMMC this floats the bus
  // pads so their pull-ups don't back-feed the card through sleep. Every
  // save() this session already landed, so no file is open at this point.
  persist::shutdown();
#ifdef ENABLE_SERIAL_LOG
  Serial.end();
#endif

  // 1. Drop Wi-Fi so the modem power domain isn't held through deep sleep.
  wifilink::off();

  // 2. Panel deep-sleep command, while its rail is still powered (skipped if
  //    this boot never brought the panel up — the low-battery gauge check).
  if (g_panelBegun) ui.display().deepSleep();

  // 3. Arm wake on the requested buttons (active-LOW) + the refresh timer.
  for (gpio_num_t p : kWakePins) pinMode(p, INPUT_PULLUP);
  freeink::PowerManager::armWakeOnPins(wakeMask, /*wakeLow=*/true);
  if (timerUs) esp_sleep_enable_timer_wakeup(timerUs);

  // 4. Hold GPIO1 (power.latch0) HIGH through deep sleep.
  const int8_t latch = BoardConfig::ACTIVE.power.latch0;
  if (latch >= 0) {
    const auto g = static_cast<gpio_num_t>(latch);
    gpio_hold_dis(g);
    pinMode(latch, OUTPUT);
    digitalWrite(latch, HIGH);
    gpio_hold_en(g);
  }

  // 5. Cut the gated touch / SD rails and hold them off.
  freeink::PowerManager::powerDownRailsForSleep();

  // 6. Wait for every wake button to be released (a held GPIO0 strap through
  //    the reset would drop the chip into download mode), then sleep.
  for (bool held = true; held;) {
    held = false;
    for (gpio_num_t p : kWakePins)
      if (digitalRead(p) == LOW) held = true;
    if (held) delay(20);
  }

  freeink::PowerManager::deepSleep();  // isolate + hold_en + esp_deep_sleep_start
  while (true) {                       // unreachable — satisfy [[noreturn]]
  }
}

// True while background work could still be touching the network or the SD
// card: anything queued or running on the network worker (every screen's
// commands, their re-reads, the shared refresh). A held-open live request
// (app/live.h) is not background work: sleep simply abandons it.
static bool anyBackgroundBusy() { return net::busy(); }

// Let in-flight background work land before sleeping or restarting. Bounded:
// a wedged HTTP call must not keep the device awake forever — every client
// call has its own few-second timeout, so this is only ever a backstop.
static void waitForBackgroundIdle() {
  constexpr uint32_t kMaxWaitMs = 10000;
  const uint32_t start = millis();
  while (anyBackgroundBusy() && millis() - start < kMaxWaitMs) delay(20);
}

static uint64_t allWakePinsMask() {
  uint64_t mask = 0;
  for (gpio_num_t p : kWakePins) mask |= 1ULL << p;
  return mask;
}

// THE deep-sleep entry point. `timerSec` = 0 means only a button wakes it.
// Shutdown arms Power alone (Left/Right can't turn it on by accident); every
// other reason arms all three buttons. Whatever frame is on the panel stays
// there through sleep, so callers draw their sleep frame first.
[[noreturn]] static void sleepFor(rtcstate::SleepReason reason, uint32_t timerSec) {
  waitForBackgroundIdle();
  // Keep what's on screen for the next wake — including anything the user
  // changed since the last refresh (a light toggled). Skipped if a refresh
  // is somehow still running: its half-updated state must not be cached.
  if (!anyBackgroundBusy()) persist::save();
  rtcstate::g.reason = reason;
  rtcstate::g.carouselPage = carouselPage;
  const uint64_t mask =
      reason == rtcstate::SleepReason::Shutdown ? (1ULL << kPowerPin) : allWakePinsMask();
  Serial.printf("[power] sleep reason=%d timer=%lus\n", static_cast<int>(reason),
                static_cast<unsigned long>(timerSec));
  deepSleepOnPins(mask, static_cast<uint64_t>(timerSec) * 1000000ULL);
}

// ===========================================================================
// Power menu (a 10s Power hold, from any stage) -> Restart / Shut down.
// ===========================================================================
// Shut down: the X4 Pro has no PMIC soft-off, so "off" is a deep sleep with no
// timer that only the Power button wakes. Its reason (Shutdown) routes the
// next Power press through a normal boot — straight to the cached carousel
// when there is one, the first-run setup otherwise. Never returns.
[[noreturn]] static void shutdown() {
  waitForBackgroundIdle();
  frontlightOff();
  screen_power::drawOff();
  sleepFor(rtcstate::SleepReason::Shutdown, /*timerSec=*/0);
}

// Restart: ESP.restart() reports a non-sleep reset reason, which boot.h
// always treats as a cold boot. Park the SD cache first, like a sleep does.
[[noreturn]] static void restartDevice() {
  waitForBackgroundIdle();
  frontlightOff();
  ui.clear();
  ui.centered("Restarting", 380, 40);
  commitFrame(Rf::Clean);
  ui.syncDisplay();
  if (!anyBackgroundBusy()) persist::save();
  persist::shutdown();
  ESP.restart();
  while (true) {  // unreachable — satisfy [[noreturn]]
  }
}
