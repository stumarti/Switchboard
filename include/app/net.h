#pragma once

// ===========================================================================
// net — the ONE task that talks to the network after boot. Every screen's
// button press becomes a Command posted here; the shared data refresh runs
// here too. Replaces the per-screen action tasks, each of which silently
// DROPPED a press whenever it — or the data refresh — was already busy
// (which, right after every wake, was for the whole first refresh).
//
//   post(cmd)         from the UI: queued, never dropped. A "set to a
//                     value" command (brightness, volume, a setpoint) with
//                     a coalesce key replaces a still-queued one with the
//                     same key, so a drag sends its latest value, not every
//                     step; one-shot presses (a D-pad key, a scene) are each
//                     sent, in order.
//   readbacks         after a command, the state it changed is re-read once
//                     the taps stop (kReadbackQuietMs), and once per burst —
//                     not the old fixed 400-600 ms stall after every tap.
//   requestRefresh()  the full data pull (app/data_refresh.h), run when no
//                     command is waiting; it calls serviceCommands() between
//                     its requests, so a press never waits for the refresh.
//
// Each Command carries a pointer to its screen's in-flight counter, held
// above zero from post() until its readback has landed: screens read that
// counter (their g_busy) to know when to repaint with the confirmed state,
// and the refresh skips re-fetching anything still in flight, so it can't
// overwrite what the user just changed on screen.
//
// The UI thread owns the Wi-Fi link (app/wifi_link.h): post() asks it to
// (re)join if the radio is off, and the worker simply waits for the link.
// ===========================================================================

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "globals_client.h"
#include "screen_common.h"  // g_weatherBusy
#include "app/wifi_link.h"

namespace net {

// Home Assistant connection, as every client call wants it.
struct Ha {
  const char* h;
  uint16_t p;
  const char* t;
};
inline Ha ha() { return Ha{globalsclient::haHost, globalsclient::haPort, globalsclient::haToken}; }

struct Command;
using ExecFn = void (*)(const Command&);
using ReadbackFn = void (*)(int arg);

struct Command {
  ExecFn exec = nullptr;          // the service call; nullptr = a plain re-read
  ReadbackFn readback = nullptr;  // what to re-read afterwards (optional)
  int readbackArg = 0;
  volatile uint8_t* busy = nullptr;  // the owning screen's in-flight counter
  uint32_t coalesceKey = 0;          // 0 = always send; else replaces a queued twin
  // A background poll (a playing track, a moving blind), not a press: only
  // queued while the link is already up, so it never wakes an idle radio.
  bool optional = false;
  // Payload — whatever exec() needs.
  int i = 0;
  float f = 0;
  bool on = false;
  char s1[64] = "";
  char s2[96] = "";
};

// Build a coalesce key from a short tag + an entity/id string (FNV-1a).
inline uint32_t key(const char* tag, const char* id = "") {
  uint32_t h = 2166136261u;
  for (const char* p = tag; *p; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  h = (h ^ '/') * 16777619u;
  for (const char* p = id; p && *p; ++p) h = (h ^ static_cast<uint8_t>(*p)) * 16777619u;
  return h ? h : 1;
}

inline constexpr int kQueueCap = 16;
inline constexpr int kReadbackCap = 8;
inline constexpr uint32_t kReadbackQuietMs = 700;  // taps stop -> re-read once
inline constexpr uint32_t kLinkWaitMs = 15000;     // a command waits this long for Wi-Fi

// --- shared state (guarded by g_mux) ----------------------------------------
inline portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
inline Command g_queue[kQueueCap];
inline int g_head = 0, g_count = 0;

struct Readback {
  ReadbackFn fn;
  int arg;
  volatile uint8_t* busy;
  uint8_t holds;  // how many commands' busy counts this readback releases
};
inline Readback g_readbacks[kReadbackCap];
inline int g_readbackCount = 0;
inline uint32_t g_lastCommandMs = 0;

inline volatile bool g_executing = false;  // a command or readback is running now
inline volatile bool g_refreshWanted = false;
inline void (*g_refreshFn)() = nullptr;    // app/data_refresh.h's refresh body
inline TaskHandle_t g_task = nullptr;
// Last time the worker did any network work — the UI thread powers the
// radio down once this (and the user) has been quiet long enough.
inline volatile uint32_t g_lastActivityMs = 0;

// True on the worker task itself (a readback posting a follow-up).
inline bool onWorker() { return g_task && xTaskGetCurrentTaskHandle() == g_task; }

inline void wake() {
  if (g_task) xTaskNotifyGive(g_task);
}

// Anything queued, running, or waiting to be re-read?
inline bool busy() {
  portENTER_CRITICAL(&g_mux);
  const bool b = g_count > 0 || g_readbackCount > 0;
  portEXIT_CRITICAL(&g_mux);
  return b || g_executing || g_refreshWanted || g_weatherBusy;
}

// Queue a command (UI thread). Never drops a press: if the queue is somehow
// full, the oldest non-coalescable entry would be the only thing lost, and
// that needs 16 presses faster than the network can send them.
inline void post(const Command& c) {
  // No worker = no interactive session (the unattended timer wake): nothing
  // would ever run it, and a queued command would hold sleep back.
  if (!g_task) return;
  const bool fromWorker = onWorker();
  if (c.optional && !wifilink::isUp()) return;
  bool replaced = false;
  portENTER_CRITICAL(&g_mux);
  if (c.coalesceKey) {
    for (int n = 0; n < g_count; ++n) {
      Command& q = g_queue[(g_head + n) % kQueueCap];
      if (q.coalesceKey == c.coalesceKey) {
        q = c;  // same busy counter — already counted
        replaced = true;
        break;
      }
    }
  }
  if (!replaced) {
    if (g_count == kQueueCap) {  // full: drop the oldest, releasing its count
      Command& old = g_queue[g_head];
      if (old.busy && *old.busy) *old.busy = static_cast<uint8_t>(*old.busy - 1);
      g_head = (g_head + 1) % kQueueCap;
      --g_count;
    }
    g_queue[(g_head + g_count) % kQueueCap] = c;
    ++g_count;
    if (c.busy) *c.busy = static_cast<uint8_t>(*c.busy + 1);
  }
  portEXIT_CRITICAL(&g_mux);
  g_lastActivityMs = millis();
  // The link belongs to the UI thread; a follow-up posted by the worker
  // itself (e.g. Xbox art after a state read) only runs with the link up.
  if (!fromWorker) wifilink::ensureStarted();  // bring the radio back if it's off
  wake();
}

// Ask for the full data refresh (UI thread; coalesces: one pending request
// at a time). Brings the radio back if it was powered down while idle.
inline void requestRefresh() {
  if (!g_task) return;
  wifilink::ensureStarted();
  g_refreshWanted = true;
  g_weatherBusy = true;  // screens already read this as "a refresh is due / running"
  g_lastActivityMs = millis();
  wake();
}

// --- worker internals ---------------------------------------------------------

inline bool popCommand(Command& out) {
  portENTER_CRITICAL(&g_mux);
  const bool have = g_count > 0;
  if (have) {
    out = g_queue[g_head];
    g_head = (g_head + 1) % kQueueCap;
    --g_count;
  }
  portEXIT_CRITICAL(&g_mux);
  return have;
}

inline void release(volatile uint8_t* busy, uint8_t n = 1) {
  if (!busy) return;
  portENTER_CRITICAL(&g_mux);
  *busy = *busy > n ? static_cast<uint8_t>(*busy - n) : 0;
  portEXIT_CRITICAL(&g_mux);
}

// Merge a finished command's re-read into the pending set.
inline void addReadback(const Command& c) {
  portENTER_CRITICAL(&g_mux);
  for (int n = 0; n < g_readbackCount; ++n) {
    Readback& r = g_readbacks[n];
    if (r.fn == c.readback && r.arg == c.readbackArg && r.busy == c.busy) {
      ++r.holds;
      portEXIT_CRITICAL(&g_mux);
      return;
    }
  }
  if (g_readbackCount < kReadbackCap) {
    g_readbacks[g_readbackCount++] = Readback{c.readback, c.readbackArg, c.busy, 1};
    portEXIT_CRITICAL(&g_mux);
    return;
  }
  portEXIT_CRITICAL(&g_mux);
  // No room to defer it: re-read right away.
  c.readback(c.readbackArg);
  release(c.busy);
}

inline bool popReadback(Readback& out) {
  portENTER_CRITICAL(&g_mux);
  const bool have = g_readbackCount > 0;
  if (have) {
    out = g_readbacks[0];
    for (int n = 1; n < g_readbackCount; ++n) g_readbacks[n - 1] = g_readbacks[n];
    --g_readbackCount;
  }
  portEXIT_CRITICAL(&g_mux);
  return have;
}

inline bool waitForLink(uint32_t ms) {
  const uint32_t start = millis();
  while (!wifilink::isUp()) {
    if (millis() - start > ms) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  wifilink::ensureMdns();
  return true;
}

// Run one queued command (worker only). A command whose link never comes up
// is given up on: its busy count is released so the screen repaints with
// whatever state it has (the next refresh corrects anything optimistic).
inline void runCommand(const Command& c) {
  g_executing = true;
  const bool link = waitForLink(kLinkWaitMs);
  const bool ok = link && globalsclient::ok;
  if (ok && !c.exec && c.readback) {
    // A plain re-read (a poll, art for a page turn): nothing to wait out.
    c.readback(c.readbackArg);
    release(c.busy);
  } else {
    if (ok && c.exec) c.exec(c);
    g_lastCommandMs = millis();
    if (ok && c.readback) addReadback(c);
    else release(c.busy);
  }
  g_lastActivityMs = millis();
  g_executing = false;
}

// Send every queued command now — the worker's main loop, and the refresh
// between its requests, so a press never waits behind a long refresh.
inline void serviceCommands() {
  Command c;
  while (popCommand(c)) runCommand(c);
}

// Re-read what the last burst of commands changed, once it's gone quiet.
inline void serviceReadbacks() {
  if (millis() - g_lastCommandMs < kReadbackQuietMs) return;
  Readback r;
  while (popReadback(r)) {
    g_executing = true;
    if (wifilink::isUp() && globalsclient::ok) r.fn(r.arg);
    g_lastActivityMs = millis();
    release(r.busy, r.holds);
    g_executing = false;
    if (g_count > 0) return;  // new taps came in: send them first
  }
}

inline void workerTask(void*) {
  for (;;) {
    serviceCommands();
    serviceReadbacks();
    if (g_refreshWanted && g_count == 0 && g_refreshFn) {
      g_refreshWanted = false;
      g_executing = true;
      if (waitForLink(kLinkWaitMs)) g_refreshFn();
      g_lastActivityMs = millis();
      g_executing = false;
      g_weatherBusy = g_refreshWanted;  // another request may have come in meanwhile
      continue;
    }
    // Sleep until posted to — or briefly, while a readback's quiet period runs.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(g_readbackCount ? 100 : 1000));
  }
}

// Start the worker (once, after boot has the cached config loaded).
inline void start(void (*refreshFn)()) {
  g_refreshFn = refreshFn;
  if (g_task) return;
  // A roomy stack: the refresh parses big HA JSON documents and album art
  // decodes a JPEG. Same core and priority the per-screen tasks used.
  xTaskCreatePinnedToCore(workerTask, "sb_net", 16384, nullptr, 1, &g_task, 1);
}

}  // namespace net
