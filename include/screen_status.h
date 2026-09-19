#pragma once

// ===========================================================================
// screen_status — the weather/status carousel page (page 0). Outdoor weather
// from the room config's weatherEntity, indoor temperature from its
// climateEntity, a 3-day outlook, and a footer showing when data last landed.
//
// Data loading is shared across all four carousel pages (one HA round trip
// fills weather + climate + air + forecast + light + cover), so
// kickWeatherRefresh()/refreshStandby() stay in main.cpp rather than being
// owned by this one screen — see screen_fwd.h.
// ===========================================================================

#include <math.h>

#include "screen_common.h"
#include "ha_client.h"
#include "weather_icons.h"

namespace screen_status {

// "THURSDAY, 20 AUGUST" from the last HA Date header (UTC). Empty if no clock.
inline void formatDate(char* out, size_t cap) {
  out[0] = 0;
  if (!haclient::clockValid) return;
  static const char* kDow[] = {"SUNDAY",   "MONDAY", "TUESDAY",  "WEDNESDAY",
                               "THURSDAY", "FRIDAY", "SATURDAY"};
  static const char* kMon[] = {"JANUARY",   "FEBRUARY", "MARCH",    "APRIL",
                               "MAY",       "JUNE",     "JULY",     "AUGUST",
                               "SEPTEMBER", "OCTOBER",  "NOVEMBER", "DECEMBER"};
  const struct tm& t = haclient::clockUtc;
  if (t.tm_wday < 0 || t.tm_wday > 6 || t.tm_mon < 0 || t.tm_mon > 11) return;
  snprintf(out, cap, "%s, %d %s", kDow[t.tm_wday], t.tm_mday, kMon[t.tm_mon]);
}

// "Updated 14:07 UTC", from the HA Date header on the last fetch.
inline void formatFooter(char* out, size_t cap) {
  if (haclient::clockValid) {
    snprintf(out, cap, "Updated %02d:%02d UTC", haclient::clockUtc.tm_hour,
             haclient::clockUtc.tm_min);
  } else {
    snprintf(out, cap, "Not updated yet");
  }
}

// The page body. Assumes the status bar is already drawn. Uses the full
// height: weather block up top, detail rows spread through the middle, the
// 3-day outlook anchored to the bottom.
inline void draw() {
  const int16_t cx = Ui::W / 2;
  char buf[64];

  if (!haclient::weather.ok) {
    ui.text("Weather unavailable", 0, 320, Ui::W, 28, TextAlign::Center, Color::Black);
    const char* why = WiFi.status() != WL_CONNECTED ? "no network"
                      : !globalsclient::ok          ? globalsclient::status
                      : deviceconfig::weatherEntity[0] ? haclient::weather.status
                                                       : "no weatherEntity in room config";
    ui.text(why, 0, 356, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1, Ui::kFontSmall);
    return;
  }

  // --- date --------------------------------------------------------
  formatDate(buf, sizeof(buf));
  if (buf[0]) ui.text(buf, 0, 64 + kPad, Ui::W, 32, TextAlign::Center, Color::Black);

  // --- condition glyph + temperature -----------------------------
  ui.iconCentered(weatherConditionIcon(haclient::weather.condition), 96 + kPad);
  const int tempWhole =
      haclient::weather.hasTemp ? static_cast<int>(lroundf(haclient::weather.temp)) : 0;
  {
    char n[8];
    snprintf(n, sizeof(n), "%d", tempWhole);
    const fu::Size tsz = ui.measure(n, Ui::kFontTemp);
    const int16_t blockW = static_cast<int16_t>(tsz.width + 8 + 16);
    drawDegrees(static_cast<int16_t>(cx - blockW / 2), 216 + kPad, Ui::kFontTemp, tempWhole);
  }
  ui.text(haclient::weather.label, 0, 306 + kPad, Ui::W, 28, TextAlign::Center, Color::Black);

  // --- indoor temperature (1 dp) --------------------------------
  drawDottedLine(40, 350 + kPad, Ui::W - 80);
  if (haclient::climate.hasTemp) {
    const fu::Size lbl = ui.measure("Indoor  ", 0);
    char n[12];
    snprintf(n, sizeof(n), "%.1f", static_cast<double>(haclient::climate.temp));
    const fu::Size nsz = ui.measure(n, 0);
    const int16_t rowW = static_cast<int16_t>(kWx_ui_indoor.w + 10 + lbl.width + nsz.width + 12);
    int16_t x = static_cast<int16_t>(cx - rowW / 2);
    ui.icon(kWx_ui_indoor, x, static_cast<int16_t>(378 + kPad - kWx_ui_indoor.h / 2));
    x = static_cast<int16_t>(x + kWx_ui_indoor.w + 10);
    ui.text("Indoor  ", x, 366 + kPad, lbl.width, 24, TextAlign::Left);
    drawDegreesStr(static_cast<int16_t>(x + lbl.width), 366 + kPad, 0, n);
  } else {
    ui.text("Indoor  --", 0, 366 + kPad, Ui::W, 24, TextAlign::Center, Color::DarkGray);
  }
  drawDottedLine(40, 404 + kPad, Ui::W - 80);

  // --- wind + humidity kept tight; UV + air quality below ----------
  {
    char buf2[28];
    if (haclient::weather.hasWind) {
      snprintf(buf2, sizeof(buf2), "%d %s", static_cast<int>(lroundf(haclient::weather.wind)),
               haclient::weather.windUnit);
      drawDetailRow(424 + kPad, kWx_ui_wind, "Wind", buf2);
    }
    if (haclient::weather.hasHumidity) {
      snprintf(buf2, sizeof(buf2), "%d%%", haclient::weather.humidity);
      drawDetailRow(458 + kPad, kWx_ui_humidity, "Humidity", buf2);
    }
    if (haclient::weather.hasUv) {
      const float u = haclient::weather.uv;
      const char* band = u < 3 ? "Low" : u < 6 ? "Moderate" : u < 8 ? "High"
                                  : u < 11 ? "Very high" : "Extreme";
      snprintf(buf2, sizeof(buf2), "%.0f  %s", static_cast<double>(u), band);
      drawDetailRow(506 + kPad, kWx_ui_light, "UV index", buf2);
    }
    if (haclient::air.ok) {
      if (haclient::air.has)
        snprintf(buf2, sizeof(buf2), "%d  %s", haclient::air.index, haclient::air.category);
      else
        snprintf(buf2, sizeof(buf2), "%s", haclient::air.category);
      drawDetailRow(542 + kPad, kWx_ui_air, "Air quality", buf2);
    }
  }

  // --- 3-day outlook, anchored near the bottom ------------------
  if (haclient::forecast.ok) {
    const int16_t colW = static_cast<int16_t>(Ui::W / 3);
    const int shown = haclient::forecast.count < 3 ? haclient::forecast.count : 3;
    for (int i = 0; i < shown; ++i) {
      const haclient::ForecastDay& fd = haclient::forecast.day[i];
      const int16_t colX = static_cast<int16_t>(i * colW);
      const int16_t colCx = static_cast<int16_t>(colX + colW / 2);
      ui.text(fd.dow[0] ? fd.dow : "--", colX, 616, colW, 22, TextAlign::Center, Color::Black);
      const freeink::Icon& ic = weatherForecastIcon(fd.condition);
      ui.icon(ic, static_cast<int16_t>(colCx - ic.w / 2), 644);
      char t[24];
      if (fd.hasHi && fd.hasLo)
        snprintf(t, sizeof(t), "%d / %d", static_cast<int>(lroundf(fd.hi)),
                 static_cast<int>(lroundf(fd.lo)));
      else if (fd.hasHi)
        snprintf(t, sizeof(t), "%d", static_cast<int>(lroundf(fd.hi)));
      else
        t[0] = 0;
      if (t[0]) ui.text(t, colX, 700, colW, 26, TextAlign::Center, Color::Black);
    }
  }

  // --- footer: when the data was last pulled --------------------
  formatFooter(buf, sizeof(buf));
  ui.text(buf, 0, 736, Ui::W, 24, TextAlign::Center, Color::DarkGray, 1, Ui::kFont12);
}

}  // namespace screen_status
