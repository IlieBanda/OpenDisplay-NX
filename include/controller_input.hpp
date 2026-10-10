#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace controller {
using json = nlohmann::json;
struct Stick {
  double x = 0, y = 0;
};
inline Stick response(Stick stick) {
  const double length = std::hypot(stick.x, stick.y);
  constexpr double deadzone = 0.18;
  if (length <= deadzone)
    return {};
  const double amount =
      std::clamp((length - deadzone) / (1 - deadzone), 0.0, 1.0);
  const double speed = amount * amount;
  return {stick.x / length * speed, stick.y / length * speed};
}
class Pointer {
  double x = 0.5, y = 0.5, scrollX = 0, scrollY = 0;
  bool leftDown = false, rightDown = false, moving = false, armed = false,
       rightArmed = false;
  json touch(const char *phase) const {
    return {{"type", "touch"}, {"phase", phase}, {"x", x}, {"y", y}};
  }

public:
  void reset() { *this = Pointer{}; }
  std::vector<json> cancel() {
    std::vector<json> events;
    if (leftDown)
      events.push_back(touch("cancelled"));
    if (rightDown) {
      auto event = touch("cancelled");
      event["type"] = "mouse";
      events.push_back(std::move(event));
    }
    leftDown = rightDown = moving = armed = rightArmed = false;
    scrollX = scrollY = 0;
    return events;
  }
  std::vector<json> update(bool enabled, Stick left, Stick right, bool held,
                           double seconds, double remoteX, double remoteY,
                           int width, int height, bool rightHeld = false) {
    if (!enabled)
      return cancel();
    if (!held)
      armed = true;
    held = held && armed;
    if (!rightHeld)
      rightArmed = true;
    rightHeld = rightHeld && rightArmed;
    left = response(left);
    right = response(right);
    const bool motion = left.x != 0 || left.y != 0;
    if (!moving && !leftDown && !rightDown) {
      x = std::clamp(remoteX, 0.0, 1.0);
      y = std::clamp(remoteY, 0.0, 1.0);
    }
    const double dt = std::clamp(seconds, 0.0, 0.05);
    const double oldX = x, oldY = y;
    x = std::clamp(x + left.x * 750 * dt / std::max(width, 1), 0.0, 1.0);
    y = std::clamp(y - left.y * 750 * dt / std::max(height, 1), 0.0, 1.0);
    std::vector<json> events;
    if (held != leftDown) {
      events.push_back(touch(held ? "began" : "ended"));
      leftDown = held;
    } else if (x != oldX || y != oldY) {
      events.push_back(touch("moved"));
    }
    if (rightHeld != rightDown) {
      auto event = touch(rightHeld ? "began" : "ended");
      event["type"] = "mouse";
      events.push_back(std::move(event));
      rightDown = rightHeld;
    }
    scrollX += -right.x * 900 * dt;
    scrollY += right.y * 900 * dt;
    const double dx = std::trunc(scrollX), dy = std::trunc(scrollY);
    if (dx != 0 || dy != 0) {
      events.push_back({{"type", "scroll"}, {"dx", dx}, {"dy", dy}});
      scrollX -= dx;
      scrollY -= dy;
    }
    if (right.x == 0 && right.y == 0)
      scrollX = scrollY = 0;
    moving = motion;
    return events;
  }
};
} // namespace controller
