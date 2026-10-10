#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
namespace perf {
struct Stamp {
  int64_t id = 0;
  double capture = 0, sent = 0, arrival = 0, decoded = 0;
};
struct Sample {
  double fps = 0, mbps = 0, decodeMs = 0, uploadMs = 0, presentMs = 0;
  double transferMs = 0, textureMs = 0;
  double delayMs = -1, delayP95 = -1, rttMs = -1, localMs = 0;
  double intervalP50 = 0, intervalP95 = 0;
  uint64_t replaced = 0;
  int capFps = 0, macEncoderDrops = 0, macNetworkDrops = 0, macPending = 0;
  bool idle = true, offsetKnown = false;
};
class Meter {
  mutable std::mutex mutex;
  double window = 0, lastReceive = -1, lastPresent = -1, offset = 0;
  uint64_t frames = 0, bytes = 0;
  std::deque<std::pair<double, double>> offsets;
  std::vector<double> decodes, uploads, presents, delays, local, intervals;
  std::vector<double> transfers, textures;
  Sample value;
  static void add(std::vector<double> &v, double n) {
    if (!std::isfinite(n) || n < 0 || n > 5000)
      return;
    if (v.size() >= 240)
      v.erase(v.begin());
    v.push_back(n);
  }
  static double percentile(std::vector<double> v, double q,
                           double missing = 0) {
    if (v.empty())
      return missing;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t((v.size() - 1) * q))];
  }

public:
  void reset(double now) {
    std::lock_guard<std::mutex> lock(mutex);
    window = now;
    lastReceive = lastPresent = -1;
    frames = bytes = 0;
    offsets.clear();
    decodes.clear();
    uploads.clear();
    transfers.clear();
    textures.clear();
    presents.clear();
    delays.clear();
    local.clear();
    intervals.clear();
    value = {};
  }
  void receive(double now, size_t size) {
    std::lock_guard<std::mutex> lock(mutex);
    lastReceive = now;
    bytes += size;
  }
  void decoded(double duration) {
    std::lock_guard<std::mutex> lock(mutex);
    add(decodes, duration);
  }
  void uploadStages(double transferMs, double textureMs) {
    std::lock_guard<std::mutex> lock(mutex);
    add(transfers, transferMs);
    add(textures, textureMs);
  }
  void replace() {
    std::lock_guard<std::mutex> lock(mutex);
    ++value.replaced;
  }
  void pong(double sentWall, double receivedWall, double macWall) {
    const double rtt = receivedWall - sentWall;
    if (!std::isfinite(rtt) || rtt < 0 || rtt > 2000 || !std::isfinite(macWall))
      return;
    std::lock_guard<std::mutex> lock(mutex);
    offsets.emplace_back(rtt, macWall - (sentWall + receivedWall) / 2);
    if (offsets.size() > 15)
      offsets.pop_front();
    offset = std::min_element(
                 offsets.begin(), offsets.end(),
                 [](const auto &a, const auto &b) { return a.first < b.first; })
                 ->second;
    value.rttMs = rtt;
    value.offsetKnown = true;
  }
  void sender(int captureFps, int encoderDrops, int networkDrops, int pending) {
    std::lock_guard<std::mutex> lock(mutex);
    value.capFps = captureFps;
    value.macEncoderDrops = encoderDrops;
    value.macNetworkDrops = networkDrops;
    value.macPending = pending;
  }
  void presented(const Stamp &stamp, double mono, double wall, double uploadMs,
                 double presentMs) {
    std::lock_guard<std::mutex> lock(mutex);
    ++frames;
    add(uploads, uploadMs);
    add(presents, presentMs);
    if (stamp.arrival > 0)
      add(local, mono - stamp.arrival);
    if (lastPresent >= 0 && mono - lastPresent < 1500)
      add(intervals, mono - lastPresent);
    lastPresent = mono;
    if (value.offsetKnown && stamp.capture > 0)
      add(delays, wall + offset - stamp.capture);
  }
  double senderTime(double wall) const {
    std::lock_guard<std::mutex> lock(mutex);
    return value.offsetKnown ? wall + offset : 0;
  }
  Sample sample(double now) {
    std::lock_guard<std::mutex> lock(mutex);
    value.idle = lastReceive < 0 || now - lastReceive > 1500;
    double elapsed = now - window;
    if (elapsed >= 1000) {
      value.fps = frames * 1000.0 / elapsed;
      value.mbps = bytes * 8.0 / elapsed / 1000;
      value.decodeMs = percentile(decodes, .5);
      value.uploadMs = percentile(uploads, .5);
      value.transferMs = percentile(transfers, .5);
      value.textureMs = percentile(textures, .5);
      value.presentMs = percentile(presents, .5);
      value.localMs = percentile(local, .5);
      value.delayMs = percentile(delays, .5, -1);
      value.delayP95 = percentile(delays, .95, -1);
      value.intervalP50 = percentile(intervals, .5);
      value.intervalP95 = percentile(intervals, .95);
      frames = bytes = 0;
      window = now;
      decodes.clear();
      uploads.clear();
      transfers.clear();
      textures.clear();
      presents.clear();
      local.clear();
      delays.clear();
      intervals.clear();
    }
    return value;
  }
};
} // namespace perf
