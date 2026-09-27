#pragma once

// ===========================================================================
// frontlight — the lamp's live levels plus their round trip through the RTC
// record (rtc_state.h) across deep sleep: the rail is cut for sleep, so
// without this the lamp always resumes dark. The control shade
// (screen_shade.h) reads/writes flBrightPct/flWarmPct and calls the apply*/
// ctlStep* helpers below — which is why this is included before the screens.
// ===========================================================================

#include <FrontlightManager.h>

#include "screen_common.h"
#include "app/rtc_state.h"

static uint8_t flBrightPct = 50;
static uint8_t flWarmPct = 50;  // 0 = cool, 100 = warm

// True once frontlight.begin() has run this boot. The low-battery timer
// check never brings the lamp up, and must not overwrite the saved state
// with "off at 50%" on its way back to sleep.
static bool g_frontlightBegun = false;

static void frontlightBegin() {
  frontlight.begin();
  g_frontlightBegun = true;
}

// screen_shade.h isn't included until below (it needs flBrightPct/flWarmPct,
// defined here, already visible) — but ctlStepBrightness/ctlStepWarmth need
// to flag it dirty, so forward-declare just that one flag.
namespace screen_shade { extern bool dirty; }

static bool frontlightIsOn() { return frontlight.present() && frontlight.brightness() > 0; }

static void frontlightSetOn(bool on) {
  if (!frontlight.present()) return;
  if (on && flBrightPct == 0) flBrightPct = 50;  // was turned all the way down
  frontlight.setBrightness(on ? flBrightPct : 0);
  if (on && frontlight.hasColorTemperature()) frontlight.setColorTemperature(flWarmPct);
}

static void applyBrightness() {
  if (flBrightPct > 100) flBrightPct = 100;
  if (frontlight.present()) frontlight.setBrightness(flBrightPct);  // 0 == off
}
static void applyWarmth() {
  if (flWarmPct > 100) flWarmPct = 100;
  if (frontlight.present() && frontlight.hasColorTemperature() && flBrightPct > 0)
    frontlight.setColorTemperature(flWarmPct);
}
static void ctlStepBrightness(int d) {
  const int n = static_cast<int>(flBrightPct) + d;
  flBrightPct = static_cast<uint8_t>(n < 0 ? 0 : n > 100 ? 100 : n);
  applyBrightness();
  screen_shade::dirty = true;
}
static void ctlStepWarmth(int d) {
  const int n = static_cast<int>(flWarmPct) + d;
  flWarmPct = static_cast<uint8_t>(n < 0 ? 0 : n > 100 ? 100 : n);
  applyWarmth();
  screen_shade::dirty = true;
}

// Snapshot the live frontlight state into the RTC record. Call just before a
// deep sleep, while the rail is still up so brightness() reads true.
static void saveFrontlightToRtc() {
  if (!g_frontlightBegun) return;
  rtcstate::g.flOn        = frontlightIsOn();
  rtcstate::g.flBrightPct = flBrightPct;
  rtcstate::g.flWarmPct   = flWarmPct;
}

// Pull the saved brightness/warmth levels back into flBrightPct/flWarmPct,
// without touching the hardware.
static void restoreFrontlightLevelsFromRtc() {
  flBrightPct = rtcstate::g.flBrightPct > 100 ? 100 : rtcstate::g.flBrightPct;
  flWarmPct   = rtcstate::g.flWarmPct > 100 ? 100 : rtcstate::g.flWarmPct;
}

static void frontlightOff() {
  if (frontlight.present()) frontlight.off();
}
