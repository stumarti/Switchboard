#pragma once
// Arduino-ESP32's WebServer, for the screenshots: serves nothing.
#include <functional>
#include "Arduino.h"
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };
struct WebServer {
  explicit WebServer(int) {}
  void on(const char*, HTTPMethod, std::function<void()>) {}
  void onNotFound(std::function<void()>) {}
  void begin() {}
  void handleClient() {}
  void stop() {}
  String arg(const char*) { return String(); }
  void send(int, const char*, const String&) {}
  void sendHeader(const char*, const String&) {}
};
