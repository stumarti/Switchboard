#pragma once

// ===========================================================================
// boot — setup(): work out WHY the chip is running, then take one of four
// paths.
//
//   Path             When                                       Shows
//   ───────────────  ─────────────────────────────────────────  ──────────────────────
//   LowBatteryCheck  timer wake, slept on the charge screen      nothing (gauge read only)
//   TimerRefresh     timer wake, slept on carousel/error screen  moon frame, only if stale
//   Resume           button wake, OR any cold boot/reset of a    cached carousel at once,
//                    set-up device with a cache on SD            Wi-Fi + refresh in the
//                                                                background ("updating" glyph)
//   FirstRun         not set up yet (no saved Wi-Fi / not        splash -> Wi-Fi setup ->
//                    paired), or a cold boot with no cache       pairing -> carousel
//
// The splash therefore only ever appears on a device that genuinely needs
// setting up — never on a wake. The deciding inputs are the wake cause and
// the RTC record's sleep reason (rtc_state.h), which sleepFor() stamps on
// every sleep.
// ===========================================================================

#include <Arduino.h>
#include <esp_sleep.h>
#include <BoardConfig.h>
#include <XteinkDetect.h>

#include "config.h"
#include "device_config_client.h"
#include "globals_client.h"
#include "local_settings.h"
#include "mdi_icon.h"
#include "pairing_client.h"
#include "persist.h"
#include "theme_client.h"
#include "screen_common.h"
#include "screen_error.h"
#include "screen_pairing.h"
#include "screen_splash.h"
#include "screen_wifi.h"
#include "app/rtc_state.h"
#include "app/input.h"
#include "app/frontlight.h"
#include "app/wifi_link.h"
#include "app/carousel.h"
#include "app/data_refresh.h"
#include "app/power.h"

namespace boot {

using rtcstate::SleepReason;

// First-run setup gives up (and sleeps, any button resumes it) after this
// long with nothing happening: Wi-Fi setup after the screen timeout with no
// input, pairing after this long without a button press.
inline constexpr uint32_t kPairingTimeoutMs = 10u * 60u * 1000u;
inline constexpr uint32_t kTimerWakeJoinMs = 12000;

struct Wake {
  esp_sleep_wakeup_cause_t cause;
  bool fromSleep;  // a genuine deep-sleep wake (timer or button)
  bool byTimer;
  SleepReason reason;  // why we slept — None unless fromSleep
};

// A genuine deep-sleep wake reports TIMER (the refresh timer) or EXT1 (a
// physical button — PowerManager::armWakeOnPins arms ext1 on the S3). ANY
// other reset reason — the reset/EN pin, a power cycle, brownout, panic, or
// ESP.restart() — reports UNDEFINED. RTC memory survives an EN-pin reset
// exactly like it survives a real deep-sleep wake, so the saved reason alone
// can't tell those apart; a reset must never replay a timer path.
inline Wake readWake() {
  Wake w{};
  w.cause = esp_sleep_get_wakeup_cause();
  w.fromSleep = w.cause == ESP_SLEEP_WAKEUP_TIMER || w.cause == ESP_SLEEP_WAKEUP_EXT1;
  w.byTimer = w.cause == ESP_SLEEP_WAKEUP_TIMER;
  const SleepReason saved = rtcstate::takeReason();  // always consumed
  w.reason = w.fromSleep ? saved : SleepReason::None;
  return w;
}

// CRITICAL for responsiveness: cap the USB-CDC TX wait at 1 ms. The default
// is 100 ms per chunk with ~20 retries, so a single log line stalls the CPU
// up to ~2 s whenever a host is plugged but not draining (every bench boot /
// wake). With ENABLE_SERIAL_LOG the SDK logs freely through boot, so this is
// the difference between an instant wake and a multi-second one. (CrossPoint
// does the same — "a load-bearing 1".) Logs are dropped when nobody reads.
inline void initSerial(const Wake& w) {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(1);
  if (!w.fromSleep) delay(200);  // let USB-CDC enumerate for a bench boot only
  Serial.printf("\n[switchboard] boot  cause=%d fromSleep=%d reason=%d\n",
                static_cast<int>(w.cause), w.fromSleep, static_cast<int>(w.reason));
}

// CRITICAL — power the peripheral rails BEFORE touching the display. On the
// X4 Pro the panel (and SD slot) sit behind a master rail enable on GPIO1
// (power.latch0 in the profile). holdPowerRails() drives it HIGH; without it
// the panel rail is unpowered and NOTHING appears on screen. It also
// releases any gpio_hold latched by the previous sleep.
inline void initRails() {
  BoardConfig::holdPowerRails();
  // Insurance for the shared-bus case: an SD rail left latched off could
  // clamp the display SPI lines. No-op on the X4 Pro's native SDMMC wiring.
  BoardConfig::releaseSdRail();
}

// Detect the real panel controller BEFORE the display comes up — newer X4
// Pro units carry a UC8179 instead of the SSD1677, and skipping this leaves
// them blank (CrossPoint runs the same step) — then bring the panel up.
inline void initPanel() {
  freeink::applyXteinkDisplayController();
  delay(10);  // let the rails settle before the panel reset dance
  ui.begin();
  g_panelBegun = true;
}

// NVS settings, then the SD cache and theme — everything a paint needs, no
// network. Returns whether the cached carousel state loaded.
inline bool loadLocalState() {
  deviceconfig::loadSlug();  // the room this remote drives (Settings -> Select room)
  deviceconfig::loadSlugUserPicked();
  localsettings::load();     // on-device toggles/timeouts (Settings -> Developer/Timeouts)
  pairing::loadFromNvs();

  // TEMP DEBUG — the SD mount + read sits on the critical path to the first
  // wake paint, so time it until it's been checked against real hardware.
  const uint32_t t0 = millis();
  const bool sdOk = persist::begin();
  const uint32_t t1 = millis();
  const bool cacheOk = persist::load();
  const uint32_t t2 = millis();
  Serial.printf("[persist] SD mount=%lums (ok=%d)  cache load=%lums (ok=%d)\n",
                static_cast<unsigned long>(t1 - t0), sdOk, static_cast<unsigned long>(t2 - t1),
                cacheOk);

  // Any theme pack already on SD (a no-op, falling back to the compiled-in
  // look, with no card or no pack yet), then the per-item MDI icons for the
  // cached config — so the first paint already shows them.
  themeclient::loadAtBoot();
  mdiicon::resolveAll(nullptr, mdiicon::Fetch::CacheOnly);
  return cacheOk;
}

// At/below the charge screen's threshold (0 = the gauge couldn't be read).
inline bool batteryCritical() { return g_battPct >= 1 && g_battPct <= kLowBatteryPct; }
// Still low enough to stay parked on the charge screen: the same +1
// hysteresis the charge screen itself uses before letting go.
inline bool batteryStillLow() { return g_battPct >= 1 && g_battPct <= kLowBatteryPct + 1; }

// Read the fuel gauge, retrying a couple of times — it shares the I2C bus
// with touch and a read can collide. g_battPct stays 0 if every try fails.
inline void readBattery() {
  for (int i = 0; i < 3 && g_battPct == 0; ++i) {
    pollBattery(/*force=*/true);
    if (g_battPct == 0) delay(20);
  }
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// Timer wake from the charge screen: read the gauge and nothing else — no
// panel, no Wi-Fi — and go straight back to sleep while it's still low. The
// charge screen stays on the panel untouched. Returns only if the battery
// has recovered (or can't be read), for the caller to do a normal timer
// refresh instead.
inline void runLowBatteryCheck() {
  frontlightBegin();  // initialized (so its pins are driven), but kept off
  restoreFrontlightLevelsFromRtc();  // keep the saved lamp levels through this sleep
  frontlightOff();
  readBattery();
  Serial.printf("[boot] low-battery check: %u%%\n", g_battPct);
  if (batteryStillLow()) sleepFor(SleepReason::LowBattery, screen_low_battery::kRecheckSec);
}

// Unattended timer wake: reconnect quietly, re-pull, and sleep again. No one
// is looking, so the frontlight stays off, and the panel is only refreshed
// when what it shows is actually stale.
[[noreturn]] inline void runTimerRefresh(const Wake& w) {
  stage = Stage::Standby;
  input.begin();
  frontlightBegin();  // initialized (so its pins are driven), but kept off
  restoreFrontlightLevelsFromRtc();
  frontlightOff();
  readBattery();
  if (batteryCritical()) {
    // Went critically low while asleep: park on the charge screen rather
    // than spending a Wi-Fi join on every refresh from here on.
    screen_low_battery::enter();
    screen_low_battery::sleepNow();
  }

  const bool wifi = wifilink::joinBlocking(kTimerWakeJoinMs);
  if (wifi) {
    ensureMdns();
    refreshStandby();
  } else if (w.reason == SleepReason::ErrorScreen) {
    // Still offline — leave the error screen up and retry later.
    sleepFor(SleepReason::ErrorScreen, kErrorRetrySec);
  }

  if (wifi && !globalsclient::ok) {  // server/HA still unreachable
    screen_no_ha::draw(/*sleeping=*/true);
    sleepFor(SleepReason::ErrorScreen, kErrorRetrySec);
  }
  if (wifi && !deviceconfig::ok) {  // server up, but no config for this room
    screen_no_room::draw(/*sleeping=*/true);
    sleepFor(SleepReason::ErrorScreen, kErrorRetrySec);
  }

  // Nobody's interacting — revert to the status page. Skip the refresh when
  // nothing new was fetched and the panel already shows the sleeping status
  // page (an offline timer wake would otherwise flash for nothing).
  const bool panelCurrent =
      !wifi && w.reason == SleepReason::Carousel && rtcstate::g.carouselPage == 0;
  carouselPage = 0;
  if (!panelCurrent) drawStandby(/*sleeping=*/true);
  sleepFor(SleepReason::Carousel,
           static_cast<uint32_t>(deviceconfig::refreshIntervalMin) * 60u);
}

// Button wake, or a cold boot of a set-up device with a cache: paint the
// cached page straight away with the lamp on, then join Wi-Fi and refresh in
// the background — the status bar's "updating" glyph shows until the fresh
// data lands (or the join gives up). stages.h's syncCarouselWithNetwork()
// picks it up from there. Nothing here blocks on the network.
inline void runResume(const Wake& w) {
  // Our first paint is a self-contained Full refresh, so the driver needn't
  // force its own resync first. Only valid when the panel kept our last
  // frame through sleep.
  if (w.fromSleep) ui.skipInitialResync();
  input.begin();
  frontlightBegin();
  restoreFrontlightLevelsFromRtc();
  readBattery();  // one read on the still-quiet I2C bus, before the input task

  if (batteryCritical()) {
    // Straight to the charge screen — no carousel paint, no lamp, no Wi-Fi
    // until it's dismissed (enterStandby() starts the join then).
    screen_low_battery::enter();
    startInputTask();
    startNetwork();
    return;
  }

  stage = Stage::Standby;
  // A button wake returns to the page it slept on; anything else (an error
  // screen's retry, a cold boot) starts on the status page.
  carouselPage = w.reason == SleepReason::Carousel && rtcstate::g.carouselPage < kCarouselPages
                     ? rtcstate::g.carouselPage
                     : 0;
  ensureCarouselPageEnabled();  // unless a config change hid it meanwhile

  frontlightSetOn(true);  // light on before the paint
  // One paint, straight from the cached data, with the "updating" glyph.
  // Drawn BEFORE Wi-Fi or the refresh task touch anything, so there's no
  // race with a fetch resetting the haclient structs mid-paint. If the cache
  // isn't good (never fetched, or no SD card), every page's own
  // "<X> unavailable" placeholder shows through until the refresh lands.
  g_wakeUpdating = true;
  drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::WakeRepaint));
  standbyIdleSinceMs = millis();

  startInputTask();
  startNetwork();
  wifilink::beginJoin();  // non-blocking; the refresh kicks when it lands
  standbyPrevBusy = false;
  standbyPrevWifi = false;
}

// Not set up yet: splash, then the blocking Wi-Fi and pairing setup screens
// (each gives up and sleeps if left alone — any button resumes setup), then
// the carousel.
[[noreturn]] inline void sleepSetupPaused(const char* why) {
  frontlightOff();
  screen_splash::drawPaused(why);
  sleepFor(SleepReason::None, /*timerSec=*/0);
}

inline void runFirstRun() {
  input.begin();
  frontlightBegin();
  frontlightOff();  // setup runs dark; the carousel lights it
  readBattery();

  stage = Stage::Splash;
  screen_splash::draw();
  screen_splash::holdFor(1200);

  stage = Stage::Wifi;
  if (!screen_wifi::run(idleToSleepMs())) sleepSetupPaused("no Wi-Fi network was picked");

  // The "waiting for approval" gate for a never-paired (or revoked) device —
  // a no-op if NVS already has a valid token. Must run before anything below
  // talks to the server: every server call needs the device's bearer token.
  wifilink::beginJoin();  // provisioning already joined — this just marks the link Up
  if (!screen_pairing::run(kPairingTimeoutMs)) sleepSetupPaused("still waiting for approval");

  // Data loads in the background, on the network worker — never block on
  // the HA calls.
  startNetwork();
  kickWeatherRefresh();
  startInputTask();  // hand input off to its own task from here on

  // Boot straight into the carousel (the status page) with the lamp lit; it
  // self-sleeps after idleToSleepMs() of no input.
  carouselPage = 0;
  enterStandby();
}

// ---------------------------------------------------------------------------

enum class Path : uint8_t { LowBatteryCheck, TimerRefresh, Resume, FirstRun };

inline const char* pathName(Path p) {
  switch (p) {
    case Path::LowBatteryCheck: return "low-battery check";
    case Path::TimerRefresh:    return "timer refresh";
    case Path::Resume:          return "resume";
    case Path::FirstRun:        return "first run";
  }
  return "?";
}

inline bool isResumableSleep(SleepReason r) {
  return r == SleepReason::Carousel || r == SleepReason::ErrorScreen ||
         r == SleepReason::LowBattery;
}

// The timer paths are decided first, before anything touches the radio.
inline Path choosePath(const Wake& w, bool cacheOk) {
  if (w.byTimer && w.reason == SleepReason::LowBattery) return Path::LowBatteryCheck;
  if (w.byTimer && isResumableSleep(w.reason)) return Path::TimerRefresh;

  // A button wake from a resumable sleep was set up when it slept — skip
  // the saved-credentials check, which would bring the radio up before the
  // first paint.
  if (w.fromSleep && isResumableSleep(w.reason) && pairing::paired) return Path::Resume;

  const bool setUp = pairing::paired && wifilink::hasSavedCredentials();
  if (!setUp) return Path::FirstRun;
  // A cold boot, reset or power-on from Shut down resumes only if a cache
  // loaded — otherwise there's nothing to paint, and the splash is the
  // better feedback while Wi-Fi joins. (A genuine wake, above, resumes even
  // with no SD cache: the pages show placeholders until the refresh lands.)
  return cacheOk ? Path::Resume : Path::FirstRun;
}

inline void run() {
  const Wake w = readWake();
  initSerial(w);
  initRails();

  // Cheapest path first: it never brings the panel or the SD card up.
  if (w.byTimer && w.reason == SleepReason::LowBattery) {
    runLowBatteryCheck();  // returns only if the battery recovered
  }

  initPanel();
  const bool cacheOk = loadLocalState();
  Path path = choosePath(w, cacheOk);
  if (path == Path::LowBatteryCheck) path = Path::TimerRefresh;  // recovered: normal refresh
  Serial.printf("[boot] path: %s\n", pathName(path));

  switch (path) {
    case Path::TimerRefresh:
      runTimerRefresh(w);  // noreturn
    case Path::Resume:
      runResume(w);
      break;
    case Path::FirstRun:
    case Path::LowBatteryCheck:
      runFirstRun();
      break;
  }
}

}  // namespace boot
