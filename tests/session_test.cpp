#include "outbox.hpp"
#include "wire.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
int main() {
  outbox::Queue queue;
  queue.append("{\"type\":\"ping\"}");
  std::vector<uint8_t> transmitted;
  int calls = 0;
  // Begin sending a frame, then encounter backpressure before graceful close.
  assert(queue.pump([&](const uint8_t *p, size_t) {
    if (++calls == 2) {
      errno = EAGAIN;
      return -1;
    }
    transmitted.insert(transmitted.end(), p, p + 2);
    return 2;
  }));
  assert(!queue.empty());
  queue.append("{\"type\":\"closing\"}");
  assert(queue.pump([&](const uint8_t *p, size_t n) {
    size_t count = std::min(size_t(3), n);
    transmitted.insert(transmitted.end(), p, p + count);
    return int(count);
  }));
  assert(queue.empty());
  std::vector<std::string> controls;
  wire::Framer framer;
  assert(framer.feed(transmitted.data(), transmitted.size(),
                     [&](const std::vector<uint8_t> &p) {
                       controls.emplace_back(p.begin(), p.end());
                     }));
  assert((controls == std::vector<std::string>{"{\"type\":\"ping\"}",
                                               "{\"type\":\"closing\"}"}));
  queue.append("x");
  assert(!queue.pump([](const uint8_t *, size_t) {
    errno = ECONNRESET;
    return -1;
  }));
  queue.clear();
  assert(queue.empty());
  std::cout << "OK: closing after partial writes and backpressure\n";
}
