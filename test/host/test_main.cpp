// Host tests: the firmware's pure logic, compiled with g++ on a PC (no
// hardware, no network) — see test/host/run.sh. Covers the Home Assistant
// state parsers every page uses (ha_client.h) and the HTTP helpers'
// host-name handling (http_json.h).

#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ha_client.h"
#include "device_config_client.h"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                           \
  do {                                                                        \
    ++g_checks;                                                               \
    if (!(cond)) {                                                            \
      ++g_failures;                                                           \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                         \
  } while (0)
#define CHECK_STR(a, b) CHECK(std::strcmp((a), (b)) == 0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 0.01)

// The server's /api/devices/<slug>/state answer, as the firmware gets it.
static const char* kState = R"({
  "states": {
    "weather.home": {"state": "partlycloudy", "attributes": {"temperature": 18.4, "humidity": 60, "wind_speed": 12, "wind_speed_unit": "km/h", "uv_index": 3}},
    "climate.room": {"state": "heat", "attributes": {"temperature": 21, "current_temperature": 20.5, "min_temp": 7, "max_temp": 30, "target_temp_step": 0.5, "hvac_modes": ["off", "heat", "auto"]}},
    "light.all": {"state": "on", "attributes": {"brightness": 128, "effect": "Rainbow",
      "effect_list": ["Solid", "Rainbow", "", "An effect name far too long to send back"]}},
    "light.a": {"state": "off", "attributes": {}},
    "light.gone": {"state": "unavailable", "attributes": {}},
    "sensor.t": {"state": "19.25", "attributes": {}},
    "sensor.bad": {"state": "unknown", "attributes": {}},
    "cover.x": {"state": "open", "attributes": {"current_position": 73}},
    "media_player.m": {"state": "playing", "attributes": {"media_title": "Song", "media_artist": "Band", "entity_picture": "/api/media_player_proxy/media_player.m?cache=1", "volume_level": 0.35, "is_volume_muted": false}}
  },
  "forecast": {"weather.home": [
    {"datetime": "2026-09-21T00:00:00Z", "condition": "rainy", "temperature": 16, "templow": 8},
    {"datetime": "2026-09-22T00:00:00Z", "condition": "sunny", "temperature": 19.5, "templow": 9},
    {"datetime": "2026-09-23T00:00:00Z", "condition": "cloudy", "temperature": 18, "templow": 7}
  ]}
})";

static void testParsers() {
  std::puts("ha_client: apply* parsers");
  JsonDocument doc;
  CHECK(deserializeJson(doc, kState) == DeserializationError::Ok);
  JsonObjectConst st = doc["states"];
  using namespace haclient;

  CHECK(applyWeather(st["weather.home"]));
  CHECK_STR(weather.condition, "partlycloudy");
  CHECK(weather.hasTemp);
  CHECK_NEAR(weather.temp, 18.4f);
  CHECK(weather.humidity == 60);
  CHECK_STR(weather.windUnit, "km/h");

  CHECK(applyClimate(st["climate.room"]));
  CHECK_STR(climate.mode, "heat");
  CHECK_NEAR(climate.temp, 20.5f);
  CHECK_NEAR(climate.target, 21.0f);
  CHECK_NEAR(climate.maxTemp, 30.0f);
  CHECK(climate.modeCount == 3);
  CHECK_STR(climate.modes[2], "auto");

  CHECK(applyLight(st["light.all"]));
  CHECK(lightGroup.on);
  CHECK(lightGroup.brightnessPct == 50);  // 128/255
  CHECK_STR(lightGroup.effect, "Rainbow");
  CHECK(lightGroup.effectCount == 2);  // a blank name and one too long to send back are left out
  CHECK_STR(lightGroup.effects[1], "Rainbow");

  bool on = true;
  CHECK(applyOnOff(st["light.a"], on));
  CHECK(!on);
  // Not in the answer (HA didn't have it): the value is left alone.
  bool untouched = true;
  CHECK(!applyOnOff(st["light.missing"], untouched));
  CHECK(untouched);

  float v = 0;
  CHECK(applySensorValue(st["sensor.t"], v));
  CHECK_NEAR(v, 19.25f);
  float keep = 5;
  CHECK(!applySensorValue(st["sensor.bad"], keep));  // "unknown" is no reading

  Cover c;
  CHECK(applyCover(st["cover.x"], c));
  CHECK_STR(c.state, "open");
  CHECK(c.hasPosition && c.position == 73);

  CHECK(applyMedia(st["media_player.m"]));
  CHECK_STR(media.state, "playing");
  CHECK_STR(media.title, "Song");
  CHECK_STR(media.artist, "Band");
  CHECK_STR(media.picture, "/api/media_player_proxy/media_player.m?cache=1");
  CHECK(media.hasVolume && media.volumePct == 35);
  CHECK(media.hasMuted && !media.muted);

  CHECK(applyForecast(doc["forecast"]["weather.home"].as<JsonArrayConst>()));
  CHECK(forecast.count == 3);
  CHECK_STR(forecast.day[1].condition, "sunny");
  CHECK_NEAR(forecast.day[1].hi, 19.5f);
  CHECK_NEAR(forecast.day[2].lo, 7.0f);

  // An entity HA had no state for: not ok, and says why.
  CHECK(!applyWeather(st["weather.nope"]));
  CHECK(!weather.ok);
}

static void testClock() {
  std::puts("ha_client: HTTP Date -> clock");
  using namespace haclient;
  CHECK(dayOfWeek(2026, 9, 28) == 1);  // a Monday
  CHECK(dayOfWeek(2000, 1, 1) == 6);   // a Saturday
  clockValid = false;
  parseHttpDate("Mon, 28 Sep 2026 12:30:05 GMT");
  CHECK(clockValid);
  CHECK(clockUtc.tm_year == 126 && clockUtc.tm_mon == 8 && clockUtc.tm_mday == 28);
  CHECK(clockUtc.tm_hour == 12 && clockUtc.tm_min == 30 && clockUtc.tm_sec == 5);
  CHECK(clockUtc.tm_wday == 1);
  clockValid = false;
  parseHttpDate("not a date");
  CHECK(!clockValid);
}

static void testHosts() {
  std::puts("http_json: host names");
  char label[24];
  httpjson::mdnsLabel("homeassistant.local", label, sizeof(label));
  CHECK_STR(label, "homeassistant");
  httpjson::mdnsLabel("switchboard", label, sizeof(label));
  CHECK_STR(label, "switchboard");
  httpjson::mdnsLabel("192.168.1.4", label, sizeof(label));
  CHECK_STR(label, "");  // a literal address needs no lookup

  CHECK(httpjson::resolveHost("192.168.1.4") == IPAddress(192, 168, 1, 4));
  // An mDNS name: remembered once found, forgotten on a failed connect.
  httpjson::rememberHost("switchboard", static_cast<uint32_t>(IPAddress(10, 0, 0, 7)));
  CHECK(httpjson::resolveHost("switchboard.local") == IPAddress(10, 0, 0, 7));
  httpjson::forgetHost("switchboard");
  CHECK(httpjson::resolveHost("switchboard") == IPAddress(0, 0, 0, 0));  // host stub: mDNS misses
}

static void testReceiver() {
  std::puts("ha_client: Enigma2 receiver");
  using namespace haclient;
  JsonDocument doc;
  CHECK(deserializeJson(doc, R"({"state":"on","attributes":{"media_channel":"BBC One HD","media_title":"BBC One HD",
    "media_series_title":"Six O'Clock News","source":"BBC One HD","volume_level":0.4,"is_volume_muted":false}})") ==
        DeserializationError::Ok);
  CHECK(applyReceiver(doc.as<JsonVariantConst>()));
  CHECK_STR(receiver.channel, "BBC One HD");
  CHECK_STR(receiver.programme, "Six O'Clock News");
  CHECK_STR(receiver.source, "BBC One HD");
  CHECK(receiver.hasVolume && receiver.volumePct == 40);
  // Off, and no volume in this report: the last level is kept.
  JsonDocument off;
  CHECK(deserializeJson(off, R"({"state":"off","attributes":{}})") == DeserializationError::Ok);
  CHECK(applyReceiver(off.as<JsonVariantConst>()));
  CHECK_STR(receiver.state, "off");
  CHECK(receiver.volumePct == 40);
  CHECK_STR(receiver.channel, "");

  // The server's now / next and picon srcs; an older server sends none.
  JsonDocument rx;
  CHECK(deserializeJson(rx, R"({"receiver":{"source":"box",
    "now":{"time":"18:00–18:30","title":"Six O'Clock News","desc":"The news."},
    "next":{"time":"18:30","title":"Regional News","desc":""},
    "picon":"rx:den:/picon/1_0_19_1B1F_802_2_11A0000_0_0_0.png",
    "favourites":["rx:den:/picon/bbc1.png","",""]}})") == DeserializationError::Ok);
  applyReceiverInfo(rx["receiver"].as<JsonObjectConst>());
  CHECK_STR(receiverInfo.nowTime, "18:00–18:30");
  CHECK_STR(receiverInfo.nowTitle, "Six O'Clock News");
  CHECK_STR(receiverInfo.nextTime, "18:30");
  CHECK_STR(receiverInfo.nextTitle, "Regional News");
  CHECK_STR(receiverInfo.picon, "rx:den:/picon/1_0_19_1B1F_802_2_11A0000_0_0_0.png");
  CHECK_STR(receiverInfo.favPicon[0], "rx:den:/picon/bbc1.png");
  CHECK_STR(receiverInfo.favPicon[1], "");
  JsonDocument none;
  applyReceiverInfo(none["receiver"].as<JsonObjectConst>());
  CHECK_STR(receiverInfo.nowTitle, "");
  CHECK_STR(receiverInfo.picon, "");
}

static void testRoomConfig() {
  std::puts("device_config: TV apps, the receiver, light controls, hub quick actions");
  JsonDocument doc;
  CHECK(deserializeJson(doc, R"({"name":"Den",
    "screens":{"receiver":true},
    "lighting":{"group":{"enabled":true,"entity":"light.den","controls":{"brightness":true,"color":true,"effects":true}}},
    "hub":{"quickActionsEnabled":false,"items":[]},
    "tv":{"appList":[{"name":"Plex","launch":"com.plexapp.android","icon":"plex"},{"name":"Empty","launch":""},
                     {"name":"YouTube","launch":"com.google.android.youtube.tv","icon":""}],
          "apps":{"Plex":"com.plexapp.android"}},
    "receiver":{"name":"Vu+ Uno","mediaPlayerEntity":"media_player.vu","channels":[
      {"name":"BBC One","source":"BBC One HD","icon":"alpha-b-box"},{"name":"No source","source":""}]}})") ==
        DeserializationError::Ok);
  deviceconfig::applyJson(doc.as<JsonVariantConst>());
  CHECK(deviceconfig::tvAppCount == 2);  // an app with no launch value is skipped
  CHECK_STR(deviceconfig::tvApps[0].name, "Plex");
  CHECK_STR(deviceconfig::tvApps[0].icon, "plex");
  CHECK_STR(deviceconfig::tvApps[1].pkg, "com.google.android.youtube.tv");
  CHECK(deviceconfig::screenReceiver);
  CHECK(deviceconfig::lightGroupColor && deviceconfig::lightGroupEffects && !deviceconfig::lightGroupColorTemp);
  CHECK(!deviceconfig::hubQuickActions);
  CHECK_STR(deviceconfig::receiverName, "Vu+ Uno");
  CHECK(deviceconfig::receiverChannelCount == 1);
  CHECK_STR(deviceconfig::receiverChannels[0].source, "BBC One HD");

  // An older server: the TV app map, and no receiver (its page stays off).
  JsonDocument old;
  CHECK(deserializeJson(old, R"({"tv":{"apps":{"YouTube":"com.google.android.youtube.tv"}}})") ==
        DeserializationError::Ok);
  deviceconfig::applyJson(old.as<JsonVariantConst>());
  CHECK(deviceconfig::tvAppCount == 1);
  CHECK_STR(deviceconfig::tvApps[0].name, "YouTube");
  CHECK_STR(deviceconfig::tvApps[0].icon, "");
  CHECK(!deviceconfig::screenReceiver);
  CHECK(deviceconfig::receiverChannelCount == 0);
  CHECK(!deviceconfig::lightGroupColor && !deviceconfig::lightGroupEffects);
  CHECK(deviceconfig::hubQuickActions);  // no hub switch: quick actions stay on
}

static void testHub() {
  std::puts("quick access: toggle states, services and service data");
  using namespace haclient;
  CHECK(hubStateActive("cover", "open") && !hubStateActive("cover", "closed"));
  CHECK(hubStateActive("lock", "locked") && !hubStateActive("lock", "unlocked"));
  CHECK(hubStateActive("vacuum", "cleaning") && !hubStateActive("vacuum", "docked"));
  CHECK(hubStateActive("media_player", "playing") && !hubStateActive("media_player", "off"));
  CHECK(hubStateActive("light", "on") && !hubStateActive("switch", "off"));
  CHECK_STR(hubToggleService("cover", true), "open_cover");
  CHECK_STR(hubToggleService("vacuum", false), "return_to_base");
  CHECK_STR(hubToggleService("lock", true), "lock");
  CHECK_STR(hubToggleService("fan", false), "turn_off");

  // The server sends service data as the JSON text typed in the editor.
  JsonDocument doc;
  CHECK(deserializeJson(doc, R"({"hub":{"items":[
    {"name":"Dim","target":"lighting","action":{"type":"run","entity":"light.den","service":"light.turn_on",
      "data":"{\"brightness_pct\": 40, \"rgb_color\": [255, 0, 0]}"}},
    {"name":"Bad","target":"blinds","action":{"type":"run","service":"script.turn_on","data":"not json"}},
    {"name":"Obj","target":"status","action":{"type":"run","service":"notify.me","data":{"message":"hi"}}}]}})") ==
        DeserializationError::Ok);
  deviceconfig::applyJson(doc.as<JsonVariantConst>());
  CHECK(deviceconfig::hubItemCount == 3);
  CHECK_STR(deviceconfig::hubItems[0].actionData, "\"brightness_pct\":40,\"rgb_color\":[255,0,0]");
  CHECK_STR(deviceconfig::hubItems[1].actionData, "");
  CHECK_STR(deviceconfig::hubItems[2].actionData, "\"message\":\"hi\"");
  CHECK_STR(deviceconfig::hubItems[1].target, "blinds");
}

int main() {
  testHub();
  testParsers();
  testReceiver();
  testRoomConfig();
  testClock();
  testHosts();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
