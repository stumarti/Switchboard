// ===========================================================================
// Switchboard — X4 Pro boot sequence
//
//   1. SPLASH  : Switchboard logo (MDI 'remote' mark) + name + slogan
//   2. WIFI    : run through the Wi-Fi connection, live status on screen
//   3. STANDBY : HA weather/climate — the resting screen; boots here with the
//                frontlight lit, self-sleeps (frontlight off + moon) after 30s
//   4. HOME    : blank layout scaffold — a key/tap on Standby opens it
//   5. DEBUG   : a debug screen (chip, RAM, panel, wifi) — hold Home to reach it
//
// Built on the FreeInk SDK HAL (EInkDisplay / InputManager / BoardConfig),
// selected for the X4 Pro by -DFREEINK_DEVICE_X4PRO in platformio.ini.
//
// --- File layout -----------------------------------------------------------
// Every screen lives in its own include/screen_*.h, each exposing a small,
// consistent surface — draw() to render it, enter() to transition into it,
// and (where the screen owns live HA data) kick()/fetch() to load it. This
// file keeps only what's genuinely cross-cutting: the input layer, sleep/wake
// (deep sleep + the fast-wake path), and the carousel itself — paging,
// the carousel dots, the jump list, and the shared weather/HA data fetch that
// feeds all four carousel pages.
// ===========================================================================

#include <Arduino.h>
#include <math.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <InputManager.h>
#include <FrontlightManager.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <XteinkDetect.h>
#include <PowerManager.h>

#include "config.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "room_list_client.h"
#include "ha_client.h"
#include "persist.h"

// screen_fwd.h forward-declares the handful of carousel/sleep-wake entry
// points defined at the bottom of this file that the screens below call into.
// Every other cross-screen reference is resolved by include order: a screen
// is only included after the screen_*.h's — and this file's own general
// state (the frontlight section below) — it depends on. The rest of the
// screen_*.h includes sit further down, after that shared state exists.
#include "screen_common.h"
#include "screen_fwd.h"

// ===========================================================================
// Input layer — a dedicated FreeRTOS task, so a slow e-ink refresh on the main
// loop can never drop a press. The task owns input.update(); the loop drains
// the event queue and reads the shared level state and must NOT call
// input.update() / input.was*() itself.
// ===========================================================================
enum class Ev : uint8_t { BtnLeft, BtnRight, BtnPower, HomeTap, HomeLong, Tap, Swipe };
struct InEvent {
  Ev ev;
  float a, b, c, d;
};

// One frame's worth of input, assembled from the queue + level state. All
// touch coordinates are LOGICAL screen pixels (Ui::touchToLogical applied).
struct InFrame {
  bool btnLeft, btnRight, btnPower;
  bool homeTap, homeLong;
  bool touchPress;   int16_t px, py;              // synthesized touch-down edge
  bool touchHeld;    int16_t hx, hy;              // current position while held
  bool tap;          int16_t tx, ty;
  bool swipe;        int16_t sx0, sy0, sx1, sy1;  // endpoints
};

static QueueHandle_t g_inQueue = nullptr;
static volatile bool     g_powerHeld = false;
static volatile uint32_t g_powerHeldMs = 0;
static volatile bool     g_touchDown = false;
static volatile float    g_touchNx = 0, g_touchNy = 0;

static void inputTask(void*) {
  const uint8_t btnIdx[3] = {InputManager::BTN_UP, InputManager::BTN_DOWN, InputManager::BTN_POWER};
  const Ev btnEv[3] = {Ev::BtnLeft, Ev::BtnRight, Ev::BtnPower};
  for (;;) {
    input.update();

    InEvent e{};
    for (int i = 0; i < 3; ++i)
      if (input.wasPressed(btnIdx[i])) { e.ev = btnEv[i]; xQueueSend(g_inQueue, &e, 0); }

    if (input.wasHomeKeyLongPressed()) { e.ev = Ev::HomeLong; xQueueSend(g_inQueue, &e, 0); }
    else if (input.wasHomeKeyTapped()) { e.ev = Ev::HomeTap;  xQueueSend(g_inQueue, &e, 0); }

    float x, y, x2, y2;
    if (input.wasSwipe(x, y, x2, y2)) {
      e.ev = Ev::Swipe; e.a = x; e.b = y; e.c = x2; e.d = y2;
      xQueueSend(g_inQueue, &e, 0);
    } else if (input.wasTouchTap(x, y)) {
      e.ev = Ev::Tap; e.a = x; e.b = y;
      xQueueSend(g_inQueue, &e, 0);
    }

    g_powerHeld = input.isPressed(InputManager::BTN_POWER);
    g_powerHeldMs = input.getPowerButtonHeldTime();
    float hx, hy;
    g_touchDown = input.isTouchHeldAt(hx, hy);
    if (g_touchDown) { g_touchNx = hx; g_touchNy = hy; }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Start the input task. Safe to call once, after input.begin(). Until then the
// boot sequence (splash / Wi-Fi provisioning) polls input synchronously.
static void startInputTask() {
  if (g_inQueue) return;
  g_inQueue = xQueueCreate(32, sizeof(InEvent));
  // Priority 2 — above the Arduino loop task (1) so it preempts even a
  // spin-waiting e-ink refresh. Same core as loop; core 0 runs the Wi-Fi stack.
  xTaskCreatePinnedToCore(inputTask, "sb_input", 4096, nullptr, 2, nullptr, 1);
}

// A tap anywhere (returns normalized 0..1 position too).
static bool tapped(float& nx, float& ny) {
  return input.hasTouch() && input.wasTouchTap(nx, ny);
}

// ===========================================================================
// Frontlight — general sleep/wake state: mirrored into RTC memory before
// every deep sleep so a wake (fast wake to Home, or a Standby timer wake)
// comes back with the lamp exactly as the user left it. The rail is cut for
// sleep, so without this it always resumes dark. The control shade
// (screen_shade.h) reads/writes flBrightPct/flWarmPct and calls the apply*/
// ctlStep* helpers below.
// ===========================================================================
static uint8_t flBrightPct = 50;
static uint8_t flWarmPct = 50;  // 0 = cool, 100 = warm

RTC_DATA_ATTR static bool    rtcFlOn        = false;
RTC_DATA_ATTR static uint8_t rtcFlBrightPct = 50;
RTC_DATA_ATTR static uint8_t rtcFlWarmPct   = 50;

// screen_shade.h isn't included until below (it needs flBrightPct/flWarmPct,
// defined here, already visible) — but ctlStepBrightness/ctlStepWarmth need
// to flag it dirty, so forward-declare just that one flag.
namespace screen_shade { extern bool dirty; }

static bool frontlightIsOn() { return frontlight.present() && frontlight.brightness() > 0; }

static void frontlightSetOn(bool on) {
  if (!frontlight.present()) return;
  if (on && flBrightPct == 0) flBrightPct = 50;  // was turned all the way down
  frontlight.setBrightness(on ? flBrightPct : 0);
  if (on && frontlight.hasColorTemperature()) frontlight.setColorTemperature(flWarmPct);
}

static void applyBrightness() {
  if (flBrightPct > 100) flBrightPct = 100;
  if (frontlight.present()) frontlight.setBrightness(flBrightPct);  // 0 == off
}
static void applyWarmth() {
  if (flWarmPct > 100) flWarmPct = 100;
  if (frontlight.present() && frontlight.hasColorTemperature() && flBrightPct > 0)
    frontlight.setColorTemperature(flWarmPct);
}
static void ctlStepBrightness(int d) {
  const int n = static_cast<int>(flBrightPct) + d;
  flBrightPct = static_cast<uint8_t>(n < 0 ? 0 : n > 100 ? 100 : n);
  applyBrightness();
  screen_shade::dirty = true;
}
static void ctlStepWarmth(int d) {
  const int n = static_cast<int>(flWarmPct) + d;
  flWarmPct = static_cast<uint8_t>(n < 0 ? 0 : n > 100 ? 100 : n);
  applyWarmth();
  screen_shade::dirty = true;
}

// Snapshot the live frontlight state into RTC memory. Call just before a deep
// sleep, while the rail is still up so brightness() reads true.
static void saveFrontlightToRtc() {
  rtcFlOn        = frontlightIsOn();
  rtcFlBrightPct = flBrightPct;
  rtcFlWarmPct   = flWarmPct;
}

// Restore the frontlight from RTC memory (brightness/warmth levels; on/off per
// rtcFlOn). Call once, after frontlight.begin(), on a wake out of Standby.
static void restoreFrontlightFromRtc() {
  flBrightPct = rtcFlBrightPct > 100 ? 100 : rtcFlBrightPct;
  flWarmPct   = rtcFlWarmPct > 100 ? 100 : rtcFlWarmPct;
  if (!frontlight.present()) return;
  if (rtcFlOn) {
    frontlight.setBrightness(flBrightPct);
    if (frontlight.hasColorTemperature()) frontlight.setColorTemperature(flWarmPct);
  } else {
    frontlight.off();
  }
}

// True while the background weather/HA refresh (weatherTask, defined below
// with the rest of that machinery) is in flight. Declared this early, before
// the screen_*.h includes, so screen_climate/lighting/blinds's own kick()
// functions can gate their action tasks on it too — without this, an action
// task and the weather task can run concurrently and race over shared mDNS/
// HTTP resources.
static volatile bool g_weatherBusy = false;

// The rest of the screens — included here, rather than up top with
// screen_common/screen_fwd, because screen_shade.h needs flBrightPct/
// flWarmPct/applyBrightness/applyWarmth (just defined above) already visible;
// each is otherwise only included after the screen_*.h's it itself depends on.
#include "screen_wifi.h"
#include "screen_settings_info.h"
#include "screen_room_pick.h"
#include "screen_developer.h"
#include "screen_timeouts.h"
#include "screen_settings.h"
#include "screen_shade.h"
#include "screen_status.h"
#include "screen_climate.h"
#include "screen_lighting.h"
#include "screen_blinds.h"
#include "screen_music.h"
#include "screen_tv.h"
#include "screen_wifi_networks.h"
#include "screen_debug.h"
#include "screen_error.h"
#include "screen_splash.h"

// ===========================================================================
// The carousel — paging through the resting screens. Page 0 is the
// weather/status page (header = room name); pages 1/2/6 are the Lighting/
// Blinds/Climate control screens; the rest are scaffolds ("coming soon").
// The carousel stays awake and interactive for idleToSleepMs() (Settings ->
// Timeouts -> Screen timeout); then it powers the frontlight down, stamps a
// moon on the status bar, and deep-sleeps until the refresh timer or a
// physical button.
// ===========================================================================
static constexpr uint8_t kCarouselPages = 8;
// Climate last (not Wifi) so it's one Left-press away from Status, wrapping
// around the end of the carousel — the page swapped in most often, per request.
static const char* const kCarouselNames[kCarouselPages] = {
    "Status", "Lighting", "Blinds", "Music", "TV", "Xbox", "Wifi", "Climate"};
static constexpr uint8_t kPageLighting = 1;
static constexpr uint8_t kPageBlinds   = 2;
static constexpr uint8_t kPageMusic    = 3;
static constexpr uint8_t kPageTv       = 4;
static constexpr uint8_t kPageXbox     = 5;
static constexpr uint8_t kPageWifi     = 6;
static constexpr uint8_t kPageClimate  = 7;

// Per-room config (deviceconfig::screens.*) can hide any page but Status
// (page 0, which has no toggle — it's always shown).
static bool pageEnabled(uint8_t page) {
  switch (page) {
    case kPageLighting: return deviceconfig::screenLighting;
    case kPageBlinds:   return deviceconfig::screenBlinds;
    case kPageMusic:    return deviceconfig::screenMusic;
    case kPageTv:       return deviceconfig::screenTv;
    case kPageXbox:     return deviceconfig::screenXbox;
    case kPageClimate:  return deviceconfig::screenClimate;
    case kPageWifi:     return deviceconfig::screenWifi;
    default:            return true;  // Status, and any future page with no toggle
  }
}
// If the current page just got hidden (a config refresh disabled it while
// the carousel was sitting on it), step forward to the nearest enabled one.
// Status is always enabled, so this can never spin forever.
static void ensureCarouselPageEnabled() {
  for (uint8_t tries = 0; tries < kCarouselPages && !pageEnabled(carouselPage); ++tries)
    carouselPage = static_cast<uint8_t>((carouselPage + 1) % kCarouselPages);
}

// Was a hardcoded 120000 (2 min); now Settings -> Timeouts -> Screen timeout
// (localsettings::idleToSleepMin, persisted in NVS).
static inline uint32_t idleToSleepMs() {
  return static_cast<uint32_t>(localsettings::idleToSleepMin) * 60000u;
}
static uint32_t standbyLastFetchMs = 0;    // millis() of the last HA refresh (0 = never)

// The carousel page the device fell asleep on. A button wake restores it; a
// timer wake (nobody's there) reverts to the status page — and when we sleep
// on a non-status page we arm a wake (Settings -> Timeouts -> Control page
// timeout, localsettings::controlPageRevertMin) just to make that revert happen.
RTC_DATA_ATTR static uint8_t rtcCarouselPage = 0;
static inline uint32_t revertToStatusSec() {
  return static_cast<uint32_t>(localsettings::controlPageRevertMin) * 60u;
}

// The battery percentage at/below which the charge screen takes over.
static constexpr uint8_t kLowBatteryPct = 5;

// --- power button: click sleeps/wakes, a 10s hold shuts down ---------------
static constexpr unsigned long kPowerShutdownHoldMs = 10000;

static const freeink::Icon* carouselIcon(uint8_t page) {
  switch (page) {
    case 0: return &kNav_status;
    case 1: return &kNav_lighting;
    case 2: return &kNav_blinds;
    case 3: return &kNav_music;
    case 4: return &kNav_tv;
    case 5: return &kNav_xbox;
    case 6: return &kNav_wifi;
    case 7: return &kNav_climate;
    default: return nullptr;
  }
}

// A row of position dots along the very bottom — the carousel affordance.
// Only enabled pages get a dot, so a hidden page doesn't leave a "gap" dot
// nobody can land on.
static void drawCarouselDots() {
  uint8_t visible = 0;
  for (uint8_t i = 0; i < kCarouselPages; ++i) if (pageEnabled(i)) ++visible;
  if (visible == 0) return;

  const int16_t sp = 20, rad = 4, d = 8;
  const int16_t total = static_cast<int16_t>((visible - 1) * sp);
  int16_t x = static_cast<int16_t>(Ui::W / 2 - total / 2);
  const int16_t y = static_cast<int16_t>(Ui::H - 20);
  for (uint8_t i = 0; i < kCarouselPages; ++i) {
    if (!pageEnabled(i)) continue;
    if (i == carouselPage)
      ui.fillRect(static_cast<int16_t>(x - rad), static_cast<int16_t>(y - rad), d, d, Color::Black,
                  rad);
    else
      ui.strokeRect(static_cast<int16_t>(x - rad), static_cast<int16_t>(y - rad), d, d, 1, rad);
    x = static_cast<int16_t>(x + sp);
  }
}

// Set while a post-wake reconnect + refresh is in flight, so the status bar
// carries a small "updating" glyph over the still-cached page instead of a
// blocking modal card — cleared in the SAME repaint that shows the fresh
// data (the g_weatherBusy busy->idle edge below), so a wake costs exactly
// one repaint at wake + one when the refresh actually lands, never more.
static bool g_wakeUpdating = false;

// Just the carousel pixels (status bar + page body + dots), no commit — split
// out so the wake path can paint the real page underneath and stack a popup
// on top before a single commitFrame(). `sleeping` only affects the moon
// glyph here; drawStandby() below also uses it to force Rf::Full.
static void drawStandbyContent(bool sleeping, int pressed) {
  ui.clear();
  const bool statusPage = carouselPage == 0;
  drawStatusBar(statusPage ? (deviceconfig::name[0] ? deviceconfig::name : deviceconfig::activeSlug)
                           : kCarouselNames[carouselPage],
                /*showMoon=*/sleeping, carouselIcon(carouselPage), g_wakeUpdating);

  if (carouselPage == 0) {
    screen_status::draw();
  } else if (carouselPage == kPageClimate) {
    screen_climate::draw(pressed);
  } else if (carouselPage == kPageLighting) {
    screen_lighting::draw(pressed);
  } else if (carouselPage == kPageBlinds) {
    screen_blinds::draw(pressed);
  } else if (carouselPage == kPageMusic) {
    screen_music::draw(pressed);
  } else if (carouselPage == kPageTv) {
    screen_tv::draw(pressed);
  } else if (carouselPage == kPageWifi) {
    screen_wifi_networks::draw();
  } else {
    ui.text(kCarouselNames[carouselPage], 0, static_cast<int16_t>(Ui::H / 2 - 60), Ui::W, 64,
            TextAlign::Center, Color::Black);
    ui.text("coming soon", 0, static_cast<int16_t>(Ui::H / 2 + 16), Ui::W, 26, TextAlign::Center,
            Color::DarkGray, 1, Ui::kFontSmall);
  }

  drawCarouselDots();
}

// ===========================================================================
// Refresh policy — WHAT kind of redraw an interaction causes decides its Rf,
// not which screen it happens to be on. loop()'s ~40 drawStandby() call sites
// used to each pick Rf::Fast/Clean/Full by hand, so the reasoning for any one
// of them only existed as a comment next to that specific line — the same
// mistake (a dithered region flashed under Fast, or a plain button tap forced
// through an unnecessary Clean scrub) was one copy-pasted line away at every
// new call site. Add a new interaction by picking the RefreshEvent that
// matches what it redraws, not by copying whatever a similar-looking existing
// line used.
//
// Mirrors screen_common.h's commitFrame(), which already centralizes the
// Fast -> Clean auto-promotion (kCleanEvery) below whatever mode is picked
// here — this table is the layer above that, picking the STARTING mode.
// ===========================================================================
enum class RefreshEvent : uint8_t {
  // A held touch updates a value live, every frame (brightness/volume bars).
  // Same-magnitude repaints in a tight loop — Fast keeps them from stalling
  // the drag; commitFrame()'s kCleanEvery scrubs the accumulated ghosting.
  Drag,
  // A tap changes a small, localized bit of on-screen state: a toggle, a
  // step button, a chip activating, a blinds/TV/Music control settling. The
  // default for "something small just changed."
  TapFeedback,
  // A dense dithered/anti-aliased region redraws: Climate's arc, Music's
  // album art. These visibly ghost under a partial (Fast) refresh, so they
  // always get a real scrub even though they're triggered by a plain tap.
  DitheredRedraw,
  // The page's CONTENT changes wholesale: a carousel page change, a tab
  // switch, the jump list opening/closing, async Wi-Fi/weather data landing.
  // Always a clean scrub — a partial refresh over entirely different content
  // reads as visual noise, not a "this button did something" cue.
  ContentSwitch,
  // The first paint after a deep-sleep wake, straight from cached data.
  // Always Clean even though nothing dithered is necessarily on screen:
  // Uc8179Driver::displayStart() (freeink-sdk) only honors Rf::Fast as a true
  // partial once _oldPlaneValid is set by a completed refresh, which is false
  // on this first post-wake paint (fresh object, wiped RAM) — Fast would
  // silently fall back to the same full-style flash Clean already gives, so
  // asking for Fast here buys nothing.
  WakeRepaint,
};

static inline Rf refreshModeFor(RefreshEvent e) {
  switch (e) {
    case RefreshEvent::Drag:           return Rf::Fast;
    case RefreshEvent::TapFeedback:    return Rf::Fast;
    case RefreshEvent::DitheredRedraw: return Rf::Clean;
    case RefreshEvent::ContentSwitch:  return Rf::Clean;
    case RefreshEvent::WakeRepaint:    return Rf::Clean;
  }
  return Rf::Clean;  // unreachable — every enumerator is handled above
}

// The carousel. `sleeping` adds the moon + forces a clean frame for deep sleep.
// `pressed` inverts one action-bar button (Climate/Lighting) for tap feedback.
static void drawStandby(bool sleeping = false, Rf r = Rf::Clean, int pressed = -1) {
  if (sleeping) r = Rf::Full;
  drawStandbyContent(sleeping, pressed);
  commitFrame(r);
}

// --- carousel jump list (Home tap) ----------------------------------
// A tap on a page row jumps there; the tail rows reach Settings, the hardware
// self-test, and the error-screen preview.
// NOTE: this grid is a fixed 2-col x 5-row layout (kJumpTileH etc. below) —
// exactly 10 destinations fit. Self-test was dropped to make room for Wifi
// (a real carousel page) rather than shrinking every tile to fit an 11th;
// the hardware self-test screen is still reachable via Home-long-press from
// the No-HA / No-Room error screens (see Stage::NoHA / Stage::NoRoom below).
static bool jumpOpen = false;  // the carousel jump-to list (Home tap)
static const char* const kJumpItems[] = {"Status", "Lighting", "Blinds", "Music",
                                         "TV",     "Xbox",     "Wifi",    "Climate",
                                         "Settings", "Error states"};
static constexpr int kJumpCount = 10;
static constexpr int kJumpSettings = 8, kJumpErrors = 9;
// 2-wide grid of icon+label tiles (5 rows for the 10 destinations) instead of
// a linear list — each tile carries its own Material icon (weather_icons.h's
// kWx_jump_* set, baked once at a consistent size for every destination,
// independent of whatever size that same glyph is used at elsewhere).
static constexpr int16_t kJumpCols    = 2;
static constexpr int16_t kJumpMarginX = 20;
static constexpr int16_t kJumpGapX    = 14;
static constexpr int16_t kJumpGapY    = 14;
static constexpr int16_t kJumpTileW =
    (Ui::W - 2 * kJumpMarginX - (kJumpCols - 1) * kJumpGapX) / kJumpCols;
static constexpr int16_t kJumpTileH = 128;
static constexpr int16_t kJumpTop   = kStatusBarH + 12 + kPad;
static int jumpSel = 0;

static const freeink::Icon* jumpIcon(int i) {
  switch (i) {
    case 0: return &kWx_jump_status;
    case 1: return &kWx_jump_lighting;
    case 2: return &kWx_jump_blinds;
    case 3: return &kWx_jump_music;
    case 4: return &kWx_jump_tv;
    case 5: return &kWx_jump_xbox;
    case 6: return &kWx_jump_wifi;
    case 7: return &kWx_jump_climate;
    case kJumpSettings: return &kWx_jump_settings;
    case kJumpErrors:   return &kWx_jump_errors;
    default: return nullptr;
  }
}

static void jumpTilePos(int slot, int16_t& x, int16_t& y) {
  const int col = slot % kJumpCols, row = slot / kJumpCols;
  x = static_cast<int16_t>(kJumpMarginX + col * (kJumpTileW + kJumpGapX));
  y = static_cast<int16_t>(kJumpTop + row * (kJumpTileH + kJumpGapY));
}

// The jump grid drops any carousel page this room's config has hidden
// (deviceconfig::screens.*) — Settings/Self-test/Error states (indices >=
// kCarouselPages) have no such toggle and are always present. Rebuilt
// wherever the grid is opened/redrawn so a config change takes effect.
static int jumpVisible[kJumpCount];
static int jumpVisibleCount = 0;
static void rebuildJumpVisible() {
  jumpVisibleCount = 0;
  for (int i = 0; i < kJumpCount; ++i) {
    if (i < kCarouselPages && !pageEnabled(static_cast<uint8_t>(i))) continue;
    jumpVisible[jumpVisibleCount++] = i;
  }
}
// Which slot (0-based grid position, not the original kJumpItems index)
// jumpSel's original index currently occupies, or 0 if it's been hidden.
static int jumpSlotFor(int original) {
  for (int slot = 0; slot < jumpVisibleCount; ++slot)
    if (jumpVisible[slot] == original) return slot;
  return 0;
}
// Tile slot a logical tap at (tx,ty) fell on, or -1.
static int jumpHitTest(int16_t tx, int16_t ty) {
  for (int slot = 0; slot < jumpVisibleCount; ++slot) {
    int16_t x, y;
    jumpTilePos(slot, x, y);
    if (tx >= x && tx < x + kJumpTileW && ty >= y && ty < y + kJumpTileH) return slot;
  }
  return -1;
}

static void drawJumpList() {
  rebuildJumpVisible();
  if (jumpSel >= jumpVisibleCount) jumpSel = 0;
  ui.clear();
  drawStatusBar("Jump to");
  for (int slot = 0; slot < jumpVisibleCount; ++slot) {
    const int i = jumpVisible[slot];
    int16_t x, y;
    jumpTilePos(slot, x, y);
    const bool sel = slot == jumpSel;
    const bool cur = i == carouselPage && i < kCarouselPages;
    if (sel) ui.fillRect(x, y, kJumpTileW, kJumpTileH, Color::Black, 16);
    else     ui.strokeRect(x, y, kJumpTileW, kJumpTileH, 2, 16);
    const Color fg = sel ? Color::White : Color::Black;
    const freeink::Icon* ic = jumpIcon(i);
    if (ic)
      ui.icon(*ic, static_cast<int16_t>(x + (kJumpTileW - ic->w) / 2), static_cast<int16_t>(y + 20), fg);
    ui.text(kJumpItems[i], x, static_cast<int16_t>(y + 76), kJumpTileW, 28, TextAlign::Center, fg);
    if (cur)
      ui.text("now", x, static_cast<int16_t>(y + kJumpTileH - 22), kJumpTileW, 18, TextAlign::Center,
              sel ? Color::LightGray : Color::DarkGray, 1, Ui::kFontSmall);
  }
  commitFrame(Rf::Clean);
}

static void jumpTo(int i) {
  jumpOpen = false;
  standbyIdleSinceMs = millis();
  if (i >= 0 && i < kCarouselPages) {
    carouselPage = static_cast<uint8_t>(i);
    stage = Stage::Standby;
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ContentSwitch));
  } else if (i == kJumpSettings) {
    screen_settings::enter();
  } else if (i == kJumpErrors) {
    screen_err_preview::enter();
  }
}

// The physical buttons, all active-LOW: Left=GPIO0, Right=GPIO7, Power=GPIO3.
// (Home is a GT911 capacitive key — it can't wake the chip from deep sleep.)
static const gpio_num_t kWakePins[] = {GPIO_NUM_0, GPIO_NUM_7, GPIO_NUM_3};

// Deep-sleep the chip, waking on ANY of the physical buttons (a full chip reset
// back through setup()) and optionally after `timerUs` microseconds (0 = none).
// Never returns.
//
// Mirrors CrossPoint's enterDeepSleep() + HalPowerManager::startDeepSleep():
// tear down Wi-Fi, panel deep-sleep command, arm ext1 wake, HOLD the master
// peripheral-rail latch (GPIO1) HIGH — PowerManager::deepSleep() runs
// esp_sleep_config_gpio_isolate() which otherwise lets the latch float, the
// rail drops on battery, and the next press cold-boots instead of fast-waking.
[[noreturn]] static void deepSleepWithWake(uint64_t timerUs) {
  // 0. Remember the frontlight state — its rail is cut below, so the wake path
  //    has to put it back deliberately. Let any in-flight async refresh land
  //    first so the panel keeps a complete frame through sleep. Tear down
  //    USB-CDC (CrossPoint does the same) so the host sees a clean disconnect
  //    and the peripheral isn't holding a power domain across the GPIO wake.
  saveFrontlightToRtc();
  ui.syncDisplay();
  // Unmount the SD cache — on the X4 Pro's native SDMMC this floats the bus
  // pads so their pull-ups don't back-feed the card through sleep. Every
  // save() this session already landed, so no file is open at this point.
  persist::shutdown();
#ifdef ENABLE_SERIAL_LOG
  Serial.end();
#endif

  // 1. Drop Wi-Fi so the modem power domain isn't held through deep sleep.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  // 2. Panel deep-sleep command, while its rail is still powered.
  ui.display().deepSleep();

  // 3. Arm wake on every physical button (active-LOW) + the refresh timer.
  uint64_t mask = 0;
  for (gpio_num_t p : kWakePins) {
    pinMode(p, INPUT_PULLUP);
    mask |= 1ULL << p;
  }
  freeink::PowerManager::armWakeOnPins(mask, /*wakeLow=*/true);
  if (timerUs) esp_sleep_enable_timer_wakeup(timerUs);

  // 4. Hold GPIO1 (power.latch0) HIGH through deep sleep.
  const int8_t latch = BoardConfig::ACTIVE.power.latch0;
  if (latch >= 0) {
    const auto g = static_cast<gpio_num_t>(latch);
    gpio_hold_dis(g);
    pinMode(latch, OUTPUT);
    digitalWrite(latch, HIGH);
    gpio_hold_en(g);
  }

  // 5. Cut the gated touch / SD rails and hold them off.
  freeink::PowerManager::powerDownRailsForSleep();

  // 6. Wait for every wake button to be released (a held GPIO0 strap through
  //    the reset would drop the chip into download mode), then sleep.
  for (bool held = true; held;) {
    held = false;
    for (gpio_num_t p : kWakePins)
      if (digitalRead(p) == LOW) held = true;
    if (held) delay(20);
  }

  freeink::PowerManager::deepSleep();  // isolate + hold_en + esp_deep_sleep_start
  while (true) {                       // unreachable — satisfy [[noreturn]]
  }
}

// Deep-sleep from the carousel: wake after `timerSec` (0 = only a button wakes),
// or on a physical button to come back. Never returns. The panel keeps showing
// whatever frame was last flushed (moon + no frontlight).
[[noreturn]] static void standbySleep(uint32_t timerSec) {
  rtcStandbyActive = true;
  deepSleepWithWake(static_cast<uint64_t>(timerSec) * 1000000ULL);
}

// mDNS resolves SWITCHBOARD_SERVER_HOST. begin() re-inits the responder every
// call, so guard it — one success is enough, and racing task calls must not
// tear it down under each other.
static void ensureMdns() {
  static volatile bool up = false;
  if (up || WiFi.status() != WL_CONNECTED) return;
  if (MDNS.begin("switchboard-remote")) up = true;
}

// Pull everything the carousel shows, then mirror it into RTC memory so it
// survives the deep sleep. On any failure the last good state is restored
// from RTC (a network blip must not blank the screen). Returns true if the
// weather fetch succeeded. Drives ALL FOUR carousel pages in one HA round
// trip, so it lives here rather than being owned by screen_status alone.
static bool refreshStandby() {
  if (WiFi.status() != WL_CONNECTED) {
    persist::load();
    return false;
  }

  // TEMP DEBUG — confirm whether globals are being (re)fetched or served from
  // the RTC-persisted cache. Remove once the blank-Wi-Fi-names issue is resolved.
  Serial.printf("[globals] refreshStandby: ok=%d (%s)\n", globalsclient::ok,
                globalsclient::ok ? "skipping fetch, using cached/persisted globals"
                                   : "will fetch");
  if (!globalsclient::ok) globalsclient::fetch();  // HA host/token — rarely changes
  deviceconfig::fetch();                           // entity ids + refresh interval

  bool gotWeather = false;
  for (int attempt = 0; attempt < 2 && !gotWeather; ++attempt) {
    // 2nd pass: the HA host/token we had was stale (config changed, or a bad
    // persist blob poisoned it) — re-pull /api/globals and try once more.
    if (attempt == 1) {
      globalsclient::fetch();
      deviceconfig::fetch();
    }
    if (!globalsclient::ok) break;
    const char* h = globalsclient::haHost;
    const uint16_t p = globalsclient::haPort;
    const char* t = globalsclient::haToken;
    gotWeather = haclient::fetchWeather(h, p, t, deviceconfig::weatherEntity);
    haclient::fetchClimate(h, p, t, deviceconfig::climateEntity);
    haclient::fetchAir(h, p, t, deviceconfig::airQualityEntity);
    haclient::fetchForecast(h, p, t, deviceconfig::weatherEntity);
    if (deviceconfig::lightGroupEnabled) {
      haclient::fetchLight(h, p, t, deviceconfig::lightGroupEntity);
      // Individual on/off for the Lighting page's Lights tab — one small GET
      // per configured light (kitchen: 4), so this does add to the refresh's
      // total time; acceptable at the normal 15+ minute refresh cadence.
      for (int i = 0; i < deviceconfig::lightCount && i < deviceconfig::kMaxLights; ++i)
        haclient::fetchLightOn(h, p, t, deviceconfig::lights[i].entity, haclient::lightItemOn[i]);
    }
    if (deviceconfig::blindsGroupEnabled)
      haclient::fetchCover(h, p, t, deviceconfig::blindsGroupEntity);
    // Two individual blinds, no group entity to read a combined position from
    // -> the Blinds page's side-by-side panel layout needs each one's own state.
    if (deviceconfig::blindsItemCount == 2)
      for (int i = 0; i < 2; ++i)
        haclient::fetchCoverItem(h, p, t, deviceconfig::blindsItems[i].entity, i);
    // Climate page's additional-sensor footer (kitchen: Window/Wall/Thermostat).
    for (int i = 0; i < deviceconfig::climateSensorCount && i < 6; ++i)
      haclient::climateSensorOk[i] = haclient::fetchSensorValue(
          h, p, t, deviceconfig::climateSensors[i].entity, haclient::climateSensorValue[i]);
    if (deviceconfig::mediaEnabled)
      haclient::fetchMedia(h, p, t, deviceconfig::mediaEntity);
  }

  standbyLastFetchMs = millis();

  if (gotWeather && globalsclient::ok && deviceconfig::ok) {
    ensureCarouselPageEnabled();  // a config change may have hidden the current page
    persist::save();
  } else {
    persist::load();  // roll back any half-updated / cleared client state
  }
  return gotWeather;
}

// Background weather refresh — so a wake never blocks on the HA calls.
// (g_weatherBusy itself is declared earlier, before the screen_*.h includes —
// screen_climate/lighting/blinds's own kick() functions gate on it too, so an
// action's background task never races the weather task over shared mDNS/
// HTTP resources.)
static void weatherTask(void*) {
  g_weatherBusy = true;
  ensureMdns();
  refreshStandby();
  g_weatherBusy = false;
  vTaskDelete(nullptr);
}

static void kickWeatherRefresh() {
  if (g_weatherBusy) return;
  g_weatherBusy = true;  // set before create so a racing caller can't double-spawn
  if (xTaskCreatePinnedToCore(weatherTask, "sb_wx", 8192, nullptr, 1, nullptr, 1) != pdPASS)
    g_weatherBusy = false;
}

// Wi-Fi / weather state the Standby loop watched at its last repaint, so it
// only repaints on an actual change (seeded on entry so entry doesn't count).
static bool standbyPrevBusy = false;
static bool standbyPrevWifi = false;

// Enter the awake carousel. NOTHING here blocks: draw once from the
// last-known (persisted) data, light the frontlight, start the idle clock, and
// kick an async weather refresh. loop()'s Stage::Standby case repaints when the
// fetch lands or Wi-Fi changes.
static void enterStandby() {
  stage = Stage::Standby;
  screen_shade::open = false;
  jumpOpen = false;
  if (frontlight.present() && !frontlightIsOn()) frontlightSetOn(true);
  drawStandby(/*sleeping=*/false);
  standbyIdleSinceMs = millis();
  standbyPrevBusy = g_weatherBusy;
  standbyPrevWifi = WiFi.status() == WL_CONNECTED;
  if (WiFi.status() == WL_CONNECTED && !g_weatherBusy) kickWeatherRefresh();
}

// Idle timeout / Power on the carousel: kill the frontlight, stamp the moon,
// remember the page, and deep-sleep. On a control page we arm a wake
// (Settings -> Timeouts -> Control page timeout) so the device reverts to the
// status page while nobody's looking; on the status page we use the normal
// weather-refresh interval.
//
// Settings -> Developer -> Disable standby short-circuits this to a no-op (a
// bench-flashing aid — see localsettings::standbyDisabled) — the one reason
// this can no longer be [[noreturn]]. Both call sites already `break` right
// after calling it, so a returning call is handled the same as a sleeping one.
static void standbySleepNow() {
  if (localsettings::standbyDisabled) {
    standbyIdleSinceMs = millis();  // don't re-trigger next loop() tick
    return;
  }
  rtcCarouselPage = carouselPage;
  if (frontlight.present()) frontlight.off();
  drawStandby(/*sleeping=*/true);
  const uint32_t refreshSec = static_cast<uint32_t>(deviceconfig::refreshIntervalMin) * 60u;
  uint32_t timerSec;
  if (carouselPage != 0)
    timerSec = (refreshSec > 0 && refreshSec < revertToStatusSec()) ? refreshSec : revertToStatusSec();
  else
    timerSec = refreshSec;
  standbySleep(timerSec);  // noreturn
}

// ===========================================================================
// Shutdown — a 10s Power hold, from any stage.
// ===========================================================================
// A real deep sleep (not the soft Asleep state above): waits for release, arms
// wake-on-power-button, then powers down. Waking is a full chip reset back
// through setup()'s Splash / Wi-Fi sequence, same as a fresh boot. Never
// returns.
[[noreturn]] static void shutdown() {
  ui.clear();
  ui.centered("Shutting down", 380, 40);
  ui.centered("press Power to turn on", 430, 26, Color::DarkGray);
  ui.flushFull();
  deepSleepWithWake(0);  // no timer — any button wakes it
}

// Drain the input task's queue + level state into one InFrame (touch coords
// mapped to logical pixels), logging each event to the serial monitor.
static InFrame drainInput() {
  InFrame f{};
  if (g_inQueue) {
    InEvent e;
    while (xQueueReceive(g_inQueue, &e, 0) == pdTRUE) {
      switch (e.ev) {
        case Ev::BtnLeft:  f.btnLeft = true;  Serial.println("[in] button LEFT"); break;
        case Ev::BtnRight: f.btnRight = true; Serial.println("[in] button RIGHT"); break;
        case Ev::BtnPower: f.btnPower = true; Serial.println("[in] button POWER"); break;
        case Ev::HomeTap:  f.homeTap = true;  Serial.println("[in] HOME key tap"); break;
        case Ev::HomeLong: f.homeLong = true; Serial.println("[in] HOME key long-press"); break;
        case Ev::Tap:
          f.tap = true;
          Ui::touchToLogical(e.a, e.b, f.tx, f.ty);
          Serial.printf("[touch] tap   n=(%.3f,%.3f) -> (%d,%d)\n", e.a, e.b, f.tx, f.ty);
          break;
        case Ev::Swipe:
          f.swipe = true;
          Ui::touchToLogical(e.a, e.b, f.sx0, f.sy0);
          Ui::touchToLogical(e.c, e.d, f.sx1, f.sy1);
          Serial.printf("[touch] SWIPE (%d,%d)->(%d,%d)  d=(%+d,%+d)\n", f.sx0, f.sy0, f.sx1, f.sy1,
                        f.sx1 - f.sx0, f.sy1 - f.sy0);
          break;
      }
    }
  }
  f.touchHeld = g_touchDown;
  if (f.touchHeld) Ui::touchToLogical(g_touchNx, g_touchNy, f.hx, f.hy);
  return f;
}

// Reconnect Wi-Fi from the ESP32's stored credentials with no UI — for the
// Standby timer-wake path, where a visible Wi-Fi screen would just flash.
static bool wifiConnectSilent(uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // reuse the SSID/pass persisted in NVS
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(100);
  return WiFi.status() == WL_CONNECTED;
}

// ===========================================================================
// Arduino entry points
// ===========================================================================
void setup() {
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  // A genuine deep-sleep wake reports TIMER (the refresh timer) or EXT1 (a
  // physical button — see PowerManager::armWakeOnPins, which arms ext1 on the
  // S3). ANY other reset reason — the reset/EN pin, a power cycle, brownout,
  // panic, or ESP.restart() — reports UNDEFINED here. rtcStandbyActive alone
  // isn't enough to tell those apart: it's plain RTC memory, so it survives
  // an EN-pin reset exactly like it survives a real deep-sleep wake, and
  // without this check a reset pressed while the device was asleep replayed
  // the fast wake-from-standby path (skip splash, skip Wi-Fi setup, repaint
  // straight from cache) instead of the full cold boot a reset should give —
  // the whole point of a reset button.
  const bool realDeepSleepWake =
      wakeCause == ESP_SLEEP_WAKEUP_TIMER || wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  const bool wokeFromStandby = rtcStandbyActive && realDeepSleepWake;
  rtcStandbyActive = false;
  rtcNoHA = false;  // the timer-wake path re-checks HA reachability directly

  Serial.begin(115200);
  // CRITICAL for responsiveness: cap the USB-CDC TX wait at 1 ms. The default is
  // 100 ms per chunk with ~20 retries, so a single log line stalls the CPU up to
  // ~2 s whenever a host is plugged but not draining (every bench boot / wake).
  // With ENABLE_SERIAL_LOG the SDK logs freely through boot, so this is the
  // difference between an instant wake and a multi-second one. (CrossPoint does
  // the same — "a load-bearing 1".) Logs are simply dropped when nobody reads.
  Serial.setTxTimeoutMs(1);
  if (!wokeFromStandby) delay(200);  // let USB-CDC enumerate for a bench boot only
  Serial.printf("\n[switchboard] boot  wake=%d fromStandby=%d\n", static_cast<int>(wakeCause),
                wokeFromStandby);

  // CRITICAL — power the peripheral rails BEFORE touching the display.
  // On the X4 Pro the panel (and SD slot) sit behind a master rail enable on
  // GPIO1, carried in the profile as power.latch0. holdPowerRails() drives it
  // HIGH; without this call GPIO1 stays low, the panel rail is unpowered, and
  // NOTHING appears on screen no matter how correct the drawing code is. It
  // also releases any gpio_hold latched by a previous firmware's sleep path.
  // (selectDevice() calls this internally; we call it directly since this is a
  // single-device build and ACTIVE is already the X4 Pro profile.)
  BoardConfig::holdPowerRails();
  // Insurance for the shared-bus case: if an SD card rail were left latched
  // off it could clamp the display SPI lines. No-op on the X4 Pro's native
  // SDMMC wiring, but harmless and matches the SDK's documented ordering.
  BoardConfig::releaseSdRail();

  // Detect the real panel controller BEFORE the display comes up. Newer X4 Pro
  // units carry a UC8179 instead of the SSD1677; this probe promotes
  // ACTIVE.displayController to the sibling part so the correct driver (and BUSY
  // polarity) is used. Skipping it leaves a UC8179 unit blank. CrossPoint runs
  // this same step at boot.
  freeink::applyXteinkDisplayController();
  delay(10);  // let the rails settle before the panel reset dance

  // Display first — everything else can wait until the panel shows something.
  ui.begin();
  deviceconfig::loadSlug();  // the room this remote drives (NVS; Settings -> Select room)
  localsettings::load();     // on-device debug toggles (NVS; Settings -> Developer)
  // TEMP DEBUG — the SD mount + read sits on the critical path to the first
  // wake paint (replacing what used to be an instant RTC memcpy), so time it
  // until it's been checked against real hardware. Remove once confirmed fine.
  {
    const uint32_t t0 = millis();
    const bool sdOk = persist::begin();
    const uint32_t t1 = millis();
    const bool cacheOk = persist::load();
    const uint32_t t2 = millis();
    Serial.printf("[persist] SD mount=%lums (ok=%d)  cache load=%lums (ok=%d)\n",
                  static_cast<unsigned long>(t1 - t0), sdOk, static_cast<unsigned long>(t2 - t1),
                  cacheOk);
  }

  // A physical-button wake out of Standby: backlight on immediately, then the
  // cached page repaints once with a small status-bar "updating" glyph while
  // Wi-Fi joins — BLOCKING, unlike the old fire-and-forget WiFi.begin(), so
  // the fresh data fetch is already in flight by the time this function
  // returns. Restarts the 30s idle-to-sleep clock.
  if (wokeFromStandby && wakeCause != ESP_SLEEP_WAKEUP_TIMER) {
    ui.skipInitialResync();  // our HALF is a self-contained scrub — no forced FULL
    input.begin();
    frontlight.begin();
    restoreFrontlightFromRtc();

    pollBattery(true);  // one read on the still-quiet I2C bus
    if (frontlight.present()) frontlightSetOn(true);  // light on before the paint

    stage = Stage::Standby;
    carouselPage = rtcCarouselPage;  // come back to the page it slept on
    ensureCarouselPageEnabled();     // unless a config change hid it meanwhile

    // One paint: the real page, straight from the still-good persist::load()
    // data, with a small "updating" glyph in the status bar (drawStatusBar's
    // `updating` param, driven by g_wakeUpdating) rather than a blocking
    // modal card. Drawn BEFORE wifiConnectSilent()/kickWeatherRefresh() below
    // even touch anything, so there's no race with fetchWeather() et al
    // resetting haclient::weather (.ok = false) the instant their task
    // starts — the cached data this paints is never in danger of being
    // clobbered first. If that cached data isn't actually good (never
    // fetched, or a stale/invalid RTC blob), every page's own "<X>
    // unavailable" placeholder just shows through, same as any other draw.
    // Uc8179Driver::displayStart() (freeink-sdk) only honors RefreshMode::Fast
    // as a true partial once _oldPlaneValid is set by a completed refresh; on
    // this first paint after deep sleep it's still false (fresh object,
    // wiped RAM), so Fast would silently fall back to the same full-style
    // flash Clean already does here — no faster, and it wouldn't change what
    // pixels land either way.
    g_wakeUpdating = true;
    drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::WakeRepaint));
    standbyIdleSinceMs = millis();

    startInputTask();
    const bool wifi = wifiConnectSilent(WIFI_JOIN_TIMEOUT_MS);

    if (wifi) {
      ensureMdns();
      kickWeatherRefresh();
      // No repaint here — the icon set above stays accurate through the
      // async fetch that just started; loop()'s g_weatherBusy busy->idle
      // edge (below) clears it and shows the fresh data in ONE further
      // repaint once the fetch actually lands. A wake costs exactly two
      // repaints total, never a third for "fetch in flight, then done".
    } else {
      // Nothing to wait for — that busy->idle edge will never fire, so drop
      // the icon and repaint here instead of leaving it stuck on screen.
      g_wakeUpdating = false;
      drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::WakeRepaint));
    }
    standbyPrevBusy = g_weatherBusy;
    standbyPrevWifi = wifi;
    return;
  }

  input.begin();
  frontlight.begin();
  pollBattery(true);  // one read on the still-quiet I2C bus (before the input task)
  // A wake out of Standby restores the lamp; a cold boot / shutdown wake starts dark.
  if (wokeFromStandby) {
    restoreFrontlightFromRtc();
  } else if (frontlight.present()) {
    frontlight.off();
  }

  // Scheduled refresh: the screen's own deep-sleep timer fired. Skip the splash
  // / Wi-Fi UI — reconnect quietly, re-pull, redraw, sleep again. No user is
  // present, so the frontlight stays off and the moon stays up.
  if (wokeFromStandby && wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
    stage = Stage::Standby;
    const bool wifi = wifiConnectSilent(12000);
    if (wifi) {
      ensureMdns();
      refreshStandby();
    }
    if (frontlight.present()) frontlight.off();
    if (wifi && !globalsclient::ok) {
      // Still can't reach HA — hold the No-HA screen, retry in another 30 min.
      screen_no_ha::draw(/*sleeping=*/true);
      rtcNoHA = true;
      rtcStandbyActive = true;
      deepSleepWithWake(30ULL * 60ULL * 1000000ULL);
    }
    rtcNoHA = false;
    // A timer wake means nobody's interacting — revert to the status page and
    // sleep again on the normal weather-refresh interval.
    carouselPage = 0;
    rtcCarouselPage = 0;
    drawStandby(/*sleeping=*/true);
    standbySleep(static_cast<uint32_t>(deviceconfig::refreshIntervalMin) * 60u);  // never returns
  }

  // Cold boot: a brief splash, then straight into Wi-Fi + Standby.
  screen_splash::draw();
  const uint32_t splashStart = millis();
  float nx, ny;
  while (millis() - splashStart < 1200) {
    input.update();
    if (input.wasAnyPressed() || tapped(nx, ny)) break;
    delay(20);
  }

  stage = Stage::Wifi;
  screen_wifi::run();

  // Weather loads in the background — the boot path never blocks on the HA calls.
  if (WiFi.status() == WL_CONNECTED) {
    ensureMdns();
    kickWeatherRefresh();
  }

  startInputTask();  // hand input off to its own task from here on

  // Boot straight into the carousel (page 0 = the status screen) with the
  // frontlight lit; it self-sleeps after idleToSleepMs() of no input.
  enterStandby();
}

void loop() {
  InFrame in = drainInput();
  pollBattery();  // self-throttled to ~1.5 s

  // synthesize a touch-down edge from the shared level state
  static bool prevTouchDown = false;
  if (in.touchHeld && !prevTouchDown) {
    in.touchPress = true;
    in.px = in.hx;
    in.py = in.hy;
  }
  prevTouchDown = in.touchHeld;

  // --- Power: hold 10s -> shutdown, from any stage ----------------------
  if (g_powerHeld && g_powerHeldMs >= kPowerShutdownHoldMs) {
    shutdown();  // noreturn
  }

  switch (stage) {
    case Stage::Standby: {  // the carousel
      // --- control panel overlay (opened by HOLDING Home) ---
      if (screen_shade::open) {
        standbyIdleSinceMs = millis();
        if (in.touchHeld) screen_shade::sliderDrag(in.hx, in.hy);
        if (!in.touchHeld) screen_shade::dragging = false;
        if (in.homeTap || in.homeLong) { input.suppressTouchContact(); screen_shade::close(); break; }
        if (in.tap && !screen_shade::dragging) screen_shade::handleTap(in.tx, in.ty);
        if (screen_shade::open && screen_shade::dirty && !ui.refreshBusy()) {
          screen_shade::dirty = false;
          screen_shade::draw(screen_shade::pressed);          // shows the new level + pressed button
          if (screen_shade::pressed >= 0) { screen_shade::pressed = -1; screen_shade::dirty = true; }  // then release it
        }
        break;
      }

      // --- carousel jump list overlay (opened by TAPPING Home) ---
      if (jumpOpen) {
        standbyIdleSinceMs = millis();
        if (in.homeTap || in.btnPower) { jumpOpen = false; drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ContentSwitch)); break; }
        if (in.btnLeft)  { jumpSel = (jumpSel + jumpVisibleCount - 1) % jumpVisibleCount; drawJumpList(); break; }
        if (in.btnRight) { jumpSel = (jumpSel + 1) % jumpVisibleCount; drawJumpList(); break; }
        if (in.tap) {
          const int slot = jumpHitTest(in.tx, in.ty);
          if (slot >= 0) jumpTo(jumpVisible[slot]);
        }
        break;
      }

      // Lighting page: a held drag in the brightness bar updates the level
      // live on every frame (mirrors screen_shade's own slider / CrossPoint's
      // FrontlightPanelActivity) — a plain tap that never drags falls through
      // to the barHit tap-to-set handling further below instead.
      if (carouselPage == kPageLighting) {
        if (in.touchPress && screen_lighting::barHit(in.px, in.py)) screen_lighting::dragging = true;
        if (screen_lighting::dragging) {
          if (in.touchHeld) {
            screen_lighting::setPct(screen_lighting::pctFromX(in.hx));
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::Drag));
          } else {
            screen_lighting::dragging = false;
          }
          break;
        }
      }

      // Music page: a held drag in the volume bar updates the level live,
      // same convention as the Lighting brightness bar above.
      if (carouselPage == kPageMusic) {
        if (in.touchPress && screen_music::barHit(in.px, in.py)) screen_music::dragging = true;
        if (screen_music::dragging) {
          if (in.touchHeld) {
            screen_music::setVolumePct(screen_music::pctFromX(in.hx));
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::Drag));
          } else {
            screen_music::dragging = false;
          }
          break;
        }
      }

      // TV page: same drag convention for its volume row.
      if (carouselPage == kPageTv) {
        if (in.touchPress && screen_tv::barHit(in.px, in.py)) screen_tv::dragging = true;
        if (screen_tv::dragging) {
          if (in.touchHeld) {
            screen_tv::setVolumePct(screen_tv::pctFromX(in.hx));
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::Drag));
          } else {
            screen_tv::dragging = false;
          }
          break;
        }
      }

      // Hold Home -> the control panel we set brightness/warmth in
      if (in.homeLong) {
        input.suppressTouchContact();
        screen_shade::open = true;
        screen_shade::dirty = false;
        screen_shade::dragging = false;
        standbyIdleSinceMs = millis();
        screen_shade::draw();
        break;
      }

      // Tap Home -> the jump list (pick any carousel page, Settings, Self-test)
      if (in.homeTap) {
        input.suppressTouchContact();
        jumpOpen = true;
        rebuildJumpVisible();
        jumpSel = jumpSlotFor(carouselPage);
        standbyIdleSinceMs = millis();
        drawJumpList();
        break;
      }

      // Left / Right -> move through the carousel, skipping any page this
      // room's config has hidden (deviceconfig::screens.*). Status is always
      // enabled, so this can never spin forever even if everything else is off.
      if (in.btnLeft || in.btnRight) {
        uint8_t p = carouselPage;
        for (uint8_t tries = 0; tries < kCarouselPages; ++tries) {
          p = in.btnLeft ? static_cast<uint8_t>((p + kCarouselPages - 1) % kCarouselPages)
                         : static_cast<uint8_t>((p + 1) % kCarouselPages);
          if (pageEnabled(p)) break;
        }
        carouselPage = p;
        standbyIdleSinceMs = millis();
        drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ContentSwitch));
        break;
      }

      // Power -> sleep now (a no-op if Settings -> Developer -> Disable
      // standby is on, in which case this must not fall through below).
      if (in.btnPower) { standbySleepNow(); break; }

      // Lighting page: the "All lights" controls (toggle switch, DARKER/
      // BRIGHTER buttons, tap-to-set brightness bar), then a SCENE or
      // per-light chip below it.
      if (carouselPage == kPageLighting && in.tap) {
        if (screen_lighting::toggleHit(in.tx, in.ty)) {
          screen_lighting::toggle();
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
          break;
        }
        if (in.ty >= screen_lighting::kLcRow2Y && in.ty < screen_lighting::kLcRow2Y + screen_lighting::kLcBtnSz) {
          int col = -1;
          if (in.tx >= screen_lighting::kLcDarkerX && in.tx < screen_lighting::kLcDarkerX + screen_lighting::kLcBtnSz)
            col = 0;
          else if (in.tx >= screen_lighting::kLcBrighterX &&
                   in.tx < screen_lighting::kLcBrighterX + screen_lighting::kLcBtnSz)
            col = 1;
          if (col >= 0) {
            screen_lighting::adjust(col == 0 ? -1 : +1);
            screen_lighting::g_pressed = col;
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/col);
            break;
          }
        }
        // Tap a point in the brightness bar -> jump straight to that level.
        if (screen_lighting::barHit(in.tx, in.ty)) {
          screen_lighting::setPct(screen_lighting::pctFromX(in.tx));
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
          break;
        }
        if (deviceconfig::lightGroupColorTemp) {
          const int tc = screen_lighting::tempBtnHit(in.tx, in.ty);
          if (tc >= 0) {
            screen_lighting::kickColorTemp(screen_lighting::kTempPresets[tc].kelvin);
            screen_lighting::g_pressed = tc + 2;
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/tc + 2);
            break;
          }
        }
        const int tab = screen_lighting::tabHit(in.tx, in.ty);
        if (tab >= 0 && tab != screen_lighting::tab) {
          screen_lighting::tab = tab;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ContentSwitch));  // clean scrub — switches the whole chip list
          break;
        }
        // The page indicator (only present past 12 items on whichever tab is
        // active) advances that tab's own page — checked before chipHit so
        // it doesn't also register as a chip tap underneath it.
        {
          const int total = screen_lighting::tab == 0 ? deviceconfig::sceneCount : deviceconfig::lightCount;
          const int pc = screen_lighting::listPageCount(total);
          if (screen_lighting::pagerHit(in.tx, in.ty, pc)) {
            int& page = screen_lighting::tab == 0 ? screen_lighting::scenePage : screen_lighting::lightPage;
            page = (page + 1) % pc;
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ContentSwitch));
            break;
          }
        }
        const int si = screen_lighting::tab == 0 && deviceconfig::sceneCount
            ? chipHit(screen_lighting::kChipsY,
                     screen_lighting::listVisibleCount(deviceconfig::sceneCount, screen_lighting::scenePage),
                     in.tx, in.ty)
            : -1;
        const int li = screen_lighting::tab == 1 && deviceconfig::lightCount
            ? chipHit(screen_lighting::kChipsY,
                     screen_lighting::listVisibleCount(deviceconfig::lightCount, screen_lighting::lightPage),
                     in.tx, in.ty)
            : -1;
        if (si >= 0 || li >= 0) {
          if (si >= 0) {
            const int abs = screen_lighting::scenePage * screen_lighting::kListPageSize + si;
            screen_lighting::activateScene(deviceconfig::scenes[abs].entity);
            screen_lighting::lastScene = abs;  // drawn filled black until another scene runs
          } else {
            const int abs = screen_lighting::lightPage * screen_lighting::kListPageSize + li;
            screen_lighting::toggleItem(deviceconfig::lights[abs].entity);
            // Flip the icon optimistically — the next refresh corrects it if
            // the toggle didn't actually take.
            haclient::lightItemOn[abs] = !haclient::lightItemOn[abs];
          }
          screen_lighting::g_pressed = -1;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
          break;
        }
      }

      // Climate page: the arc's MINUS/PLUS step buttons (circular hit-test),
      // the center mode button (cycles hvac_modes), and the HVAC mode bar
      // (tap a mode to jump straight to it).
      if (carouselPage == kPageClimate && in.tap) {
        if (screen_climate::stepHit(in.tx, in.ty, screen_climate::kMinusCx) ||
            screen_climate::stepHit(in.tx, in.ty, screen_climate::kPlusCx)) {
          const bool plus = screen_climate::stepHit(in.tx, in.ty, screen_climate::kPlusCx);
          screen_climate::adjust(plus ? +1 : -1);
          screen_climate::g_pressed = plus ? 1 : 0;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::DitheredRedraw), /*pressed=*/screen_climate::g_pressed);
          break;
        }
        if (screen_climate::modeButtonHit(in.tx, in.ty)) {
          screen_climate::cycleMode();
          screen_climate::g_pressed = 2;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::DitheredRedraw), /*pressed=*/2);
          break;
        }
        const int mi = screen_climate::modeBtnHit(in.tx, in.ty);
        if (mi >= 0) {
          screen_climate::setMode(haclient::climate.modes[mi]);
          screen_climate::g_pressed = 3 + mi;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::DitheredRedraw), /*pressed=*/screen_climate::g_pressed);
          break;
        }
      }

      // Blinds page, two individual blinds: the row layout's CLOSE/STOP/OPEN
      // buttons — row 0 is always "All blinds", rows 1/2 are the two items.
      if (carouselPage == kPageBlinds && in.tap && deviceconfig::blindsItemCount == 2) {
        const int hit = screen_blinds::rowBtnHit(in.tx, in.ty);
        if (hit >= 0) {
          const int row = hit / 3, col = hit % 3;
          const screen_blinds::Act act = col == 0   ? screen_blinds::Act::Close
                                         : col == 1  ? screen_blinds::Act::Stop
                                                     : screen_blinds::Act::Open;
          if (row == 0) screen_blinds::commandAll(act);
          else          screen_blinds::commandItem(row - 1, act);
          screen_blinds::g_pressed = hit;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/hit);
          break;
        }
      }

      // Blinds page: the CLOSE / STOP / OPEN action bar. One partial refresh
      // shows the pressed button + the optimistic new value; the "action
      // settled" repaint below clears the press.
      if (carouselPage == kPageBlinds && in.tap && in.ty >= kBarBtnY &&
          deviceconfig::blindsItemCount != 2) {
        const int col = in.tx < Ui::W / 3 ? 0 : in.tx < Ui::W * 2 / 3 ? 1 : 2;
        if (col == 0) screen_blinds::command(screen_blinds::Act::Close);
        else if (col == 1) screen_blinds::command(screen_blinds::Act::Stop);
        else screen_blinds::command(screen_blinds::Act::Open);
        screen_blinds::g_pressed = col;
        standbyIdleSinceMs = millis();
        drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/col);
        break;
      }

      // Music page: MUTE toggle, VOL-/VOL+ buttons, tap-to-set volume bar,
      // then the PREV / PLAY-PAUSE / NEXT transport bar.
      if (carouselPage == kPageMusic && in.tap) {
        if (screen_music::toggleHit(in.tx, in.ty)) {
          screen_music::toggleMute();
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
          break;
        }
        if (in.ty >= screen_music::kMuRow2Y && in.ty < screen_music::kMuRow2Y + screen_music::kMuBtnSz) {
          int col = -1;
          if (in.tx >= screen_music::kMuDownX && in.tx < screen_music::kMuDownX + screen_music::kMuBtnSz)
            col = 0;
          else if (in.tx >= screen_music::kMuUpX && in.tx < screen_music::kMuUpX + screen_music::kMuBtnSz)
            col = 1;
          if (col >= 0) {
            screen_music::adjustVolume(col == 0 ? -1 : +1);
            screen_music::g_pressed = 3 + col;
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/3 + col);
            break;
          }
        }
        if (screen_music::barHit(in.tx, in.ty)) {
          screen_music::setVolumePct(screen_music::pctFromX(in.tx));
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
          break;
        }
        if (in.ty >= kBarBtnY) {
          const int col = in.tx < Ui::W / 3 ? 0 : in.tx < Ui::W * 2 / 3 ? 1 : 2;
          if (col == 0) screen_music::prev();
          else if (col == 1) screen_music::togglePlay();
          else screen_music::next();
          screen_music::g_pressed = col;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/col);
          break;
        }
      }

      // TV page: D-pad, MUTE toggle, VOL-/VOL+/tap-to-set volume row, an app
      // icon, then the BACK/HOME/POWER bar — every button is a one-shot
      // remote/media_player call, no state to read back (unlike Music's
      // play/pause/volume level).
      if (carouselPage == kPageTv && in.tap) {
        const int dp = screen_tv::dpadHit(in.tx, in.ty);
        if (dp >= 0) {
          screen_tv::kickCommand(screen_tv::kDpadCmds[dp]);
          screen_tv::g_pressed = dp;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/dp);
          break;
        }
        if (screen_tv::muteToggleHit(in.tx, in.ty)) {
          screen_tv::toggleMute();
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
          break;
        }
        if (in.ty >= screen_tv::kVolRow2Y && in.ty < screen_tv::kVolRow2Y + screen_tv::kVolBtnSz) {
          int col = -1;
          if (in.tx >= screen_tv::kVolDownX && in.tx < screen_tv::kVolDownX + screen_tv::kVolBtnSz)
            col = 0;
          else if (in.tx >= screen_tv::kVolUpX && in.tx < screen_tv::kVolUpX + screen_tv::kVolBtnSz)
            col = 1;
          if (col >= 0) {
            screen_tv::adjustVolume(col == 0 ? -1 : +1);
            screen_tv::g_pressed = 9 + col;
            standbyIdleSinceMs = millis();
            drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/9 + col);
            break;
          }
        }
        if (screen_tv::barHit(in.tx, in.ty)) {
          screen_tv::setVolumePct(screen_tv::pctFromX(in.tx));
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
          break;
        }
        const int ap = screen_tv::appsHit(in.tx, in.ty);
        if (ap >= 100) {
          screen_tv::kickApp(ap - 100);
          screen_tv::g_pressed = ap;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/ap);
          break;
        }
        if (in.ty >= kBarBtnY) {
          const int col = in.tx < Ui::W / 3 ? 0 : in.tx < Ui::W * 2 / 3 ? 1 : 2;
          screen_tv::kickCommand(screen_tv::kBarCmds[col]);
          screen_tv::g_pressed = 5 + col;
          standbyIdleSinceMs = millis();
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback), /*pressed=*/5 + col);
          break;
        }
      }

      // Wifi page: tap a network row -> straight to its QR screen (no
      // press-flash needed, same as Settings -> Select room's row tap).
      if (carouselPage == kPageWifi && in.tap) {
        const int i = screen_wifi_networks::hitTest(in.tx, in.ty);
        if (i >= 0) {
          standbyIdleSinceMs = millis();
          screen_wifi_networks::enterQr(i);
          break;
        }
      }

      if (in.tap || in.swipe || in.touchPress || in.touchHeld) standbyIdleSinceMs = millis();

      // Async Wi-Fi / weather: kick the fetch when Wi-Fi lands; repaint the
      // CURRENT page when it finishes (so Climate/Lighting stop showing
      // "unavailable" the moment their data lands); divert to No-HA if the
      // Switchboard/HA server can't be reached.
      {
        const bool busy = g_weatherBusy;
        const bool wifi = WiFi.status() == WL_CONNECTED;
        if (wifi && !standbyPrevWifi && !busy) kickWeatherRefresh();
        if ((standbyPrevBusy && !busy) || wifi != standbyPrevWifi) {
          g_wakeUpdating = false;  // the fetch this was tracking is done (or gave up)
          if (wifi && !busy && !globalsclient::ok) { screen_no_ha::enter(); break; }
          if (wifi && !busy && globalsclient::ok && !deviceconfig::ok) { screen_no_room::enter(); break; }
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ContentSwitch));
        }
        standbyPrevBusy = busy;
        standbyPrevWifi = wifi;
      }

      // Blinds: while a cover is opening/closing, re-poll its position so the
      // page tracks the movement, then repaint when the read lands.
      if (carouselPage == kPageBlinds && deviceconfig::blindsItemCount != 2 &&
          !screen_blinds::g_busy && !g_weatherBusy) {
        static uint32_t lastCoverPoll = 0;
        const bool moving = !strcmp(haclient::cover.state, "opening") ||
                            !strcmp(haclient::cover.state, "closing");
        if (moving && millis() - lastCoverPoll > 2500) {
          lastCoverPoll = millis();
          screen_blinds::kick(screen_blinds::Act::Refresh);
        }
      }
      // Same, per panel, for the two-individual-blinds row layout.
      if (carouselPage == kPageBlinds && deviceconfig::blindsItemCount == 2 && !g_weatherBusy) {
        static uint32_t lastItemPoll[2] = {0, 0};
        for (int i = 0; i < 2; ++i) {
          if (screen_blinds::g_itemBusy[i]) continue;
          const char* st = haclient::coverItems[i].state;
          const bool moving = !strcmp(st, "opening") || !strcmp(st, "closing");
          if (moving && millis() - lastItemPoll[i] > 2500) {
            lastItemPoll[i] = millis();
            screen_blinds::kickItem(i, screen_blinds::Act::Refresh);
          }
        }
      }
      // And the "All blinds" row's real group entity, when this room has one
      // (otherwise "All blinds" is synthesized from the two items' state
      // above, which the per-item repoll already keeps live).
      if (carouselPage == kPageBlinds && deviceconfig::blindsItemCount == 2 &&
          screen_blinds::hasRealGroup() && !screen_blinds::g_busy && !g_weatherBusy) {
        static uint32_t lastGroupPoll = 0;
        const bool moving = !strcmp(haclient::cover.state, "opening") ||
                            !strcmp(haclient::cover.state, "closing");
        if (moving && millis() - lastGroupPoll > 2500) {
          lastGroupPoll = millis();
          screen_blinds::kick(screen_blinds::Act::Refresh);
        }
      }

      // Music: while playing, re-poll every 30s so a track change
      // (title/artist) shows up without waiting for the normal 15+ minute
      // standby refresh.
      if (carouselPage == kPageMusic && !screen_music::g_busy && !g_weatherBusy) {
        static uint32_t lastMediaPoll = 0;
        if (!strcmp(haclient::media.state, "playing") && millis() - lastMediaPoll > 30000) {
          lastMediaPoll = millis();
          screen_music::kick(screen_music::Act::Refresh);
        }
      }

      // Once an action settles (or its kick was a no-op), repaint the page with
      // the confirmed state and clear any pressed-button style. Blinds/
      // Lighting settle as RefreshEvent::TapFeedback — this fires after EVERY
      // tap (CLOSE/STOP/OPEN, the lighting toggle/DARKER/BRIGHTER/WARM/
      // DAYLIGHT/COOL), and a forced HALF scrub on every single one of those
      // reads as the whole page "reloading" and the button visibly popping
      // back to normal ("re-enabling") a moment after every press.
      // commitFrame()'s own kCleanEvery still promotes one of these to a real
      // scrub periodically, so ghosting doesn't build up — it just doesn't
      // happen on every tap. Climate settles as RefreshEvent::DitheredRedraw
      // instead: its arc is a dense dithered-dot band that ghosts visibly
      // under repeated Fast partial refreshes, so every COOLER/MODE/WARMER
      // tap does a full scrub of the page.
      if (!in.tap) {
        static bool prevLightBusy = false, prevCoverBusy = false, prevItemBusy = false,
                    prevMusicBusy = false, prevTvBusy = false;
        if (screen_climate::g_pressed >= 0 && !screen_climate::g_busy && carouselPage == kPageClimate) {
          screen_climate::g_pressed = -1;
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::DitheredRedraw));
        } else if (carouselPage == kPageBlinds && deviceconfig::blindsItemCount != 2 &&
                   ((prevCoverBusy && !screen_blinds::g_busy) || (screen_blinds::g_pressed >= 0 && !screen_blinds::g_busy))) {
          screen_blinds::g_pressed = -1;
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
        } else if (carouselPage == kPageBlinds && deviceconfig::blindsItemCount == 2 &&
                   ((prevItemBusy && !screen_blinds::anyItemBusy()) ||
                    (prevCoverBusy && !screen_blinds::g_busy) ||
                    (screen_blinds::g_pressed >= 0 && !screen_blinds::anyItemBusy() &&
                     !screen_blinds::g_busy))) {
          screen_blinds::g_pressed = -1;
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
        } else if (carouselPage == kPageLighting &&
                   ((prevLightBusy && !screen_lighting::g_busy) || (screen_lighting::g_pressed >= 0 && !screen_lighting::g_busy))) {
          screen_lighting::g_pressed = -1;
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
        } else if (carouselPage == kPageMusic &&
                   ((prevMusicBusy && !screen_music::g_busy) || (screen_music::g_pressed >= 0 && !screen_music::g_busy))) {
          screen_music::g_pressed = -1;
          // Clean, not Fast: a freshly-fetched album art bitmap is a big
          // dithered image (same reasoning as Climate's arc) that ghosts
          // badly under a partial refresh.
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::DitheredRedraw));
        } else if (carouselPage == kPageTv &&
                   ((prevTvBusy && !screen_tv::g_busy) || (screen_tv::g_pressed >= 0 && !screen_tv::g_busy))) {
          screen_tv::g_pressed = -1;
          drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::TapFeedback));
        }
        prevLightBusy = screen_lighting::g_busy;
        prevCoverBusy = screen_blinds::g_busy;
        prevItemBusy = screen_blinds::anyItemBusy();
        prevMusicBusy = screen_music::g_busy;
        prevTvBusy = screen_tv::g_busy;
      }

      // Critically low battery -> the charge screen, once per wake.
      if (!screen_low_battery::shown && g_battPct >= 1 && g_battPct <= kLowBatteryPct) {
        screen_low_battery::enter();
        break;
      }

      if (millis() - standbyIdleSinceMs > idleToSleepMs()) {
        standbySleepNow();  // frontlight off, moon, deep sleep (unless standby is disabled)
      }
      break;
    }

    case Stage::NoHA: {
      if (in.homeLong) { screen_debug::enter(); break; }
      if (in.homeTap) { screen_settings::enter(); break; }  // "open configuration"
      if (in.btnLeft || in.btnRight || in.btnPower || in.tap || in.touchPress) {
        // RETRY: back to the carousel, re-run the fetch
        carouselPage = 0;
        kickWeatherRefresh();
        enterStandby();
        break;
      }
      // A finished retry that still can't reach HA keeps us here; a good one
      // returns to the carousel.
      {
        static bool prevBusy = false;
        const bool busy = g_weatherBusy;
        if (prevBusy && !busy) {
          if (WiFi.status() == WL_CONNECTED && globalsclient::ok) { carouselPage = 0; enterStandby(); }
          else screen_no_ha::draw(/*sleeping=*/false);
        }
        prevBusy = busy;
      }
      if (millis() - standbyIdleSinceMs > idleToSleepMs()) {
        noHASleepNow();  // noreturn — sleep, retry in 30 min
      }
      break;
    }

    case Stage::NoRoom: {
      if (in.homeLong) { screen_debug::enter(); break; }
      if (in.homeTap)  { screen_room_pick::enter(); break; }  // "open Settings" -> pick a room
      if (in.btnLeft || in.btnRight || in.btnPower || in.tap || in.touchPress) {
        carouselPage = 0;
        kickWeatherRefresh();
        enterStandby();
        break;
      }
      {
        static bool prevBusy = false;
        const bool busy = g_weatherBusy;
        if (prevBusy && !busy) {
          if (WiFi.status() == WL_CONNECTED && globalsclient::ok && deviceconfig::ok) {
            carouselPage = 0;
            enterStandby();
          } else if (WiFi.status() == WL_CONNECTED && !globalsclient::ok) {
            screen_no_ha::enter();
          } else {
            screen_no_room::draw(/*sleeping=*/false);
          }
        }
        prevBusy = busy;
      }
      if (millis() - standbyIdleSinceMs > idleToSleepMs()) {
        noHASleepNow();  // noreturn — sleep, retry in 30 min
      }
      break;
    }

    case Stage::LowBattery: {
      if (in.btnLeft || in.btnRight || in.btnPower || in.homeTap || in.homeLong || in.tap) {
        carouselPage = 0;
        enterStandby();
        break;
      }
      // Recovered while we were parked here (plugged in) -> back to the carousel.
      if (g_battPct > kLowBatteryPct + 1) {
        carouselPage = 0;
        enterStandby();
        break;
      }
      // Otherwise keep the charge screen on the panel and deep-sleep to save
      // power — never redraw over it here.
      if (millis() - standbyIdleSinceMs > idleToSleepMs()) {
        screen_low_battery::sleepNow();  // noreturn
      }
      break;
    }

    case Stage::ErrPreview: {  // debug: page through the error screens
      if (in.btnLeft) {
        screen_err_preview::prev();
      } else if (in.btnRight) {
        screen_err_preview::next();
      } else if (in.homeTap || in.homeLong || in.btnPower || in.tap) {
        carouselPage = 0;
        enterStandby();
      }
      break;
    }

    case Stage::Settings: {
      if (in.homeTap) { carouselPage = 0; enterStandby(); break; }
      if (in.btnLeft)  { screen_settings::sel = (screen_settings::sel + screen_settings::kCount - 1) % screen_settings::kCount; screen_settings::pressed = -1; screen_settings::draw(); break; }
      if (in.btnRight) { screen_settings::sel = (screen_settings::sel + 1) % screen_settings::kCount; screen_settings::pressed = -1; screen_settings::draw(); break; }
      // Flash the row black for one frame before acting on it, same as a
      // touch tap below — Power alone (no prior highlight change) reads as a
      // press on the cursor row too.
      if (in.btnPower) {
        screen_settings::pressed = screen_settings::sel;
        screen_settings::draw();
        screen_settings::activate(screen_settings::sel);
        break;
      }
      if (in.tap) {
        const int i = screen_settings::hitTest(in.ty);
        if (i >= 0) {
          screen_settings::pressed = i;
          screen_settings::draw();
          screen_settings::activate(i);
        }
      }
      break;
    }

    case Stage::SettingsInfo: {
      if (in.homeTap || in.homeLong || in.btnLeft || in.btnPower || in.tap) screen_settings::enter();
      break;
    }

    case Stage::RoomPick: {
      if (in.homeTap || in.btnPower) { screen_settings::enter(); break; }
      if (!roomlist::ok) { if (in.btnLeft || in.tap) screen_settings::enter(); break; }
      if (in.btnLeft)  { screen_room_pick::sel = (screen_room_pick::sel + roomlist::count - 1) % roomlist::count; screen_room_pick::draw(); break; }
      if (in.btnRight) { screen_room_pick::sel = (screen_room_pick::sel + 1) % roomlist::count; screen_room_pick::draw(); break; }
      if (in.tap) {
        const int16_t top = static_cast<int16_t>(kStatusBarH + 12 + kPad);
        const int i = in.ty >= top ? (in.ty - top) / 82 : -1;
        if (i >= 0 && i < roomlist::count) screen_room_pick::pick(i);
      }
      break;
    }

    case Stage::Developer: {
      if (in.homeTap) { screen_settings::enter(); break; }
      if (in.btnLeft)  { screen_developer::sel = (screen_developer::sel + screen_developer::kCount - 1) % screen_developer::kCount; screen_developer::pressed = -1; screen_developer::draw(); break; }
      if (in.btnRight) { screen_developer::sel = (screen_developer::sel + 1) % screen_developer::kCount; screen_developer::pressed = -1; screen_developer::draw(); break; }
      if (in.btnPower) {
        screen_developer::pressed = screen_developer::sel;
        screen_developer::draw();
        if (screen_developer::sel == screen_developer::kCount - 1) screen_settings::enter();
        else screen_developer::activate(screen_developer::sel);
        break;
      }
      if (in.tap) {
        const int i = screen_developer::hitTest(in.ty);
        if (i >= 0 && i < screen_developer::kCount - 1) {
          screen_developer::pressed = i;
          screen_developer::draw();
          screen_developer::activate(i);
        } else if (i == screen_developer::kCount - 1) {
          screen_settings::enter();
        }
      }
      break;
    }

    case Stage::Timeouts: {
      if (in.homeTap) { screen_settings::enter(); break; }
      if (in.btnLeft)  { screen_timeouts::sel = (screen_timeouts::sel + screen_timeouts::kCount - 1) % screen_timeouts::kCount; screen_timeouts::pressed = -1; screen_timeouts::draw(); break; }
      if (in.btnRight) { screen_timeouts::sel = (screen_timeouts::sel + 1) % screen_timeouts::kCount; screen_timeouts::pressed = -1; screen_timeouts::draw(); break; }
      if (in.btnPower) {
        screen_timeouts::pressed = screen_timeouts::sel;
        screen_timeouts::draw();
        if (screen_timeouts::sel < 3) screen_timeouts::cycle(screen_timeouts::sel); else screen_settings::enter();
        break;
      }
      if (in.tap) {
        const int i = screen_timeouts::hitTest(in.ty);
        if (i >= 0 && i < 3) {
          screen_timeouts::pressed = i;
          screen_timeouts::draw();
          screen_timeouts::cycle(i);
        } else if (i >= 3) {
          screen_settings::enter();
        }
      }
      break;
    }

    case Stage::WifiQr: {
      if (in.homeTap || in.homeLong || in.btnLeft || in.btnPower || in.tap) {
        carouselPage = kPageWifi;
        stage = Stage::Standby;
        drawStandby(/*sleeping=*/false, refreshModeFor(RefreshEvent::ContentSwitch));
      }
      break;
    }

    case Stage::Debug: {
      bool dirty = false;

      if (in.btnLeft  && !screen_debug::btnSeen[screen_debug::TB_LEFT])  { screen_debug::btnSeen[screen_debug::TB_LEFT]  = true; dirty = true; }
      if (in.btnRight && !screen_debug::btnSeen[screen_debug::TB_RIGHT]) { screen_debug::btnSeen[screen_debug::TB_RIGHT] = true; dirty = true; }
      if (in.btnPower && !screen_debug::btnSeen[screen_debug::TB_POWER]) { screen_debug::btnSeen[screen_debug::TB_POWER] = true; dirty = true; }
      if (in.homeTap) {
        if (!screen_debug::btnSeen[screen_debug::TB_HOME]) { screen_debug::btnSeen[screen_debug::TB_HOME] = true; dirty = true; }
        screen_debug::stepBacklight();  // the Home key also steps the backlight
        dirty = true;
      }

      // --- touch: track position + count, redraw the crosshair -----------
      if (in.touchHeld) {
        screen_debug::lastTouchX = in.hx;
        screen_debug::lastTouchY = in.hy;
        dirty = true;
      }
      bool forceFull = false;  // set by the "Full refresh" button
      if (in.tap) {
        screen_debug::lastTouchX = in.tx;
        screen_debug::lastTouchY = in.ty;
        screen_debug::touchCount++;
        if (screen_debug::tapBacklight(in.tx, in.ty)) screen_debug::stepBacklight();
        if (screen_debug::tapWarmth(in.tx, in.ty)) screen_debug::stepWarmth();
        if (screen_debug::tapFullRefresh(in.tx, in.ty)) forceFull = true;
        dirty = true;
      }

      if (in.homeLong) {  // hold Home -> restart the self-test
        screen_debug::enter();
        break;
      }
      if (in.btnPower && screen_debug::btnSeen[screen_debug::TB_POWER]) {  // 2nd Power press -> leave the self-test
        carouselPage = 0;
        enterStandby();
        break;
      }

      if (dirty) {
        // Partial (fast) refresh keeps feedback snappy, but ghosting builds up.
        // Promote to a full refresh when the user asks (button) or once enough
        // partials have accumulated that the panel would start to look dirty.
        const bool autoFull = screen_debug::partialsSinceFull >= 40;
        screen_debug::draw(/*full=*/forceFull || autoFull);
      }
      break;
    }

    default:
      break;
  }

  delay(5);
}
