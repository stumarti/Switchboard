#pragma once
// No card on the host: every open fails, so the SD cache is simply absent.
#include "Arduino.h"
struct FsFile {
  size_t write(uint8_t) { return 0; }
  size_t write(const uint8_t*, size_t) { return 0; }
  int read() { return -1; }
  int read(uint8_t*, size_t) { return -1; }
  size_t readBytes(char*, size_t) { return 0; }
  size_t size() { return 0; }
  void close() {}
  explicit operator bool() const { return false; }
};
struct HostSd {
  bool begin() { return false; }
  bool ensureDirectoryExists(const char*) { return false; }
  bool exists(const char*) { return false; }
  bool openFileForRead(const char*, const char*, FsFile&) { return false; }
  bool openFileForWrite(const char*, const char*, FsFile&) { return false; }
  bool remove(const char*) { return false; }
  bool removeDir(const char*) { return false; }
  bool rename(const char*, const char*) { return false; }
  void shutdown() {}
};
inline HostSd SdMan;
