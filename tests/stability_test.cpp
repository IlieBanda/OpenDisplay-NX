#include "stability.hpp"
#include "wire.hpp"
#include <cassert>
#include <string>
int main() {
  assert(stability::validSurface(1280, 768, 1280, 720));
  assert(!stability::validSurface(1280, 768, 1281, 720));
  assert(!stability::validSurface(640, 768, 1280, 720));
  assert(!stability::validSurface(1280, 2147483647, 1280, 720));
  assert(!stability::validPicture(0, 720));
  stability::Recovery r;
  assert(r.failed(true) == stability::Recovery::Continue);
  // A successfully queued packet with no decoded picture doesn't reset r.
  assert(r.failed(true) == stability::Recovery::Continue);
  assert(r.failed(true) == stability::Recovery::Software);
  r.recovered();
  for(int i=0;i<11;++i) assert(r.failed(false) == stability::Recovery::Continue);
  assert(r.failed(false) == stability::Recovery::Disconnect);
  r.recovered(); assert(r.failures == 0);
  auto safe = [](std::string s) {return wire::safeJsonDepth(std::vector<uint8_t>(s.begin(), s.end()));};
  assert(safe(R"({"text":"[{{\\\"", "a":[1]})"));
  assert(safe(std::string(24, '[')+"0"+std::string(24, ']')));
  assert(!safe(std::string(25, '[')+"0"+std::string(25, ']')));
  assert(!safe("{\"unterminated"));
  assert(!safe("}{"));
}
