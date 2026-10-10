#include "performance.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
  perf::Meter meter;
  meter.reset(0);
  meter.pong(10000, 10010, 11005); // Mac clock +1000ms, round trip 10ms.
  for (int i = 1; i <= 60; ++i) {
    double t = i * 1000. / 60;
    meter.receive(t, 1000);
    meter.decoded(2);
    perf::Stamp stamp;
    stamp.arrival = t - 20;
    stamp.capture = 10000 + t + 1000 - 30;
    meter.presented(stamp, t, 10000 + t, 3, 1);
  }
  auto s = meter.sample(1000);
  assert(std::abs(s.fps - 60) < .01 && std::abs(s.mbps - .48) < .01);
  assert(s.offsetKnown && std::abs(s.delayMs - 30) < .01 && s.rttMs == 10);
  assert(s.localMs == 20 && s.decodeMs == 2 && s.uploadMs == 3 &&
         s.presentMs == 1);
  assert(std::abs(s.intervalP50 - 1000. / 60) < .01 && !s.idle);
  // Static redraws are not new video images. No fake 60 FPS on an idle screen.
  s = meter.sample(2600);
  assert(s.fps == 0 && s.idle);
  meter.reset(3000);
  meter.presented({}, 3100, 50000, 1, 1);
  s = meter.sample(4000);
  assert(s.delayMs < 0 && !s.offsetKnown);
  meter.pong(200, 100, 300); // Bad/unsolicited timing cannot establish a clock.
  assert(!meter.sample(4000).offsetKnown);
  meter.pong(0, 10, 1005);
  meter.pong(20, 220, 1500);
  s = meter.sample(4000);
  assert(s.offsetKnown && s.rttMs == 200);
  // Delay uses the lowest-RTT sample rather than a later asymmetric RTT.
  perf::Stamp stamp;
  stamp.capture = 2030;
  meter.presented(stamp, 4200, 1060, 0, 0);
  s = meter.sample(5000);
  assert(s.delayMs == 30);
  std::cout << "OK: actual video FPS, idle, clock sync, unknown delay, "
               "minimum-RTT offset and pipeline measurements\n";
}
