#pragma once
#include <cmath>

namespace touch {
// Screen coordinates, independent of encoded video size. Tune on hardware;
// UIKit points are not Switch touchscreen pixels.
struct Gesture {
  static constexpr double slop = 14, holdMs = 120;
  bool pendingDown = false, downSent = false, blocked = false;
  bool scrolling = false;
  double startX = 0, startY = 0, startTime = 0;
  double x = 0, y = 0, screenX = 0, screenY = 0;
  double panX = 0, panY = 0;
  unsigned finger = 0;
  int panCount = 0;

  template <class Emit> void cancel(Emit emit) {
    if (downSent) emit("cancelled", x, y);
    pendingDown = downSent = scrolling = false;
    blocked = true; // Require every finger to lift before accepting a new press.
  }
  template <class Emit, class Scroll>
  void update(int count, unsigned id, double px, double py, double nx,
              double ny, double time, bool enabled, double scaleX,
              double scaleY, Emit emit, Scroll scroll) {
    if (!enabled) { cancel(emit); return; }
    if (count == 0) {
      if (pendingDown) {
        if (!downSent) emit("began", startX, startY);
        emit("ended", x, y);
      }
      pendingDown = downSent = scrolling = blocked = false;
      return;
    }
    if (count >= 2) {
      if (!scrolling) {
        cancel(emit);
        scrolling = true;
        emit("moved", nx, ny); // Choose the window before scrolling.
      } else if (count == 2 && panCount == 2) {
        const double dx = (px - panX) * scaleX;
        const double dy = (py - panY) * scaleY;
        if (dx != 0 || dy != 0) scroll(dx, dy);
      }
      panX = px; panY = py; panCount = count;
      return;
    }
    scrolling = false;
    if (blocked) return;
    if (pendingDown && finger != id) { cancel(emit); return; }
    if (!pendingDown) {
      pendingDown = true; finger = id;
      startX = x = nx; startY = y = ny;
      screenX = px; screenY = py; startTime = time;
      return;
    }
    const bool moved = x != nx || y != ny;
    if (!downSent && (time - startTime >= holdMs ||
        std::hypot(px - screenX, py - screenY) > slop)) {
      emit("began", startX, startY);
      downSent = true;
      // Restore the current position after committing at the initial point.
      if (nx != startX || ny != startY) emit("moved", nx, ny);
    } else if (moved) emit("moved", nx, ny);
    x = nx; y = ny;
  }
};
}
