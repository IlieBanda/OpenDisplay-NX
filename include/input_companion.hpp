#pragma once
#include "json.hpp"
#include "outbox.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

// Separate bounded channel; failure never tears down the video connection.
class InputCompanion {
  using json = nlohmann::json;
  int listener = -1, peer = -1;
  uint16_t boundPort = 0;
  pthread_t worker{};
  bool started = false;
  std::atomic<bool> running{false}, authenticated{false};
  std::mutex mutex;
  in_addr allowed{};
  std::string code;
  json identity;
  unsigned attempts = 0;
  double lastReceive = 0, lastAcceptCheck = 0;
  std::vector<uint8_t> incoming;
  outbox::Queue outgoing;
  static double clock() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  static void nonblocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  }
  void closePeer() {
    authenticated = false;
    if (peer >= 0)
      close(peer);
    peer = -1;
    incoming.clear();
    outgoing.clear();
  }
  void receive() {
    uint8_t bytes[4096];
    int count = recv(peer, bytes, sizeof(bytes), 0);
    if (count < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
      return;
    if (count <= 0) {
      closePeer();
      return;
    }
    incoming.insert(incoming.end(), bytes, bytes + count);
    while (incoming.size() >= 4) {
      uint32_t size = (uint32_t(incoming[0]) << 24) |
                      (uint32_t(incoming[1]) << 16) |
                      (uint32_t(incoming[2]) << 8) | incoming[3];
      if (size == 0 || size > 8192) {
        closePeer();
        return;
      }
      if (incoming.size() < size + 4)
        return;
      // Preflight depth without exceptions (the Switch build disables them).
      unsigned depth = 0;
      bool quoted = false, escaped = false, validDepth = true;
      for (size_t i = 4; i < size + 4; ++i) {
        const uint8_t c = incoming[i];
        if (quoted) {
          if (escaped)
            escaped = false;
          else if (c == '\\')
            escaped = true;
          else if (c == '"')
            quoted = false;
        } else if (c == '"')
          quoted = true;
        else if (c == '[' || c == '{') {
          if (++depth > 8) {
            validDepth = false;
            break;
          }
        } else if (c == ']' || c == '}') {
          if (depth == 0) {
            validDepth = false;
            break;
          }
          --depth;
        }
      }
      if (!validDepth || depth != 0 || quoted) {
        closePeer();
        return;
      }
      auto message = json::parse(incoming.begin() + 4,
                                 incoming.begin() + 4 + size, nullptr, false);
      incoming.erase(incoming.begin(), incoming.begin() + size + 4);
      if (!message.is_object() || !message.contains("type") ||
          !message["type"].is_string()) {
        closePeer();
        return;
      }
      if (!authenticated) {
        if (message["type"] != "auth" || !message.contains("code") ||
            !message["code"].is_string() || message["code"] != code) {
          ++attempts;
          closePeer();
          return;
        }
        authenticated = true;
        outgoing.append(identity.dump());
      } else if (message["type"] == "ping") {
        outgoing.append("{\"type\":\"pong\"}");
      } else {
        closePeer();
        return;
      }
      lastReceive = clock();
      if (outgoing.size() > 128) {
        closePeer();
        return;
      }
    }
  }
  void loop() {
    while (running) {
      bool activePeer = false;
      {
        std::lock_guard<std::mutex> lock(mutex);
        const double now = clock();
        const bool checkAccept = !code.empty() && now - lastAcceptCheck >= 100;
        if (checkAccept)
          lastAcceptCheck = now;
        pollfd sockets[2] = {{checkAccept ? listener : -1, POLLIN, 0},
                             {peer, POLLIN, 0}};
        if (checkAccept || peer >= 0)
          poll(sockets, 2, 0);
        int one = 1;
        sockaddr_in source{};
        socklen_t length = sizeof(source);
        int accepted =
            sockets[0].revents & POLLIN
                ? accept(listener, reinterpret_cast<sockaddr *>(&source),
                         &length)
                : -1;
        if (accepted >= 0) {
          if (peer >= 0 || code.empty() || attempts >= 5 ||
              source.sin_addr.s_addr != allowed.s_addr)
            close(accepted);
          else {
            peer = accepted;
            nonblocking(peer);
#ifdef SO_NOSIGPIPE
            setsockopt(peer, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
            setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            lastReceive = clock();
          }
        }
        if (peer >= 0 && sockets[1].revents & (POLLIN | POLLHUP | POLLERR))
          receive();
        if (peer >= 0 && clock() - lastReceive > (authenticated ? 10000 : 3000))
          closePeer();
        if (peer >= 0 && !outgoing.empty() &&
            !outgoing.pump([&](const uint8_t *p, size_t n) {
              return send(peer, p, n, 0);
            }))
          closePeer();
        activePeer = peer >= 0;
      }
#ifdef __SWITCH__
      svcSleepThread(activePeer ? 8000000 : 50000000);
#else
      usleep(activePeer ? 8000 : 50000);
#endif
    }
  }

public:
  ~InputCompanion() { stop(); }
  bool start(uint16_t port = 9011) {
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
      return false;
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(listener, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    if (bind(listener, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) < 0 ||
        listen(listener, 1) < 0) {
      close(listener);
      listener = -1;
      return false;
    }
    socklen_t size = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size);
    boundPort = ntohs(address.sin_port);
    nonblocking(listener);
    running = true;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 256 * 1024);
    int error = pthread_create(
        &worker, &attributes,
        [](void *p) -> void * {
          static_cast<InputCompanion *>(p)->loop();
          return nullptr;
        },
        this);
    pthread_attr_destroy(&attributes);
    started = error == 0;
    if (!started) {
      running = false;
      close(listener);
      listener = -1;
    }
    return started;
  }
  uint16_t port() const { return boundPort; }
  bool connected() const { return authenticated; }
  void begin(in_addr address, std::string pairingCode, json ready) {
    std::lock_guard<std::mutex> lock(mutex);
    closePeer();
    allowed = address;
    code = std::move(pairingCode);
    identity = std::move(ready);
    attempts = 0;
  }
  void end() {
    std::lock_guard<std::mutex> lock(mutex);
    closePeer();
    code.clear();
    allowed = {};
  }
  bool queue(const json &message) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!authenticated)
      return false;
    if (outgoing.size() >= 128) {
      closePeer();
      return false;
    }
    outgoing.append(message.dump());
    return true;
  }
  void stop() {
    running = false;
    if (started) {
      pthread_join(worker, nullptr);
      started = false;
    }
    end();
    if (listener >= 0)
      close(listener);
    listener = -1;
  }
};
