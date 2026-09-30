#pragma once
// Just enough of the Arduino core for the firmware to compile on a PC, so
// tools/screenshots can draw its real screens. Nothing here talks to hardware.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <algorithm>
using std::min;
using std::max;
struct String : std::string {
  using std::string::string;
  String() {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  const char* c_str() const { return std::string::c_str(); }
  int toInt() const { return atoi(c_str()); }
  size_t length() const { return size(); }
  bool isEmpty() const { return empty(); }
};
struct HostSerial {
  template <class... A> void printf(const char* f, A... a) {}
  void println(const char* = "") {}
  void print(const char*) {}
  void begin(int) {}
  void flush() {}
  void setTxTimeoutMs(int) {}
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  explicit operator bool() const { return false; }
};
inline HostSerial Serial;
inline uint32_t g_hostMillis = 100000;
inline uint32_t millis() { return g_hostMillis; }
inline uint64_t micros() { return uint64_t(g_hostMillis) * 1000; }
inline void delay(uint32_t ms) { g_hostMillis += ms; }
inline void yield() {}
inline long random(long a, long b = 0) { return b ? a : 0; }
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 1; }
inline void analogWrite(int, int) {}
inline void ledcAttach(int, int, int) {}
inline void ledcWrite(int, int) {}
struct EspClass {
  void restart() {}
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getFreePsram() { return 8000000; }
  uint32_t getPsramSize() { return 8388608; }
  uint32_t getHeapSize() { return 320000; }
  uint64_t getEfuseMac() { return 0x0000AABBCCDDEEFFull; }
};
inline EspClass ESP;
inline void* ps_malloc(size_t n) { return malloc(n); }
inline void* ps_calloc(size_t n, size_t s) { return calloc(n, s); }
inline uint32_t getCpuFrequencyMhz() { return 240; }
#define PROGMEM
#define IRAM_ATTR
#define F(x) x
