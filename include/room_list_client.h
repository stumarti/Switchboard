#pragma once

// ===========================================================================
// Room-list client — GETs the Switchboard server's /api/devices (the list of
// configured rooms/devices) for the Settings -> Select room picker.
// ===========================================================================

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "http_json.h"

namespace roomlist {

struct Room {
  char slug[32] = "";
  char name[32] = "";
};

inline Room rooms[10];
inline int count = 0;
inline bool ok = false;
inline char status[64] = "";

inline bool fetch() {
  ok = false;
  count = 0;

  JsonDocument doc;
  if (!httpjson::get(SWITCHBOARD_SERVER_HOST, SWITCHBOARD_SERVER_PORT, "/api/devices",
                     /*bearer=*/nullptr, doc, status, sizeof(status))) {
    return false;
  }

  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    if (count >= static_cast<int>(sizeof(rooms) / sizeof(rooms[0]))) break;
    const char* slug = o["slug"] | "";
    if (!*slug) continue;
    snprintf(rooms[count].slug, sizeof(rooms[count].slug), "%s", slug);
    snprintf(rooms[count].name, sizeof(rooms[count].name), "%s", o["name"] | slug);
    ++count;
  }

  ok = count > 0;
  if (!ok) snprintf(status, sizeof(status), "no rooms on server");
  return ok;
}

}  // namespace roomlist
