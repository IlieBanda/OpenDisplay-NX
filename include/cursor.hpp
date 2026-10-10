#pragma once
#include "json.hpp"
#include <cmath>
#include <cstdint>
#include <png.h>
#include <string>
#include <vector>

namespace cursor {
inline bool base64(const std::string &input, std::vector<uint8_t> &output) {
  output.clear();
  if (input.empty() || input.size() > 32000 || input.size() % 4)
    return false;
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '+')
      return 62;
    if (c == '/')
      return 63;
    return -1;
  };
  for (size_t i = 0; i < input.size(); i += 4) {
    int a = value(input[i]), b = value(input[i + 1]);
    bool p2 = input[i + 2] == '=', p3 = input[i + 3] == '=';
    int c = p2 ? 0 : value(input[i + 2]), d = p3 ? 0 : value(input[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0 || (p2 && !p3) ||
        ((p2 || p3) && i + 4 != input.size()))
      return false;
    if ((p2 && (b & 15)) || (p3 && !p2 && (c & 3)))
      return false;
    uint32_t bits = (a << 18) | (b << 12) | (c << 6) | d;
    output.push_back(bits >> 16);
    if (!p2)
      output.push_back(bits >> 8);
    if (!p3)
      output.push_back(bits);
  }
  return output.size() <= 24000;
}
struct Image {
  std::vector<uint8_t> rgba;
  unsigned width = 0, height = 0;
  double nw = 0, nh = 0, ax = 0, ay = 0;
  unsigned revision = 0;
  bool load(const nlohmann::json &j) {
    if (!j.is_object() || !j.contains("png") || !j["png"].is_string())
      return false;
    for (const char *key : {"nw", "nh", "ax", "ay"})
      if (!j.contains(key) || !j[key].is_number())
        return false;
    double w = j["nw"].get<double>(), h = j["nh"].get<double>(),
           x = j["ax"].get<double>(), y = j["ay"].get<double>();
    if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(x) ||
        !std::isfinite(y) || w <= 0 || h <= 0 || w > 1 || h > 1 || x < 0 ||
        x > 1 || y < 0 || y > 1)
      return false;
    std::vector<uint8_t> png;
    if (!base64(j["png"].get<std::string>(), png))
      return false;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, png.data(), png.size())) {
      png_image_free(&image);
      return false;
    }
    if (!image.width || !image.height || image.width > 512 ||
        image.height > 512) {
      png_image_free(&image);
      return false;
    }
    image.format = PNG_FORMAT_RGBA;
    std::vector<uint8_t> decoded(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, decoded.data(), 0, nullptr)) {
      png_image_free(&image);
      return false;
    }
    width = image.width;
    height = image.height;
    rgba = std::move(decoded);
    nw = w;
    nh = h;
    ax = x;
    ay = y;
    ++revision;
    png_image_free(&image);
    return true;
  }
  void clear() {
    rgba.clear();
    width = height = 0;
    ++revision;
  }
};
} // namespace cursor
