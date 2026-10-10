#pragma once
#include "wire.hpp"
#include <cstdint>
namespace h264 {
// Track raw parameter sets as the official receiver does: changed SPS/PPS
// requires rebuilding the hardware decoder, not abandoning NVDEC for CPU.
class Parameters {
  uint64_t sps = 0, pps = 0;

public:
  void reset() { sps = pps = 0; }
  static bool idr(const std::vector<uint8_t> &p) {
    for (size_t i = wire::annexB(p); i + 4 < p.size(); ++i)
      if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1 &&
          (p[i + 4] & 31) == 5)
        return true;
    return false;
  }
  bool changed(const std::vector<uint8_t> &p) {
    bool change = false;
    size_t start = wire::annexB(p);
    while (start + 4 < p.size()) {
      size_t end = start + 4;
      while (end + 4 <= p.size() && !(p[end] == 0 && p[end + 1] == 0 &&
                                      p[end + 2] == 0 && p[end + 3] == 1))
        ++end;
      if (end + 4 > p.size())
        end = p.size();
      int type = p[start + 4] & 31;
      if (type == 7 || type == 8) {
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = start + 4; i < end; ++i) {
          hash ^= p[i];
          hash *= 1099511628211ULL;
        }
        auto &previous = type == 7 ? sps : pps;
        if (previous && previous != hash)
          change = true;
        previous = hash;
      }
      start = end;
    }
    return change;
  }
};
} // namespace h264
