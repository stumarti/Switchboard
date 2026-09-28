#pragma once

// ===========================================================================
// input — the input layer: a dedicated FreeRTOS task polls the InputManager
// so a slow e-ink refresh on the main loop can never drop a press, and
// loop() drains its queue into one InFrame per tick (readInputFrame()).
// Included before the screen_*.h headers so screens can take an InFrame.
// ===========================================================================

#include <Arduino.h>
#include <InputManager.h>

#include "screen_common.h"

enum class Ev : uint8_t { BtnLeft, BtnRight, BtnPower, PowerMenu, HomeTap, HomeLong, Tap, Swipe };
struct InEvent {
  Ev ev;
  float a, b, c, d;
};

// One frame's worth of input, assembled from the queue + level state. All
// touch coordinates are LOGICAL screen pixels (Ui::touchToLogical applied).
struct InFrame {
  bool btnLeft, btnRight, btnPower;
  bool powerMenu;  // Power held for kPowerMenuHoldMs — open the power menu
  bool homeTap, homeLong;
  bool touchPress;   int16_t px, py;              // synthesized touch-down edge
  bool touchHeld;    int16_t hx, hy;              // current position while held
  bool tap;          int16_t tx, ty;
  bool swipe;        int16_t sx0, sy0, sx1, sy1;  // endpoints

  // Any user input at all this frame — every stage's idle timer resets on it.
  bool any() const {
    return btnLeft || btnRight || btnPower || powerMenu || homeTap || homeLong || touchPress ||
           touchHeld || tap || swipe;
  }
};

// --- power button: a click sleeps/selects, a 10s hold opens the power menu --
// A Power CLICK fires on release (not press, like Left/Right) so a long hold
// never also acts as a click — on the carousel a click deep-sleeps at once,
// which would otherwise swallow every hold before it reached 10s. A release
// after kPowerClickMaxMs is an abandoned hold and does nothing.
static constexpr unsigned long kPowerMenuHoldMs = 10000;
static constexpr unsigned long kPowerClickMaxMs = 1000;

static QueueHandle_t g_inQueue = nullptr;
static volatile bool     g_touchDown = false;
static volatile float    g_touchNx = 0, g_touchNy = 0;

static void inputTask(void*) {
  const uint8_t btnIdx[2] = {InputManager::BTN_UP, InputManager::BTN_DOWN};
  const Ev btnEv[2] = {Ev::BtnLeft, Ev::BtnRight};
  // A Power press already down on the task's first poll (the press that woke
  // the chip, or one held through the boot sequence) never counts as a
  // click — waking with Power must not immediately sleep again — but its
  // hold still counts toward the power menu.
  bool firstPoll = true;
  bool powerClickArmed = false;
  bool powerMenuFired = false;
  for (;;) {
    input.update();

    InEvent e{};
    for (int i = 0; i < 2; ++i)
      if (input.wasPressed(btnIdx[i])) { e.ev = btnEv[i]; xQueueSend(g_inQueue, &e, 0); }

    if (input.wasPressed(InputManager::BTN_POWER)) {
      powerClickArmed = !firstPoll;
      powerMenuFired = false;
    }
    const unsigned long powerHeldMs = input.getPowerButtonHeldTime();
    if (input.isPressed(InputManager::BTN_POWER) && !powerMenuFired && powerHeldMs >= kPowerMenuHoldMs) {
      powerMenuFired = true;
      e.ev = Ev::PowerMenu; xQueueSend(g_inQueue, &e, 0);
    }
    if (input.wasReleased(InputManager::BTN_POWER)) {
      if (powerClickArmed && !powerMenuFired && powerHeldMs < kPowerClickMaxMs) {
        e.ev = Ev::BtnPower; xQueueSend(g_inQueue, &e, 0);
      }
      powerClickArmed = false;
    }
    firstPoll = false;

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
        case Ev::PowerMenu: f.powerMenu = true; Serial.println("[in] POWER held -> power menu"); break;
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

// One loop() tick's input: the drained queue plus a synthesized touch-down
// edge (touchPress) derived from the shared level state.
static InFrame readInputFrame() {
  InFrame in = drainInput();
  static bool prevTouchDown = false;
  if (in.touchHeld && !prevTouchDown) {
    in.touchPress = true;
    in.px = in.hx;
    in.py = in.hy;
  }
  prevTouchDown = in.touchHeld;
  return in;
}
