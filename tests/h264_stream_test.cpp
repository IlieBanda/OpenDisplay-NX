#include "h264_stream.hpp"
#include <cassert>
#include <iostream>
int main() {
  std::vector<uint8_t> key = {0, 0,    0, 1, 0x67, 1, 2, 0, 0,    0,
                              1, 0x68, 3, 4, 0,    0, 0, 1, 0x65, 9};
  h264::Parameters parameters;
  assert(h264::Parameters::idr(key));
  assert(!parameters.changed(key));
  assert(!parameters.changed(key));
  auto next = key;
  next[5] = 5;
  assert(parameters.changed(next));
  assert(!parameters.changed(next));
  auto delta = key;
  delta[18] = 0x41;
  assert(!h264::Parameters::idr(delta));
  parameters.reset();
  assert(!parameters.changed(next));
  std::cout << "OK: H.264 IDR detection, stable parameter sets, SPS change and "
               "fresh session\n";
}
