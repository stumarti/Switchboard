#pragma once

// ===========================================================================
// screen_receiver — the Receiver carousel page: an Enigma2 satellite/cable
// box (Vu+, Dreambox, ...) through Home Assistant's enigma2 media_player
// (receiver.mediaPlayerEntity).
//
//   top      what's on: the channel (big) and the programme on it now
//   middle   up to six favourite channels as buttons (receiver.channels),
//            each with its optional MDI icon; the one on now is outlined
//            heavier. A tap is media_player.select_source.
//   volume   MUTE toggle, then VOL- / step blocks / VOL+ — the box reports a
//            real level (volume_level), so this is Music's absolute volume
//            row, not TV's relative keyevents
//   bar      CH- | POWER | CH+  (media_previous_track / toggle / next_track)
//
// Same command/readback pattern as Music: every press is queued on the
// network worker (app/net.h), then the box's state is re-read once the taps
// stop, and the page repaints with what it actually shows now.
// ===========================================================================

#include <string.h>

#include "screen_common.h"
#include "refresh_policy.h"
#include "app/input.h"
#include "app/net.h"
#include "screen_fwd.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"

namespace screen_receiver {

inline volatile uint8_t g_busy = 0;
// -1 none; 0/1/2 = CH- / POWER / CH+; 3/4 = VOL- / VOL+; 100+i = favourite i.
inline int g_pressed = -1;
enum class Act : uint8_t { ChannelDown, Power, ChannelUp, Volume, Mute, Favourite, Refresh };

// --- commands (network worker) --------------------------------------------
inline void readback(int) {
  const net::Ha a = net::ha();
  haclient::fetchReceiver(a.h, a.p, a.t, deviceconfig::receiverEntity);
}
inline void exec(const net::Command& c) {
  const char* e = deviceconfig::receiverEntity;
  if (!e[0]) return;
  const net::Ha a = net::ha();
  switch (static_cast<Act>(c.i)) {
    case Act::ChannelDown: haclient::callService(a.h, a.p, a.t, "media_player", "media_previous_track", e); break;
    case Act::ChannelUp:   haclient::callService(a.h, a.p, a.t, "media_player", "media_next_track", e); break;
    case Act::Power:       haclient::callService(a.h, a.p, a.t, "media_player", "toggle", e); break;
    // Volume / mute send the CURRENT optimistic value, so a drag or a run of
    // VOL taps coalesces into one call.
    case Act::Volume:      haclient::setMediaVolume(a.h, a.p, a.t, e, haclient::receiver.volumePct); break;
    case Act::Mute:        haclient::setMediaMute(a.h, a.p, a.t, e, haclient::receiver.muted); break;
    case Act::Favourite:   haclient::selectSource(a.h, a.p, a.t, e, c.s1); break;
    default: break;
  }
}
inline void kick(Act act, const char* source = nullptr) {
  net::Command c;
  c.exec = act == Act::Refresh ? nullptr : exec;
  c.readback = readback;
  c.busy = &g_busy;
  c.i = static_cast<int>(act);
  if (source) snprintf(c.s1, sizeof(c.s1), "%s", source);
  if (act == Act::Volume) c.coalesceKey = net::key("rxvol", deviceconfig::receiverEntity);
  if (act == Act::Mute)   c.coalesceKey = net::key("rxmute", deviceconfig::receiverEntity);
  if (act == Act::Refresh) {
    c.coalesceKey = net::key("rx?", deviceconfig::receiverEntity);
    c.optional = true;
  }
  net::post(c);
}

// --- now showing --------------------------------------------------------
inline constexpr int16_t kNowY = kStatusBarH + kPad;
inline constexpr int16_t kNowX = kShPad;
inline constexpr int16_t kNowW = Ui::W - kShPad * 2;

inline void drawNow() {
  const haclient::Receiver& r = haclient::receiver;
  ui.text(deviceconfig::receiverName, kNowX, kNowY, kNowW, 20, TextAlign::Left, Color::DarkGray, 1,
          Ui::kFontSmall);
  const bool off = r.ok && (!strcmp(r.state, "off") || !strcmp(r.state, "standby"));
  const char* channel = !r.ok ? "Unavailable" : off ? "Off" : r.channel[0] ? r.channel : "—";
  ui.text(channel, kNowX, static_cast<int16_t>(kNowY + 26), kNowW, 34, TextAlign::Left, Color::Black, 1,
          Ui::kFont28);
  if (r.ok && !off && r.programme[0])
    ui.text(r.programme, kNowX, static_cast<int16_t>(kNowY + 68), kNowW, 44, TextAlign::Left, Color::Black,
            2);
}

// --- favourite channels: 2 columns x 3 rows --------------------------------
inline constexpr int16_t kFavTop  = kNowY + 124;
inline constexpr int16_t kFavGap  = 10;
inline constexpr int16_t kFavW    = (Ui::W - kShPad * 2 - kFavGap) / 2;
inline constexpr int16_t kFavH    = 84;

inline void favPos(int i, int16_t& x, int16_t& y) {
  x = static_cast<int16_t>(kShPad + (i % 2) * (kFavW + kFavGap));
  y = static_cast<int16_t>(kFavTop + (i / 2) * (kFavH + kFavGap));
}
inline bool isOn(int i) {
  const haclient::Receiver& r = haclient::receiver;
  const deviceconfig::ReceiverChannel& ch = deviceconfig::receiverChannels[i];
  return r.ok && r.source[0] && !strcmp(r.source, ch.source);
}
inline void drawFavourites(int pressed) {
  const int n = deviceconfig::receiverChannelCount;
  if (n == 0) {
    ui.text("No favourite channels", 0, static_cast<int16_t>(kFavTop + kFavH), Ui::W, 22, TextAlign::Center,
            Color::DarkGray, 1, Ui::kFontSmall);
    return;
  }
  for (int i = 0; i < n; ++i) {
    int16_t x, y;
    favPos(i, x, y);
    const bool p = pressed == 100 + i;
    if (p) ui.fillRect(x, y, kFavW, kFavH, Color::Black, 14);
    else   ui.strokeRect(x, y, kFavW, kFavH, isOn(i) ? 5 : 2, 14);
    const Color fg = p ? Color::White : Color::Black;
    const freeink::Icon* ic = mdiicon::receiverIcons[i];
    int16_t textX = static_cast<int16_t>(x + 12);
    if (ic) {
      ui.icon(*ic, static_cast<int16_t>(x + 12), static_cast<int16_t>(y + (kFavH - ic->h) / 2), fg);
      textX = static_cast<int16_t>(x + 12 + ic->w + 10);
    }
    ui.text(deviceconfig::receiverChannels[i].name, textX, static_cast<int16_t>(y + (kFavH - 52) / 2),
            static_cast<int16_t>(x + kFavW - 10 - textX), 52, TextAlign::Left, fg, 2);
  }
}
inline int favHit(int16_t tx, int16_t ty) {
  for (int i = 0; i < deviceconfig::receiverChannelCount; ++i) {
    int16_t x, y;
    favPos(i, x, y);
    if (tx >= x && tx < x + kFavW && ty >= y && ty < y + kFavH) return 100 + i;
  }
  return -1;
}

// --- mute + volume (Music's absolute volume row) -----------------------------
inline constexpr int16_t kVolBtnSz  = 56;
inline constexpr int16_t kVolRowY   = kBarBtnY - 16 - kVolBtnSz;
inline constexpr int16_t kVolDownX  = kShPad;
inline constexpr int16_t kVolUpX    = Ui::W - kShPad - kVolBtnSz;
inline constexpr int16_t kVolBarH   = 28;
inline constexpr int16_t kVolBarX0  = kVolDownX + kVolBtnSz + 14;
inline constexpr int16_t kVolBarX1  = kVolUpX - 14;
inline constexpr int16_t kVolBarY   = kVolRowY + (kVolBtnSz - kVolBarH) / 2;
inline constexpr int kVolSteps = 10;
inline constexpr int16_t kVolBlockGap = 6;
inline constexpr int16_t kMuteRowH = 40;
inline constexpr int16_t kMuteY    = kVolRowY - 12 - kMuteRowH;
inline constexpr int16_t kMuteToggleW = 64, kMuteToggleH = 32;
inline constexpr int16_t kMuteToggleX = Ui::W - kShPad - kMuteToggleW;
inline constexpr int16_t kMuteToggleY = kMuteY + (kMuteRowH - kMuteToggleH) / 2;
static_assert(kFavTop + 3 * kFavH + 2 * kFavGap <= kMuteY, "favourites overlap the mute row");

inline void drawVolBtn(int col, bool pressed) {
  const int16_t x = col == 0 ? kVolDownX : kVolUpX;
  if (pressed) ui.fillRect(x, kVolRowY, kVolBtnSz, kVolBtnSz, Color::Black, 16);
  else         ui.strokeRect(x, kVolRowY, kVolBtnSz, kVolBtnSz, 2, 16);
  const freeink::Icon& ic = col == 0 ? icons::get("wx_music_vol_minus") : icons::get("wx_music_vol_plus");
  ui.icon(ic, static_cast<int16_t>(x + (kVolBtnSz - ic.w) / 2),
          static_cast<int16_t>(kVolRowY + (kVolBtnSz - ic.h) / 2), pressed ? Color::White : Color::Black);
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
inline void drawMuteRow() {
  const bool muted = haclient::receiver.muted;
  const int16_t iconY = static_cast<int16_t>(kMuteY + (kMuteRowH - 28) / 2);
  ui.icon(muted ? icons::get("wx_music_vol_off") : icons::get("wx_music_vol_on"), kShPad, iconY);
  ui.text("Mute", static_cast<int16_t>(kShPad + 38), static_cast<int16_t>(kMuteY + (kMuteRowH - 24) / 2), 150,
          24, TextAlign::Left, Color::Black);
  const uint8_t r = static_cast<uint8_t>(kMuteToggleH / 2);
  if (muted) ui.fillRect(kMuteToggleX, kMuteToggleY, kMuteToggleW, kMuteToggleH, Color::Black, r);
  else       ui.strokeRect(kMuteToggleX, kMuteToggleY, kMuteToggleW, kMuteToggleH, 2, r);
  const int16_t knobD = static_cast<int16_t>(kMuteToggleH - 8);
  const int16_t knobY = static_cast<int16_t>(kMuteToggleY + 4);
  const int16_t knobX = muted ? static_cast<int16_t>(kMuteToggleX + kMuteToggleW - knobD - 4)
                              : static_cast<int16_t>(kMuteToggleX + 4);
  ui.fillRect(knobX, knobY, knobD, knobD, muted ? Color::White : Color::Black, static_cast<uint8_t>(knobD / 2));
}
inline bool muteHit(int16_t tx, int16_t ty) {
  return tx >= kMuteToggleX && tx < kMuteToggleX + kMuteToggleW && ty >= kMuteToggleY &&
         ty < kMuteToggleY + kMuteToggleH;
}
inline bool volBarHit(int16_t tx, int16_t ty) {
  return tx >= kVolBarX0 && tx < kVolBarX1 && ty >= kVolBarY - 10 && ty < kVolBarY + kVolBarH + 10;
}
inline int pctFromX(int16_t tx) {
  const int32_t barW = kVolBarX1 - kVolBarX0;
  int32_t block = ((static_cast<int32_t>(tx - kVolBarX0) * kVolSteps) + barW / 2) / barW;
  block = block < 0 ? 0 : (block > kVolSteps ? kVolSteps : block);
  return static_cast<int>(block * 100 / kVolSteps);
}
// Returns whether the level moved (a stepper at its limit doesn't).
inline bool setVolumePct(int pct) {
  pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  if (haclient::receiver.hasVolume && pct == haclient::receiver.volumePct) return false;
  haclient::receiver.volumePct = pct;
  haclient::receiver.hasVolume = true;
  kick(Act::Volume);
  return true;
}

// --- bottom bar: CH- | POWER | CH+ ---------------------------------------------
inline void drawBar(int pressed) {
  drawActionBtn(0, pressed == 0, "CH -", /*font=*/0, 26);
  drawActionBtn(1, pressed == 1, "POWER", /*font=*/0, 26);
  drawActionBtn(2, pressed == 2, "CH +", /*font=*/0, 26);
  const int16_t gy = static_cast<int16_t>(kBarBtnY + 25);
  glyphTri(barBtnCx(0), gy, /*up=*/false, pressed == 0 ? Color::White : Color::Black, 4, 5);
  const freeink::Icon& pw = icons::get("wx_tv_power");
  ui.icon(pw, static_cast<int16_t>(barBtnCx(1) - pw.w / 2), static_cast<int16_t>(gy - pw.h / 2),
          pressed == 1 ? Color::White : Color::Black);
  glyphTri(barBtnCx(2), gy, /*up=*/true, pressed == 2 ? Color::White : Color::Black, 4, 5);
}

inline void draw(int pressed = -1) {
  if (!deviceconfig::receiverEntity[0]) {
    ui.text("Receiver", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text("not configured for this room", 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
    return;
  }
  drawNow();
  drawFavourites(pressed);
  drawMuteRow();
  drawVolBtn(0, pressed == 3);
  drawVolBtn(1, pressed == 4);
  drawVolBlocks(haclient::receiver.volumePct);
  drawBar(pressed);
}

// --- input ---------------------------------------------------------------
inline bool dragging = false;
inline bool handleDrag(const InFrame& in) {
  if (!deviceconfig::receiverEntity[0]) return false;
  if (in.touchPress && volBarHit(in.px, in.py)) dragging = true;
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

inline void pressFeedback(int id) {
  g_pressed = id;
  standbyIdleSinceMs = millis();
  drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/id);
}

inline bool handleTap(const InFrame& in) {
  if (!deviceconfig::receiverEntity[0]) return false;
  const int fav = favHit(in.tx, in.ty);
  if (fav >= 100) {
    kick(Act::Favourite, deviceconfig::receiverChannels[fav - 100].source);
    pressFeedback(fav);
    return true;
  }
  if (muteHit(in.tx, in.ty)) {
    haclient::receiver.muted = !haclient::receiver.muted;
    kick(Act::Mute);
    standbyIdleSinceMs = millis();
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
    return true;
  }
  if (in.ty >= kVolRowY && in.ty < kVolRowY + kVolBtnSz) {
    int col = -1;
    if (in.tx >= kVolDownX && in.tx < kVolDownX + kVolBtnSz) col = 0;
    else if (in.tx >= kVolUpX && in.tx < kVolUpX + kVolBtnSz) col = 1;
    if (col >= 0) {
      setVolumePct(haclient::receiver.volumePct + (col == 0 ? -10 : 10));
      g_pressed = drawStepperFeedback(3 + col);  // pressed only if it couldn't move
      return true;
    }
  }
  if (volBarHit(in.tx, in.ty)) {
    setVolumePct(pctFromX(in.tx));
    standbyIdleSinceMs = millis();
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
    return true;
  }
  if (in.ty >= kBarBtnY) {
    const int col = in.tx < Ui::W / 3 ? 0 : in.tx < Ui::W * 2 / 3 ? 1 : 2;
    kick(col == 0 ? Act::ChannelDown : col == 1 ? Act::Power : Act::ChannelUp);
    pressFeedback(col);
    return true;
  }
  return false;
}

// Same settle rule as every page: repaint once this page's commands and
// their re-read have landed, dropping the pressed style.
inline bool settleCheck(bool showing) {
  static bool prevBusy = false;
  const bool busy = g_busy;
  const bool settle = showing && ((prevBusy && !busy) || (g_pressed >= 0 && !busy));
  prevBusy = busy;
  if (settle) g_pressed = -1;
  return settle;
}

}  // namespace screen_receiver
