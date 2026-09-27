#pragma once

// ===========================================================================
// screen_xbox — the Xbox carousel page: a status hero (the running game's
// title + box art, or the Xbox mark + Standby/Off when idle) with a power
// toggle, and a library list below it (~6 rows/page) that launches a game on
// tap. Mirrors screen_tv.h's fire-and-forget background-task pattern for
// service calls, and screen_music.h/album_art.h's fetch-decode-dither
// pattern for art — see xbox_art.h for the part that's new here (multiple
// simultaneous images, cached to SD).
//
// Left/Right are the ordinary carousel page-turn here, same as every other
// page — an earlier version had this screen claim them for library paging
// instead, which broke the carousel's own Left/Right on this one page. The
// library pages by TAPPING its footer instead (footerHit() below, left/right
// half); screen_shade.h's open sheet and the jump list still claim Left/
// Right for themselves, but they're modal overlays, not a carousel page
// itself, so breaking carousel navigation isn't a risk there.
//
// No browse_media here: that's a WebSocket-only Home Assistant call, and
// this app only ever speaks the plain REST API (see ha_client.h /
// http_json.h) — every other page's "live list" (Lighting's scenes, Blinds'
// items) already works this way, pulled from deviceconfig's REST-fetched
// config rather than a live HA browse call. xbox.games[] (device_config_
// client.h) is therefore the ONLY game source; there's no "browse" mode to
// degrade from, just the ordinary "not configured" / "unavailable" empty
// states every other page already has.
// ===========================================================================

#include <strings.h>  // strcasecmp — matching xboxMedia.title against configured game names

#include "screen_common.h"
#include "screen_fwd.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"
#include "xbox_art.h"

namespace screen_xbox {

inline volatile bool g_busy = false;
// -1 none; 0 = power toggle; 1..6 = library rows (row i on the current page).
inline int g_pressed = -1;
enum class Act : uint8_t { Power, Launch, Refresh };
inline Act g_act = Act::Power;
inline bool g_powerOn = false;      // optimistic target for the pending Power action
inline char g_launchId[48] = "";    // productId for the pending Launch action

// Defined further down (after the art cache) — forward-declared so task()
// can refresh the hero art right after every state fetch it does.
inline void loadVisibleArt();

inline void task(void*) {
  g_busy = true;
  ensureMdns();
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  if (globalsclient::ok) {
    if (g_act == Act::Power) {
      haclient::setXboxPower(h, p, t, deviceconfig::xboxRemoteEntity, g_powerOn);
      delay(500);
    } else if (g_act == Act::Launch) {
      // Tapping a game while the console is off/unknown wakes it first —
      // real hardware needs several seconds to start accepting media
      // commands after remote.turn_on, so this delay is a best guess, not a
      // confirmed boot time; lengthen it here if launches land before the
      // console's ready to receive them.
      const bool needsWake = strcmp(haclient::xboxMedia.state, "on") != 0 &&
                             strcmp(haclient::xboxMedia.state, "playing") != 0 &&
                             strcmp(haclient::xboxMedia.state, "idle") != 0;
      if (needsWake) {
        haclient::setXboxPower(h, p, t, deviceconfig::xboxRemoteEntity, true);
        delay(4000);
      }
      haclient::launchXboxGame(h, p, t, deviceconfig::xboxMediaEntity, g_launchId);
      delay(600);
    }
    haclient::fetchXboxMedia(h, p, t, deviceconfig::xboxMediaEntity);
    // Whatever just happened (power flip, launch, or a plain periodic poll)
    // may have changed which game is running — refresh the hero (and, since
    // the playing game moves to the front of the list, possibly row 0 too).
    loadVisibleArt();
  }
  g_busy = false;
  vTaskDelete(nullptr);
}

inline void kick(Act act) {
  if (g_busy || g_weatherBusy) return;
  g_act = act;
  g_busy = true;
  if (xTaskCreatePinnedToCore(task, "sb_xbox", 8192, nullptr, 1, nullptr, 1) != pdPASS) g_busy = false;
}
inline void togglePower() {
  g_powerOn = strcmp(haclient::xboxMedia.state, "off") == 0 ||
             strcmp(haclient::xboxMedia.state, "") == 0 ||
             strcmp(haclient::xboxMedia.state, "unavailable") == 0;
  kick(Act::Power);
}
inline void launch(const char* productId) {
  snprintf(g_launchId, sizeof(g_launchId), "%s", productId);
  kick(Act::Launch);
}
// Periodic/background state refresh (no service call) — mirrors
// screen_music.h's Act::Refresh, polled from main.cpp while this page is
// showing and the console might be mid-session.
inline void kickRefresh() { kick(Act::Refresh); }

// --- library paging: the playing game (if any) is moved to the front, then
// the rest keep deviceconfig order. Recomputed on demand (cheap — at most
// kMaxXboxGames=12 string compares), never cached, so a config/state change
// is picked up on the very next draw() with no invalidation to track.
inline constexpr int kRowsPerPage = 6;
inline int page = 0;

// Index into deviceconfig::xboxGames[] for display slot `i` (0-based across
// the WHOLE list, not just the current page).
inline int displayIndex(int i) {
  const int n = deviceconfig::xboxGameCount;
  if (n == 0) return -1;
  int playing = -1;
  if (haclient::xboxMedia.ok && haclient::xboxMedia.title[0]) {
    for (int g = 0; g < n; ++g)
      if (strcasecmp(deviceconfig::xboxGames[g].name, haclient::xboxMedia.title) == 0) playing = g;
  }
  if (playing < 0) return i < n ? i : -1;
  if (i == 0) return playing;
  const int g = i - 1 < playing ? i - 1 : i;  // skip over `playing`'s original slot
  return g < n ? g : -1;
}
inline int pageCount() {
  const int n = deviceconfig::xboxGameCount;
  return n == 0 ? 1 : (n + kRowsPerPage - 1) / kRowsPerPage;
}
inline void clampPage() {
  const int pc = pageCount();
  if (page >= pc) page = pc - 1;
  if (page < 0) page = 0;
}
inline void nextPage() { page = (page + 1) % pageCount(); loadVisibleArt(); }
inline void prevPage() { page = (page + pageCount() - 1) % pageCount(); loadVisibleArt(); }

// --- art cache: the hero + up to kRowsPerPage row thumbnails currently on
// screen. Re-derived whenever the page (or the playing game) changes —
// loadVisibleArt() below frees the old set and fetches the new one in a
// background task so a page turn never blocks on the network.
inline uint8_t* g_heroBits = nullptr;
inline char g_heroKey[64] = "";  // productId (or "off"/"standby") this hero art was built for
inline uint8_t* g_rowBits[kRowsPerPage] = {};
inline char g_rowKey[kRowsPerPage][48] = {};
inline volatile bool g_artBusy = false;
inline bool g_artDirty = false;  // set by the art task when a repaint should follow

inline void freeHero() {
  if (g_heroBits) { free(g_heroBits); g_heroBits = nullptr; }
  g_heroKey[0] = 0;
}
inline void freeRow(int slot) {
  if (g_rowBits[slot]) { free(g_rowBits[slot]); g_rowBits[slot] = nullptr; }
  g_rowKey[slot][0] = 0;
}

inline void artTask(void*) {
  g_artBusy = true;
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;

  // Hero: the running game's entity_picture, or nothing (draw the mark) when
  // idle/off — keyed by state+title so a track/game change re-fetches but an
  // unrelated repaint (e.g. the power toggle's own settle) doesn't.
  char heroKey[64];
  const bool playing = haclient::xboxMedia.ok && haclient::xboxMedia.title[0] &&
                      haclient::xboxMedia.picture[0];
  snprintf(heroKey, sizeof(heroKey), "%s", playing ? haclient::xboxMedia.title : "");
  if (strcmp(heroKey, g_heroKey) != 0) {
    freeHero();
    if (playing && globalsclient::ok) {
      char slug[64];
      xboxart::slugify(haclient::xboxMedia.title, slug, sizeof(slug));
      g_heroBits = xboxart::get(h, p, t, slug, haclient::xboxMedia.picture, xboxart::kHeroSize);
    }
    snprintf(g_heroKey, sizeof(g_heroKey), "%s", heroKey);
    g_artDirty = true;
  }

  // Rows: whatever's visible on the current page.
  clampPage();
  for (int slot = 0; slot < kRowsPerPage; ++slot) {
    const int gi = displayIndex(page * kRowsPerPage + slot);
    const char* pid = gi >= 0 ? deviceconfig::xboxGames[gi].productId : "";
    if (strcmp(pid, g_rowKey[slot]) == 0) continue;  // already correct for this slot
    freeRow(slot);
    if (gi >= 0 && globalsclient::ok && deviceconfig::xboxGames[gi].art[0]) {
      char slug[48];
      xboxart::slugify(pid, slug, sizeof(slug));
      g_rowBits[slot] = xboxart::get(h, p, t, slug, deviceconfig::xboxGames[gi].art, xboxart::kRowSize);
    }
    snprintf(g_rowKey[slot], sizeof(g_rowKey[slot]), "%s", pid);
    g_artDirty = true;
  }

  g_artBusy = false;
  vTaskDelete(nullptr);
}
// Kick a background art refresh for whatever's visible now. Cheap to call
// often (page turns, state landing) — it no-ops per-slot for anything
// already cached under the right key, so a repeat call mostly just confirms
// nothing changed.
inline void loadVisibleArt() {
  if (g_artBusy || !globalsclient::ok) return;
  g_artBusy = true;  // set here (not just in the task) so back-to-back calls this frame coalesce
  if (xTaskCreatePinnedToCore(artTask, "sb_xbart", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    g_artBusy = false;
}

// --- layout ---------------------------------------------------------------
// kHeroArtSize/kThumbSz must equal xbox_art.h's kHeroSize/kRowSize exactly —
// the real (fetched) art and the placeholder glyph share the same slot, and
// xboxart::icon() reports whatever size the caller sized the bitmap for.
inline constexpr int16_t kHeroTop     = kStatusBarH + 12 + kPad;
inline constexpr int16_t kHeroArtSize = xboxart::kHeroSize;
inline constexpr int16_t kHeroH       = 150;
// Same pill+sliding-knob toggle as TV's mute row / Music's mute row /
// Lighting's on-off row (drawMuteToggle/drawToggle) — 64x32, knob inset 4px,
// diameter = height-8. Copied rather than shared (this codebase's existing
// convention: three near-identical drawToggle()s already exist rather than
// one shared helper), so this is a fourth, not a break from the pattern.
inline constexpr int16_t kPowerW = 64, kPowerH = 32;
inline constexpr int16_t kPowerX = static_cast<int16_t>(Ui::W - kShPad - kPowerW);
inline constexpr int16_t kPowerY = kHeroTop;

inline constexpr int16_t kListTop = static_cast<int16_t>(kHeroTop + kHeroH + kPad);
inline constexpr int16_t kRowH    = 64;
inline constexpr int16_t kRowGap  = 4;
inline constexpr int16_t kThumbSz = xboxart::kRowSize;

inline constexpr int16_t kFooterY = static_cast<int16_t>(kListTop + kRowsPerPage * (kRowH + kRowGap) + 6);

inline void drawPowerCapsule(bool on) {
  const uint8_t r = static_cast<uint8_t>(kPowerH / 2);
  if (on) ui.fillRect(kPowerX, kPowerY, kPowerW, kPowerH, Color::Black, r);
  else    ui.strokeRect(kPowerX, kPowerY, kPowerW, kPowerH, 2, r);
  const int16_t knobD = static_cast<int16_t>(kPowerH - 8);
  const int16_t knobY = static_cast<int16_t>(kPowerY + (kPowerH - knobD) / 2);
  const int16_t knobX = on ? static_cast<int16_t>(kPowerX + kPowerW - knobD - 4)
                           : static_cast<int16_t>(kPowerX + 4);
  ui.fillRect(knobX, knobY, knobD, knobD, on ? Color::White : Color::Black,
              static_cast<uint8_t>(knobD / 2));
  // A power glyph just left of the capsule stands in for the separate
  // "Mute"/label text TV and Music's rows have room for — the hero row's
  // title text already fills most of that space here.
  ui.icon(icons::get("wx_tv_power"), static_cast<int16_t>(kPowerX - icons::get("wx_tv_power").w - 8),
          static_cast<int16_t>(kPowerY + (kPowerH - icons::get("wx_tv_power").h) / 2));
}
inline bool powerHit(int16_t tx, int16_t ty) {
  return tx >= kPowerX && tx < kPowerX + kPowerW && ty >= kPowerY && ty < kPowerY + kPowerH;
}

inline void drawHero() {
  const bool running = haclient::xboxMedia.ok && haclient::xboxMedia.title[0];
  if (g_heroBits) {
    ui.icon(xboxart::icon(g_heroBits, xboxart::kHeroSize), kShPad, kHeroTop);
  } else {
    // No art (idle/off, or a fetch/decode failure) — the Xbox mark stands in.
    constexpr int16_t kMarkSize = 100;
    ui.iconScaled(icons::get("wx_jump_xbox"), kShPad, static_cast<int16_t>(kHeroTop + (kHeroArtSize - kMarkSize) / 2),
                 kMarkSize, kMarkSize);
  }
  const int16_t textX = static_cast<int16_t>(kShPad + kHeroArtSize + 16);
  const int16_t textW = static_cast<int16_t>(kPowerX - 10 - textX);
  const char* status = !haclient::xboxMedia.ok         ? "Unavailable"
                       : running                        ? haclient::xboxMedia.title
                       : !strcmp(haclient::xboxMedia.state, "off") ? "Off"
                                                          : "Standby";
  ui.text(status, textX, static_cast<int16_t>(kHeroTop + kPowerH + 10), textW, 32, TextAlign::Left,
          Color::Black, 2, Ui::kFont28);
  if (running)
    ui.text("Playing", textX, static_cast<int16_t>(kHeroTop + kPowerH + 46), textW, 22,
            TextAlign::Left, Color::DarkGray, 1, Ui::kFontSmall);

  drawPowerCapsule(g_powerOn);
  drawDottedLine(kShPad, static_cast<int16_t>(kHeroTop + kHeroH - 1),
                static_cast<int16_t>(Ui::W - 2 * kShPad));
}

inline void drawRow(int slot, int gi, bool pressed) {
  const int16_t y = static_cast<int16_t>(kListTop + slot * (kRowH + kRowGap));
  const bool isPlaying = slot == 0 && page == 0 && gi >= 0 && haclient::xboxMedia.ok &&
                        haclient::xboxMedia.title[0] &&
                        strcasecmp(deviceconfig::xboxGames[gi].name, haclient::xboxMedia.title) == 0;
  const Color bg = isPlaying ? Color::Black : Color::White;
  const Color fg = isPlaying ? Color::White : Color::Black;
  if (isPlaying)  ui.fillRect(kShPad, y, static_cast<int16_t>(Ui::W - 2 * kShPad), kRowH, bg, 12);
  else if (pressed) ui.fillRect(kShPad, y, static_cast<int16_t>(Ui::W - 2 * kShPad), kRowH, Color::Black, 12);

  const Color rowFg = pressed && !isPlaying ? Color::White : fg;
  const int16_t thumbY = static_cast<int16_t>(y + (kRowH - kThumbSz) / 2);
  if (g_rowBits[slot]) {
    ui.icon(xboxart::icon(g_rowBits[slot], xboxart::kRowSize), static_cast<int16_t>(kShPad + 10), thumbY);
  } else {
    ui.iconScaled(icons::get("wx_jump_xbox"), static_cast<int16_t>(kShPad + 10), thumbY, kThumbSz, kThumbSz);
  }

  const int16_t nameX = static_cast<int16_t>(kShPad + 10 + kThumbSz + 14);
  const int16_t nameW = static_cast<int16_t>(Ui::W - kShPad - 10 - nameX - 60);
  ui.text(gi >= 0 ? deviceconfig::xboxGames[gi].name : "", nameX,
          static_cast<int16_t>(y + kRowH / 2 - 14), nameW, 28, TextAlign::Left, rowFg);
  if (isPlaying)
    ui.text("Playing", nameX, static_cast<int16_t>(y + kRowH / 2 + 12), nameW, 18, TextAlign::Left,
            Color::LightGray, 1, Ui::kFontSmall);

  if (gi >= 0)
    ui.icon(icons::get("wx_music_play"), static_cast<int16_t>(Ui::W - kShPad - 10 - icons::get("wx_music_play").w),
            static_cast<int16_t>(y + (kRowH - icons::get("wx_music_play").h) / 2), rowFg);
}

// Row slot (0..kRowsPerPage-1) a logical tap fell on, or -1.
inline int rowHit(int16_t tx, int16_t ty) {
  if (tx < kShPad || tx >= Ui::W - kShPad) return -1;
  for (int slot = 0; slot < kRowsPerPage; ++slot) {
    const int16_t y = static_cast<int16_t>(kListTop + slot * (kRowH + kRowGap));
    if (ty >= y && ty < y + kRowH) return slot;
  }
  return -1;
}

// The footer is the library's page control now that Left/Right go back to
// paging the CAROUSEL (they used to page this list locally, which broke the
// carousel's own Left/Right convention on this one page — see main.cpp's
// Stage::Standby tap dispatch). Tapping its left/right half pages the
// library instead; only shown/tappable when there's more than one page.
inline constexpr int16_t kFooterH = 26;
inline bool hasMultiplePages() { return pageCount() > 1; }
// -1 = no hit; 0 = left half (prev page); 1 = right half (next page).
inline int footerHit(int16_t tx, int16_t ty) {
  if (!hasMultiplePages() || ty < kFooterY || ty >= kFooterY + kFooterH) return -1;
  return tx < Ui::W / 2 ? 0 : 1;
}

inline void draw(int pressed = -1) {
  if (!deviceconfig::xboxMediaEntity[0] && !deviceconfig::xboxRemoteEntity[0]) {
    ui.text("Xbox", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text("not configured for this room", 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
    return;
  }

  drawHero();

  clampPage();
  const int n = deviceconfig::xboxGameCount;
  if (n == 0) {
    ui.text("No games configured", 0, static_cast<int16_t>(kListTop + 40), Ui::W, 26,
            TextAlign::Center, Color::DarkGray, 1, Ui::kFontSmall);
  } else {
    for (int slot = 0; slot < kRowsPerPage; ++slot) {
      const int gi = displayIndex(page * kRowsPerPage + slot);
      if (gi < 0) break;
      drawRow(slot, gi, pressed == slot + 1);
    }
  }

  if (hasMultiplePages()) {
    char footer[28];
    snprintf(footer, sizeof(footer), "<  Page %d of %d  >", page + 1, pageCount());
    ui.text(footer, 0, kFooterY, Ui::W, kFooterH, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
  }
  // Art loads from nextPage()/prevPage() and task()'s post-fetch refresh, not
  // from here — draw() runs on every tap/repaint and must stay cheap; it
  // just renders whatever's already in g_heroBits/g_rowBits.
}

}  // namespace screen_xbox
