#pragma once

// ===========================================================================
// screen_fwd — forward declarations for the handful of carousel / sleep-wake
// entry points main.cpp owns but individual screen_*.h files need to call
// into (e.g. the control shade returning to the carousel, or an error screen
// sleeping itself). Actual definitions live in main.cpp, textually AFTER
// every screen_*.h is included there, hence the forward declarations.
//
// Everything else is resolved by plain include order in main.cpp: each
// screen_*.h is included only after the screen_*.h's it calls into, so no
// forward declaration is needed for e.g. screen_shade.h calling
// screen_settings::enter().
// ===========================================================================

#include "screen_common.h"  // Rf

// These keep the internal (file-static) linkage the rest of the firmware
// uses — there's only ever the one translation unit (main.cpp pulls in every
// header), so `static` just documents "not meant to cross a TU boundary"
// rather than actually being load-bearing.

// carousel (main.cpp)
static void drawStandby(bool sleeping, Rf r, int pressed);
static void enterStandby();

// shared weather/HA data fetch — drives every carousel page; kicked off
// whenever Wi-Fi lands or a screen wants a fresh read (main.cpp).
static void kickWeatherRefresh();

// mDNS resolve, guarded so racing background tasks don't tear it down under
// each other (main.cpp) — every screen's background action task calls this
// before talking to the Switchboard server.
static void ensureMdns();

// The actual deep-sleep mechanism (main.cpp) — screens that sleep on their
// own schedule (e.g. No-HA's 30-minute retry) call straight into it.
[[noreturn]] static void deepSleepWithWake(uint64_t timerUs);
