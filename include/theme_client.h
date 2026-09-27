#pragma once

// ===========================================================================
// theme_client — checks Switchboard-Server's /api/theme for a newer icon/
// font pack version than what's stored in NVS, and downloads+applies
// whichever changed. Piggybacks on the existing config-refresh cadence
// (called from app/data_refresh.h's refreshStandby(), the same background task that
// already calls deviceconfig::fetch()/globalsclient::fetch()) rather than
// running its own timer.
// ===========================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"
#include "http_json.h"
#include "icons.h"
#include "fonts.h"
#include "screen_common.h"  // ui — rebindFonts() must run again after a font pack (re)loads

namespace themeclient {

inline char iconsVersion[24] = "";
inline char fontsVersion[24] = "";

// Set by Settings -> Refresh now; the next background refresh (refreshStandby())
// takes it and re-downloads BOTH packs plus every configured MDI icon
// (mdi_icon.h) regardless of version. Without it nothing already on SD is
// ever re-pulled — only a version bump, or a pack missing from SD, downloads.
inline volatile bool refreshRequested = false;

// Read-and-clear, so a request made mid-refresh lands on the next one.
inline bool takeRefreshRequest() {
  const bool r = refreshRequested;
  refreshRequested = false;
  return r;
}

inline void loadVersionsFromNvs() {
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/true)) {
    snprintf(iconsVersion, sizeof(iconsVersion), "%s", p.getString("iconsVer", "").c_str());
    snprintf(fontsVersion, sizeof(fontsVersion), "%s", p.getString("fontsVer", "").c_str());
    p.end();
  }
}

inline void saveVersionToNvs(const char* key, const char* version) {
  Preferences p;
  if (p.begin("switchboard", /*readOnly=*/false)) {
    p.putString(key, version);
    p.end();
  }
}

// Called once at boot, right after persist::begin() mounts the SD card -
// loads whatever pack is already on SD (if any) and re-binds the font slots
// so the very first real screen (not the splash, which is already drawn by
// this point) reflects it. A no-op-but-harmless call when there's no card or
// no pack yet — icons::get()/fonts::get() already fall back to the
// compiled-in defaults.
inline void loadAtBoot() {
  loadVersionsFromNvs();
  icons::reloadFromPack();
  fonts::reloadFromPack();
  ui.rebindFonts();
}

// One check-and-update pass. Cheap when nothing changed (one small JSON GET);
// only downloads a pack when its version differs from what's applied, when
// the server has one but nothing is loaded from SD (card swapped/wiped), or
// when `force` (Settings -> Refresh now). Best-effort throughout — any
// failure just means "try again next refresh cycle," the previously loaded
// theme (or the compiled-in default) keeps showing.
// Download whichever pack the server has a different version of (or that
// isn't loaded from SD, or everything when `force`). The versions come from
// /api/theme (checkForUpdate() below) or the /bundle response
// (app/data_refresh.h).
inline void applyVersions(const char* serverIcons, const char* serverFonts, const char* token,
                          bool force = false) {
  if (serverIcons && *serverIcons && persist::g_ready &&
      (force || !iconpack::loaded || strcmp(serverIcons, iconsVersion) != 0)) {
    if (iconpack::downloadAndLoad(token)) {
      snprintf(iconsVersion, sizeof(iconsVersion), "%s", serverIcons);
      saveVersionToNvs("iconsVer", iconsVersion);
    }
  }
  if (serverFonts && *serverFonts && persist::g_ready &&
      (force || !fontpack::loaded || strcmp(serverFonts, fontsVersion) != 0)) {
    if (fontpack::downloadAndLoad(token)) {
      snprintf(fontsVersion, sizeof(fontsVersion), "%s", serverFonts);
      saveVersionToNvs("fontsVer", fontsVersion);
      ui.rebindFonts();  // the loaded BitmapFont objects moved — re-point every slot
    }
  }
}

// One check-and-update pass. Cheap when nothing changed (one small JSON GET);
// only downloads a pack when its version differs from what's applied, when
// the server has one but nothing is loaded from SD (card swapped/wiped), or
// when `force` (Settings -> Refresh now). Best-effort throughout — any
// failure just means "try again next refresh cycle," the previously loaded
// theme (or the compiled-in default) keeps showing.
inline void checkForUpdate(const char* token, bool force = false) {
  char status[64];
  JsonDocument doc;
  if (!httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, "/api/theme", token, doc,
                     status, sizeof(status))) {
    return;
  }
  applyVersions(doc["iconsVersion"] | "", doc["fontsVersion"] | "", token, force);
}
}  // namespace themeclient
