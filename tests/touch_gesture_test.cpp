#include "touch_gesture.hpp"
#include <cassert>
#include <string>
#include <vector>
struct Event { std::string phase; double x, y; };
int main() {
  touch::Gesture g;
  std::vector<Event> events;
  int scrolls = 0;
  auto emit = [&](const char *p, double x, double y) { events.push_back({p,x,y}); };
  auto step = [&](int n, double px, double t, bool enabled = true, unsigned id = 1) {
    g.update(n,id,px,100,px/1280,100./720,t,enabled,1,1,emit,
             [&](double dx, double) { assert(dx == 10); ++scrolls; });
  };
  step(1,100,0); assert(events.empty());
  step(1,103,20); assert(events.back().phase == "moved");
  step(0,0,40); assert(events.size() == 3);
  assert(events[1].phase == "began" && events[1].x == 100./1280);
  assert(events[2].phase == "ended" && events[2].x == 103./1280);
  events.clear();
  step(1,100,100); step(1,100,219); assert(events.empty());
  step(1,100,220); assert(events.back().phase == "began");
  step(1,100,230,false); assert(events.back().phase == "cancelled");
  step(1,100,240); assert(!g.pendingDown); step(0,0,250);
  events.clear();
  step(1,100,300); step(1,115,310);
  assert(events.size() == 2 && events[0].phase == "began" && events[1].phase == "moved");
  step(2,120,320); assert(events[2].phase == "cancelled");
  step(2,130,330); assert(scrolls == 1);
  step(1,130,340); assert(!g.pendingDown); step(0,0,350);
  events.clear();
  step(1,100,400); step(2,100,410);
  assert(events.size() == 1 && events[0].phase == "moved");
  step(1,100,420); step(0,0,430); assert(events.size() == 1);
  events.clear();
  step(1,100,500); step(1,100,510,false); step(0,0,520);
  assert(events.empty());
  step(1,100,600); step(1,100,610,true,2); step(0,0,620);
  assert(events.empty());
}
