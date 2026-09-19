#pragma once

// ===========================================================================
// screen_music — the Music carousel page: a media_player entity's mute
// status + name + mute toggle, the current title/artist, a VOL- / step-block
// volume row / VOL+ pinned just above the PREV / PLAY-PAUSE / NEXT transport
// bar (within thumb reach of it), and that transport bar itself. Mirrors
// screen_climate/screen_blinds/screen_lighting's optimistic-update +
// background-task pattern.
//
// The volume row is drawn as kMuSteps discrete blocks (solid = filled, an
// outline = empty) rather than a continuously-filled bar — a stepped on/off
// region redraws as clean solid rectangles under e-ink's 1-bit dithering,
// where a partial-pixel bar fill doesn't. Tapping (or dragging) anywhere in
// the row snaps to the nearest block, same "anywhere sets the value"
// convention as Lighting's brightness bar.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"
#include "album_art.h"

namespace screen_music {

inline volatile bool g_busy = false;
// -1 none, 0/1/2 = PREV/PLAY-PAUSE/NEXT (bottom bar), 3/4 = VOL-/VOL+.
inline int g_pressed = -1;
enum class Act : uint8_t { PlayPause, Next, Prev, Volume, Mute, Refresh };
inline Act g_act = Act::Refresh;

inline void task(void*) {
  g_busy = true;
  ensureMdns();
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  const char* e = deviceconfig::mediaEntity;
  if (globalsclient::ok && e && *e) {
    switch (g_act) {
      case Act::PlayPause: haclient::callService(h, p, t, "media_player", "media_play_pause", e); break;
      case Act::Next:      haclient::callService(h, p, t, "media_player", "media_next_track", e); break;
      case Act::Prev:      haclient::callService(h, p, t, "media_player", "media_previous_track", e); break;
      case Act::Volume:    haclient::setMediaVolume(h, p, t, e, haclient::media.volumePct); break;
      case Act::Mute:      haclient::setMediaMute(h, p, t, e, haclient::media.muted); break;
      default: break;
    }
    if (g_act != Act::Refresh) delay(400);  // let HA apply before we read back
    haclient::fetchMedia(h, p, t, e);

    // Album art: only worth a fetch (a JPEG download + decode) when the
    // track actually changed — entity_picture's URL changes with it. Not
    // playing / no art -> drop whatever we were showing rather than let it
    // go stale.
    if (haclient::media.ok && haclient::media.picture[0]) {
      if (strcmp(haclient::media.picture, albumart::g_sourceUrl) != 0)
        albumart::fetch(h, p, t, haclient::media.picture);
    } else {
      albumart::clear();
    }
  }
  g_busy = false;
  vTaskDelete(nullptr);
}

inline void kick(Act act) {
  if (g_busy || g_weatherBusy) return;
  g_act = act;
  g_busy = true;
  // A bigger stack than the other pages' tasks: album-art fetches decode a
  // JPEG (TJpg_Decoder's own workspace lives on the heap, but the HTTP
  // client + local buffers here are meaningfully heavier than a plain HA
  // state GET).
  if (xTaskCreatePinnedToCore(task, "sb_media", 16384, nullptr, 1, nullptr, 1) != pdPASS)
    g_busy = false;
}

// PLAY/PAUSE toggles optimistically between the two states (media_player has
// no single "play_pause" state to read back until the next fetch lands).
inline void togglePlay() {
  haclient::MediaPlayer& m = haclient::media;
  snprintf(m.state, sizeof(m.state), "%s", strcmp(m.state, "playing") == 0 ? "paused" : "playing");
  kick(Act::PlayPause);
}
inline void next() { kick(Act::Next); }
inline void prev() { kick(Act::Prev); }

// VOL- (-1) / VOL+ (+1): step the volume 10% (optimistic), then POST.
inline void adjustVolume(int dir) {
  haclient::MediaPlayer& m = haclient::media;
  if (!m.hasVolume) return;
  int v = m.volumePct + dir * 10;
  m.volumePct = v < 0 ? 0 : (v > 100 ? 100 : v);
  kick(Act::Volume);
}
inline void toggleMute() {
  haclient::media.muted = !haclient::media.muted;
  kick(Act::Mute);
}

// --- layout -----------------------------------------------------------
inline constexpr int16_t kMuX = 20;
inline constexpr int16_t kMuW = Ui::W - 40;
inline constexpr int16_t kMuY = kStatusBarH + kPad + 8;

// Row 1: speaker icon (filled "high" when unmuted, "off" when muted — the
// page's own state indicator, mirroring the Lighting card's bulb) + the
// configured player name + a MUTE toggle switch on the right.
inline constexpr int16_t kMuIconSz  = 36;
inline constexpr int16_t kMuTextX   = kMuX + kMuIconSz + 10;
inline constexpr int16_t kMuToggleW = 64;
inline constexpr int16_t kMuToggleH = 32;
inline constexpr int16_t kMuToggleX = kMuX + kMuW - kMuToggleW;
inline constexpr int16_t kMuToggleY = kMuY + (kMuIconSz - kMuToggleH) / 2;

// Volume row: VOL- icon button | step-block volume row | VOL+ icon button —
// pinned just above the PREV/PLAY-PAUSE/NEXT transport bar (rather than up
// under the name row) so it's within the same thumb reach as the transport
// controls.
inline constexpr int16_t kMuBtnSz = 56;
inline constexpr int16_t kMuRowGap = 16;  // above the transport bar
inline constexpr int16_t kMuRow2Y = kBarBtnY - kMuRowGap - kMuBtnSz;
inline constexpr int16_t kMuDownX = kMuX;
inline constexpr int16_t kMuUpX   = kMuX + kMuW - kMuBtnSz;
inline constexpr int16_t kMuBarH  = 28;
inline constexpr int16_t kMuBarX0 = kMuDownX + kMuBtnSz + 14;
inline constexpr int16_t kMuBarX1 = kMuUpX - 14;
inline constexpr int16_t kMuBarY  = kMuRow2Y + (kMuBtnSz - kMuBarH) / 2;
// The row is drawn as this many discrete blocks (solid = filled, outline =
// empty) instead of a continuously-filled bar — cleaner under e-ink's 1-bit
// dithering than a partial-pixel fill, and each tap/drag position snaps to
// the nearest block (see pctFromX).
inline constexpr int kMuSteps = 10;
inline constexpr int16_t kMuBlockGap = 6;

// Album art (when available) + now-playing title/artist, vertically
// centered as one group in the air between the name row and the volume row
// rather than clinging to either.
inline constexpr int16_t kNowY0 = kMuY + kMuIconSz + 16;
inline constexpr int16_t kArtGap = 26;       // between artwork and title
inline constexpr int16_t kTitleH = 34;
inline constexpr int16_t kTitleArtistGap = 6;
inline constexpr int16_t kArtistH = 24;

inline void drawToggle(int16_t x, int16_t y, bool on) {
  const uint8_t r = static_cast<uint8_t>(kMuToggleH / 2);
  if (on) ui.fillRect(x, y, kMuToggleW, kMuToggleH, Color::Black, r);
  else    ui.strokeRect(x, y, kMuToggleW, kMuToggleH, 2, r);
  const int16_t knobD = static_cast<int16_t>(kMuToggleH - 8);
  const int16_t knobY = static_cast<int16_t>(y + (kMuToggleH - knobD) / 2);
  const int16_t knobX = on ? static_cast<int16_t>(x + kMuToggleW - knobD - 4)
                           : static_cast<int16_t>(x + 4);
  ui.fillRect(knobX, knobY, knobD, knobD, on ? Color::White : Color::Black,
              static_cast<uint8_t>(knobD / 2));
}
inline bool toggleHit(int16_t tx, int16_t ty) {
  return tx >= kMuToggleX && tx < kMuToggleX + kMuToggleW && ty >= kMuToggleY &&
         ty < kMuToggleY + kMuToggleH;
}

inline bool barHit(int16_t tx, int16_t ty) {
  return tx >= kMuBarX0 && tx < kMuBarX1 && ty >= kMuBarY - 10 && ty < kMuBarY + kMuBarH + 10;
}
// A finger down inside the bar drags the volume live, same convention as
// Lighting's brightness slider / the control shade's own drag pattern.
inline bool dragging = false;
// Touch anywhere in the row sets the volume — snapped to the nearest of the
// kMuSteps block boundaries (0, 10, 20, ... 100) that draw() renders, so what
// you tap is exactly the state you see, not a value one step off from it.
inline int pctFromX(int16_t tx) {
  const int32_t barW = kMuBarX1 - kMuBarX0;
  int32_t block = ((static_cast<int32_t>(tx - kMuBarX0) * kMuSteps) + barW / 2) / barW;
  block = block < 0 ? 0 : (block > kMuSteps ? kMuSteps : block);
  return static_cast<int>(block * 100 / kMuSteps);
}
inline void setVolumePct(int pct) {
  haclient::MediaPlayer& m = haclient::media;
  if (!m.hasVolume) return;
  m.volumePct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  kick(Act::Volume);
}

// col 0 = VOL- (left), col 1 = VOL+ (right).
inline int16_t volBtnX(int col) { return col == 0 ? kMuDownX : kMuUpX; }
inline void drawVolBtn(int col, bool pressed) {
  const int16_t x = volBtnX(col);
  if (pressed) ui.fillRect(x, kMuRow2Y, kMuBtnSz, kMuBtnSz, Color::Black, 16);
  else         ui.strokeRect(x, kMuRow2Y, kMuBtnSz, kMuBtnSz, 2, 16);
  const freeink::Icon& ic = col == 0 ? kWx_music_vol_minus : kWx_music_vol_plus;
  ui.icon(ic, static_cast<int16_t>(x + (kMuBtnSz - ic.w) / 2),
          static_cast<int16_t>(kMuRow2Y + (kMuBtnSz - ic.h) / 2), pressed ? Color::White : Color::Black);
}

// kMuSteps solid/outline blocks spanning kMuBarX0..kMuBarX1, filled up to the
// nearest step for the current volume.
inline void drawVolBlocks(int volumePct) {
  const int16_t barW = static_cast<int16_t>(kMuBarX1 - kMuBarX0);
  const int16_t blockW = static_cast<int16_t>((barW - (kMuSteps - 1) * kMuBlockGap) / kMuSteps);
  const int filled = (volumePct * kMuSteps + 50) / 100;  // nearest step, 0..kMuSteps
  int16_t x = kMuBarX0;
  for (int i = 0; i < kMuSteps; ++i) {
    if (i < filled) ui.fillRect(x, kMuBarY, blockW, kMuBarH, Color::Black, 6);
    else            ui.strokeRect(x, kMuBarY, blockW, kMuBarH, 2, 6);
    x = static_cast<int16_t>(x + blockW + kMuBlockGap);
  }
}

// The bottom PREV / PLAY-PAUSE / NEXT bar — same scaffold as Blinds' CLOSE/
// STOP/OPEN bar (drawActionBtn + barBtnCx), just with different glyphs and a
// bigger (default 24px face) label than Blinds/Tv's small one.
inline constexpr int16_t kBarLabelH = 26;
inline void drawBar(int pressed, bool playing) {
  drawActionBtn(0, pressed == 0, "PREV", /*font=*/0, kBarLabelH);
  drawActionBtn(1, pressed == 1, playing ? "PAUSE" : "PLAY", /*font=*/0, kBarLabelH);
  drawActionBtn(2, pressed == 2, "NEXT", /*font=*/0, kBarLabelH);
  // Icon centered a bit higher than the small-label default, so the bigger
  // label below it still has clean room within the button box.
  const int16_t gy = static_cast<int16_t>(kBarBtnY + 25);
  const freeink::Icon& mid = playing ? kWx_music_pause : kWx_music_play;
  ui.icon(kWx_music_prev, static_cast<int16_t>(barBtnCx(0) - kWx_music_prev.w / 2),
          static_cast<int16_t>(gy - kWx_music_prev.h / 2), pressed == 0 ? Color::White : Color::Black);
  ui.icon(mid, static_cast<int16_t>(barBtnCx(1) - mid.w / 2),
          static_cast<int16_t>(gy - mid.h / 2), pressed == 1 ? Color::White : Color::Black);
  ui.icon(kWx_music_next, static_cast<int16_t>(barBtnCx(2) - kWx_music_next.w / 2),
          static_cast<int16_t>(gy - kWx_music_next.h / 2), pressed == 2 ? Color::White : Color::Black);
}

inline void draw(int pressed = -1) {
  const haclient::MediaPlayer& m = haclient::media;

  if (!deviceconfig::mediaEnabled) {
    ui.text("Music", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text("not configured for this room", 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
    return;
  }
  if (!m.ok) {
    const bool connecting = WiFi.status() != WL_CONNECTED;
    ui.text(connecting ? "Connecting to Wi-Fi" : "Music unavailable", 0, 300, Ui::W, 28,
            TextAlign::Center, Color::Black);
    ui.text(connecting ? "one moment..." : m.status, 0, 336, Ui::W, 20, TextAlign::Center,
            Color::DarkGray, 1, Ui::kFontSmall);
    return;
  }

  const bool playing = !strcmp(m.state, "playing");

  // Row 1: speaker icon + name + MUTE toggle.
  ui.icon(m.muted ? kWx_music_vol_off : kWx_music_vol_on, kMuX, kMuY);
  ui.text(deviceconfig::mediaName[0] ? deviceconfig::mediaName : "Music", kMuTextX,
          static_cast<int16_t>(kMuY + (kMuIconSz - 26) / 2),
          static_cast<int16_t>(kMuToggleX - kMuTextX - 12), 26, TextAlign::Left, Color::Black);
  drawToggle(kMuToggleX, kMuToggleY, m.muted);

  // Volume row: VOL- | step blocks | VOL+, just above the transport bar.
  if (m.hasVolume) {
    drawVolBtn(0, pressed == 3);
    drawVolBtn(1, pressed == 4);
    drawVolBlocks(m.volumePct);
  }

  // Album art (the page's visual centerpiece when there is any) + title
  // (falls back to a placeholder) + artist, centered as one group in the
  // air between the name row and the volume row.
  const bool hasArt = albumart::g_ok && albumart::g_bits;
  const int16_t contentH = static_cast<int16_t>(
      (hasArt ? albumart::kArtSize + kArtGap : 0) + kTitleH + kTitleArtistGap + kArtistH);
  int16_t y = static_cast<int16_t>(kNowY0 + (kMuRow2Y - kNowY0 - contentH) / 2);
  if (hasArt) {
    ui.icon(albumart::icon(), static_cast<int16_t>(Ui::W / 2 - albumart::kArtSize / 2), y, Color::Black);
    y = static_cast<int16_t>(y + albumart::kArtSize + kArtGap);
  }
  const char* title = m.title[0] ? m.title : (playing ? "Playing" : "Nothing playing");
  ui.text(title, 0, y, Ui::W, kTitleH, TextAlign::Center, Color::Black, 1, Ui::kFont28);
  if (m.artist[0])
    ui.text(m.artist, 0, static_cast<int16_t>(y + kTitleH + kTitleArtistGap), Ui::W, kArtistH,
            TextAlign::Center, Color::DarkGray);

  drawBar(pressed, playing);
}

}  // namespace screen_music
