#pragma once

// ===========================================================================
// quick_access — the Home-tap overlay over the carousel: the fixed "Jump to"
// grid, or, when the room config sends hub.items[], the Quick Access hub
// (tiles that navigate, plus optional toggle/run action strips).
// ===========================================================================

#include <Arduino.h>

#include "screen_common.h"
#include "refresh_policy.h"
#include "device_config_client.h"
#include "ha_client.h"
#include "globals_client.h"
#include "mdi_icon.h"
#include "local_settings.h"
#include "app/carousel.h"
#include "app/input.h"
#include "screen_settings.h"
#include "screen_error.h"

// NOTE: this grid is a fixed 2-col x 5-row layout (kJumpTileH etc. below) —
// exactly 10 destinations fit. Self-test was dropped to make room for Wifi
// (a real carousel page) rather than shrinking every tile to fit an 11th;
// the hardware self-test screen is still reachable via Home-long-press from
// the No-HA / No-Room error screens (see app/stages.h).
static const char* const kJumpItems[] = {"Status", "Lighting", "Blinds", "Music",
                                         "TV",     "Xbox",     "Wifi",    "Climate",
                                         "Settings", "Error states"};
static constexpr int kJumpCount = 10;
static constexpr int kJumpSettings = 8, kJumpErrors = 9;
// 2-wide grid of icon+label tiles (5 rows for the 10 destinations) instead of
// a linear list — each tile carries its own Material icon (weather_icons.h's
// kWx_jump_* set, baked once at a consistent size for every destination,
// independent of whatever size that same glyph is used at elsewhere).
static constexpr int16_t kJumpCols    = 2;
static constexpr int16_t kJumpMarginX = 20;
static constexpr int16_t kJumpGapX    = 14;
static constexpr int16_t kJumpGapY    = 14;
static constexpr int16_t kJumpTileW =
    (Ui::W - 2 * kJumpMarginX - (kJumpCols - 1) * kJumpGapX) / kJumpCols;
static constexpr int16_t kJumpTileH = 128;
static constexpr int16_t kJumpTop   = kStatusBarH + 12 + kPad;
static int jumpSel = 0;

static const freeink::Icon* jumpIcon(int i) {
  switch (i) {
    case 0: return &icons::get("wx_jump_status");
    case 1: return &icons::get("wx_jump_lighting");
    case 2: return &icons::get("wx_jump_blinds");
    case 3: return &icons::get("wx_jump_music");
    case 4: return &icons::get("wx_jump_tv");
    case 5: return &icons::get("wx_jump_xbox");
    case 6: return &icons::get("wx_jump_wifi");
    case 7: return &icons::get("wx_jump_climate");
    case kJumpSettings: return &icons::get("wx_jump_settings");
    case kJumpErrors:   return &icons::get("wx_jump_errors");
    default: return nullptr;
  }
}

static void jumpTilePos(int slot, int16_t& x, int16_t& y) {
  const int col = slot % kJumpCols, row = slot / kJumpCols;
  x = static_cast<int16_t>(kJumpMarginX + col * (kJumpTileW + kJumpGapX));
  y = static_cast<int16_t>(kJumpTop + row * (kJumpTileH + kJumpGapY));
}

// The jump grid drops any carousel page this room's config has hidden
// (deviceconfig::screens.*) — Settings/Self-test/Error states (indices >=
// kCarouselPages) have no such toggle and are always present. Rebuilt
// wherever the grid is opened/redrawn so a config change takes effect.
static int jumpVisible[kJumpCount];
static int jumpVisibleCount = 0;
static void rebuildJumpVisible() {
  jumpVisibleCount = 0;
  for (int i = 0; i < kJumpCount; ++i) {
    if (i < kCarouselPages && !pageEnabled(static_cast<uint8_t>(i))) continue;
    jumpVisible[jumpVisibleCount++] = i;
  }
}
// Which slot (0-based grid position, not the original kJumpItems index)
// jumpSel's original index currently occupies, or 0 if it's been hidden.
static int jumpSlotFor(int original) {
  for (int slot = 0; slot < jumpVisibleCount; ++slot)
    if (jumpVisible[slot] == original) return slot;
  return 0;
}
// Tile slot a logical tap at (tx,ty) fell on, or -1.
static int jumpHitTest(int16_t tx, int16_t ty) {
  for (int slot = 0; slot < jumpVisibleCount; ++slot) {
    int16_t x, y;
    jumpTilePos(slot, x, y);
    if (tx >= x && tx < x + kJumpTileW && ty >= y && ty < y + kJumpTileH) return slot;
  }
  return -1;
}

// ===========================================================================
// Quick Access hub — a config-driven grid (deviceconfig::hubItems[]) that
// takes over this same jump-list grid (kJumpTop/kJumpTileW/kJumpTileH/
// jumpTilePos above) whenever the server sends hub.items[]. The fixed
// kJumpItems grid above is the fallback for a room whose config hasn't been
// migrated to send "hub" yet — both live in drawJumpList() below, branching
// on deviceconfig::hubItemCount.
//
// Each tile is two independent tap zones (see hubZoneHit()): the top 2/3
// navigates locally (no HA call); the bottom 1/3, when quickActionsEnabled
// and the item has an action, fires a toggle/run HA service call. This is
// the one part of the jump list that touches Home Assistant at all — plain
// navigation never did and still doesn't.
static int hubPage = 0;
static constexpr int kHubPerPage = kJumpCount;  // reuses the same 2x5 grid

static int hubPageCount() {
  return deviceconfig::hubItemCount == 0
             ? 1
             : (deviceconfig::hubItemCount + kHubPerPage - 1) / kHubPerPage;
}
static void hubClampPage() {
  const int pc = hubPageCount();
  if (hubPage >= pc) hubPage = pc - 1;
  if (hubPage < 0) hubPage = 0;
}

// target string -> carouselPage, or -1 for anything this app has no screen
// for (e.g. "vacuum" — falls back to Settings on tap; see hubNavigate()).
static int hubTargetPage(const char* target) {
  if (!strcmp(target, "media")) return kPageMusic;
  if (!strcmp(target, "climate")) return kPageClimate;
  if (!strcmp(target, "lighting")) return kPageLighting;
  if (!strcmp(target, "blinds")) return kPageBlinds;
  if (!strcmp(target, "tv")) return kPageTv;
  if (!strcmp(target, "xbox")) return kPageXbox;
  if (!strcmp(target, "guestwifi") || !strcmp(target, "wifi")) return kPageWifi;
  return -1;
}
static const freeink::Icon* hubIconFor(const char* target) {
  const int page = hubTargetPage(target);
  if (page >= 0) return jumpIcon(page);
  return &icons::get("wx_jump_settings");  // unrecognized target: same glyph its Settings fallback uses
}

// Live on/off for every configured Toggle item, indexed by deviceconfig::
// hubItems[]'s own index. Small and flat (kMaxHubItems=20 bools) even though
// only the current page's entries are ever FETCHED (hubLoadPageState()) —
// storage being flat just means a page you've already visited this session
// keeps showing its last-known state instead of blanking when you page away
// and back, which reads better than losing it.
// (Lives in haclient — haclient::hubToggleOn — so persist.h caches it with
// the rest of the pages' state; aliased here for brevity.)
static bool (&hubToggleOn)[haclient::kMaxHubToggles] = haclient::hubToggleOn;
static_assert(haclient::kMaxHubToggles == deviceconfig::kMaxHubItems,
              "hub toggle state must have one slot per hub item");
static volatile bool hubStateBusy = false;
// Set by hubStateTask/hubActionTask when a repaint should follow; consumed
// (and cleared) by the settle check alongside every other page's g_busy
// pattern, but keyed on this flag instead of a g_pressed/g_busy pair since
// the hub's "busy" (an HA call in flight) doesn't gate a press-flash the way
// every other page's does — the strip's fill IS the state, there's no
// separate momentary press style to clear.
static bool hubDirty = false;

static void hubStateTask(void*) {
  hubStateBusy = true;
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  if (globalsclient::ok) {
    hubClampPage();
    const int start = hubPage * kHubPerPage;
    const int end = start + kHubPerPage < deviceconfig::hubItemCount ? start + kHubPerPage
                                                                     : deviceconfig::hubItemCount;
    for (int i = start; i < end; ++i) {
      if (deviceconfig::hubItems[i].actionType != deviceconfig::HubAction::Toggle) continue;
      bool on = hubToggleOn[i];
      if (haclient::fetchHubToggleState(h, p, t, deviceconfig::hubItems[i].actionEntity, on) &&
          on != hubToggleOn[i]) {
        hubToggleOn[i] = on;
        hubDirty = true;
      }
    }
  }
  hubStateBusy = false;
  vTaskDelete(nullptr);
}
// "Subscribe narrowly" (spec language) translates to REST polling scope
// here, same as every other page: only fetch state for whichever page's
// Toggle entities are actually on screen right now, not the whole list.
static void hubLoadPageState() {
  if (hubStateBusy || !globalsclient::ok) return;
  hubStateBusy = true;
  if (xTaskCreatePinnedToCore(hubStateTask, "sb_hubst", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    hubStateBusy = false;
}
static void hubNextPage() { hubPage = (hubPage + 1) % hubPageCount(); hubLoadPageState(); }
static void hubPrevPage() { hubPage = (hubPage + hubPageCount() - 1) % hubPageCount(); hubLoadPageState(); }

// The pending/last action-strip tap — a Run flashes its strip once
// (hubFlashUntilMs), a Toggle just tracks hubToggleOn[] (set optimistically
// on tap, reconciled from the real fetch this same task does after the call).
static volatile bool hubActionBusy = false;
static int hubActionIdx = -1;
static bool hubActionOn = false;
static deviceconfig::HubAction hubActionKind = deviceconfig::HubAction::None;
static int hubFlashIdx = -1;
static uint32_t hubFlashUntilMs = 0;

static void hubActionTask(void*) {
  hubActionBusy = true;
  ensureMdns();
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  if (globalsclient::ok && hubActionIdx >= 0 && hubActionIdx < deviceconfig::hubItemCount) {
    const deviceconfig::HubItem& it = deviceconfig::hubItems[hubActionIdx];
    if (hubActionKind == deviceconfig::HubAction::Toggle) {
      haclient::hubToggle(h, p, t, it.actionEntity, hubActionOn);
      delay(400);  // let HA apply before reading back, same convention as Climate/Blinds
      bool on = hubActionOn;
      haclient::fetchHubToggleState(h, p, t, it.actionEntity, on);
      hubToggleOn[hubActionIdx] = on;
    } else if (hubActionKind == deviceconfig::HubAction::Run) {
      haclient::hubRun(h, p, t, it.actionEntity, it.actionService, it.actionData);
    }
  }
  hubDirty = true;
  hubActionBusy = false;
  vTaskDelete(nullptr);
}
static void hubFireAction(int idx) {
  if (hubActionBusy || g_weatherBusy || idx < 0 || idx >= deviceconfig::hubItemCount) return;
  const deviceconfig::HubItem& it = deviceconfig::hubItems[idx];
  hubActionIdx = idx;
  hubActionKind = it.actionType;
  if (it.actionType == deviceconfig::HubAction::Toggle) {
    hubActionOn = !hubToggleOn[idx];
    hubToggleOn[idx] = hubActionOn;  // optimistic; hubActionTask reconciles it
  } else if (it.actionType == deviceconfig::HubAction::Run) {
    hubFlashIdx = idx;
    hubFlashUntilMs = millis() + 600;
  } else {
    return;
  }
  hubActionBusy = true;
  if (xTaskCreatePinnedToCore(hubActionTask, "sb_hubact", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    hubActionBusy = false;
}

// Returns the visible-page slot (0..kHubPerPage-1) a tap landed in and which
// zone via `zoneOut` (0 = top 2/3, navigate; 1 = bottom 1/3, action), or -1
// if the tap missed every tile. Shared by layout and hit-testing — see
// drawHubGrid() below, which computes the exact same topH split.
static int hubZoneHit(int16_t tx, int16_t ty, int& zoneOut) {
  for (int slot = 0; slot < kHubPerPage; ++slot) {
    int16_t x, y;
    jumpTilePos(slot, x, y);
    if (tx < x || tx >= x + kJumpTileW || ty < y || ty >= y + kJumpTileH) continue;
    const int16_t topH = static_cast<int16_t>(kJumpTileH * 2 / 3);
    zoneOut = (ty - y) < topH ? 0 : 1;
    return slot;
  }
  return -1;
}

static void hubNavigate(int idx) {
  jumpOpen = false;
  standbyIdleSinceMs = millis();
  const int page = hubTargetPage(deviceconfig::hubItems[idx].target);
  if (page >= 0 && page < kCarouselPages) {
    carouselPage = static_cast<uint8_t>(page);
    stage = Stage::Standby;
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ScreenSwitch));
  } else {
    // Unrecognized target (e.g. "vacuum" — no such screen exists here yet):
    // Settings is a safer fallback than silently doing nothing on tap.
    screen_settings::enter();
  }
}

static void drawHubGrid() {
  hubClampPage();
  const int start = hubPage * kHubPerPage;
  const int shown =
      deviceconfig::hubItemCount - start < kHubPerPage ? deviceconfig::hubItemCount - start : kHubPerPage;
  const bool qa = localsettings::quickActionsEnabled;
  if (hubFlashIdx >= 0 && millis() > hubFlashUntilMs) hubFlashIdx = -1;

  for (int slot = 0; slot < shown; ++slot) {
    const int gi = start + slot;
    const deviceconfig::HubItem& it = deviceconfig::hubItems[gi];
    int16_t x, y;
    jumpTilePos(slot, x, y);
    const bool hasAction = qa && it.actionType != deviceconfig::HubAction::None;
    const int16_t topH = hasAction ? static_cast<int16_t>(kJumpTileH * 2 / 3) : kJumpTileH;

    ui.strokeRect(x, y, kJumpTileW, kJumpTileH, 2, 16);
    // A custom icon picked for this button (admin UI's icon picker) wins
    // over the fixed glyph its `target` would otherwise show.
    const freeink::Icon* ic = mdiicon::hubIcons[gi] ? mdiicon::hubIcons[gi] : hubIconFor(it.target);
    if (ic)
      ui.icon(*ic, static_cast<int16_t>(x + (kJumpTileW - ic->w) / 2), static_cast<int16_t>(y + 14),
              Color::Black);
    ui.text(it.name, x, static_cast<int16_t>(y + 70), kJumpTileW, 24, TextAlign::Center, Color::Black,
            1, Ui::kFontSmall);
    const int page = hubTargetPage(it.target);
    if (page >= 0 && page == carouselPage && page < kCarouselPages)
      ui.text("now", x, static_cast<int16_t>(y + topH - 18), kJumpTileW, 16, TextAlign::Center,
              Color::DarkGray, 1, Ui::kFontSmall);

    if (hasAction) {
      const int16_t stripY = static_cast<int16_t>(y + topH);
      const int16_t stripH = static_cast<int16_t>(kJumpTileH - topH);
      drawDottedLine(static_cast<int16_t>(x + 8), stripY, static_cast<int16_t>(kJumpTileW - 16));
      const bool on = it.actionType == deviceconfig::HubAction::Toggle && hubToggleOn[gi];
      const bool filled = hubFlashIdx == gi || on;
      if (filled)
        ui.fillRect(x, static_cast<int16_t>(stripY + 1), kJumpTileW, static_cast<int16_t>(stripH - 1),
                    Color::Black, 14);
      const Color sfg = filled ? Color::White : Color::Black;
      const char* label = it.actionType == deviceconfig::HubAction::Toggle ? (on ? "On" : "Off") : "Run";
      ui.text(label, x, static_cast<int16_t>(stripY + (stripH - 20) / 2), kJumpTileW, 20,
              TextAlign::Center, sfg, 1, Ui::kFontSmall);
    }
  }

  if (hubPageCount() > 1) {
    char footer[24];
    snprintf(footer, sizeof(footer), "Page %d of %d", hubPage + 1, hubPageCount());
    ui.text(footer, 0, static_cast<int16_t>(Ui::H - 26), Ui::W, 20, TextAlign::Center, Color::DarkGray,
            1, Ui::kFontSmall);
  }
}

// `r` defaults to Full (screen entry / page turn); a hub action-strip tap's
// settle repaint below passes Fast instead — see the RefreshEvent table's
// policy (control feedback never flashes) at the top of this file, which
// applies here exactly as it does to every carousel page's own controls.
static void drawJumpList(Rf r = Rf::Full) {
  ui.clear();
  drawStatusBar(deviceconfig::hubItemCount > 0 ? "Quick Access" : "Jump to");
  if (deviceconfig::hubItemCount > 0) {
    drawHubGrid();
  } else {
    rebuildJumpVisible();
    if (jumpSel >= jumpVisibleCount) jumpSel = 0;
    for (int slot = 0; slot < jumpVisibleCount; ++slot) {
      const int i = jumpVisible[slot];
      int16_t x, y;
      jumpTilePos(slot, x, y);
      const bool sel = slot == jumpSel;
      const bool cur = i == carouselPage && i < kCarouselPages;
      if (sel) ui.fillRect(x, y, kJumpTileW, kJumpTileH, Color::Black, 16);
      else     ui.strokeRect(x, y, kJumpTileW, kJumpTileH, 2, 16);
      const Color fg = sel ? Color::White : Color::Black;
      const freeink::Icon* ic = jumpIcon(i);
      if (ic)
        ui.icon(*ic, static_cast<int16_t>(x + (kJumpTileW - ic->w) / 2), static_cast<int16_t>(y + 20),
                fg);
      ui.text(kJumpItems[i], x, static_cast<int16_t>(y + 76), kJumpTileW, 28, TextAlign::Center, fg);
      if (cur)
        ui.text("now", x, static_cast<int16_t>(y + kJumpTileH - 22), kJumpTileW, 18, TextAlign::Center,
                sel ? Color::LightGray : Color::DarkGray, 1, Ui::kFontSmall);
    }
  }
  commitFrame(r);
}

static void jumpTo(int i) {
  jumpOpen = false;
  standbyIdleSinceMs = millis();
  if (i >= 0 && i < kCarouselPages) {
    carouselPage = static_cast<uint8_t>(i);
    stage = Stage::Standby;
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ScreenSwitch));
  } else if (i == kJumpSettings) {
    screen_settings::enter();
  } else if (i == kJumpErrors) {
    screen_err_preview::enter();
  }
}

// ===========================================================================
// Input — Home tap on the carousel opens the overlay; while it's open,
// app/stages.h routes every tick here instead of to the carousel page.
// ===========================================================================
static void openQuickAccess() {
  input.suppressTouchContact();
  jumpOpen = true;
  standbyIdleSinceMs = millis();
  if (deviceconfig::hubItemCount > 0) {
    hubClampPage();
    hubLoadPageState();  // fetch this page's toggle strips as it opens
  } else {
    rebuildJumpVisible();
    jumpSel = jumpSlotFor(carouselPage);
  }
  drawJumpList();
}

// One carousel tick with the overlay open. (No idle-timer reset here — any
// real input already resets it, so an overlay left open still lets the
// device sleep.)
static void tickQuickAccess(const InFrame& in) {
  if (in.homeTap || in.btnPower) { jumpOpen = false; drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ScreenSwitch)); return; }

  // A background state fetch or action call finished — same
  // unconditional per-tick dirty-check convention as screen_shade's
  // own overlay right above, so it repaints whether or not this tick
  // also carried a tap.
  if (deviceconfig::hubItemCount > 0 && hubDirty && !ui.refreshBusy()) {
    hubDirty = false;
    drawJumpList(Rf::Fast);
  }

  if (deviceconfig::hubItemCount > 0) {
    // Hub mode: Left/Right PAGE the grid (like Xbox's library), not
    // move a highlight — the hub is tap-driven (dual zones), it has no
    // highlight-then-Power-to-activate cursor the legacy list below
    // still does.
    if (in.btnLeft)  { hubPrevPage(); drawJumpList(Rf::Full); return; }
    if (in.btnRight) { hubNextPage(); drawJumpList(Rf::Full); return; }
    if (in.tap) {
      int zone = 0;
      const int slot = hubZoneHit(in.tx, in.ty, zone);
      if (slot >= 0) {
        const int gi = hubPage * kHubPerPage + slot;
        if (gi < deviceconfig::hubItemCount) {
          const bool hasAction =
              localsettings::quickActionsEnabled &&
              deviceconfig::hubItems[gi].actionType != deviceconfig::HubAction::None;
          if (zone == 0 || !hasAction) {
            hubNavigate(gi);
          } else {
            hubFireAction(gi);
            drawJumpList(Rf::Fast);  // optimistic strip flip / flash, no flash-class refresh
          }
        }
      }
    }
    return;
  }

  // Legacy fixed grid: Left/Right move a highlight, tap (or Power on
  // the highlighted tile, same convention as Settings/Timeouts) jumps.
  if (in.btnLeft)  { jumpSel = (jumpSel + jumpVisibleCount - 1) % jumpVisibleCount; drawJumpList(Rf::Fast); return; }
  if (in.btnRight) { jumpSel = (jumpSel + 1) % jumpVisibleCount; drawJumpList(Rf::Fast); return; }
  if (in.tap) {
    const int slot = jumpHitTest(in.tx, in.ty);
    if (slot >= 0) jumpTo(jumpVisible[slot]);
  }
}
