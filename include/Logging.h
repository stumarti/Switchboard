#pragma once

// ---------------------------------------------------------------------------
// Logging.h — consumer-provided logging facility for the FreeInk SDK.
//
// Several SDK libraries (FrontlightManager, and others) `#include <Logging.h>`
// and call LOG_INF(tag, fmt, ...). The SDK deliberately does NOT ship this
// header: it expects the firmware/app to supply one so log output lands in the
// app's own Serial stream (the one the PlatformIO monitor reads) and, in a
// full build, a crash-report ring buffer.
//
// This is a minimal implementation for a standalone app: every level routes to
// Serial with a [LEVEL tag] prefix. It's safe to call before Serial.begin()
// (the writes simply go nowhere). Define SWITCHBOARD_QUIET_LOG to compile the
// info/debug chatter out entirely.
// ---------------------------------------------------------------------------

#include <Arduino.h>

// Core sink: printf-style to Serial with a level+tag prefix and a newline.
static inline void logPrintf(const char* level, const char* tag,
                             const char* fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print('[');
  Serial.print(level);
  Serial.print(' ');
  Serial.print(tag);
  Serial.print("] ");
  Serial.println(buf);
}

// The SDK calls LOG_INF(tag, fmt, ...); provide the usual family alongside it.
#ifdef SWITCHBOARD_QUIET_LOG
#define LOG_DBG(tag, ...) ((void)0)
#define LOG_INF(tag, ...) ((void)0)
#else
#define LOG_DBG(tag, ...) logPrintf("DBG", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) logPrintf("INF", tag, __VA_ARGS__)
#endif
#define LOG_WRN(tag, ...) logPrintf("WRN", tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) logPrintf("ERR", tag, __VA_ARGS__)
