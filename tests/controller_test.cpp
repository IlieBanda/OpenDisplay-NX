#include "controller_input.hpp"
#include <cassert>
#include <iostream>
using controller::Pointer;
int main() {
  Pointer pointer;
  auto run = [&](bool enabled, controller::Stick left, controller::Stick right,
                 bool held, double dt = 1.0 / 60) {
    return pointer.update(enabled, left, right, held, dt, 0.5, 0.5, 1280, 720);
  };
  assert(run(true, {0.1, -0.1}, {}, false).empty());
  auto move = run(true, {1, 0}, {}, false);
  assert(move.size() == 1 && move[0]["phase"] == "moved");
  assert(move[0]["x"].get<double>() > 0.5 && move[0]["y"] == 0.5);
  auto press = run(true, {}, {}, true);
  assert(press.size() == 1 && press[0]["phase"] == "began");
  assert(run(true, {}, {}, true).empty());
  auto drag = run(true, {0, 1}, {}, true);
  assert(drag[0]["phase"] == "moved" && drag[0]["y"].get<double>() < 0.5);
  auto release = run(true, {}, {}, false);
  assert(release[0]["phase"] == "ended");
  auto scroll = run(true, {}, {0, -1}, false);
  assert(scroll.size() == 1 && scroll[0]["type"] == "scroll");
  assert(scroll[0]["dy"].get<double>() < 0);
  run(true, {}, {}, true);
  auto cancel = run(false, {}, {}, true);
  assert(cancel.size() == 1 && cancel[0]["phase"] == "cancelled");
  assert(run(false, {}, {}, true).empty());
  assert(run(true, {}, {}, true).empty()); // held on return must not click
  run(true, {}, {}, false);
  assert(run(true, {}, {}, true)[0]["phase"] == "began");
  pointer.reset();
  assert(pointer.update(true, {}, {}, false, 0.016, 0.5, 0.5, 1280, 720, false)
             .empty());
  auto right =
      pointer.update(true, {}, {}, false, 0.016, 0.5, 0.5, 1280, 720, true);
  assert(right.size() == 1 && right[0]["type"] == "mouse" &&
         right[0]["phase"] == "began");
  auto rightCancel = pointer.cancel();
  assert(rightCancel.size() == 1 && rightCancel[0]["type"] == "mouse" &&
         rightCancel[0]["phase"] == "cancelled");
  pointer.reset();
  int gentleScroll = 0;
  for (int i = 0; i < 20; ++i) {
    for (auto &event : run(true, {}, {0, 0.3}, false))
      if (event["type"] == "scroll")
        gentleScroll += event["dy"].get<int>();
  }
  assert(gentleScroll > 0 &&
         gentleScroll < 10); // fractional deltas survive sender rounding
  pointer.reset();
  double finalX = 0;
  for (int i = 0; i < 300; ++i) {
    auto events = run(true, {1, 0}, {}, false, 1);
    if (!events.empty())
      finalX = events[0]["x"].get<double>();
  }
  assert(finalX == 1); // clamp position and long frame time
  auto displacement = [](int rate) {
    Pointer p;
    double x = 0;
    for (int i = 0; i < rate; ++i) {
      auto events =
          p.update(true, {0.5, 0}, {}, false, 1.0 / rate, 0, 0.5, 1280, 720);
      x = events[0]["x"].get<double>();
    }
    return x;
  };
  assert(std::abs(displacement(30) - displacement(60)) < 1e-9);
  std::cout << "OK: stick deadzone, rate independence, drag, release, focus "
               "cancellation, scroll\n";
}
