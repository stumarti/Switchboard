#pragma once

// ===========================================================================
// rtc_state — everything that has to survive a deep sleep, in ONE record in
// RTC slow memory. Replaces the old scattered rtcStandbyActive / rtcNoHA /
// rtcLowBattery / rtcCarouselPage / rtcFl* flags, each of which one sleep
// path or another forgot to set (the low-battery sleep never set
// rtcStandbyActive, so every wake from it cold-booted through the splash).
//
// Every deep sleep goes through sleepFor() (app/power.h), which stamps
// `reason`; boot::run() (app/boot.h) reads it back (plus the wake cause) to pick the boot path.
// The magic XORs in sizeof(State), so a layout change after an OTA reads as
// "no state" rather than garbage.
// ===========================================================================

#include <Arduino.h>
#include <esp_attr.h>

namespace rtcstate {

// Why the device last went to sleep — decides where a wake resumes.
enum class SleepReason : uint8_t {
  None = 0,      // not asleep from anywhere resumable (setup paused, fresh RTC)
  Carousel,      // idle / Power on the carousel -> resume on `carouselPage`
  ErrorScreen,   // idle on No-HA / No-room -> a timer wake retries the fetch
  LowBattery,    // idle on the charge screen -> timer wakes only re-read the gauge
  Shutdown,      // power menu -> Shut down; only Power wakes it
};

struct State {
  uint32_t magic;
  SleepReason reason;
  uint8_t carouselPage;  // the page the carousel slept on
  // Frontlight as the user left it — its rail is cut for sleep, so a wake
  // has to put it back deliberately.
  bool flOn;
  uint8_t flBrightPct;
  uint8_t flWarmPct;
};

inline constexpr uint32_t kMagic = 0x53425254u ^ static_cast<uint32_t>(sizeof(State));  // 'SBRT'

RTC_DATA_ATTR inline State g = {};

inline bool valid() { return g.magic == kMagic; }

// Defaults for a first boot (or a stale/invalid record after an OTA).
inline void reset() {
  g = State{};
  g.magic = kMagic;
  g.reason = SleepReason::None;
  g.flBrightPct = 50;
  g.flWarmPct = 50;
}

// Called once, first thing in boot: make the record usable and hand back
// why we slept. The reason is cleared so a later reset (EN pin, crash)
// can't replay it — only the next sleepFor() sets it again.
inline SleepReason takeReason() {
  if (!valid()) reset();
  const SleepReason r = g.reason;
  g.reason = SleepReason::None;
  return r;
}

}  // namespace rtcstate
