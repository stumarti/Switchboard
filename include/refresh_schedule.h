#pragma once

// ===========================================================================
// refresh_schedule — how long a remote sleeps before its next timer refresh.
//
// Normally the refresh interval itself: 30 min after it went to sleep. With
// the room's "on the clock" option (deviceconfig::refreshAligned) the wake
// lands on the interval's marks in local time instead (every 30 min = :00
// and :30, every hour = on the hour), plus this remote's stagger
// (deviceconfig::refreshStaggerSec, spread across the house by the server)
// so a houseful of remotes doesn't hit the server in the same second.
//
// It needs the time: the Date header of a response this boot (a timer wake
// always refreshes before it sleeps again). Without one, or with an interval
// that doesn't divide a day, it falls back to the plain interval.
//
// The deep-sleep timer runs off the chip's RC oscillator, which drifts a few
// percent: a wake can land a little early or late. Each wake re-aims from
// the fresh clock, so the error never builds up; a wake that came up a
// little early counts as that mark's refresh (see kMinSleepSec).
// ===========================================================================

#include <stdint.h>

#include "device_config_client.h"
#include "ha_client.h"

namespace refreshschedule {

// A wake closer than this to the next mark skips to the one after: the
// refresh just done already covered it.
inline constexpr uint32_t kMinSleepSec = 90;

// Seconds from `nowUtc` to the next interval mark in local time
// (utcOffsetMin), plus `staggerSec`. Pure, for the host tests.
inline uint32_t alignedSleepSec(int64_t nowUtc, int utcOffsetMin, uint32_t intervalSec, uint32_t staggerSec) {
  if (intervalSec == 0) return 0;
  if (staggerSec >= intervalSec) staggerSec %= intervalSec;
  const int64_t local = nowUtc + static_cast<int64_t>(utcOffsetMin) * 60 - staggerSec;
  int64_t into = local % intervalSec;
  if (into < 0) into += intervalSec;
  uint32_t sleep = static_cast<uint32_t>(intervalSec - into);
  if (sleep < kMinSleepSec) sleep += intervalSec;
  return sleep;
}

// The timer for a carousel sleep of `intervalSec` (the refresh interval).
inline uint32_t sleepSec(uint32_t intervalSec) {
  int64_t now = 0;
  // Aligning only means something for an interval that divides a day evenly
  // (5/10/15/30/60 min all do).
  if (!deviceconfig::refreshAligned || intervalSec == 0 || 86400u % intervalSec != 0 ||
      !haclient::nowUtc(now))
    return intervalSec;
  return alignedSleepSec(now, deviceconfig::utcOffsetMin, intervalSec, deviceconfig::refreshStaggerSec);
}

}  // namespace refreshschedule
