#include "input_companion.hpp"
#include <cassert>
#include <csignal>
#include <functional>
#include <iostream>
#include <thread>
using json = nlohmann::json;
static bool waitFor(std::function<bool()> check) {
  for (int i = 0; i < 200; ++i) {
    if (check())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}
static int connectTo(unsigned port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = inet_addr("127.0.0.1");
  assert(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ==
         0);
  timeval timeout{2, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  return fd;
}
static void sendMessage(int fd, json j, bool fragmented = false) {
  outbox::Queue out;
  out.append(j.dump());
  assert(out.pump([&](const uint8_t *p, size_t n) {
    return send(fd, p, fragmented ? 1 : n, 0);
  }));
}
static json receiveMessage(int fd) {
  uint8_t header[4];
  assert(recv(fd, header, 4, MSG_WAITALL) == 4);
  unsigned n = uint32_t(header[0]) << 24 | uint32_t(header[1]) << 16 |
               uint32_t(header[2]) << 8 | header[3];
  assert(n > 0 && n <= 8192);
  std::string body(n, '\0');
  assert(recv(fd, body.data(), n, MSG_WAITALL) == int(n));
  return json::parse(body);
}
int main() {
  signal(SIGPIPE, SIG_IGN);
  InputCompanion companion;
  assert(companion.start(0));
  in_addr allowed{inet_addr("127.0.0.1")};
  companion.begin(
      allowed, "123456",
      {{"type", "ready"}, {"id", "test"}, {"device", "Switch OLED"}});
  int bad = connectTo(companion.port());
  sendMessage(bad, {{"type", "auth"}, {"code", "000000"}});
  char byte;
  assert(recv(bad, &byte, 1, 0) == 0);
  close(bad);
  assert(!companion.connected());
  int good = connectTo(companion.port());
  sendMessage(good, {{"type", "auth"}, {"code", "123456"}}, true);
  assert(receiveMessage(good)["type"] == "ready");
  assert(waitFor([&] { return companion.connected(); }));
  sendMessage(good, {{"type", "ping"}});
  assert(receiveMessage(good)["type"] == "pong");
  assert(companion.queue({{"type", "text"}, {"text", "Привет 中文"}}));
  assert(receiveMessage(good)["text"] == "Привет 中文");
  companion.end();
  assert(recv(good, &byte, 1, 0) == 0);
  close(good);
  assert(!companion.connected());
  // A new video session has a new code. Oversized data is rejected before
  // parsing.
  companion.begin(allowed, "654321", {{"type", "ready"}});
  int oversized = connectTo(companion.port());
  uint8_t size[] = {0, 0, 32, 1};
  assert(send(oversized, size, 4, 0) == 4);
  assert(recv(oversized, &byte, 1, 0) == 0);
  close(oversized);
  int nested = connectTo(companion.port());
  outbox::Queue deep;
  deep.append(std::string("{\"type\":\"auth\",\"code\":\"654321\",\"extra\":") +
              std::string(100, '[') + "0" + std::string(100, ']') + "}");
  assert(deep.pump(
      [&](const uint8_t *p, size_t n) { return send(nested, p, n, 0); }));
  assert(recv(nested, &byte, 1, 0) == 0);
  close(nested);
  assert(!companion.connected());
  // Five wrong codes lock this video session without affecting video sockets.
  for (int i = 0; i < 5; ++i) {
    int fd = connectTo(companion.port());
    sendMessage(fd, {{"type", "auth"}, {"code", "123456"}});
    assert(recv(fd, &byte, 1, 0) == 0);
    close(fd);
  }
  int locked = connectTo(companion.port());
  assert(recv(locked, &byte, 1, 0) == 0);
  close(locked);
  companion.stop();
  std::cout << "OK: companion pairing, fragmented auth, heartbeat, Unicode, "
               "close, size limits, attempt limit\n";
}
