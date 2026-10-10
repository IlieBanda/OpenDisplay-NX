#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace wire {
constexpr size_t maxFrame = 8 * 1024 * 1024;
inline bool control(const std::vector<uint8_t> &p) {
  return !p.empty() && p.size() < 32768 && p[0] == '{' &&
         std::find(p.begin(), p.end(), 0) == p.end();
}
// Reject deeply nested controls before the recursive JSON parser sees them.
inline bool safeJsonDepth(const std::vector<uint8_t> &p) {
  int depth = 0;
  bool string = false, escape = false;
  for (uint8_t c : p) {
    if (string) {
      if (escape) escape = false;
      else if (c == '\\') escape = true;
      else if (c == '"') string = false;
    } else if (c == '"') string = true;
    else if (c == '{' || c == '[') { if (++depth > 24) return false; }
    else if (c == '}' || c == ']') { if (--depth < 0) return false; }
  }
  return depth == 0 && !string;
}
inline size_t annexB(const std::vector<uint8_t> &p) {
  for (size_t i = 0; i + 4 <= p.size(); ++i)
    if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1)
      return i;
  return p.size();
}
class Framer {
  std::vector<uint8_t> buf;
  size_t used = 0;

public:
  void reset() {
    buf.clear();
    used = 0;
  }
  template <class F> bool feed(const uint8_t *p, size_t n, F emit) {
    buf.insert(buf.end(), p, p + n);
    while (buf.size() - used >= 4) {
      auto h = buf.data() + used;
      size_t len = (uint32_t(h[0]) << 24) | (uint32_t(h[1]) << 16) |
                   (uint32_t(h[2]) << 8) | h[3];
      if (!len || len > maxFrame) {
        reset();
        return false;
      }
      if (buf.size() - used < 4 + len)
        break;
      std::vector<uint8_t> frame(h + 4, h + 4 + len);
      used += 4 + len;
      emit(frame);
    }
    if (used) {
      buf.erase(buf.begin(), buf.begin() + used);
      used = 0;
    }
    return true;
  }
};
} // namespace wire
