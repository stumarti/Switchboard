#pragma once
// A real (if minimal) HTTP/1.1 client over POSIX sockets, so the firmware on
// the host talks to an actual Switchboard Server: tools/screenshots draws
// each screen from the same bundle and state a remote would get.
#include "Arduino.h"
#include "WiFiClient.h"
#include <map>
#include <vector>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
typedef WiFiClient Stream;
#define HTTPC_ERROR_CONNECTION_REFUSED -1
struct HTTPClient {
  std::string host, port, path;
  std::vector<std::pair<std::string, std::string>> reqHeaders;
  std::map<std::string, std::string> respHeaders;
  WiFiClient body;
  void setTimeout(int) {}
  void setConnectTimeout(int) {}
  void setReuse(bool) {}
  bool begin(const char* url) {
    std::string u(url);
    if (u.rfind("http://", 0) != 0) return false;
    u = u.substr(7);
    const size_t slash = u.find('/');
    std::string hp = u.substr(0, slash);
    path = slash == std::string::npos ? "/" : u.substr(slash);
    const size_t colon = hp.find(':');
    host = hp.substr(0, colon);
    port = colon == std::string::npos ? "80" : hp.substr(colon + 1);
    reqHeaders.clear();
    return true;
  }
  bool begin(WiFiClient&, const char* url) { return begin(url); }
  void addHeader(const char* k, const char* v) { reqHeaders.emplace_back(k, v); }
  void collectHeaders(const char**, int) {}
  int send(const char* method, const std::string& data) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) return -1;
    const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    const bool ok = fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok) { if (fd >= 0) close(fd); return -1; }
    std::string req = std::string(method) + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n";
    for (auto& h : reqHeaders) req += h.first + ": " + h.second + "\r\n";
    if (*method == 'P') req += "Content-Length: " + std::to_string(data.size()) + "\r\n";
    req += "\r\n" + data;
    for (size_t sent = 0; sent < req.size();) {
      const ssize_t n = ::send(fd, req.data() + sent, req.size() - sent, 0);
      if (n <= 0) { close(fd); return -1; }
      sent += static_cast<size_t>(n);
    }
    std::string all;
    char tmp[16384];
    for (ssize_t n; (n = recv(fd, tmp, sizeof(tmp), 0)) > 0;) all.append(tmp, static_cast<size_t>(n));
    close(fd);
    const size_t end = all.find("\r\n\r\n");
    if (end == std::string::npos) return -1;
    const int code = atoi(all.c_str() + 9);
    respHeaders.clear();
    size_t at = all.find("\r\n") + 2;
    while (at < end) {
      const size_t eol = all.find("\r\n", at);
      std::string line = all.substr(at, eol - at);
      const size_t c = line.find(':');
      if (c != std::string::npos) {
        std::string k = line.substr(0, c), v = line.substr(c + 1);
        for (auto& ch : k) ch = static_cast<char>(tolower(ch));
        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
        respHeaders[k] = v;
      }
      at = eol + 2;
    }
    std::string b = all.substr(end + 4);
    if (respHeaders["transfer-encoding"] == "chunked") {
      std::string out;
      for (size_t p = 0;;) {
        const size_t eol = b.find("\r\n", p);
        if (eol == std::string::npos) break;
        const size_t len = strtoul(b.c_str() + p, nullptr, 16);
        if (!len) break;
        out += b.substr(eol + 2, len);
        p = eol + 2 + len + 2;
      }
      b = out;
    }
    body = WiFiClient();
    body.buf = b;
    return code;
  }
  int GET() { return send("GET", ""); }
  int POST(String s) { return send("POST", s); }
  int POST(const uint8_t* p, size_t n) { return send("POST", std::string(reinterpret_cast<const char*>(p), n)); }
  int getSize() { return static_cast<int>(body.buf.size()); }
  bool connected() { return body.connected(); }
  String header(const char* k) {
    std::string key(k);
    for (auto& ch : key) ch = static_cast<char>(tolower(ch));
    auto it = respHeaders.find(key);
    return it == respHeaders.end() ? String() : String(it->second);
  }
  String getString() { return String(body.buf); }
  WiFiClient& getStream() { return body; }
  WiFiClient* getStreamPtr() { return &body; }
  static String errorToString(int) { return String("connection refused"); }
  void end() {}
};
