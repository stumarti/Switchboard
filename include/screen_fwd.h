#pragma once

// ===========================================================================
// screen_fwd — forward declarations for the handful of carousel / sleep-wake
// entry points the app/ layer owns but individual screen_*.h files need to
// call into (e.g. the control shade returning to the carousel, or an error
// screen sleeping itself). Actual definitions live in include/app/*.h,
// included by main.cpp textually AFTER every screen_*.h, hence the forward
// declarations.
//
// Everything else is resolved by plain include order in main.cpp: each
// screen_*.h is included only after the screen_*.h's it calls into, so no
// forward declaration is needed for e.g. screen_shade.h calling
// screen_settings::enter().
// ===========================================================================

#include "screen_common.h"  // Rf
#include "app/rtc_state.h"

// These keep the internal (file-static) linkage the rest of the firmware
// uses — there's only ever the one translation unit (main.cpp pulls in every
// header), so `static` just documents "not meant to cross a TU boundary"
// rather than actually being load-bearing.

// carousel (app/carousel.h)
// The default `r` is Full, not Fast/Clean: every real call site passes an
// explicit refreshModeFor(...) value, so the default only matters if a
// future call site forgets to — and a forgotten mode should fail safe to a
// clean full paint, never a silent flashing Half.
static void drawStandby(bool sleeping = false, Rf r = Rf::Full, int pressed = -1);
// A value stepper's tap feedback (app/carousel.h); returns the pressed id
// it drew (-1 if the new value alone showed the tap).
static int drawStepperFeedback(int pressedIfUnchanged);
static void enterStandby();

// shared weather/HA data fetch — drives every carousel page; kicked off
// whenever Wi-Fi lands or a screen wants a fresh read (app/data_refresh.h).
static void kickWeatherRefresh();

// mDNS resolve, guarded so racing background tasks don't tear it down under
// each other (app/data_refresh.h) — every screen's background action task calls this
// before talking to the Switchboard server.
static void ensureMdns();

// Sleep / restart (app/power.h) — the ONE way anything sleeps or restarts,
// so every path waits for in-flight network/SD work and stamps the RTC
// record's sleep reason the next boot routes on. Screens that sleep on their
// own schedule (No-HA's 30-minute retry, the charge screen) call these.
static void waitForBackgroundIdle();
[[noreturn]] static void sleepFor(rtcstate::SleepReason reason, uint32_t timerSec);
[[noreturn]] static void restartDevice();
