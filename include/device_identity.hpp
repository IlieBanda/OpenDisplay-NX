#pragma once

namespace device {
enum class Model { Unknown, Standard, Lite, Oled };
inline constexpr const char *nameForModel(Model model) {
  switch (model) {
  case Model::Lite: return "Switch Lite";
  case Model::Oled: return "Switch OLED";
  default: return "Nintendo Switch";
  }
}
// Initialized once before discovery, rendering or worker startup.
inline const char *name = nameForModel(Model::Unknown);
}
