#!/usr/bin/env python3
"""Weather-icon generator for the Standby screen.

Rasterizes the Material Design Icons (MDI) weather glyphs — one per Home
Assistant `weather` condition — plus a few small chrome glyphs, into
freeink::Icon C structs, with the optical center baked in (same format and
packing as the SDK's libs/assets/Icons/tools/gen_icons.py).

The SVGs live in tools/weather_svg/ (vendored copies of @mdi/svg 7.4.47 so
this is reproducible without npm). To refresh them:

    npm i @mdi/svg
    cp node_modules/@mdi/svg/svg/<name>.svg tools/weather_svg/

Usage:
    python tools/gen_weather_icons.py

Requires: resvg_py and Pillow  (pip install resvg-py pillow)

Output: include/weather_icons.h  (checked in — regenerate when the manifest
or the SVGs change).
"""
import io
import os
import re
import sys

import resvg_py
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SVG_DIR = os.path.join(HERE, "weather_svg")
OUT = os.path.join(HERE, "..", "include", "weather_icons.h")

# Luminance (0-255, composited on white) at/below which a pixel counts as ink.
THRESHOLD = 128

# --- Home Assistant `weather` conditions -> MDI glyph ----------------------
# Matches the Home Assistant frontend's own weatherIcons map (data/weather.ts)
# so the device shows the same picture the HA dashboard does. `alias` is the
# C identifier stem; the enum value it maps from is alias with '_' -> '-'.
# Each condition is emitted at the hero size and the small forecast-column size.
BIG_SIZE = 120
FCAST_SIZE = 44
WEATHER = [
    ("clear_night", "weather-night"),
    ("cloudy", "weather-cloudy"),
    ("exceptional", "alert-circle-outline"),
    ("fog", "weather-fog"),
    ("hail", "weather-hail"),
    ("lightning", "weather-lightning"),
    ("lightning_rainy", "weather-lightning-rainy"),
    ("partlycloudy", "weather-partly-cloudy"),
    ("pouring", "weather-pouring"),
    ("rainy", "weather-rainy"),
    ("snowy", "weather-snowy"),
    ("snowy_rainy", "weather-snowy-rainy"),
    ("sunny", "weather-sunny"),
    ("windy", "weather-windy"),
    ("windy_variant", "weather-windy-variant"),
    # Not a HA condition, but handy if a caller wants a night partly-cloudy.
    ("night_partlycloudy", "weather-night-partly-cloudy"),
]

# --- small chrome glyphs for the detail rows ------------------------------
SMALL_SIZE = 24
SMALL = [
    ("ui_wind", "weather-windy"),
    ("ui_humidity", "water-percent"),
    ("ui_indoor", "home-thermometer-outline"),
    ("ui_air", "air-filter"),
]

# --- control-shade glyphs (a touch bigger, for tap targets) --------------
CHROME_SIZE = 28
CHROME = [
    ("ui_light", "lightbulb-on-outline"),
    ("ui_bright", "brightness-6"),
    ("ui_dimmer", "brightness-4"),   # Lighting card's DARKER button
    ("ui_brighter", "brightness-7"), # Lighting card's BRIGHTER button
    ("ui_warm", "thermometer"),
    ("ui_refresh", "refresh"),
    ("ui_cog", "cog"),
    ("ui_chevron_up", "chevron-up"),
    ("ui_temp_warm", "white-balance-incandescent"),   # Lighting card's WARM preset
    ("ui_temp_daylight", "white-balance-sunny"),      # Lighting card's DAYLIGHT preset
    ("ui_temp_cool", "white-balance-iridescent"),     # Lighting card's COOL preset
    ("ui_room", "home-outline"),        # Settings: Select room
    ("ui_info", "information-outline"), # Settings: Device info
    ("ui_wifi", "wifi"),                # Settings: Wi-Fi setup
    ("ui_restart", "restart"),          # Settings: Restart
    ("ui_back", "arrow-left"),          # Settings: Back
]

# --- Lighting card's group bulb glyph — bigger still, it's the page's own
# on/off indicator, not just a tap target. Two states: filled+radiating rays
# when on (reads "brighter"), plain outline when off.
BULB_SIZE = 36
BULB = [
    ("ui_bulb_on", "lightbulb-on"),
    ("ui_bulb_off", "lightbulb-outline"),
]

# --- Climate page's HVAC-mode glyphs — used both in the center readout and
# the mode-select bar, so they need to read clearly at a slightly bigger size
# than the general CHROME tap targets.
CLIMATE_MODE_SIZE = 30
CLIMATE_MODE = [
    ("climate_off", "power"),
    ("climate_heat", "fire"),
    ("climate_cool", "snowflake"),
    ("climate_auto", "thermostat-auto"),
    ("climate_fan", "fan"),
]

# --- Climate page's current-temperature icon — bigger and plainer than the
# small chrome glyphs (no house metaphor), since it sits right next to the
# giant target-temperature digits.
CLIMATE_THERMO_SIZE = 40
CLIMATE_THERMO = [
    ("climate_thermo", "thermometer"),
]

# --- Blinds page's position readout — a modern open/closed glyph in place of
# the plain position percentage. Same icon Home Assistant itself picks for
# cover device_class "blind" (mdi:blinds / mdi:blinds-open).
BLINDS_SIZE = 64
BLINDS = [
    ("blinds_open", "blinds-open"),
    ("blinds_closed", "blinds"),
]

# --- Music page: a speaker glyph for row 1's mute state (mirrors the
# Lighting card's bulb_on/off), VOL-/VOL+ row-2 buttons (mirrors
# dimmer/brighter), and a plain (uncircled — the button itself draws the
# outline/fill) PLAY/PAUSE/SKIP set for the bottom transport bar.
MUSIC_SPEAKER_SIZE = 36
MUSIC_SPEAKER = [
    ("music_vol_on", "volume-high"),
    ("music_vol_off", "volume-off"),
]
MUSIC_CHROME_SIZE = 28
MUSIC_CHROME = [
    ("music_vol_minus", "volume-minus"),
    ("music_vol_plus", "volume-plus"),
]
MUSIC_TRANSPORT_SIZE = 42
MUSIC_TRANSPORT = [
    ("music_play", "play"),
    ("music_pause", "pause"),
    ("music_next", "skip-next"),
    ("music_prev", "skip-previous"),
]

# --- TV page's app-launch row (bottom, one icon per configured app) — a
# couple of named streaming-service brand glyphs plus a generic fallback for
# any app in the room config that isn't one of them.
TV_APP_SIZE = 36
TV_APP = [
    ("tv_app_netflix", "netflix"),
    ("tv_app_youtube", "youtube"),
    ("tv_app_generic", "television"),
]

# --- TV page's bottom BACK/HOME/POWER bar — same slot as Music's transport
# bar, so sized to match its 42px icons.
TV_BAR_SIZE = 42
TV_BAR = [
    ("tv_back", "arrow-left"),
    ("tv_home", "home"),
    ("tv_power", "power"),
]

# --- Jump-to grid (Home tap) — one glyph per destination, all baked at one
# consistent size so the 2-wide grid of icon+label tiles reads uniformly,
# independent of whatever size any of these already exist at elsewhere.
JUMP_SIZE = 40
JUMP = [
    ("jump_status", "view-dashboard-outline"),
    ("jump_lighting", "lightbulb-on-outline"),
    ("jump_blinds", "blinds"),
    ("jump_music", "music-note-outline"),
    ("jump_tv", "television"),
    ("jump_xbox", "microsoft-xbox"),
    ("jump_climate", "thermostat"),
    ("jump_wifi", "wifi"),
    ("jump_settings", "cog"),
    ("jump_selftest", "progress-check"),
    ("jump_errors", "alert-circle-outline"),
]


def rasterize(name, px):
    path = os.path.join(SVG_DIR, name + ".svg")
    if not os.path.exists(path):
        sys.exit(f"ERROR: missing SVG: {path}")
    svg = open(path, encoding="utf-8").read()
    png = resvg_py.svg_to_bytes(
        svg_string=svg, width=px, height=px, style_sheet="path{fill:#000000}"
    )
    img = Image.open(io.BytesIO(bytes(png))).convert("RGBA")
    bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
    bg.paste(img, mask=img.split()[3])
    return bg.convert("L")


def pack(img, px):
    """1-bpp, MSB-first, bit 1 = transparent / bit 0 = ink. -> (bytes, opticalCenterY)."""
    pix = img.load()
    data = []
    sum_y = count = 0
    for y in range(px):
        for xb in range(0, px, 8):
            byte = 0
            for b in range(8):
                x = xb + b
                white = 1
                if x < px and pix[x, y] <= THRESHOLD:
                    white = 0
                    sum_y += y
                    count += 1
                byte |= white << (7 - b)
            data.append(byte)
    center = round(sum_y / count) if count else px // 2
    return data, center


def ident(s):
    return re.sub(r"[^A-Za-z0-9]", "_", s)


def emit(alias, mdi, px, out, sym="kWx"):
    data, center = pack(rasterize(mdi, px), px)
    arr = f"{sym}_{ident(alias)}_bits"
    body = ", ".join(f"0x{b:02X}" for b in data)
    out.append(f"// {alias}  (mdi: {mdi}, {px}px)")
    out.append(f"static const uint8_t {arr}[] = {{{body}}};")
    out.append(
        f"static const freeink::Icon {sym}_{ident(alias)} = "
        f"{{{px}, {px}, {center}, {arr}}};"
    )
    out.append("")


def main():
    out = [
        "#pragma once",
        "",
        "#include <string.h>",
        "",
        '#include "Icon.h"',
        "",
        "// ===========================================================================",
        "// Weather icons for the Standby screen — GENERATED by tools/gen_weather_icons.py",
        "// from Material Design Icons (@mdi/svg 7.4.47). Do not hand-edit; rerun the",
        "// generator. One big glyph per Home Assistant `weather` condition, plus small",
        "// chrome glyphs. weatherConditionIcon() maps a raw HA state string to its icon.",
        "// ===========================================================================",
        "",
        f"// Hero condition glyphs: {BIG_SIZE}px.",
    ]
    for alias, mdi in WEATHER:
        emit(alias, mdi, BIG_SIZE, out, sym="kWx")

    out.append(f"// Forecast-column condition glyphs: {FCAST_SIZE}px.")
    for alias, mdi in WEATHER:
        emit(alias, mdi, FCAST_SIZE, out, sym="kWxF")

    out.append(f"// Small chrome glyphs: {SMALL_SIZE}px.")
    for alias, mdi in SMALL:
        emit(alias, mdi, SMALL_SIZE, out, sym="kWx")

    out.append(f"// Control-shade glyphs: {CHROME_SIZE}px.")
    for alias, mdi in CHROME:
        emit(alias, mdi, CHROME_SIZE, out, sym="kWx")

    out.append(f"// Lighting card bulb glyphs: {BULB_SIZE}px.")
    for alias, mdi in BULB:
        emit(alias, mdi, BULB_SIZE, out, sym="kWx")

    out.append(f"// Climate page mode glyphs: {CLIMATE_MODE_SIZE}px.")
    for alias, mdi in CLIMATE_MODE:
        emit(alias, mdi, CLIMATE_MODE_SIZE, out, sym="kWx")

    out.append(f"// Climate page thermometer glyph: {CLIMATE_THERMO_SIZE}px.")
    for alias, mdi in CLIMATE_THERMO:
        emit(alias, mdi, CLIMATE_THERMO_SIZE, out, sym="kWx")

    out.append(f"// Blinds page position glyphs: {BLINDS_SIZE}px.")
    for alias, mdi in BLINDS:
        emit(alias, mdi, BLINDS_SIZE, out, sym="kWx")

    out.append(f"// Music page speaker glyphs: {MUSIC_SPEAKER_SIZE}px.")
    for alias, mdi in MUSIC_SPEAKER:
        emit(alias, mdi, MUSIC_SPEAKER_SIZE, out, sym="kWx")
    out.append(f"// Music page volume-button glyphs: {MUSIC_CHROME_SIZE}px.")
    for alias, mdi in MUSIC_CHROME:
        emit(alias, mdi, MUSIC_CHROME_SIZE, out, sym="kWx")
    out.append(f"// Music page transport glyphs: {MUSIC_TRANSPORT_SIZE}px.")
    for alias, mdi in MUSIC_TRANSPORT:
        emit(alias, mdi, MUSIC_TRANSPORT_SIZE, out, sym="kWx")

    out.append(f"// TV page app-launch row glyphs: {TV_APP_SIZE}px.")
    for alias, mdi in TV_APP:
        emit(alias, mdi, TV_APP_SIZE, out, sym="kWx")
    out.append(f"// TV page bottom bar glyphs: {TV_BAR_SIZE}px.")
    for alias, mdi in TV_BAR:
        emit(alias, mdi, TV_BAR_SIZE, out, sym="kWx")

    out.append(f"// Jump-to grid glyphs: {JUMP_SIZE}px.")
    for alias, mdi in JUMP:
        emit(alias, mdi, JUMP_SIZE, out, sym="kWx")

    # --- condition -> icon lookups ---------------------------------------
    for fn, sym, note in (
        ("weatherConditionIcon", "kWx", "hero"),
        ("weatherForecastIcon", "kWxF", "forecast-column"),
    ):
        out += [
            f"// Map a raw Home Assistant weather state to its {note} glyph;",
            "// unknown / empty falls back to the cloudy glyph.",
            f"inline const freeink::Icon& {fn}(const char* condition) {{",
            "  struct Row { const char* key; const freeink::Icon* icon; };",
            "  static const Row kRows[] = {",
        ]
        for alias, _ in WEATHER:
            key = alias.replace("_", "-")
            out.append(f'      {{"{key}", &{sym}_{ident(alias)}}},')
        out += [
            "  };",
            "  if (condition && *condition) {",
            "    for (const Row& r : kRows) {",
            "      if (strcmp(r.key, condition) == 0) return *r.icon;",
            "    }",
            "  }",
            f"  return {sym}_cloudy;",
            "}",
            "",
        ]

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))
    n = (len(WEATHER) * 2 + len(SMALL) + len(CHROME) + len(BULB) + len(CLIMATE_MODE) +
         len(CLIMATE_THERMO) + len(BLINDS) + len(MUSIC_SPEAKER) + len(MUSIC_CHROME) +
         len(MUSIC_TRANSPORT) + len(TV_APP) + len(TV_BAR) + len(JUMP))
    print(f"wrote {os.path.relpath(OUT, HERE)}: {n} icons")


if __name__ == "__main__":
    main()
