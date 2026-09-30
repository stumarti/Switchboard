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
#include "refresh_schedule.h"
#include "ota_policy.h"

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
  {
    // The server's state for a hub toggle follows the same rules: an open
    // cover, a locked lock and a playing speaker are all "on".
    JsonDocument hub;
    deserializeJson(hub, R"({"cover.b": {"state": "open"}, "lock.d": {"state": "locked"}, "media_player.s": {"state": "playing"}, "switch.c": {"state": "off"}})");
    bool b = false, d = false, sp = false, c = true, gone = true;
    CHECK(applyHubToggle(hub["cover.b"], "cover.b", b) && b);
    CHECK(applyHubToggle(hub["lock.d"], "lock.d", d) && d);
    CHECK(applyHubToggle(hub["media_player.s"], "media_player.s", sp) && sp);
    CHECK(applyHubToggle(hub["switch.c"], "switch.c", c) && !c);
    CHECK(!applyHubToggle(hub["switch.x"], "switch.x", gone) && gone);
  }
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

static void testRefreshSchedule() {
  std::puts("refresh_schedule: wakes on the clock, staggered");
  using refreshschedule::alignedSleepSec;
  struct tm t = {};
  t.tm_year = 126; t.tm_mon = 8; t.tm_mday = 28; t.tm_hour = 12; t.tm_min = 7; t.tm_sec = 30;
  const int64_t now = haclient::epochOf(t);
  CHECK(now == 1790597250);  // 2026-09-28 12:07:30 UTC
  // Every 30 min, UTC: the next mark is 12:30:00, 22.5 min away; +12 s stagger.
  CHECK(alignedSleepSec(now, 0, 1800, 0) == 1350);
  CHECK(alignedSleepSec(now, 0, 1800, 12) == 1362);
  // Hourly in local time UTC+5:30 (17:37:30 local): the next hour is 18:00.
  CHECK(alignedSleepSec(now, 330, 3600, 0) == 1350);
  // Every 15 min, UTC-4 (08:07:30): 08:15.
  CHECK(alignedSleepSec(now, -240, 900, 0) == 450);
  // Woke a little early for 12:30 (12:29:20, drift): that refresh counts, so
  // the next is 13:00, not 40 s away.
  t.tm_min = 29; t.tm_sec = 20;
  CHECK(alignedSleepSec(haclient::epochOf(t), 0, 1800, 0) == 1840);
  // Woke late (12:31): straight on to 13:00.
  t.tm_min = 31; t.tm_sec = 0;
  CHECK(alignedSleepSec(haclient::epochOf(t), 0, 1800, 0) == 1740);
  // A stagger longer than the interval wraps: 20 min 30 s on 15 min = +5:30.
  CHECK(alignedSleepSec(now, 0, 900, 1230) == alignedSleepSec(now, 0, 900, 330));
  // A stagger past the mark is still aimed at: 12:30:05 with +20 s -> 12:30:20
  // is too close (15 s), so 13:00:20.
  t.tm_min = 30; t.tm_sec = 5;
  CHECK(alignedSleepSec(haclient::epochOf(t), 0, 1800, 20) == 1815);

  // Not aligned, or no clock this boot: the plain interval.
  deviceconfig::refreshAligned = false;
  CHECK(refreshschedule::sleepSec(1800) == 1800);
  deviceconfig::refreshAligned = true;
  haclient::clockThisBoot = false;
  CHECK(refreshschedule::sleepSec(1800) == 1800);
  haclient::parseHttpDate("Mon, 28 Sep 2026 12:07:30 GMT");  // host millis() is 0
  deviceconfig::utcOffsetMin = 60;
  deviceconfig::refreshStaggerSec = 30;
  CHECK(refreshschedule::sleepSec(1800) == 1380);  // 13:07:30 local -> 13:30:30
  CHECK(refreshschedule::sleepSec(7 * 60) == 7 * 60);  // doesn't divide a day: not aligned

  JsonDocument doc;
  CHECK(deserializeJson(doc, R"({"standby":{"refreshIntervalMin":15,"refreshAligned":true,"utcOffsetMin":-300,
    "refreshStaggerSec":42}})") == DeserializationError::Ok);
  deviceconfig::applyJson(doc.as<JsonVariantConst>());
  CHECK(deviceconfig::refreshIntervalMin == 15 && deviceconfig::refreshAligned);
  CHECK(deviceconfig::utcOffsetMin == -300 && deviceconfig::refreshStaggerSec == 42);
  JsonDocument old;
  CHECK(deserializeJson(old, R"({"standby":{"refreshIntervalMin":30}})") == DeserializationError::Ok);
  deviceconfig::applyJson(old.as<JsonVariantConst>());
  CHECK(!deviceconfig::refreshAligned && deviceconfig::refreshStaggerSec == 0);  // an older server
}

static void testOtaPolicy() {
  std::puts("ota: update window, battery, retries, the server's offer");
  using namespace otapolicy;
  CHECK(inWindow(3, 2, 5) && !inWindow(5, 2, 5) && !inWindow(1, 2, 5));
  CHECK(inWindow(23, 22, 4) && inWindow(2, 22, 4) && !inWindow(12, 22, 4));  // past midnight
  CHECK(!inWindow(-1, 2, 5));  // no clock: never
  CHECK(scheduledDue(true, "v0.2.0", 3, 2, 5, 80, 30, 0));
  CHECK(!scheduledDue(true, "", 3, 2, 5, 80, 30, 0));           // nothing offered
  CHECK(!scheduledDue(true, "v0.2.0", 3, 2, 5, 25, 30, 0));     // battery too low
  CHECK(!scheduledDue(true, "v0.2.0", 3, 2, 5, 0, 30, 0));      // battery not read yet
  CHECK(!scheduledDue(true, "v0.2.0", 3, 2, 5, 80, 30, kMaxScheduledTries));  // gave up on it
  CHECK(!scheduledDue(false, "v0.2.0", 3, 2, 5, 80, 30, 0));
  // "Update now": any hour, schedule or not; still needs an offer and battery.
  CHECK(nowDue(true, true, "v0.2.0", 80, 30));
  CHECK(!nowDue(true, false, "v0.2.0", 80, 30));
  CHECK(!nowDue(true, true, "", 80, 30));
  CHECK(!nowDue(true, true, "v0.2.0", 25, 30));
  CHECK(!nowDue(false, true, "v0.2.0", 80, 30));

  JsonDocument doc;
  CHECK(deserializeJson(doc, R"({"firmware":{"enabled":true,"button":true,"now":true,"schedule":{"fromHour":2,"toHour":5},
    "minBattery":40,"offer":{"version":"v0.2.0","size":1510672,
    "sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}}})") == DeserializationError::Ok);
  deviceconfig::applyJson(doc.as<JsonVariantConst>());
  CHECK(deviceconfig::fwEnabled && deviceconfig::fwButton && deviceconfig::fwNow);
  CHECK(deviceconfig::fwFromHour == 2 && deviceconfig::fwToHour == 5 && deviceconfig::fwMinBattery == 40);
  CHECK_STR(deviceconfig::fwOfferVersion, "v0.2.0");
  CHECK(deviceconfig::fwOfferSize == 1510672);
  // An incomplete offer (a short checksum) is no offer; updates off = nothing.
  JsonDocument bad;
  CHECK(deserializeJson(bad, R"({"firmware":{"enabled":true,"offer":{"version":"v0.2.0","size":10,"sha256":"abc"}}})") ==
        DeserializationError::Ok);
  deviceconfig::applyJson(bad.as<JsonVariantConst>());
  CHECK_STR(deviceconfig::fwOfferVersion, "");
  CHECK(!deviceconfig::fwButton && !deviceconfig::fwNow && deviceconfig::fwFromHour == -1);
  JsonDocument off;
  CHECK(deserializeJson(off, R"({"firmware":{"enabled":false,"button":true}})") == DeserializationError::Ok);
  deviceconfig::applyJson(off.as<JsonVariantConst>());
  CHECK(!deviceconfig::fwEnabled && !deviceconfig::fwButton);
}

int main() {
  testOtaPolicy();
  testRefreshSchedule();
  testHub();
  testParsers();
  testReceiver();
  testRoomConfig();
  testClock();
  testHosts();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
