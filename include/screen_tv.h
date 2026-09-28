#pragma once

// ===========================================================================
// screen_tv — the TV carousel page: a D-pad talking to tv.remoteEntity's
// remote.send_command (the Android TV Remote integration's own keyevent
// vocabulary), a single row of app-launch icons talking to
// tv.mediaPlayerEntity's media_player.play_media (content type "app" — see
// haclient::launchApp), a VOL-/step-block/VOL+
// row + MUTE toggle (same widget and placement as Music's volume row, just
// above the transport bar), and a BACK/HOME/POWER bar at the very bottom.
//
// Every button here is fire-and-forget (no state to read back, unlike
// Music's play/pause/volume level) — one HTTP POST per tap, same
// background-task pattern as the other pages so a slow/unreachable HA call
// never blocks the UI thread. The volume row is the one exception worth
// calling out: the Android TV remote entity only exposes relative
// VOLUME_UP/VOLUME_DOWN/MUTE keyevents, not a readable level the way Music's
// media_player has one, so g_volPct below is a local, optimistic indicator
// (starts at 50, nudged by whichever block you tap/drag to) rather than a
// value fetched from Home Assistant — each move still fires the matching
// number of real keyevents underneath.
// ===========================================================================

#include <strings.h>

#include "screen_common.h"
#include "refresh_policy.h"
#include "app/input.h"
#include "app/net.h"
#include "screen_fwd.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"

namespace screen_tv {

// Commands in flight (app/net.h). TV commands have no state to read back.
inline volatile uint8_t g_busy = 0;
// -1 none; 0-4 = dpad (up,left,ok,right,down); 5-7 = bottom bar
// (back,home,power); 8 = mute toggle; 9/10 = vol-/vol+; 100+i = app icon i.
inline int g_pressed = -1;

// Local-only optimistic volume indicator — see the header note above.
inline int g_volPct = 50;
// Signed relative keyevents not yet sent. Taps and drags ADD to it; the one
// queued Volume command sends whatever has accumulated when it runs, so a
// fast run of VOL taps is never lost and never sent twice.
inline int g_volSteps = 0;
inline bool g_muted = false;

// --- commands (run on the network worker, app/net.h) -------------------
inline void execKey(const net::Command& c) {
  const net::Ha a = net::ha();
  if (c.s1[0]) haclient::sendRemoteCommand(a.h, a.p, a.t, deviceconfig::tvRemoteEntity, c.s1);
}
inline void execApp(const net::Command& c) {
  if (c.i < 0 || c.i >= deviceconfig::tvAppCount) return;
  const net::Ha a = net::ha();
  haclient::launchApp(a.h, a.p, a.t, deviceconfig::tvMediaEntity, deviceconfig::tvApps[c.i].pkg);
}
inline void execVolume(const net::Command&) {
  portENTER_CRITICAL(&net::g_mux);
  const int steps = g_volSteps;
  g_volSteps = 0;
  portEXIT_CRITICAL(&net::g_mux);
  const char* cmd = steps > 0 ? "VOLUME_UP" : "VOLUME_DOWN";
  const int n = steps > 0 ? steps : -steps;
  const net::Ha a = net::ha();
  for (int i = 0; i < n; ++i) {
    haclient::sendRemoteCommand(a.h, a.p, a.t, deviceconfig::tvRemoteEntity, cmd);
    if (i + 1 < n) delay(150);  // let the TV register discrete keyevents
  }
}

inline void postTv(net::ExecFn exec, uint32_t coalesceKey = 0) {
  net::Command c;
  c.exec = exec;
  c.busy = &g_busy;
  c.coalesceKey = coalesceKey;
  net::post(c);
}
// A D-pad / BACK / HOME / POWER key: every press is sent, in order.
inline void kickCommand(const char* cmd) {
  net::Command c;
  c.exec = execKey;
  c.busy = &g_busy;
  snprintf(c.s1, sizeof(c.s1), "%s", cmd);
  net::post(c);
}
inline void kickApp(int idx) {
  net::Command c;
  c.exec = execApp;
  c.busy = &g_busy;
  c.i = idx;
  net::post(c);
}

// --- D-pad: a plus-shape in a 3x3 grid (corners empty) --------------------
inline constexpr int16_t kDpadBtn  = 95;  // bigger tap target + OK/arrow glyphs
inline constexpr int16_t kDpadGap  = 8;
inline constexpr int16_t kDpadSize = kDpadBtn * 3 + kDpadGap * 2;
inline constexpr int16_t kDpadTop  = kStatusBarH + kPad + 20;
inline constexpr int16_t kDpadLeft = (Ui::W - kDpadSize) / 2;

struct DpadCell { int8_t col, row; };
// order: up, left, ok, right, down
inline constexpr DpadCell kDpadCells[5] = {{1, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}};
inline constexpr const char* kDpadCmds[5] = {"DPAD_UP", "DPAD_LEFT", "DPAD_CENTER", "DPAD_RIGHT",
                                             "DPAD_DOWN"};

inline void dpadCellPos(int i, int16_t& x, int16_t& y) {
  x = static_cast<int16_t>(kDpadLeft + kDpadCells[i].col * (kDpadBtn + kDpadGap));
  y = static_cast<int16_t>(kDpadTop + kDpadCells[i].row * (kDpadBtn + kDpadGap));
}
// A small sideways triangle (left/right), same band-drawing technique as the
// shared glyphTri() (screen_common.h) — that one only does up/down. `colW`/
// `heightStep` scale the glyph; defaults match the original small dpad size.
inline void glyphTriH(int16_t cx, int16_t cy, bool right, Color c, int16_t colW = 3,
                      int16_t heightStep = 4) {
  for (int i = 0; i < 6; ++i) {
    // A right-pointing glyph tapers to its tip on the right (tall base on the
    // left, shrinking as i/x increases); left-pointing is the mirror.
    const int16_t rh = static_cast<int16_t>((right ? 6 - i : i + 1) * heightStep);
    const int16_t xx = static_cast<int16_t>(cx - 3 * colW + i * colW);
    ui.fillRect(xx, static_cast<int16_t>(cy - rh / 2), colW, rh, c);
  }
}
inline void drawDpad(int pressed) {
  for (int i = 0; i < 5; ++i) {
    int16_t x, y;
    dpadCellPos(i, x, y);
    const bool p = i == pressed;
    if (p) ui.fillRect(x, y, kDpadBtn, kDpadBtn, Color::Black, 14);
    else   ui.strokeRect(x, y, kDpadBtn, kDpadBtn, 2, 14);
    const Color fg = p ? Color::White : Color::Black;
    const int16_t cx = static_cast<int16_t>(x + kDpadBtn / 2), cy = static_cast<int16_t>(y + kDpadBtn / 2);
    switch (i) {
      case 0: glyphTri(cx, cy, /*up=*/true, fg, 5, 6); break;
      case 1: glyphTriH(cx, cy, /*right=*/false, fg, 5, 6); break;
      case 2: ui.text("OK", x, static_cast<int16_t>(cy - 14), kDpadBtn, 28, TextAlign::Center, fg, 1,
                      Ui::kFont28); break;
      case 3: glyphTriH(cx, cy, /*right=*/true, fg, 5, 6); break;
      case 4: glyphTri(cx, cy, /*up=*/false, fg, 5, 6); break;
    }
  }
}
inline int dpadHit(int16_t tx, int16_t ty) {
  for (int i = 0; i < 5; ++i) {
    int16_t x, y;
    dpadCellPos(i, x, y);
    if (tx >= x && tx < x + kDpadBtn && ty >= y && ty < y + kDpadBtn) return i;
  }
  return -1;
}

// --- bottom bar: BACK | HOME | POWER (same slot as Music's transport bar) -
inline constexpr const char* kBarCmds[3] = {"BACK", "HOME", "POWER"};
inline constexpr int16_t kBarLabelH = 26;  // bigger label, matches Music's transport bar

inline void drawBar(int pressed) {
  drawActionBtn(0, pressed == 5, "BACK", /*font=*/0, kBarLabelH);
  drawActionBtn(1, pressed == 6, "HOME", /*font=*/0, kBarLabelH);
  drawActionBtn(2, pressed == 7, "POWER", /*font=*/0, kBarLabelH);
  const int16_t gy = static_cast<int16_t>(kBarBtnY + 25);
  ui.icon(icons::get("wx_tv_back"), static_cast<int16_t>(barBtnCx(0) - icons::get("wx_tv_back").w / 2),
          static_cast<int16_t>(gy - icons::get("wx_tv_back").h / 2), pressed == 5 ? Color::White : Color::Black);
  ui.icon(icons::get("wx_tv_home"), static_cast<int16_t>(barBtnCx(1) - icons::get("wx_tv_home").w / 2),
          static_cast<int16_t>(gy - icons::get("wx_tv_home").h / 2), pressed == 6 ? Color::White : Color::Black);
  ui.icon(icons::get("wx_tv_power"), static_cast<int16_t>(barBtnCx(2) - icons::get("wx_tv_power").w / 2),
          static_cast<int16_t>(gy - icons::get("wx_tv_power").h / 2), pressed == 7 ? Color::White : Color::Black);
}

// --- volume row: VOL- icon | step blocks | VOL+ icon, same widget/placement
// as Music's volume row (just above the transport bar) -------------------
inline constexpr int16_t kVolBtnSz  = 56;
inline constexpr int16_t kVolRowGap = 16;  // above the transport bar
inline constexpr int16_t kVolRow2Y  = kBarBtnY - kVolRowGap - kVolBtnSz;
inline constexpr int16_t kVolDownX  = kShPad;
inline constexpr int16_t kVolUpX    = Ui::W - kShPad - kVolBtnSz;
inline constexpr int16_t kVolBarH   = 28;
inline constexpr int16_t kVolBarX0  = kVolDownX + kVolBtnSz + 14;
inline constexpr int16_t kVolBarX1  = kVolUpX - 14;
inline constexpr int16_t kVolBarY   = kVolRow2Y + (kVolBtnSz - kVolBarH) / 2;
inline constexpr int kVolSteps = 10;
inline constexpr int16_t kVolBlockGap = 6;

inline int16_t volBtnX(int col) { return col == 0 ? kVolDownX : kVolUpX; }
inline void drawVolBtn(int col, bool pressed) {
  const int16_t x = volBtnX(col);
  if (pressed) ui.fillRect(x, kVolRow2Y, kVolBtnSz, kVolBtnSz, Color::Black, 16);
  else         ui.strokeRect(x, kVolRow2Y, kVolBtnSz, kVolBtnSz, 2, 16);
  const freeink::Icon& ic = col == 0 ? icons::get("wx_music_vol_minus") : icons::get("wx_music_vol_plus");
  ui.icon(ic, static_cast<int16_t>(x + (kVolBtnSz - ic.w) / 2),
          static_cast<int16_t>(kVolRow2Y + (kVolBtnSz - ic.h) / 2), pressed ? Color::White : Color::Black);
}
inline void drawVolBlocks(int pct) {
  const int16_t barW = static_cast<int16_t>(kVolBarX1 - kVolBarX0);
  const int16_t blockW = static_cast<int16_t>((barW - (kVolSteps - 1) * kVolBlockGap) / kVolSteps);
  const int filled = (pct * kVolSteps + 50) / 100;
  int16_t x = kVolBarX0;
  for (int i = 0; i < kVolSteps; ++i) {
    if (i < filled) ui.fillRect(x, kVolBarY, blockW, kVolBarH, Color::Black, 6);
    else            ui.strokeRect(x, kVolBarY, blockW, kVolBarH, 2, 6);
    x = static_cast<int16_t>(x + blockW + kVolBlockGap);
  }
}
inline bool barHit(int16_t tx, int16_t ty) {
  return tx >= kVolBarX0 && tx < kVolBarX1 && ty >= kVolBarY - 10 && ty < kVolBarY + kVolBarH + 10;
}
inline bool dragging = false;
// Touch anywhere in the row -> nearest block boundary, same convention as
// Music's pctFromX.
inline int pctFromX(int16_t tx) {
  const int32_t barW = kVolBarX1 - kVolBarX0;
  int32_t block = ((static_cast<int32_t>(tx - kVolBarX0) * kVolSteps) + barW / 2) / barW;
  block = block < 0 ? 0 : (block > kVolSteps ? kVolSteps : block);
  return static_cast<int>(block * 100 / kVolSteps);
}
inline void setVolumePct(int pct) {
  const int from = g_volPct;
  const int to = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  const int steps = (to - from) / 10;
  if (steps == 0) return;
  g_volPct = from + steps * 10;  // keep the indicator on the keyevents actually sent
  portENTER_CRITICAL(&net::g_mux);
  g_volSteps += steps;
  portEXIT_CRITICAL(&net::g_mux);
  postTv(execVolume, net::key("tvvol"));
}
inline void adjustVolume(int dir) { setVolumePct(g_volPct + dir * 10); }
// MUTE is a toggle keyevent on the TV: every press is sent.
inline void toggleMute() {
  g_muted = !g_muted;
  net::Command c;
  c.exec = execKey;
  c.busy = &g_busy;
  snprintf(c.s1, sizeof(c.s1), "%s", "MUTE");
  net::post(c);
}

// --- mute row: speaker icon + label + toggle, directly above the volume
// row (mirrors Music's row-1 toggle, just relocated next to the row it
// controls instead of a page-top name row TV has no equivalent of) --------
inline constexpr int16_t kMuteRowH    = 40;
inline constexpr int16_t kMuteGap     = 12;  // above the volume row
inline constexpr int16_t kMuteY       = kVolRow2Y - kMuteGap - kMuteRowH;
inline constexpr int16_t kMuteIconSz  = 28;
inline constexpr int16_t kMuteToggleW = 64, kMuteToggleH = 32;
inline constexpr int16_t kMuteToggleX = Ui::W - kShPad - kMuteToggleW;
inline constexpr int16_t kMuteToggleY = kMuteY + (kMuteRowH - kMuteToggleH) / 2;

inline void drawMuteToggle(bool on) {
  const uint8_t r = static_cast<uint8_t>(kMuteToggleH / 2);
  if (on) ui.fillRect(kMuteToggleX, kMuteToggleY, kMuteToggleW, kMuteToggleH, Color::Black, r);
  else    ui.strokeRect(kMuteToggleX, kMuteToggleY, kMuteToggleW, kMuteToggleH, 2, r);
  const int16_t knobD = static_cast<int16_t>(kMuteToggleH - 8);
  const int16_t knobY = static_cast<int16_t>(kMuteToggleY + (kMuteToggleH - knobD) / 2);
  const int16_t knobX = on ? static_cast<int16_t>(kMuteToggleX + kMuteToggleW - knobD - 4)
                           : static_cast<int16_t>(kMuteToggleX + 4);
  ui.fillRect(knobX, knobY, knobD, knobD, on ? Color::White : Color::Black,
              static_cast<uint8_t>(knobD / 2));
}
inline bool muteToggleHit(int16_t tx, int16_t ty) {
  return tx >= kMuteToggleX && tx < kMuteToggleX + kMuteToggleW && ty >= kMuteToggleY &&
         ty < kMuteToggleY + kMuteToggleH;
}
inline void drawMuteRow(bool muted) {
  const int16_t iconY = static_cast<int16_t>(kMuteY + (kMuteRowH - kMuteIconSz) / 2);
  ui.icon(muted ? icons::get("wx_music_vol_off") : icons::get("wx_music_vol_on"), kShPad, iconY);
  ui.text("Mute", static_cast<int16_t>(kShPad + kMuteIconSz + 10),
          static_cast<int16_t>(kMuteY + (kMuteRowH - 24) / 2), 150, 24, TextAlign::Left, Color::Black);
  drawMuteToggle(muted);
}

// --- app launcher: one icon button per configured app, in a single row,
// placed at the bottom of the page (above the volume row) instead of the
// old 2-column text-chip grid -------------------------------------------
inline constexpr int16_t kAppBtnH = 76;
inline constexpr int16_t kAppGap  = 16;  // above the mute row
inline constexpr int16_t kAppsY   = kMuteY - kAppGap - kAppBtnH;

inline int16_t appBtnW(int n) {
  return static_cast<int16_t>((Ui::W - kShPad * 2 - (n - 1) * kAppGap) / (n > 0 ? n : 1));
}
inline int16_t appBtnX(int i, int n) {
  return static_cast<int16_t>(kShPad + i * (appBtnW(n) + kAppGap));
}
// The icon picked for the app in the admin UI (mdi_icon.h), else: named
// apps get their brand glyph, and anything else a generic apps glyph.
inline const freeink::Icon& appIcon(int i) {
  if (i >= 0 && i < deviceconfig::kMaxTvApps && mdiicon::tvAppIcons[i]) return *mdiicon::tvAppIcons[i];
  const char* name = deviceconfig::tvApps[i].name;
  if (name && *name) {
    if (strcasecmp(name, "netflix") == 0) return icons::get("wx_tv_app_netflix");
    if (strcasecmp(name, "youtube") == 0) return icons::get("wx_tv_app_youtube");
  }
  return icons::get("wx_tv_app_generic");
}
inline void drawAppRow(int pressed) {
  const int n = deviceconfig::tvAppCount;
  if (n == 0) return;
  const int16_t w = appBtnW(n);
  for (int i = 0; i < n; ++i) {
    const int16_t x = appBtnX(i, n);
    const bool p = pressed == 100 + i;
    if (p) ui.fillRect(x, kAppsY, w, kAppBtnH, Color::Black, 16);
    else   ui.strokeRect(x, kAppsY, w, kAppBtnH, 2, 16);
    const Color fg = p ? Color::White : Color::Black;
    const freeink::Icon& ic = appIcon(i);
    ui.icon(ic, static_cast<int16_t>(x + (w - ic.w) / 2), static_cast<int16_t>(kAppsY + 10), fg);
    ui.text(deviceconfig::tvApps[i].name, static_cast<int16_t>(x + 4),
            static_cast<int16_t>(kAppsY + 10 + ic.h + 6), static_cast<int16_t>(w - 8), 20,
            TextAlign::Center, fg, 1, Ui::kFontSmall);
  }
}
// Returns 100+i for app button i, or -1.
inline int appsHit(int16_t tx, int16_t ty) {
  const int n = deviceconfig::tvAppCount;
  if (n == 0 || ty < kAppsY || ty >= kAppsY + kAppBtnH) return -1;
  const int16_t w = appBtnW(n);
  for (int i = 0; i < n; ++i) {
    const int16_t x = appBtnX(i, n);
    if (tx >= x && tx < x + w) return 100 + i;
  }
  return -1;
}

inline void draw(int pressed = -1) {
  if (!deviceconfig::tvRemoteEntity[0] && !deviceconfig::tvMediaEntity[0]) {
    ui.text("TV", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text("not configured for this room", 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
    return;
  }
  drawDpad(pressed >= 0 && pressed < 5 ? pressed : -1);
  drawAppRow(pressed);
  drawMuteRow(g_muted);
  drawVolBtn(0, pressed == 9);
  drawVolBtn(1, pressed == 10);
  drawVolBlocks(g_volPct);
  drawBar(pressed);
}

// --- input (the carousel dispatches here while this page is showing) --

// TV page: same drag convention for its volume row.
inline bool handleDrag(const InFrame& in) {
  if (in.touchPress && barHit(in.px, in.py)) dragging = true;
  if (dragging) {
    if (in.touchHeld) {
      setVolumePct(pctFromX(in.hx));
      standbyIdleSinceMs = millis();
      drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::Drag));
    } else {
      dragging = false;
    }
    return true;
  }
  return false;
}

// TV page: D-pad, MUTE toggle, VOL-/VOL+/tap-to-set volume row, an app
// icon, then the BACK/HOME/POWER bar — every button is a one-shot
// remote/media_player call, no state to read back (unlike Music's
// play/pause/volume level).
inline bool handleTap(const InFrame& in) {
  const int dp = dpadHit(in.tx, in.ty);
  if (dp >= 0) {
    kickCommand(kDpadCmds[dp]);
    g_pressed = dp;
    standbyIdleSinceMs = millis();
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/dp);
    return true;
  }
  if (muteToggleHit(in.tx, in.ty)) {
    toggleMute();
    standbyIdleSinceMs = millis();
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
    return true;
  }
  if (in.ty >= kVolRow2Y && in.ty < kVolRow2Y + kVolBtnSz) {
    int col = -1;
    if (in.tx >= kVolDownX && in.tx < kVolDownX + kVolBtnSz)
      col = 0;
    else if (in.tx >= kVolUpX && in.tx < kVolUpX + kVolBtnSz)
      col = 1;
    if (col >= 0) {
      adjustVolume(col == 0 ? -1 : +1);
      g_pressed = drawStepperFeedback(9 + col);
      return true;
    }
  }
  if (barHit(in.tx, in.ty)) {
    setVolumePct(pctFromX(in.tx));
    standbyIdleSinceMs = millis();
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
    return true;
  }
  const int ap = appsHit(in.tx, in.ty);
  if (ap >= 100) {
    kickApp(ap - 100);
    g_pressed = ap;
    standbyIdleSinceMs = millis();
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/ap);
    return true;
  }
  if (in.ty >= kBarBtnY) {
    const int col = in.tx < Ui::W / 3 ? 0 : in.tx < Ui::W * 2 / 3 ? 1 : 2;
    kickCommand(kBarCmds[col]);
    g_pressed = 5 + col;
    standbyIdleSinceMs = millis();
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/5 + col);
    return true;
  }
  return false;
}

// Once this page's action task settles (or its kick was a no-op), the page
// repaints with the confirmed state and drops any pressed-button style —
// always RefreshEvent::TapFeedback (Fast): a flashing scrub after every tap
// reads as the whole page "reloading". commitFrame()'s kCleanEvery still
// promotes one of these to a Half scrub periodically, so ghosting (even on
// dense dithered art) doesn't build up.
//
// Call every non-tap carousel tick, whichever page is showing, so the busy
// edge is tracked continuously; returns true only for the showing page.
inline bool settleCheck(bool showing) {
  static bool prevBusy = false;
  const bool busy = g_busy;
  const bool settle = showing && ((prevBusy && !busy) || (g_pressed >= 0 && !busy));
  prevBusy = busy;
  if (settle) g_pressed = -1;
  return settle;
}

}  // namespace screen_tv
