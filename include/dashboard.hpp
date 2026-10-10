#pragma once
#include "localization.hpp"
#include "ui_font.hpp"
#include <cmath>
#include <string>
namespace dashboard {
inline constexpr int listX = 470, listY = 192, listWidth = 730, rowHeight = 70,
                     rowCount = 5;
inline int hitTest(int x, int y) {
  if (x < listX || x >= listX + listWidth || y < listY ||
      y >= listY + rowHeight * rowCount)
    return -1;
  return (y - listY) / rowHeight;
}
inline void draw(SDL_Renderer *renderer, const std::string &state,
                 const std::string &address, bool softwarePreference,
                 bool dark = true, int focus = 0, bool stats = false,
                 bool connected = false,
                 locale::Language language = locale::Language::English) {
  auto tr = [&](locale::Key key) { return locale::text(key, language); };
  const Uint8 bg = dark ? 45 : 235, fg = dark ? 255 : 45;
  auto color = [&](Uint8 v) { SDL_SetRenderDrawColor(renderer, v, v, v, 255); };
  auto text = [&](const char *s, int x, int y, int size = 24) {
    ui::label(renderer, s, x, y, size);
  };
  color(bg);
  SDL_RenderClear(renderer);
  color(fg);
  text("OpenDisplay NX", 65, 26, 28);
  color(dark ? 170 : 110);
  text("Nintendo Switch OLED", 980, 31, 18);
  color(fg);
  SDL_RenderDrawLine(renderer, 30, 88, 1250, 88);
  color(dark ? 50 : 240);
  SDL_Rect side{30, 89, 380, 558};
  SDL_RenderFillRect(renderer, &side);
  color(dark ? 81 : 208);
  SDL_RenderDrawLine(renderer, 410, 89, 410, 647);
  color(fg);
  ui::wrappedLabel(renderer,
                   tr(connected ? locale::Connected : locale::Connect), 70, 150,
                   24, 300);
  color(dark ? 175 : 110);
  text("Switch OLED", 70, 216, 20);
  if (!connected) {
    color(fg);
    const std::string instructions =
        std::string(tr(locale::Instruction1)) + " " + tr(locale::Instruction2);
    ui::wrappedLabel(renderer, instructions.c_str(), 70, 270, 18, 300);
  }
  color(dark ? 175 : 110);
  text(tr(locale::Address), 70, 405, 18);
  color(fg);
  text(address.c_str(), 70, 437, 26);
  color(dark ? 175 : 110);
  text("H.264 / 720p / Wi-Fi", 70, 480, 18);
  ui::wrappedLabel(renderer, locale::status(state.c_str(), language), 70, 548,
                   18, 300);
  color(fg);
  text(tr(locale::SettingsTitle), listX, 134, 24);
  const char *names[] = {tr(locale::Decoder), tr(locale::Statistics),
                         tr(locale::LanguageLabel), tr(locale::DisconnectKey),
                         tr(locale::ExitKey)};
  const char *values[] = {softwarePreference ? "CPU" : tr(locale::Automatic),
                          tr(stats ? locale::On : locale::Off),
                          locale::name(language), "", ""};
  for (int i = 0; i < rowCount; ++i) {
    SDL_Rect row{listX, listY + i * rowHeight, listWidth, rowHeight};
    color(dark ? 81 : 208);
    SDL_RenderDrawLine(renderer, row.x, row.y, row.x + row.w, row.y);
    if (focus == i) {
      SDL_SetRenderDrawColor(renderer, dark ? 31 : 252, dark ? 34 : 255,
                             dark ? 39 : 248, 255);
      SDL_RenderFillRect(renderer, &row);
      const double pulse =
          (std::sin(SDL_GetTicks() / 1000.0 * 6.283185307) + 1) / 2;
      SDL_SetRenderDrawColor(renderer, Uint8(25 + 112 * pulse),
                             Uint8(138 + 103 * pulse), Uint8(198 + 44 * pulse),
                             255);
      for (int border = 0; border < 5; ++border) {
        SDL_Rect edge{row.x - border, row.y - border, row.w + border * 2,
                      row.h + border * 2};
        SDL_RenderDrawRect(renderer, &edge);
      }
    }
    color(i == 3 && !connected ? (dark ? 110 : 155) : fg);
    text(names[i], row.x + 20, row.y + 20, 24);
    SDL_SetRenderDrawColor(renderer, dark ? 7 : 49, dark ? 247 : 79,
                           dark ? 198 : 235, 255);
    text(values[i], row.x + row.w - 24 - ui::textWidth(values[i], 20),
         row.y + 23, 20);
  }
  color(dark ? 81 : 208);
  SDL_RenderDrawLine(renderer, listX, listY + rowCount * rowHeight,
                     listX + listWidth, listY + rowCount * rowHeight);
  color(dark ? 175 : 110);
  text(tr(locale::Select), listX, 582, 18);
  color(fg);
  SDL_RenderDrawLine(renderer, 30, 647, 1250, 647);
  auto hint = [&](const char *button, const char *fallback, const char *label,
                  int x) {
    color(fg);
    text(ui::supportsText(button) ? button : fallback, x - 13, 664, 28);
    text(label, x + 24, 668, 20);
  };
  hint("\uE0EF", "+", tr(locale::MenuKey), 70);
  hint("\uE0E1", "B", tr(locale::Back), 880);
  hint("\uE0E0", "A", tr(locale::Confirm), 1050);
  SDL_RenderPresent(renderer);
}
} // namespace dashboard
