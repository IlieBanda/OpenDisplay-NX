#pragma once
#include "device_identity.hpp"
#include <cstdint>
#include <string>
#include <vector>
// DNS-SD packet construction kept independent of the Switch runtime.
namespace discovery {
inline void u16(std::vector<uint8_t> &b, unsigned v) {
  b.push_back(v >> 8);
  b.push_back(v);
}
inline void u32(std::vector<uint8_t> &b, unsigned v) {
  u16(b, v >> 16);
  u16(b, v);
}
inline void name(std::vector<uint8_t> &b, const std::string &s) {
  size_t start = 0;
  while (start < s.size()) {
    auto end = s.find('.', start);
    if (end == std::string::npos)
      end = s.size();
    b.push_back(end - start);
    b.insert(b.end(), s.begin() + start, s.begin() + end);
    start = end + 1;
  }
  b.push_back(0);
}
inline void record(std::vector<uint8_t> &b, const std::string &n, int type,
                   bool unique, const std::vector<uint8_t> &data,
                   unsigned ttl) {
  name(b, n);
  u16(b, type);
  u16(b, unique ? 0x8001 : 1);
  u32(b, ttl);
  u16(b, data.size());
  b.insert(b.end(), data.begin(), data.end());
}
inline std::vector<uint8_t> announcement(const std::string &id, uint32_t ip,
                                         unsigned ttl = 120) {
  std::string service = "_opensidecar._tcp.local",
              instance = std::string(device::name) + "." + service,
              host = "opendisplay-" + id.substr(0, 8) + ".local";
  std::vector<uint8_t> b, d;
  u16(b, 0);
  u16(b, 0x8400);
  u16(b, 0);
  u16(b, 5);
  u16(b, 0);
  u16(b, 0);
  name(d, service);
  record(b, "_services._dns-sd._udp.local", 12, false, d, ttl);
  d.clear();
  name(d, instance);
  record(b, service, 12, false, d, ttl);
  d.clear();
  u16(d, 0);
  u16(d, 0);
  u16(d, 9000);
  name(d, host);
  record(b, instance, 33, true, d, ttl);
  d.clear();
  for (auto s : {std::string("id=") + id, std::string("pv=3")}) {
    d.push_back(s.size());
    d.insert(d.end(), s.begin(), s.end());
  }
  record(b, instance, 16, true, d, ttl);
  d.clear();
  auto p = reinterpret_cast<uint8_t *>(&ip);
  d.insert(d.end(), p, p + 4);
  record(b, host, 1, true, d, ttl);
  return b;
}
} // namespace discovery
