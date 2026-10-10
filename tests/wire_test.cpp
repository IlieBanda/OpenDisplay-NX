#include "discovery.hpp"
#include "json.hpp"
#include "wire.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <iostream>
using Bytes = std::vector<uint8_t>;
static Bytes framed(const Bytes &p) {
  Bytes b;
  discovery::u32(b, p.size());
  b.insert(b.end(), p.begin(), p.end());
  return b;
}
int main() {
  Bytes control = {'{', '"', 't', 'y', 'p', 'e', '"', ':',
                   '"', 'p', 'i', 'n', 'g', '"', '}'};
  Bytes video = {'{', '}', 0,    0, 0, 1, 0x67, 0,   0,
                 0,   1,   0x68, 0, 0, 0, 1,    0x65};
  assert(wire::control(control));
  assert(!wire::control(video));
  assert(wire::annexB(video) == 2);
  assert(wire::annexB(control) == control.size());
  Bytes large(32768, 'x');
  large[0] = '{';
  assert(!wire::control(large));
  Bytes stream;
  for (auto p : {control, video, control}) {
    auto b = framed(p);
    stream.insert(stream.end(), b.begin(), b.end());
  }
  for (size_t chunk = 1; chunk <= stream.size(); ++chunk) {
    wire::Framer f;
    std::vector<Bytes> frames;
    for (size_t i = 0; i < stream.size(); i += chunk)
      assert(f.feed(stream.data() + i, std::min(chunk, stream.size() - i),
                    [&](const Bytes &p) { frames.push_back(p); }));
    assert((frames == std::vector<Bytes>{control, video, control}));
  }
  for (Bytes bad :
       {Bytes{0, 0, 0, 0}, Bytes{0, 128, 0, 1}, Bytes{255, 255, 255, 255}}) {
    wire::Framer f;
    assert(
        !f.feed(bad.data(), bad.size(), [](const Bytes &) { assert(false); }));
    auto b = framed(control);
    int count = 0;
    assert(f.feed(b.data(), b.size(), [&](const Bytes &) { ++count; }));
    assert(count == 1);
  }
  auto packet = discovery::announcement("01234567-89ab-4cde-8012-3456789abcde",
                                        inet_addr("192.168.1.10"));
  assert(packet[2] == 0x84 && packet[7] == 5);
  size_t pos = 12;
  int records = 0;
  while (pos < packet.size()) {
    while (packet.at(pos) != 0) {
      unsigned len = packet.at(pos++);
      assert(len <= 63);
      pos += len;
    }
    ++pos;
    unsigned type = (packet.at(pos) << 8) | packet.at(pos + 1);
    pos += 8;
    unsigned length = (packet.at(pos) << 8) | packet.at(pos + 1);
    pos += 2;
    assert(pos + length <= packet.size());
    if (type == 1) {
      assert(length == 4 && packet[pos] == 192 && packet[pos + 3] == 10);
    }
    if (type == 33) {
      assert(length >= 7 && packet[pos + 4] == 0x23 && packet[pos + 5] == 0x28);
    }
    pos += length;
    ++records;
  }
  assert(records == 5);
  std::cout << "OK: fragmentation, coalescing, length bounds, telemetry demux, "
               "DNS-SD records\n";
}
