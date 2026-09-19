#pragma once

// ===========================================================================
// screen_lighting — the Lighting carousel page.
//
// Top section, borderless: a bulb icon (filled+glowing when on, plain
// outline when off — the closest an e-ink panel can do to "animating" state,
// since there's no true animation, only a redrawn frame per state change) +
// the configured group name, a toggle switch; a DARKER icon button /
// brightness bar / BRIGHTER icon button; and, only if the room's lighting
// group has color-temperature control, WARM / DAYLIGHT / COOL preset
// buttons. A dotted rule (matching the Status page's) separates that from
// two tabs — Scenes (default) and Lights — showing one chip list at a time.
//
// The brightness bar supports a held drag, not just tap-to-set — mirrors
// CrossPoint's FrontlightPanelActivity slider (and our own control shade's
// slider, screen_shade.h): the value updates live on every touchHeld frame
// while a finger is down inside the bar, throttled onto the network only as
// fast as kick()'s g_busy gate allows (unlike CrossPoint's slider, which
// drives purely local frontlight hardware with no such throttle needed).
//
// Mirrors screen_climate's optimistic-update + background-task pattern.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"

namespace screen_lighting {

inline volatile bool g_busy = false;
inline int g_pressed = -1;
enum class Act : uint8_t { Group, Scene, ToggleItem, ColorTemp };
inline Act g_act = Act::Group;
inline char g_actEntity[64] = "";
inline int g_actKelvin = 4500;

inline void task(void*) {
  g_busy = true;
  ensureMdns();
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  const char* e = deviceconfig::lightGroupEntity;
  if (globalsclient::ok) {
    if (g_act == Act::Scene) {
      haclient::callService(h, p, t, "scene", "turn_on", g_actEntity);
    } else if (g_act == Act::ToggleItem) {
      haclient::callService(h, p, t, "light", "toggle", g_actEntity);
    } else if (g_act == Act::ColorTemp) {
      haclient::setLightColorTempKelvin(h, p, t, e, g_actKelvin);
    } else if (e && *e) {
      const haclient::Light& l = haclient::lightGroup;
      if (l.on && l.hasBrightness) haclient::setLightBrightness(h, p, t, e, l.brightnessPct);
      else                         haclient::setLightOn(h, p, t, e, l.on);
    }
    delay(500);
    if (e && *e) haclient::fetchLight(h, p, t, e);  // group reflects group + members
  }
  g_busy = false;
  vTaskDelete(nullptr);
}

inline void kick(Act act = Act::Group, const char* entity = "") {
  if (g_busy || g_weatherBusy) return;
  g_act = act;
  snprintf(g_actEntity, sizeof(g_actEntity), "%s", entity);
  g_busy = true;
  if (xTaskCreatePinnedToCore(task, "sb_light", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    g_busy = false;
}
inline void activateScene(const char* entity) { kick(Act::Scene, entity); }
inline void toggleItem(const char* entity) { kick(Act::ToggleItem, entity); }

inline void kickColorTemp(int kelvin) {
  if (g_busy || g_weatherBusy) return;
  g_act = Act::ColorTemp;
  g_actKelvin = kelvin;
  g_busy = true;
  if (xTaskCreatePinnedToCore(task, "sb_light", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    g_busy = false;
}

// DIMMER (-1) / BRIGHTER (+1): step brightness 10% (optimistic). Brightening
// from off turns it on; dimming to 0 turns it off.
inline void adjust(int dir) {
  haclient::Light& l = haclient::lightGroup;
  if (!deviceconfig::lightGroupBrightness) return;
  int b = (l.on ? l.brightnessPct : 0) + dir * 10;
  if (b < 0) b = 0;
  if (b > 100) b = 100;
  l.brightnessPct = b;
  l.hasBrightness = true;
  l.on = b > 0;
  kick();
}

// Tap-to-set: jump straight to a tapped percentage (optimistic), then POST.
// 0 turns the group off (matches DARKER stepping down to 0); anything above
// turns it on.
inline void setPct(int pct) {
  haclient::Light& l = haclient::lightGroup;
  if (!deviceconfig::lightGroupBrightness) return;
  pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  l.brightnessPct = pct;
  l.hasBrightness = true;
  l.on = pct > 0;
  kick();
}

inline void toggle() {
  haclient::Light& l = haclient::lightGroup;
  l.on = !l.on;
  if (l.on && l.hasBrightness && l.brightnessPct == 0) l.brightnessPct = 100;
  kick();
}

// --- layout -----------------------------------------------------------
inline constexpr int16_t kLcX = 20;
inline constexpr int16_t kLcW = Ui::W - 40;
inline constexpr int16_t kLcY = kStatusBarH + kPad + 8;

// Row 1: bulb icon (bigger + state-colored, so it reads as the page's own
// on/off indicator) + the configured group name, a toggle switch on the right.
inline constexpr int16_t kLcIconSz  = 36;
inline constexpr int16_t kLcTextX   = kLcX + kLcIconSz + 10;
inline constexpr int16_t kLcToggleW = 64;
inline constexpr int16_t kLcToggleH = 32;
inline constexpr int16_t kLcToggleX = kLcX + kLcW - kLcToggleW;
inline constexpr int16_t kLcToggleY = kLcY + (kLcIconSz - kLcToggleH) / 2;

// Row 2: DARKER icon button | brightness bar | BRIGHTER icon button.
inline constexpr int16_t kLcRow2Y = kLcY + kLcIconSz + 16;
inline constexpr int16_t kLcBtnSz = 56;
inline constexpr int16_t kLcDarkerX   = kLcX;
inline constexpr int16_t kLcBrighterX = kLcX + kLcW - kLcBtnSz;
inline constexpr int16_t kLcBarH  = 28;
inline constexpr int16_t kLcBarX0 = kLcDarkerX + kLcBtnSz + 14;
inline constexpr int16_t kLcBarX1 = kLcBrighterX - 14;
inline constexpr int16_t kLcBarY  = kLcRow2Y + (kLcBtnSz - kLcBarH) / 2;

// Row 3 (only if the group's lighting.group.controls.colorTemp is set):
// WARM / DAYLIGHT / COOL presets.
inline constexpr int16_t kLcTempY   = kLcRow2Y + kLcBtnSz + 16;
inline constexpr int16_t kLcTempH   = 52;
inline constexpr int16_t kLcTempGap = 12;
inline constexpr int16_t kLcTempW   = (kLcW - 2 * kLcTempGap) / 3;
struct TempPreset { const char* label; int kelvin; const freeink::Icon* icon; };
inline const TempPreset kTempPresets[3] = {
    {"WARM", 2700, &kWx_ui_temp_warm}, {"DAY", 4500, &kWx_ui_temp_daylight},
    {"COOL", 6500, &kWx_ui_temp_cool}};
inline int16_t tempBtnX(int col) {
  return static_cast<int16_t>(kLcX + col * (kLcTempW + kLcTempGap));
}

// Everything above (row 3's space is always reserved, whether or not this
// room's group supports color temperature, so the layout below is fixed
// regardless of that runtime config) — then 10px of air, a dotted rule (same
// style as the Status page's), then two tabs.
inline constexpr int16_t kLcH  = kLcTempY + kLcTempH + 14 - kLcY;
inline constexpr int16_t kDotY = kLcY + kLcH + 10;
inline constexpr int16_t kTabY = kDotY + 18;
inline constexpr int16_t kTabH = 34;
inline constexpr int16_t kChipsY = kTabY + kTabH + 14;

inline int tab = 0;  // 0 = Scenes (default), 1 = Lights
// Index into deviceconfig::scenes[] of the last-activated scene, or -1 —
// drawn filled black on the Scenes tab. Session-only (not persisted): HA has
// no "last run scene" state to read back, so a fresh boot has none selected.
inline int lastScene = -1;

// --- Scenes / Lights paging ---------------------------------------------
// The chip grid comfortably fits 12 (2x6) full-size chips; past that, each
// tab pages through its own list independently. Unlike a scheme that
// reserves a grid slot for a "next page" tile, a full page always shows all
// 12 real items — the page indicator lives in the (otherwise blank) heading
// strip above the grid instead, so it never costs a chip slot.
inline constexpr int kListPageSize = 12;
inline int scenePage = 0;
inline int lightPage = 0;

inline int listPageCount(int total) {
  return total > 0 ? (total + kListPageSize - 1) / kListPageSize : 0;
}
// How many real chips the given page shows (<= kListPageSize).
inline int listVisibleCount(int total, int page) {
  const int pc = listPageCount(total);
  if (pc == 0) return 0;
  const int base = page * kListPageSize;
  const int rem = total - base;
  return rem < kListPageSize ? rem : kListPageSize;
}

inline constexpr int16_t kPagerW = 90;
inline constexpr int16_t kPagerH = 20;
inline constexpr int16_t kPagerX = Ui::W - kShPad - kPagerW;
inline constexpr int16_t kPagerY = kChipsY - 24;
// "<page>/<count> >", right-aligned in the heading strip above the grid —
// only drawn once there's more than one page.
inline void drawPager(int page, int pc) {
  if (pc <= 1) return;
  char label[12];
  snprintf(label, sizeof(label), "%d/%d >", page + 1, pc);
  ui.text(label, kPagerX, kPagerY, kPagerW, kPagerH, TextAlign::Right, Color::DarkGray, 1,
          Ui::kFontSmall);
}
inline bool pagerHit(int16_t tx, int16_t ty, int pc) {
  if (pc <= 1) return false;
  return tx >= kPagerX && tx < kPagerX + kPagerW && ty >= kPagerY - 8 && ty < kPagerY + kPagerH + 8;
}

// A toggle switch: a filled black track + white knob on the right when on; an
// outlined track + black knob on the left when off.
inline void drawToggle(int16_t x, int16_t y, bool on) {
  const uint8_t r = static_cast<uint8_t>(kLcToggleH / 2);
  if (on) ui.fillRect(x, y, kLcToggleW, kLcToggleH, Color::Black, r);
  else    ui.strokeRect(x, y, kLcToggleW, kLcToggleH, 2, r);
  const int16_t knobD = static_cast<int16_t>(kLcToggleH - 8);
  const int16_t knobY = static_cast<int16_t>(y + (kLcToggleH - knobD) / 2);
  const int16_t knobX = on ? static_cast<int16_t>(x + kLcToggleW - knobD - 4)
                           : static_cast<int16_t>(x + 4);
  ui.fillRect(knobX, knobY, knobD, knobD, on ? Color::White : Color::Black,
              static_cast<uint8_t>(knobD / 2));
}
// True if a logical tap at (tx,ty) fell on the toggle switch.
inline bool toggleHit(int16_t tx, int16_t ty) {
  return tx >= kLcToggleX && tx < kLcToggleX + kLcToggleW && ty >= kLcToggleY &&
         ty < kLcToggleY + kLcToggleH;
}

// True if a logical tap at (tx,ty) fell in (or close above/below, for a more
// forgiving target) the brightness bar.
inline bool barHit(int16_t tx, int16_t ty) {
  return tx >= kLcBarX0 && tx < kLcBarX1 && ty >= kLcBarY - 10 && ty < kLcBarY + kLcBarH + 10;
}
// A finger is down inside the bar and dragging — mirrors screen_shade's own
// `dragging` (itself following CrossPoint's FrontlightPanelActivity pattern:
// a held touch updates the value live, on every frame, while it's down).
inline bool dragging = false;
// The percentage a tap x inside the bar implies, 0..100.
inline int pctFromX(int16_t tx) {
  const int pct = static_cast<int>((static_cast<int32_t>(tx - kLcBarX0) * 100) /
                                    (kLcBarX1 - kLcBarX0));
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

// col 0 = DARKER (left, kWx_ui_dimmer), col 1 = BRIGHTER (right, kWx_ui_brighter).
inline int16_t lcBtnX(int col) { return col == 0 ? kLcDarkerX : kLcBrighterX; }
inline void drawLcBtn(int col, bool pressed) {
  const int16_t x = lcBtnX(col);
  if (pressed) ui.fillRect(x, kLcRow2Y, kLcBtnSz, kLcBtnSz, Color::Black, 16);
  else         ui.strokeRect(x, kLcRow2Y, kLcBtnSz, kLcBtnSz, 2, 16);
  const freeink::Icon& ic = col == 0 ? kWx_ui_dimmer : kWx_ui_brighter;
  ui.icon(ic, static_cast<int16_t>(x + (kLcBtnSz - ic.w) / 2),
          static_cast<int16_t>(kLcRow2Y + (kLcBtnSz - ic.h) / 2), pressed ? Color::White : Color::Black);
}

// col 0/1/2 = WARM / DAYLIGHT / COOL.
inline void drawTempBtn(int col, bool pressed) {
  const int16_t x = tempBtnX(col);
  if (pressed) ui.fillRect(x, kLcTempY, kLcTempW, kLcTempH, Color::Black, 14);
  else         ui.strokeRect(x, kLcTempY, kLcTempW, kLcTempH, 2, 14);
  const Color fg = pressed ? Color::White : Color::Black;
  const TempPreset& p = kTempPresets[col];
  // Icon + label as one centered group, both vertically centered on the
  // button — bumped from kFont12 to the default 24px face, per request, so
  // it reads larger; the icon is scaled down a touch to leave it room.
  const int16_t iconSz = 22;
  const fu::Size ls = ui.measure(p.label, 0);
  constexpr int16_t gap = 6;
  const int16_t totalW = static_cast<int16_t>(iconSz + gap + ls.width);
  int16_t ix = static_cast<int16_t>(x + (kLcTempW - totalW) / 2);
  ui.iconScaled(*p.icon, ix, static_cast<int16_t>(kLcTempY + (kLcTempH - iconSz) / 2), iconSz, iconSz,
               fg);
  ix = static_cast<int16_t>(ix + iconSz + gap);
  ui.text(p.label, ix, static_cast<int16_t>(kLcTempY + (kLcTempH - ls.height) / 2), ls.width, ls.height,
          TextAlign::Left, fg);
}
// Which WARM/DAYLIGHT/COOL button (0/1/2), or -1, a logical tap fell on.
inline int tempBtnHit(int16_t tx, int16_t ty) {
  if (ty < kLcTempY || ty >= kLcTempY + kLcTempH) return -1;
  for (int c = 0; c < 3; ++c) {
    const int16_t x = tempBtnX(c);
    if (tx >= x && tx < x + kLcTempW) return c;
  }
  return -1;
}

inline void drawTabs() {
  static const char* const kLabels[2] = {"Scenes", "Lights"};
  const int16_t tw = kLcW / 2;
  for (int i = 0; i < 2; ++i) {
    const int16_t x = static_cast<int16_t>(kLcX + i * tw);
    const bool active = tab == i;
    ui.text(kLabels[i], x, kTabY, tw, 26, TextAlign::Center, active ? Color::Black : Color::DarkGray);
    if (active) {
      constexpr int16_t barW = 48;
      ui.fillRect(static_cast<int16_t>(x + tw / 2 - barW / 2), static_cast<int16_t>(kTabY + 28), barW,
                  3, Color::Black, 1);
    }
  }
}
// 0 (Scenes) / 1 (Lights), or -1, for a logical tap in the tab bar.
inline int tabHit(int16_t tx, int16_t ty) {
  if (ty < kTabY || ty >= kTabY + kTabH) return -1;
  return tx < kLcX + kLcW / 2 ? 0 : 1;
}

inline void draw(int pressed = -1) {
  const haclient::Light& l = haclient::lightGroup;
  const bool bri = deviceconfig::lightGroupBrightness;
  const bool ct = deviceconfig::lightGroupColorTemp;

  if (!deviceconfig::lightGroupEnabled) {
    ui.text("Lighting", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text("not configured for this room", 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
    return;
  }
  if (!l.ok && !l.hasBrightness) {
    const bool connecting = WiFi.status() != WL_CONNECTED;
    ui.text(connecting ? "Connecting to Wi-Fi" : "Lighting unavailable", 0, 300, Ui::W, 28,
            TextAlign::Center, Color::Black);
    ui.text(connecting ? "one moment..." : l.status, 0, 336, Ui::W, 20, TextAlign::Center,
            Color::DarkGray, 1, Ui::kFontSmall);
    return;
  }

  // Row 1: bulb icon (filled+glowing on, plain outline off) + group name +
  // toggle switch.
  ui.icon(l.on ? kWx_ui_bulb_on : kWx_ui_bulb_off, kLcX, kLcY);
  ui.text(deviceconfig::lightGroupName[0] ? deviceconfig::lightGroupName : "All Lights", kLcTextX,
          static_cast<int16_t>(kLcY + (kLcIconSz - 26) / 2),
          static_cast<int16_t>(kLcToggleX - kLcTextX - 12), 26, TextAlign::Left, Color::Black);
  drawToggle(kLcToggleX, kLcToggleY, l.on);

  // Row 2: DARKER | brightness bar | BRIGHTER — a continuous fill, not the
  // segmented pips the control shade uses, so it reads as a level readout
  // rather than a drag control.
  if (bri) {
    drawLcBtn(0, pressed == 0);
    drawLcBtn(1, pressed == 1);

    ui.strokeRect(kLcBarX0, kLcBarY, static_cast<int16_t>(kLcBarX1 - kLcBarX0), kLcBarH, 2, 13);
    const int pct = l.on ? l.brightnessPct : 0;
    const int16_t innerW = static_cast<int16_t>(kLcBarX1 - kLcBarX0 - 4);
    const int16_t fillW = static_cast<int16_t>(innerW * pct / 100);
    if (fillW > 0)
      ui.fillRect(static_cast<int16_t>(kLcBarX0 + 2), static_cast<int16_t>(kLcBarY + 2), fillW,
                  static_cast<int16_t>(kLcBarH - 4), Color::Black, 11);
  }

  // Row 3: WARM / DAYLIGHT / COOL — only when this room's group supports it.
  if (ct) {
    drawTempBtn(0, pressed == 2);
    drawTempBtn(1, pressed == 3);
    drawTempBtn(2, pressed == 4);
  }

  // --- Scenes / Lights tabs ------------------------------------
  drawDottedLine(kLcX, kDotY, kLcW);
  drawTabs();
  if (tab == 0) {
    if (deviceconfig::sceneCount > 0) {
      const int pc = listPageCount(deviceconfig::sceneCount);
      if (scenePage >= pc) scenePage = 0;
      const int n = listVisibleCount(deviceconfig::sceneCount, scenePage);
      const int base = scenePage * kListPageSize;
      drawChips(kChipsY, "", deviceconfig::scenes + base, n, /*onStates=*/nullptr,
                (lastScene >= base && lastScene < base + n) ? lastScene - base : -1);
      drawPager(scenePage, pc);
    } else {
      ui.text("No scenes configured", 0, kChipsY, Ui::W, 22, TextAlign::Center, Color::DarkGray, 1,
              Ui::kFontSmall);
    }
  } else {
    if (deviceconfig::lightCount > 0) {
      const int pc = listPageCount(deviceconfig::lightCount);
      if (lightPage >= pc) lightPage = 0;
      const int n = listVisibleCount(deviceconfig::lightCount, lightPage);
      const int base = lightPage * kListPageSize;
      drawChips(kChipsY, "", deviceconfig::lights + base, n, haclient::lightItemOn + base);
      drawPager(lightPage, pc);
    } else {
      ui.text("No lights configured", 0, kChipsY, Ui::W, 22, TextAlign::Center, Color::DarkGray, 1,
              Ui::kFontSmall);
    }
  }
}

}  // namespace screen_lighting
