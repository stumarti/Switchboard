#pragma once

#include "screen_common.h"  // Rf

// ===========================================================================
// Refresh policy — WHAT kind of redraw an interaction causes decides its Rf,
// not which screen it happens to be on. The ~40 drawStandby() call sites
// used to each pick Rf::Fast/Clean/Full by hand, so the reasoning for any one
// of them only existed as a comment next to that specific line — the same
// mistake (a dithered region flashed under Fast, or a plain button tap forced
// through an unnecessary Clean scrub) was one copy-pasted line away at every
// new call site. Add a new interaction by picking the RefreshEvent that
// matches what it redraws, not by copying whatever a similar-looking existing
// line used.
//
// Mirrors screen_common.h's commitFrame(), which already centralizes the
// Fast -> Clean auto-promotion (kCleanEvery) below whatever mode is picked
// here — this table is the layer above that, picking the STARTING mode.
// ===========================================================================
// Policy (per the UC8279X4 driver's three modes): Fast/DU is the only
// non-flashing mode, so it's the ONLY mode any in-screen control-feedback
// event may resolve to — a setpoint nudging, a volume/brightness change, a
// glyph flipping, a toggle, a chip or tab selecting. Full is the clean
// flash-from-white for an actual screen change (carousel page-turn,
// sub-screen push/pop). Half seeds the OLD plane as the inverse of the
// target — a real charge scrub, but still a full-waveform FLASH exactly like
// Full, just seeded differently — so it's reserved for the periodic
// ghost-cleanup cadence (commitFrame()'s kCleanEvery, below) and the manual
// "Full refresh" control-panel tile (screen_shade.h); no control-feedback
// call site may request it directly.
enum class RefreshEvent : uint8_t {
  // A held touch updates a value live, every frame (brightness/volume bars).
  // Same-magnitude repaints in a tight loop — Fast keeps them from stalling
  // the drag; commitFrame()'s kCleanEvery scrubs the accumulated ghosting.
  Drag,
  // A tap changes a small, localized bit of on-screen state: a toggle, a
  // step button, a chip/tab activating, a blinds/TV/Music control settling,
  // Climate's arc/setpoint redrawing. The default for "something just
  // changed in response to input." Dense dithered regions (Climate's arc,
  // Music's album art) still redraw Fast here — they're control feedback,
  // not a screen switch — and rely on commitFrame()'s periodic kCleanEvery
  // scrub to clear any ghosting a partial refresh leaves behind, same as
  // every other repeated-tap surface.
  TapFeedback,
  // The user NAVIGATED: a carousel page change, a jump-list open/close, a
  // Lighting tab switch or chip-list page turn, a sub-screen push/pop. A
  // real screen change reads as visual noise under a partial refresh, and
  // navigation is inherently occasional (not a tight repeated-tap loop), so
  // a full flash-from-white is the right cost here, not the concern it would
  // be for control feedback.
  ScreenSwitch,
  // Content on the CURRENT screen replaces itself without any navigation —
  // an async Wi-Fi/weather/HA fetch landing while the user is just looking
  // at the page. Usually a few numbers or a glyph change, so it's Fast: no
  // flash while the user is mid-read (commitFrame() skips it entirely when
  // nothing visible changed, and its kCleanEvery cadence still scrubs the
  // ghosting a run of partials builds up).
  DataLanding,
  // Only the status bar changed — the Wi-Fi glyph flipping as the link goes
  // down (e.g. the idle radio power-down) or comes back, or the "updating"
  // glyph dropping. A small, sparse region: Fast, no flash.
  StatusGlyph,
  // The first paint after a deep-sleep wake, straight from cached data. This
  // is the biggest "screen switch" of all — a fresh paint from an unknown
  // prior panel state — so it gets the same Full treatment: even if Fast
  // were requested, Uc8279X4Driver::displayStart() (freeink-sdk) can't honor
  // it as a true partial until _oldPlaneValid is set by a completed refresh,
  // which is false here (fresh object, wiped RAM), so it silently runs the
  // same from-white flash as Full anyway — asking for Full explicitly just
  // says what actually happens.
  WakeRepaint,
};

inline Rf refreshModeFor(RefreshEvent e) {
  switch (e) {
    case RefreshEvent::Drag:         return Rf::Fast;
    case RefreshEvent::TapFeedback:  return Rf::Fast;
    case RefreshEvent::ScreenSwitch: return Rf::Full;
    case RefreshEvent::DataLanding:  return Rf::Fast;
    case RefreshEvent::StatusGlyph:  return Rf::Fast;
    case RefreshEvent::WakeRepaint:  return Rf::Full;
  }
  return Rf::Full;  // unreachable — every enumerator is handled above
}
