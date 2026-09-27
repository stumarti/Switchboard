#pragma once

// ===========================================================================
// fonts — the one place ui.h looks up a font face by slot name, instead of
// binding a compile-time BitmapFont constant directly. Same pack-first,
// fallback-to-compiled-in shape as icons.h — see that file's header comment.
// ===========================================================================

#include <string.h>

#include "default_font_table.h"
#include "font_pack.h"

namespace fonts {

inline const freeink::ui::BitmapFont& get(const char* slot) {
  if (const freeink::ui::BitmapFont* fromPack = fontpack::find(slot)) return *fromPack;

  for (int i = 0; i < kDefaultFontCount; ++i) {
    if (!strcmp(kDefaultFonts[i].slot, slot)) return *kDefaultFonts[i].font;
  }
  return *kDefaultFonts[0].font;
}

inline bool reloadFromPack() { return fontpack::load(); }

}  // namespace fonts
