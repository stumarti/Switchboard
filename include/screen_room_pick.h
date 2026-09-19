#pragma once

// ===========================================================================
// screen_room_pick — Settings -> Select room: fetches the server's room list
// and lets the user pick which room this physical remote drives.
// ===========================================================================

#include "screen_common.h"
#include "screen_fwd.h"
#include "room_list_client.h"
#include "device_config_client.h"

namespace screen_room_pick {

inline int sel = 0;

inline void draw() {
  ui.clear();
  drawStatusBar("Select room");
  if (!roomlist::ok) {
    ui.text("No rooms", 0, 300, Ui::W, 28, TextAlign::Center, Color::Black);
    ui.text(roomlist::status, 0, 336, Ui::W, 20, TextAlign::Center, Color::DarkGray, 1,
            Ui::kFontSmall);
    commitFrame(Rf::Clean);
    return;
  }
  const int16_t top = static_cast<int16_t>(kStatusBarH + 12 + kPad);
  const int16_t rh = 82, inner = rh - 12;
  for (int i = 0; i < roomlist::count; ++i) {
    const int16_t y = static_cast<int16_t>(top + i * rh);
    const bool s = i == sel;
    const bool cur = !strcmp(roomlist::rooms[i].slug, deviceconfig::activeSlug);
    if (s) ui.fillRect(24, y, static_cast<int16_t>(Ui::W - 48), inner, Color::Black, 14);
    else   ui.strokeRect(24, y, static_cast<int16_t>(Ui::W - 48), inner, 2, 14);
    ui.text(roomlist::rooms[i].name, 48, static_cast<int16_t>(y + 14),
            static_cast<int16_t>(Ui::W - 200), 26, TextAlign::Left, s ? Color::White : Color::Black);
    ui.text(roomlist::rooms[i].slug, 48, static_cast<int16_t>(y + 42),
            static_cast<int16_t>(Ui::W - 200), 20, TextAlign::Left,
            s ? Color::LightGray : Color::DarkGray, 1, Ui::kFontSmall);
    if (cur)
      ui.text("current", static_cast<int16_t>(Ui::W - 150), static_cast<int16_t>(y + 24), 108, 22,
              TextAlign::Right, s ? Color::LightGray : Color::DarkGray, 1, Ui::kFontSmall);
  }
  commitFrame(Rf::Clean);
}

inline void enter() {
  stage = Stage::RoomPick;
  standbyIdleSinceMs = millis();
  sel = 0;
  ui.clear();
  drawStatusBar("Select room");
  ui.centered("Loading rooms...", 320, 28, Color::DarkGray);
  commitFrame(Rf::Clean);
  roomlist::fetch();  // small response; brief block on a deliberate action
  for (int i = 0; i < roomlist::count; ++i)
    if (!strcmp(roomlist::rooms[i].slug, deviceconfig::activeSlug)) sel = i;
  draw();
}

inline void pick(int i) {
  if (i < 0 || i >= roomlist::count) return;
  deviceconfig::saveSlug(roomlist::rooms[i].slug);
  deviceconfig::name[0] = 0;  // force a re-fetch to show the new room's name
  carouselPage = 0;
  kickWeatherRefresh();
  enterStandby();
}

}  // namespace screen_room_pick
