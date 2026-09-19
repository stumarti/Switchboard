#pragma once

// ===========================================================================
// screen_blinds — the Blinds carousel page: position readout / open-closed
// state, a state pill, individual-blind chips, and a CLOSE / STOP / OPEN
// action bar. Mirrors screen_climate's optimistic-update + background-task
// pattern.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"
#include "screen_lighting.h"  // kChipsY — blind chips share the lighting page's chip-list Y

namespace screen_blinds {

inline volatile bool g_busy = false;
inline int g_pressed = -1;
enum class Act : uint8_t { Open, Close, Stop, Refresh };
inline Act g_act = Act::Refresh;

inline void task(void*) {
  g_busy = true;
  ensureMdns();
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  const char* e = deviceconfig::blindsGroupEntity;
  if (globalsclient::ok && e && *e) {
    if (g_act != Act::Refresh) {
      const char* svc = g_act == Act::Open    ? "open_cover"
                        : g_act == Act::Close  ? "close_cover"
                                               : "stop_cover";
      haclient::callService(h, p, t, "cover", svc, e);
      delay(600);
    }
    haclient::fetchCover(h, p, t, e);
  }
  g_busy = false;
  vTaskDelete(nullptr);
}

inline void kick(Act act) {
  if (g_busy || g_weatherBusy) return;
  g_act = act;
  g_busy = true;
  if (xTaskCreatePinnedToCore(task, "sb_cover", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    g_busy = false;
}

// CLOSE / STOP / OPEN: update the state pill optimistically, then POST + re-read.
inline void command(Act act) {
  haclient::Cover& c = haclient::cover;
  if (act == Act::Open)  snprintf(c.state, sizeof(c.state), "opening");
  if (act == Act::Close) snprintf(c.state, sizeof(c.state), "closing");
  kick(act);
}

// --- per-item control (the two-blind side-by-side layout, below) -----------
// Independent from the group's g_busy/g_act/kick/command above: each panel
// commands its own cover entity (deviceconfig::blindsItems[i]) and gates on
// its own busy flag, so tapping one blind's buttons doesn't block the other's.
inline volatile bool g_itemBusy[2] = {false, false};
inline Act g_itemAct[2] = {Act::Refresh, Act::Refresh};

inline void itemTask(void* argIdx) {
  const int i = static_cast<int>(reinterpret_cast<intptr_t>(argIdx));
  g_itemBusy[i] = true;
  ensureMdns();
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  const char* e = deviceconfig::blindsItems[i].entity;
  if (globalsclient::ok && e && *e) {
    if (g_itemAct[i] != Act::Refresh) {
      const char* svc = g_itemAct[i] == Act::Open    ? "open_cover"
                        : g_itemAct[i] == Act::Close  ? "close_cover"
                                                       : "stop_cover";
      haclient::callService(h, p, t, "cover", svc, e);
      delay(600);
    }
    haclient::fetchCoverItem(h, p, t, e, i);
  }
  g_itemBusy[i] = false;
  vTaskDelete(nullptr);
}

inline void kickItem(int i, Act act) {
  if (i < 0 || i > 1 || g_itemBusy[i] || g_weatherBusy) return;
  g_itemAct[i] = act;
  g_itemBusy[i] = true;
  if (xTaskCreatePinnedToCore(itemTask, "sb_cov_i", 8192,
                             reinterpret_cast<void*>(static_cast<intptr_t>(i)), 1, nullptr,
                             1) != pdPASS)
    g_itemBusy[i] = false;
}

// CLOSE / STOP / OPEN for one panel — mirrors command() above but per-item.
inline void commandItem(int i, Act act) {
  if (i < 0 || i > 1) return;
  haclient::Cover& c = haclient::coverItems[i];
  if (act == Act::Open)  snprintf(c.state, sizeof(c.state), "opening");
  if (act == Act::Close) snprintf(c.state, sizeof(c.state), "closing");
  kickItem(i, act);
}
inline bool anyItemBusy() { return g_itemBusy[0] || g_itemBusy[1]; }

// Open/closed glyph for a cover's current state — from position when known
// (any position above 0 reads as "open"), otherwise the raw HA state string.
// "opening" counts as open (it's on its way there); only "closed" reads shut.
inline const freeink::Icon& blindIcon(const haclient::Cover& c) {
  const bool open = c.hasPosition ? c.position > 0 : strcmp(c.state, "closed") != 0;
  return open ? kWx_blinds_open : kWx_blinds_closed;
}

// --- layout: an "All blinds" panel on top, the two individual blinds pinned
// to the bottom third of the screen -----------------------------------
// Used instead of the single-group+chips layout below whenever
// deviceconfig::blindsItemCount == 2 (0/1/3+ items keep that layout).
//
// "All blinds" always shows here, whether or not a real HA group entity is
// configured for this room: this layout only exists because there are
// exactly two blinds and (per the room configs seen in practice) usually NO
// separate group entity — hasRealGroup() picks whether its row drives that
// real entity or synthesizes an aggregate over the two items instead
// (see syntheticGroupCover() / command() below).
//
// "All blinds" is deliberately styled apart from the two item rows below —
// a centered title, then two columns: a large centered icon on the left, a
// large centered CLOSE/STOP/OPEN column on the right — so it reads as "the
// whole room" rather than a third blind in the list. A dotted rule (same
// convention as the Settings/Wi-Fi-networks lists) separates it from the
// item rows, which stay a plain icon+name-left / buttons-across-the-right
// list and are pinned to the bottom third of the screen.
inline constexpr int16_t kRowIconSize = 40;
inline constexpr int16_t kRowBtnSize  = 44;
inline constexpr int16_t kRowBtnGap   = 10;
inline constexpr int16_t kRowBtnsW    = 3 * kRowBtnSize + 2 * kRowBtnGap;
inline constexpr int16_t kRowBtnsX0   = static_cast<int16_t>(Ui::W - kShPad - kRowBtnsW);

// The two item rows: exactly the bottom third of the screen, split evenly.
inline constexpr int16_t kItemsSectionH   = Ui::H / 3;
inline constexpr int16_t kItemsSectionTop = static_cast<int16_t>(Ui::H - kItemsSectionH);
inline constexpr int16_t kRowH            = kItemsSectionH / 2;
inline int16_t itemRowY(int i) { return static_cast<int16_t>(kItemsSectionTop + i * kRowH); }

// "All blinds": everything above the item section — a title strip, then two
// columns filling the rest.
inline constexpr int16_t kAllTop     = 84 + kPad;
inline constexpr int16_t kAllTitleH  = 40;
inline constexpr int16_t kAllGap     = 12;
inline constexpr int16_t kAllBodyTop = static_cast<int16_t>(kAllTop + kAllTitleH + kAllGap);
inline constexpr int16_t kAllBodyH   = static_cast<int16_t>(kItemsSectionTop - kAllBodyTop);
inline constexpr int16_t kAllColGap  = 20;
inline constexpr int16_t kAllColW    = (Ui::W - 2 * kShPad - kAllColGap) / 2;
inline constexpr int16_t kAllLeftX   = kShPad;
inline constexpr int16_t kAllRightX  = static_cast<int16_t>(kAllLeftX + kAllColW + kAllColGap);

// Icon: large, centered in the left column. 128 = the 64px asset at a clean
// 2x — "make it larger", per request.
inline constexpr int16_t kAllIconSize = 128;
inline constexpr int16_t kAllIconX =
    static_cast<int16_t>(kAllLeftX + (kAllColW - kAllIconSize) / 2);
inline constexpr int16_t kAllIconY =
    static_cast<int16_t>(kAllBodyTop + (kAllBodyH - kAllIconSize) / 2);

// Buttons: large, centered (as a group) in the right column, stacked
// vertically.
inline constexpr int16_t kAllBtnSize   = 90;
inline constexpr int16_t kAllBtnGap    = 14;
inline constexpr int16_t kAllBtnStackH = 3 * kAllBtnSize + 2 * kAllBtnGap;
inline constexpr int16_t kAllBtnX =
    static_cast<int16_t>(kAllRightX + (kAllColW - kAllBtnSize) / 2);
inline constexpr int16_t kAllBtnStackY0 =
    static_cast<int16_t>(kAllBodyTop + (kAllBodyH - kAllBtnStackH) / 2);

inline bool hasRealGroup() { return deviceconfig::blindsGroupEnabled; }

// A synthetic Cover for the "All blinds" row when there's no real group
// entity to read: "opening"/"closing" if either item is moving (whichever's
// state wins, arbitrarily — they're commanded together so should agree in
// practice), else "open" if either item reads open, else "closed". `ok` only
// once both items have reported in, same as any other row's "--" fallback.
inline haclient::Cover syntheticGroupCover() {
  haclient::Cover c{};
  const haclient::Cover &a = haclient::coverItems[0], &b = haclient::coverItems[1];
  c.ok = a.ok && b.ok;
  const bool aMoving = !strcmp(a.state, "opening") || !strcmp(a.state, "closing");
  const bool bMoving = !strcmp(b.state, "opening") || !strcmp(b.state, "closing");
  const bool aOpen = a.hasPosition ? a.position > 0 : strcmp(a.state, "closed") != 0;
  const bool bOpen = b.hasPosition ? b.position > 0 : strcmp(b.state, "closed") != 0;
  const char* st = aMoving ? a.state : bMoving ? b.state : (aOpen || bOpen) ? "open" : "closed";
  snprintf(c.state, sizeof(c.state), "%s", st);
  return c;
}

// "All blinds" row's CLOSE/STOP/OPEN: the real group entity's single service
// call when one's configured, otherwise both items commanded together.
inline void commandAll(Act act) {
  if (hasRealGroup()) {
    command(act);
  } else {
    commandItem(0, act);
    commandItem(1, act);
  }
}

// "All blinds": a centered title at the very top, then two columns — a large
// centered icon on the left, a large centered CLOSE/STOP/OPEN stack on the
// right — filling the rest of the space down to the item section.
inline void drawAllBlindsRow(const char* name, const haclient::Cover& c, int pressed) {
  ui.text(name, 0, kAllTop, Ui::W, kAllTitleH, TextAlign::Center, Color::Black, 1, Ui::kFont28);

  if (c.ok) ui.iconScaled(blindIcon(c), kAllIconX, kAllIconY, kAllIconSize, kAllIconSize);
  else      ui.strokeRect(kAllIconX, kAllIconY, kAllIconSize, kAllIconSize, 2, 14);

  for (int col = 0; col < 3; ++col) {
    const int16_t by = static_cast<int16_t>(kAllBtnStackY0 + col * (kAllBtnSize + kAllBtnGap));
    const bool press = pressed == col;  // "All blinds" is always base 0
    if (press) ui.fillRect(kAllBtnX, by, kAllBtnSize, kAllBtnSize, Color::Black, 16);
    else       ui.strokeRect(kAllBtnX, by, kAllBtnSize, kAllBtnSize, 2, 16);
    const Color fg = press ? Color::White : Color::Black;
    const int16_t cx = static_cast<int16_t>(kAllBtnX + kAllBtnSize / 2);
    const int16_t cy = static_cast<int16_t>(by + kAllBtnSize / 2);
    // Top to bottom: CLOSE / STOP / OPEN — same order left-to-right as the
    // item rows' buttons and the group action bar (drawBar) elsewhere.
    if (col == 0)      glyphTri(cx, cy, /*up=*/false, fg);  // CLOSE
    else if (col == 1) glyphStop(cx, cy, fg);                // STOP
    else               glyphTri(cx, cy, /*up=*/true, fg);   // OPEN
  }

  drawDottedLine(kShPad, static_cast<int16_t>(kItemsSectionTop - 1),
                static_cast<int16_t>(Ui::W - 2 * kShPad));
}

// One item row: icon+name on the left, CLOSE/STOP/OPEN across the right —
// the plain list style, distinct from the "All blinds" row above.
inline void drawItemRow(int i, const char* name, const haclient::Cover& c, int pressed) {
  const int16_t y = itemRowY(i);
  const int16_t iconY = static_cast<int16_t>(y + kRowH / 2 - kRowIconSize / 2);
  if (c.ok) ui.iconScaled(blindIcon(c), kShPad, iconY, kRowIconSize, kRowIconSize);
  else      ui.strokeRect(kShPad, iconY, kRowIconSize, kRowIconSize, 2, 8);

  const int16_t nameX = static_cast<int16_t>(kShPad + kRowIconSize + 16);
  const int16_t nameW = static_cast<int16_t>(kRowBtnsX0 - 12 - nameX);
  ui.text(name, nameX, static_cast<int16_t>(y + kRowH / 2 - 16), nameW, 32, TextAlign::Left,
          Color::Black);

  const int base = (i + 1) * 3;
  for (int col = 0; col < 3; ++col) {
    const int16_t bx = static_cast<int16_t>(kRowBtnsX0 + col * (kRowBtnSize + kRowBtnGap));
    const int16_t by = static_cast<int16_t>(y + kRowH / 2 - kRowBtnSize / 2);
    const bool press = pressed == base + col;
    if (press) ui.fillRect(bx, by, kRowBtnSize, kRowBtnSize, Color::Black, 12);
    else       ui.strokeRect(bx, by, kRowBtnSize, kRowBtnSize, 2, 12);
    const Color fg = press ? Color::White : Color::Black;
    const int16_t cx = static_cast<int16_t>(bx + kRowBtnSize / 2);
    const int16_t cy = static_cast<int16_t>(by + kRowBtnSize / 2);
    if (col == 0)      glyphTri(cx, cy, /*up=*/false, fg);  // CLOSE
    else if (col == 1) glyphStop(cx, cy, fg);                // STOP
    else               glyphTri(cx, cy, /*up=*/true, fg);   // OPEN
  }

  if (i < 1)
    drawDottedLine(kShPad, static_cast<int16_t>(y + kRowH - 1),
                  static_cast<int16_t>(Ui::W - 2 * kShPad));
}

inline void drawRowLayout(int pressed) {
  drawAllBlindsRow(deviceconfig::blindsGroupName[0] ? deviceconfig::blindsGroupName : "All blinds",
                   hasRealGroup() ? haclient::cover : syntheticGroupCover(), pressed);
  for (int i = 0; i < 2; ++i)
    drawItemRow(i, deviceconfig::blindsItems[i].name, haclient::coverItems[i], pressed);
}

// Row/col (0=CLOSE,1=STOP,2=OPEN) a logical tap fell on, encoded the same way
// drawRowLayout() draws: 0-2 for "All blinds" (its vertical button column),
// 3-5 / 6-8 for item 0 / item 1 (their horizontal button rows).
inline int rowBtnHit(int16_t tx, int16_t ty) {
  if (ty >= kAllBtnStackY0 && tx >= kAllBtnX && tx < kAllBtnX + kAllBtnSize) {
    const int col = (ty - kAllBtnStackY0) / (kAllBtnSize + kAllBtnGap);
    if (col < 0 || col > 2) return -1;
    const int16_t by = static_cast<int16_t>(kAllBtnStackY0 + col * (kAllBtnSize + kAllBtnGap));
    if (ty >= by + kAllBtnSize) return -1;  // landed in the gap between buttons
    return col;
  }
  if (tx < kRowBtnsX0) return -1;
  for (int i = 0; i < 2; ++i) {
    const int16_t y = itemRowY(i);
    if (ty < y || ty >= y + kRowH) continue;
    const int col = (tx - kRowBtnsX0) / (kRowBtnSize + kRowBtnGap);
    if (col < 0 || col > 2) return -1;
    const int16_t bx = static_cast<int16_t>(kRowBtnsX0 + col * (kRowBtnSize + kRowBtnGap));
    if (tx >= bx + kRowBtnSize) return -1;  // landed in the gap between buttons
    return (i + 1) * 3 + col;
  }
  return -1;
}

inline void drawBar(int pressed = -1) {
  drawActionBtn(0, pressed == 0, "CLOSE");
  drawActionBtn(1, pressed == 1, "STOP");
  drawActionBtn(2, pressed == 2, "OPEN");
  const int16_t gy = static_cast<int16_t>(kBarBtnY + 30);
  glyphTri(barBtnCx(0), gy, /*up=*/false, pressed == 0 ? Color::White : Color::Black);
  glyphStop(barBtnCx(1), gy, pressed == 1 ? Color::White : Color::Black);
  glyphTri(barBtnCx(2), gy, /*up=*/true, pressed == 2 ? Color::White : Color::Black);
}

inline void draw(int pressed = -1) {
  // Exactly two individual blinds -> the row layout above (All blinds row on
  // top of the two item rows), instead of the group+chips layout below.
  if (deviceconfig::blindsItemCount == 2) {
    drawRowLayout(pressed);
    return;
  }

  const int16_t cx = Ui::W / 2;
  const haclient::Cover& c = haclient::cover;

  if (!deviceconfig::blindsGroupEnabled) {
    ui.text("Blinds", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text("not configured for this room", 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
    return;
  }
  if (!c.ok) {
    const bool connecting = WiFi.status() != WL_CONNECTED;
    ui.text(connecting ? "Connecting to Wi-Fi" : "Blinds unavailable", 0, 300, Ui::W, 28,
            TextAlign::Center, Color::Black);
    ui.text(connecting ? "one moment..." : c.status, 0, 336, Ui::W, 20, TextAlign::Center,
            Color::DarkGray, 1, Ui::kFontSmall);
    return;
  }

  ui.text(deviceconfig::blindsGroupName[0] ? deviceconfig::blindsGroupName
          : (deviceconfig::name[0] ? deviceconfig::name : deviceconfig::activeSlug),
          0, 84 + kPad, Ui::W, 30, TextAlign::Center, Color::Black);

  if (c.hasPosition) {
    drawBigPct(cx, 156 + kPad, c.position);
    ui.text("OPEN", 0, 250 + kPad, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1, Ui::kFontSmall);
  } else {
    const bool open = strcmp(c.state, "closed") != 0;
    ui.text(open ? "OPEN" : "CLOSED", 0, 176 + kPad, Ui::W, 56, TextAlign::Center, Color::Black, 1, 0);
  }

  if (deviceconfig::blindsItemCount > 0)
    drawChips(screen_lighting::kChipsY, "BLINDS", deviceconfig::blindsItems,
              deviceconfig::blindsItemCount);

  drawBar(pressed);
}

}  // namespace screen_blinds
