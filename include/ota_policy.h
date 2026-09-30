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
//   MarkerScan     finds an image's "SWITCHBOARD_FW:<board>:<version>"
//                  marker as it downloads; boardMatches() says whether it's
//                  for this board (a marker with no board is the x4pro's,
//                  from before boards)
// ===========================================================================

#include <stdint.h>
#include <stddef.h>
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

// Fed an image as it downloads, a chunk at a time; keeps what follows the
// first "SWITCHBOARD_FW:" with something after it (up to a NUL). Firmware
// that holds the bare prefix too (this scanner's own kPrefix) has that copy
// skipped: an empty tail keeps looking. The prefix never overlaps itself, so
// a mismatch only has to check whether this byte starts it again.
struct MarkerScan {
  static constexpr const char* kPrefix = "SWITCHBOARD_FW:";
  static constexpr uint8_t kPrefixLen = 15;
  uint8_t matched = 0;
  bool capturing = false;
  bool found = false;
  char tail[80] = "";
  uint8_t len = 0;
  void feed(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n && !found; ++i) {
      const char c = static_cast<char>(p[i]);
      if (capturing) {
        if (c == 0 || len >= sizeof(tail) - 1) {
          tail[len] = 0;
          capturing = false;
          found = len > 0;  // the bare prefix (a string, not the marker): keep looking
          len = 0;
          if (found) len = static_cast<uint8_t>(strlen(tail));
        } else {
          tail[len++] = c;
        }
        continue;
      }
      if (c == kPrefix[matched]) {
        if (++matched == kPrefixLen) capturing = true;
      } else {
        matched = c == kPrefix[0] ? 1 : 0;
      }
    }
  }
};

inline constexpr const char* kLegacyBoard = "x4pro";

// Whether a scanned image is for `board`: "<board>:<version>", or just
// "<version>" (no ':') from before boards, which is the x4pro.
inline bool boardMatches(const MarkerScan& m, const char* board) {
  if (!m.found || !board) return false;
  const char* colon = strchr(m.tail, ':');
  if (!colon) return strcmp(board, kLegacyBoard) == 0;
  const size_t n = static_cast<size_t>(colon - m.tail);
  return n == strlen(board) && strncmp(m.tail, board, n) == 0;
}

}  // namespace otapolicy
