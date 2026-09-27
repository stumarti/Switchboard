#pragma once

// ===========================================================================
// default_font_table — the 5 font faces this firmware ships with (Atkinson
// Hyperlegible, atkinson_font.h), compiled in as the guaranteed fallback -
// same role as default_icon_table.h, just for fonts::get() (fonts.h) instead
// of icons::get(). Slot names match Ui::begin()'s (ui.h) binding names and
// Switchboard-Server's lib/assets/font-slots.js `slot` field exactly.
// ===========================================================================

#include <FreeInkUIFont.h>
#include "atkinson_font.h"

struct NamedFont {
  const char* slot;
  const freeink::ui::BitmapFont* font;
};

static const NamedFont kDefaultFonts[] = {
    {"small", &freeink::ui::kAtkinsonHL14Font},
    {"default", &freeink::ui::kAtkinsonHL24Font},
    {"temp", &freeink::ui::kAtkinsonHLDigits68Font},
    {"font12", &freeink::ui::kAtkinsonHL12Font},
    {"font28", &freeink::ui::kAtkinsonHL28Font},
};

static constexpr int kDefaultFontCount = sizeof(kDefaultFonts) / sizeof(kDefaultFonts[0]);
