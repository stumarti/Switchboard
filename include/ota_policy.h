#pragma once

// ===========================================================================
// ota_policy — when a remote may update itself (pure; host-tested). The
// install itself is ota_update.h.
//
//   scheduledDue   a timer wake inside the server's update window, with an
//                  offer, enough battery, and fewer than kMaxScheduledTries
//                  failed tries at this version already
//   nowDue         "Update now" on the server: any timer wake, whatever the
//                  window (the server stops asking once this remote has
//                  reported an attempt, so there's no retry cap here)
//   inWindow       an hour within [from, to), wrapping past midnight
// ===========================================================================

#include <stdint.h>
#include <string.h>

namespace otapolicy {

// A version that failed this many scheduled installs is left alone (until a
// different release is offered, or someone presses Update on the remote), so
// a remote with a bad signal or a failing build doesn't drain its battery
// retrying every wake.
inline constexpr uint8_t kMaxScheduledTries = 2;

inline bool inWindow(int hour, int from, int to) {
  if (from < 0 || to < 0 || hour < 0) return false;
  if (from == to) return true;  // the whole day
  return from < to ? (hour >= from && hour < to) : (hour >= from || hour < to);
}

inline bool batteryOk(uint8_t pct, uint8_t minPct) { return pct >= 1 && pct >= minPct; }

inline bool scheduledDue(bool enabled, const char* offerVersion, int localHour, int from, int to,
                         uint8_t battPct, uint8_t minBatt, uint8_t triesAtThisVersion) {
  return enabled && offerVersion && offerVersion[0] && inWindow(localHour, from, to) &&
         batteryOk(battPct, minBatt) && triesAtThisVersion < kMaxScheduledTries;
}

inline bool nowDue(bool enabled, bool now, const char* offerVersion, uint8_t battPct, uint8_t minBatt) {
  return enabled && now && offerVersion && offerVersion[0] && batteryOk(battPct, minBatt);
}

}  // namespace otapolicy
