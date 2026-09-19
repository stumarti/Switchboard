#pragma once

// ===========================================================================
// screen_climate — the Climate carousel page, styled after Home Assistant's
// own climate card: an arc gauge around the target setpoint, a mode button,
// MINUS/PLUS step buttons, a row of HVAC-mode buttons — all in the top 3/4
// of the screen — then a dotted rule and a stacked list of additional-sensor
// temperature readings (climate.additionalSensors[] in the room config,
// e.g. a window/wall/thermostat sensor) in the bottom quarter.
//
// Adapted for e-ink, deliberately:
//  - The arc's track is plotted as many small overlapping dots rather than
//    stroked as one continuous curve — there's no arc primitive here, only
//    rects/circles, so a dense dot-fill is the practical way to fake a
//    smooth curved band. Colored light gray (dithered), since it's a dial
//    face, not a progress fill.
//  - The setpoint (filled dot) and current temperature (outlined dot) are
//    both just markers on that arc, at the angle their value implies
//    between minTemp and maxTemp, over the SAME start/sweep the arc itself
//    is drawn with — they have to share that mapping or the markers land
//    outside the visible arc.
//  - HA's card lets you drag the ring; this device has no drag gesture
//    story for a circular control, so MINUS/PLUS (already this app's
//    pattern elsewhere) step the setpoint instead.
//  - Mode buttons are separate outline-or-filled rounded rects, not one
//    fused segmented pill — this app's fill primitive takes one radius for
//    all four corners, so there's no clean way to square off only the inner
//    edge of a fused segment.
//
// Mirrors the rest of the app's optimistic-update + background-task pattern.
// ===========================================================================

#include <math.h>

#include "screen_common.h"
#include "screen_fwd.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "ha_client.h"

namespace screen_climate {

inline volatile bool g_busy = false;
// -1 none, 0 = MINUS, 1 = PLUS, 2 = the center mode button, 3+i = the i'th
// mode-bar button.
inline int g_pressed = -1;
enum class Act : uint8_t { Temperature, Mode };
inline Act g_act = Act::Temperature;

inline void task(void*) {
  g_busy = true;
  ensureMdns();
  const char* h = globalsclient::haHost;
  const uint16_t p = globalsclient::haPort;
  const char* t = globalsclient::haToken;
  const char* e = deviceconfig::climateEntity;
  if (globalsclient::ok && e && *e) {
    if (g_act == Act::Mode)
      haclient::setClimateHvacMode(h, p, t, e, haclient::climate.mode);
    else
      haclient::setClimateTemperature(h, p, t, e, haclient::climate.target);
    delay(500);  // let HA apply before we read back
    haclient::fetchClimate(h, p, t, e);
  }
  g_busy = false;
  vTaskDelete(nullptr);
}

inline void kick(Act act) {
  if (g_busy || g_weatherBusy) return;
  g_act = act;
  g_busy = true;
  if (xTaskCreatePinnedToCore(task, "sb_clim", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    g_busy = false;
}

// MINUS (-1) / PLUS (+1): step the setpoint (optimistic), then POST.
inline void adjust(int dir) {
  haclient::Climate& c = haclient::climate;
  if (!c.hasTarget) return;
  const float step = c.tempStep > 0.1f ? c.tempStep : 0.5f;
  float t = c.target + dir * step;
  if (t < c.minTemp) t = c.minTemp;
  if (t > c.maxTemp) t = c.maxTemp;
  c.target = t;
  kick(Act::Temperature);
}

// Jump straight to one entry in the entity's hvac_modes list (optimistic),
// then POST.
inline void setMode(const char* m) {
  if (!m || !*m) return;
  snprintf(haclient::climate.mode, sizeof(haclient::climate.mode), "%s", m);
  kick(Act::Mode);
}
// The center mode button toggles through hvac_modes one at a time.
inline void cycleMode() {
  haclient::Climate& c = haclient::climate;
  if (c.modeCount == 0) return;
  int idx = 0;
  for (int i = 0; i < c.modeCount; ++i)
    if (!strcmp(c.modes[i], c.mode)) idx = i;
  setMode(c.modes[(idx + 1) % c.modeCount]);
}

// HA hvac mode -> a short, friendly label for the center readout.
inline const char* modeLabel(const char* m) {
  if (!m || !*m) return "--";
  if (!strcmp(m, "heat")) return "Heat";
  if (!strcmp(m, "cool")) return "Cool";
  if (!strcmp(m, "heat_cool")) return "Auto";
  if (!strcmp(m, "auto")) return "Auto";
  if (!strcmp(m, "dry")) return "Dry";
  if (!strcmp(m, "fan_only")) return "Fan";
  if (!strcmp(m, "off")) return "Off";
  return m;
}
// Material Design Icons — power/fire/snowflake/thermostat-auto/fan
// (weather_icons.h's CLIMATE_MODE tier), not the old hand-drawn assets.h set.
inline const freeink::Icon* modeIcon(const char* m) {
  if (!m || !*m) return nullptr;
  if (!strcmp(m, "heat")) return &kWx_climate_heat;
  if (!strcmp(m, "cool")) return &kWx_climate_cool;
  if (!strcmp(m, "off")) return &kWx_climate_off;
  if (!strcmp(m, "auto") || !strcmp(m, "heat_cool")) return &kWx_climate_auto;
  if (!strcmp(m, "fan_only")) return &kWx_climate_fan;
  if (!strcmp(m, "dry")) return &kWx_ui_humidity;
  return nullptr;
}

// --- arc gauge -------------------------------------------------------------
inline constexpr int16_t kRingCx = Ui::W / 2;
inline constexpr int16_t kRingCy = 298;
inline constexpr int16_t kRingR  = 160;
// Degrees clockwise from due right (screen trig: 0=+x, 90=+y/down). Sweeping
// 270 degrees from 135 (through 180/left, 270/top, 0/right) to 405 (=45)
// leaves a 90-degree gap centered on the bottom — a standard thermostat-dial
// shape. The two value markers below share this exact mapping.
inline constexpr float kRingStartDeg = 135.0f;
inline constexpr float kRingSweepDeg = 270.0f;
inline constexpr int kRingTicks = 200;  // dense enough that the dots fuse into a band

inline void ringPoint(float deg, int16_t& x, int16_t& y) {
  const float rad = deg * (3.14159265f / 180.0f);
  x = static_cast<int16_t>(lroundf(kRingCx + kRingR * cosf(rad)));
  y = static_cast<int16_t>(lroundf(kRingCy + kRingR * sinf(rad)));
}
inline float angleFor(float value, float lo, float hi) {
  float frac = hi > lo ? (value - lo) / (hi - lo) : 0.5f;
  frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
  return kRingStartDeg + frac * kRingSweepDeg;
}

inline void drawRing(const haclient::Climate& c) {
  // The track: a dense band of small dithered-gray dots along the arc — not
  // itself a progress fill, just the dial face the two markers sit on.
  for (int i = 0; i < kRingTicks; ++i) {
    const float frac = static_cast<float>(i) / (kRingTicks - 1);
    int16_t x, y;
    ringPoint(kRingStartDeg + frac * kRingSweepDeg, x, y);
    ui.fillRect(static_cast<int16_t>(x - 8), static_cast<int16_t>(y - 8), 16, 16, Color::LightGray, 8);
  }

  // Current temperature: an empty (outlined) circle.
  if (c.hasTemp) {
    int16_t x, y;
    ringPoint(angleFor(c.temp, c.minTemp, c.maxTemp), x, y);
    ui.strokeRect(static_cast<int16_t>(x - 11), static_cast<int16_t>(y - 11), 22, 22, 3, 11);
  }
  // Setpoint: a filled black circle, drawn after (and slightly bigger than)
  // the current-temp marker so the two never look ambiguous if they land at
  // nearly the same angle.
  if (c.hasTarget) {
    int16_t x, y;
    ringPoint(angleFor(c.target, c.minTemp, c.maxTemp), x, y);
    ui.fillRect(static_cast<int16_t>(x - 12), static_cast<int16_t>(y - 12), 24, 24, Color::Black, 12);
  }
}

// --- center mode button: outlined pill, icon + label, taps cycle the mode --
inline constexpr int16_t kModePillCy = kRingCy - 105;
inline constexpr int16_t kModePillH  = 46;

inline int16_t modePillW() {
  const freeink::Icon* mi = modeIcon(haclient::climate.mode);
  const fu::Size ls = ui.measure(modeLabel(haclient::climate.mode), 0);
  const int16_t iw = mi ? static_cast<int16_t>(mi->w + 8) : 0;
  return static_cast<int16_t>(iw + ls.width + 44);
}
inline void drawModeButton(bool pressed) {
  const int16_t w = modePillW();
  const int16_t x = static_cast<int16_t>(kRingCx - w / 2);
  const int16_t y = static_cast<int16_t>(kModePillCy - kModePillH / 2);
  if (pressed) ui.fillRect(x, y, w, kModePillH, Color::Black, static_cast<uint8_t>(kModePillH / 2));
  else         ui.strokeRect(x, y, w, kModePillH, 2, static_cast<uint8_t>(kModePillH / 2));
  const Color fg = pressed ? Color::White : Color::Black;
  const freeink::Icon* mi = modeIcon(haclient::climate.mode);
  const char* label = modeLabel(haclient::climate.mode);
  const fu::Size ls = ui.measure(label, 0);
  int16_t tx = static_cast<int16_t>(x + 22);
  if (mi) {
    ui.icon(*mi, tx, static_cast<int16_t>(y + (kModePillH - mi->h) / 2), fg);
    tx = static_cast<int16_t>(tx + mi->w + 8);
  }
  ui.text(label, tx, static_cast<int16_t>(y + (kModePillH - ls.height) / 2), ls.width, ls.height,
          TextAlign::Left, fg);
}
inline bool modeButtonHit(int16_t tx, int16_t ty) {
  const int16_t w = modePillW();
  const int16_t x = static_cast<int16_t>(kRingCx - w / 2);
  const int16_t y = static_cast<int16_t>(kModePillCy - kModePillH / 2);
  return tx >= x && tx < x + w && ty >= y && ty < y + kModePillH;
}

// --- MINUS / PLUS circular step buttons, sitting in the arc's open gap -----
inline constexpr int16_t kStepR  = 32;
inline constexpr int16_t kStepCy = kRingCy + kRingR - 10;
inline constexpr int16_t kMinusCx = kRingCx - 70;
inline constexpr int16_t kPlusCx  = kRingCx + 70;

inline void drawStepBtn(int16_t cx, bool plus, bool pressed) {
  const int16_t x = static_cast<int16_t>(cx - kStepR), y = static_cast<int16_t>(kStepCy - kStepR);
  const int16_t d = static_cast<int16_t>(kStepR * 2);
  if (pressed) ui.fillRect(x, y, d, d, Color::Black, static_cast<uint8_t>(kStepR));
  else         ui.strokeRect(x, y, d, d, 2, static_cast<uint8_t>(kStepR));
  const Color fg = pressed ? Color::White : Color::Black;
  if (plus) glyphPlus(cx, kStepCy, fg); else glyphMinus(cx, kStepCy, fg);
}
// True if a logical tap at (tx,ty) landed inside the circular button at cx.
inline bool stepHit(int16_t tx, int16_t ty, int16_t cx) {
  const int32_t dx = tx - cx, dy = ty - kStepCy;
  return dx * dx + dy * dy <= static_cast<int32_t>(kStepR) * kStepR;
}

// --- HVAC mode bar — sits right below the step buttons, still "with the
// controls" above the dotted line (its own Y/height, not shared with
// Blinds' bottom bar, since this page's controls now end well above where
// Blinds' bar sits) ---------------------------------------------------------
inline constexpr int16_t kModeBarY = kStepCy + kStepR + 20;
inline constexpr int16_t kModeBarH = 76;
inline constexpr int kMaxModeBtns = 4;  // as many as comfortably fit at 480px wide

inline int modeCount() {
  const int n = haclient::climate.modeCount;
  return n > kMaxModeBtns ? kMaxModeBtns : n;
}
inline int16_t modeBtnW() {
  const int n = modeCount();
  return n > 0 ? static_cast<int16_t>((Ui::W - 2 * kShPad - (n - 1) * kBarGap) / n) : 0;
}
inline int16_t modeBtnX(int i) {
  return static_cast<int16_t>(kShPad + i * (modeBtnW() + kBarGap));
}
inline void drawModeBar(int pressed) {
  const haclient::Climate& c = haclient::climate;
  const int n = modeCount();
  const int16_t w = modeBtnW();
  for (int i = 0; i < n; ++i) {
    const int16_t x = modeBtnX(i);
    const bool active = !strcmp(c.modes[i], c.mode);
    const bool fill = active || pressed == 3 + i;
    if (fill) ui.fillRect(x, kModeBarY, w, kModeBarH, Color::Black, 18);
    else      ui.strokeRect(x, kModeBarY, w, kModeBarH, 2, 18);
    const freeink::Icon* mi = modeIcon(c.modes[i]);
    const Color fg = fill ? Color::White : Color::Black;
    if (mi)
      ui.icon(*mi, static_cast<int16_t>(x + (w - mi->w) / 2),
              static_cast<int16_t>(kModeBarY + (kModeBarH - mi->h) / 2), fg);
  }
}
// Which mode button (0-based), or -1, a logical tap fell on.
inline int modeBtnHit(int16_t tx, int16_t ty) {
  if (ty < kModeBarY || ty >= kModeBarY + kModeBarH) return -1;
  const int n = modeCount();
  const int16_t w = modeBtnW();
  for (int i = 0; i < n; ++i) {
    const int16_t x = modeBtnX(i);
    if (tx >= x && tx < x + w) return i;
  }
  return -1;
}

// --- additional-sensor temperature footer ---------------------------------
// climate.additionalSensors[] (e.g. Window/Wall/Thermostat) — a dotted rule
// (matching the Status page's), then a stacked vertical list, one row per
// sensor, matching the Status page's detail-row shape (icon, label, value)
// but at a much larger size since there's only ever a few of them.
inline constexpr int16_t kDotY = kModeBarY + kModeBarH + 20;
inline constexpr int16_t kAreaRowY0 = kDotY + 22;
inline constexpr int16_t kAreaRowSpacing = 46;
inline constexpr int kMaxAreaSensors = 3;

inline void drawAreaRow(int16_t y, const char* label, const char* value) {
  const int16_t pad = 24;
  ui.icon(kWx_ui_warm, pad, static_cast<int16_t>(y + 14 - kWx_ui_warm.h / 2));
  ui.text(label, static_cast<int16_t>(pad + kWx_ui_warm.w + 12), y, 220, 34, TextAlign::Left,
          Color::Black, 1, Ui::kFont28);
  ui.text(value, static_cast<int16_t>(Ui::W - pad - 220), y, 220, 34, TextAlign::Right, Color::Black,
          1, Ui::kFont28);
}

inline void drawAreaSensors() {
  const int n = deviceconfig::climateSensorCount > kMaxAreaSensors ? kMaxAreaSensors
                                                                   : deviceconfig::climateSensorCount;
  if (n == 0) return;
  drawDottedLine(kShPad, kDotY, static_cast<int16_t>(Ui::W - 2 * kShPad));
  for (int i = 0; i < n; ++i) {
    const int16_t y = static_cast<int16_t>(kAreaRowY0 + i * kAreaRowSpacing);
    char v[16];
    if (haclient::climateSensorOk[i])
      snprintf(v, sizeof(v), "%.1f C", static_cast<double>(haclient::climateSensorValue[i]));
    else
      snprintf(v, sizeof(v), "--");
    drawAreaRow(y, deviceconfig::climateSensors[i].name, v);
  }
}

// The status bar already reads "Climate", so there is no second heading here.
inline void draw(int pressed = -1) {
  const haclient::Climate& c = haclient::climate;

  if (!c.ok) {
    ui.text("Climate unavailable", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text(deviceconfig::climateEntity[0] ? c.status : "no climateEntity in room config", 0, 336,
            Ui::W, 20, TextAlign::Center, Color::DarkGray, 1, Ui::kFontSmall);
    return;
  }

  // --- room name — bigger face, nudged up 15px -----------------------
  ui.text(deviceconfig::name[0] ? deviceconfig::name : deviceconfig::activeSlug, 0,
          static_cast<int16_t>(84 + kPad - 15), Ui::W, 34, TextAlign::Center, Color::Black, 1,
          Ui::kFont28);

  // --- arc + center readout ------------------------------------------
  drawRing(c);
  drawModeButton(pressed == 2);

  char n[12];
  if (c.hasTarget) snprintf(n, sizeof(n), "%.1f", static_cast<double>(c.target));
  else             snprintf(n, sizeof(n), "--");
  drawTempC(kRingCx, static_cast<int16_t>(kRingCy - 60), Ui::kFontTemp, n);

  if (c.hasTemp) {
    char cur[8];
    snprintf(cur, sizeof(cur), "%.1f", static_cast<double>(c.temp));
    const int16_t iconSz = 40;
    const int16_t rowY = static_cast<int16_t>(kRingCy + 55);
    const fu::Size numS = ui.measure(cur, 0);
    const int16_t ring = 10, ringGap = 4;
    const int16_t rowW = static_cast<int16_t>(iconSz + 10 + numS.width + ringGap + ring);
    int16_t x = static_cast<int16_t>(kRingCx - rowW / 2);
    ui.iconScaled(kWx_climate_thermo, x, static_cast<int16_t>(rowY + numS.height / 2 - iconSz / 2),
                  iconSz, iconSz, Color::DarkGray);
    x = static_cast<int16_t>(x + iconSz + 10);
    ui.text(cur, x, rowY, numS.width, numS.height, TextAlign::Left, Color::DarkGray);
    x = static_cast<int16_t>(x + numS.width + ringGap);
    ui.strokeRect(x, static_cast<int16_t>(rowY + 3), ring, ring, 2, static_cast<uint8_t>(ring / 2),
                  Color::DarkGray);
  }

  // --- MINUS / PLUS + mode bar (still "the controls", above the rule) -
  drawStepBtn(kMinusCx, /*plus=*/false, pressed == 0);
  drawStepBtn(kPlusCx, /*plus=*/true, pressed == 1);
  drawModeBar(pressed);

  // --- dotted rule + area-sensor footer -------------------------------
  drawAreaSensors();
}

}  // namespace screen_climate
