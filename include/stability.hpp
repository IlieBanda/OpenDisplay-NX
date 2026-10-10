#pragma once
#include <cstdint>
namespace stability {
inline bool validPicture(int width, int height) {
  return width > 0 && height > 0 && width <= 1280 && height <= 720;
}
inline bool validSurface(int width, int height, int visibleWidth, int visibleHeight) {
  return validPicture(visibleWidth, visibleHeight) && width >= visibleWidth &&
         height >= visibleHeight && width <= 1280 && height <= 768;
}
// Packet acceptance is not recovery: only a decoded image clears errors.
struct Recovery {
  unsigned failures = 0;
  enum Action { Continue, Software, Disconnect };
  void recovered() { failures = 0; }
  Action failed(bool hardware) {
    ++failures;
    if (hardware && failures >= 3) return Software;
    return failures >= 12 ? Disconnect : Continue;
  }
};
}
