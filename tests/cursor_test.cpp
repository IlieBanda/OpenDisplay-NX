#include "cursor.hpp"
#include <cassert>
#include <iostream>
using json = nlohmann::json;
static std::string encode(const std::vector<uint8_t> &bytes) {
  const char *table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < bytes.size(); i += 3) {
    size_t n = std::min(size_t(3), bytes.size() - i);
    uint32_t v = uint32_t(bytes[i]) << 16;
    if (n > 1)
      v |= uint32_t(bytes[i + 1]) << 8;
    if (n > 2)
      v |= bytes[i + 2];
    out += table[v >> 18];
    out += table[(v >> 12) & 63];
    out += n > 1 ? table[(v >> 6) & 63] : '=';
    out += n > 2 ? table[v & 63] : '=';
  }
  return out;
}
static std::vector<uint8_t> png(unsigned w, unsigned h) {
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = w;
  image.height = h;
  image.format = PNG_FORMAT_RGBA;
  std::vector<uint8_t> pixels(w * h * 4);
  for (size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i] = 255;
    pixels[i + 3] = (i == 0) ? 0 : 255;
  }
  png_alloc_size_t size = 0;
  assert(png_image_write_to_memory(&image, nullptr, &size, 0, pixels.data(), 0,
                                   nullptr));
  std::vector<uint8_t> out(size);
  assert(png_image_write_to_memory(&image, out.data(), &size, 0, pixels.data(),
                                   0, nullptr));
  out.resize(size);
  return out;
}
int main() {
  std::vector<uint8_t> bytes;
  for (auto s : {"A", "=AAA", "AA=A", "AAAA=AAA", "AB==", "AAB=", "!!!!"})
    assert(!cursor::base64(s, bytes));
  assert(cursor::base64("AA==", bytes) && bytes == std::vector<uint8_t>{0});
  assert(cursor::base64("AAE=", bytes) &&
         (bytes == std::vector<uint8_t>{0, 1}));
  assert(cursor::base64("AAEC", bytes) &&
         (bytes == std::vector<uint8_t>{0, 1, 2}));
  cursor::Image image;
  json valid = {{"png", encode(png(2, 2))},
                {"nw", 0.02},
                {"nh", 0.03},
                {"ax", 0.25},
                {"ay", 0.5}};
  assert(image.load(valid));
  assert(image.width == 2 && image.height == 2 && image.rgba.size() == 16 &&
         image.rgba[0] == 255 && image.rgba[3] == 0 && image.rgba[7] == 255 &&
         image.revision == 1);
  auto original = image.rgba;
  for (auto key : {"nw", "nh", "ax", "ay"}) {
    auto bad = valid;
    bad[key] = -1;
    assert(!image.load(bad));
    assert(image.rgba == original);
    bad[key] = "text";
    assert(!image.load(bad));
  }
  auto bad = valid;
  bad["png"] = "AAAA";
  assert(!image.load(bad));
  bad["png"] = encode(png(513, 1));
  assert(!image.load(bad));
  assert(!image.load(json::object()));
  image.clear();
  assert(image.rgba.empty());
  std::cout << "OK: cursor PNG alpha, size/hotspot, strict base64, "
               "malformed/oversized image handling\n";
}
