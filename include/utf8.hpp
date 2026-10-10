#pragma once
#include <cstdint>
namespace utf8 {
inline uint32_t next(const char *&p) {
  const auto first = static_cast<unsigned char>(*p++);
  if (first < 128)
    return first;
  int count = first >= 0xC2 && first <= 0xDF   ? 1
              : first >= 0xE0 && first <= 0xEF ? 2
              : first >= 0xF0 && first <= 0xF4 ? 3
                                               : 0;
  if (!count)
    return 0xFFFD;
  uint32_t code = first & (0x7F >> count);
  for (int i = 0; i < count; ++i) {
    const auto c = static_cast<unsigned char>(*p);
    if (!c || (c & 0xC0) != 0x80)
      return 0xFFFD;
    ++p;
    code = (code << 6) | (c & 63);
  }
  if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
      (count == 3 && code < 0x10000) || code > 0x10FFFF ||
      (code >= 0xD800 && code <= 0xDFFF))
    return 0xFFFD;
  return code;
}
} // namespace utf8
