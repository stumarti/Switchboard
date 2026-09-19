#pragma once

// ===========================================================================
// local_settings — on-device preferences that are neither the server's
// per-room config (device_config_client.h) nor the shared globals
// (globals_client.h): developer/debug toggles the user sets locally via
// Settings -> Developer. Persisted in NVS (the same "switchboard" namespace
// device_config_client.h's room slug uses) so they survive a reboot/OTA.
// ===========================================================================

#include <Arduino.h>
#include <Preferences.h>

namespace localsettings {

// Settings -> Developer -> Pixel grid: overlays a coordinate grid on every
// screen (screen_common.h's commitFrame) for lining up layout constants.
inline bool pixelGrid = false;

// Settings -> Developer -> Disable standby: for a hardware-flashing/bench
// session, where the device deep-sleeping mid-session drops its native
// USB-CDC connection and strands whoever's flashing or watching serial logs.
// Deliberately session-only (NOT persisted to NVS) — it resets to false on
// every boot so it can't get left on and silently stop the device ever
// sleeping (and draining the battery) after the bench session is over.
inline bool standbyDisabled = false;

// Settings -> Timeouts: the discrete choices every configurable timeout in
// the project picks from, so they all read from and cycle through one shared
// scale rather than each inventing its own set of steps.
inline constexpr uint16_t kTimeoutChoicesMin[] = {1, 3, 5, 15, 30};
inline constexpr int kTimeoutChoiceCount =
    sizeof(kTimeoutChoicesMin) / sizeof(kTimeoutChoicesMin[0]);

// Index of `v` in kTimeoutChoicesMin, or the 3-minute slot if it isn't one of
// the choices (e.g. a value written by an older build). Never out of range.
inline int timeoutChoiceIndex(uint16_t v) {
  for (int i = 0; i < kTimeoutChoiceCount; ++i)
    if (kTimeoutChoicesMin[i] == v) return i;
  return 1;
}
// Cycles forward through kTimeoutChoicesMin, wrapping — for a plain "tap to
// advance" row (screen timeout, control-page timeout).
inline uint16_t nextTimeoutChoice(uint16_t v) {
  return kTimeoutChoicesMin[(timeoutChoiceIndex(v) + 1) % kTimeoutChoiceCount];
}
// Same, but the sequence starts at "Off" (0) — for the refresh-interval
// override, where 0 means "use the server's refreshIntervalMin". Off -> 1 ->
// 3 -> 5 -> 15 -> 30 -> Off.
inline uint16_t nextTimeoutChoiceOrOff(uint16_t v) {
  if (v == 0) return kTimeoutChoicesMin[0];
  const int i = timeoutChoiceIndex(v);
  return (i + 1 >= kTimeoutChoiceCount) ? 0 : kTimeoutChoicesMin[i + 1];
}

// Standby carousel idle -> deep sleep. Was a hardcoded 2 min; 3 min is the
// closest of the new discrete choices, kept as the default so an un-migrated
// device doesn't start sleeping noticeably sooner than before.
inline uint16_t idleToSleepMin = 3;
// A non-status carousel page (Lighting/Blinds/...) with no interaction ->
// revert to the status page. Was a hardcoded 5 min, which IS one of the
// choices, so behavior is unchanged until the user picks something else.
inline uint16_t controlPageRevertMin = 5;
// 0 = off (use deviceconfig::refreshIntervalMin from the server's per-room
// config, the long-standing default); otherwise overrides it locally.
inline uint16_t refreshOverrideMin = 0;

inline void load() {
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/true)) {
    pixelGrid = p.getBool("pxgrid", false);
    idleToSleepMin = p.getUShort("idleMin", 3);
    controlPageRevertMin = p.getUShort("ctlMin", 5);
    refreshOverrideMin = p.getUShort("rfrOvrMin", 0);
    p.end();
  }
}

inline void setPixelGrid(bool on) {
  pixelGrid = on;
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.putBool("pxgrid", on);
    p.end();
  }
}
inline void setIdleToSleepMin(uint16_t m) {
  idleToSleepMin = m;
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.putUShort("idleMin", m);
    p.end();
  }
}
inline void setControlPageRevertMin(uint16_t m) {
  controlPageRevertMin = m;
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.putUShort("ctlMin", m);
    p.end();
  }
}
inline void setRefreshOverrideMin(uint16_t m) {
  refreshOverrideMin = m;
  Preferences p;
  if (p.begin("switchboard", false)) {
    p.putUShort("rfrOvrMin", m);
    p.end();
  }
}

}  // namespace localsettings
