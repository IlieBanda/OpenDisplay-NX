#pragma once
#include <cerrno>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
namespace outbox {
class Queue {
  std::deque<std::vector<uint8_t>> frames;
  size_t offset = 0;

public:
  bool empty() const { return frames.empty(); }
  size_t size() const { return frames.size(); }
  void clear() {
    frames.clear();
    offset = 0;
  }
  void append(const std::string &payload) {
    uint32_t n = payload.size();
    std::vector<uint8_t> b{uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8),
                           uint8_t(n)};
    b.insert(b.end(), payload.begin(), payload.end());
    frames.push_back(std::move(b));
  }
  template <class Send> bool pump(Send send) {
    while (!frames.empty()) {
      auto &b = frames.front();
      int n = send(b.data() + offset, b.size() - offset);
      if (n < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
      if (n == 0)
        return false;
      offset += n;
      if (offset == b.size()) {
        frames.pop_front();
        offset = 0;
      }
    }
    return true;
  }
};
} // namespace outbox
