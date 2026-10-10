#include "dashboard.hpp"
#include <cassert>
#include <fstream>
#include <png.h>
#include <vector>
int main(int argc, char **argv) {
  assert(SDL_Init(0) == 0);
  auto *surface =
      SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
  assert(surface);
  auto *renderer = SDL_CreateSoftwareRenderer(surface);
  assert(renderer);
  std::ifstream font(argc > 1 ? argv[1]
                              : "/System/Library/Fonts/Supplemental/Arial.ttf",
                     std::ios::binary);
  std::vector<unsigned char> data((std::istreambuf_iterator<char>(font)), {});
  assert(!data.empty());
  std::ifstream cjk("/System/Library/Fonts/STHeiti Medium.ttc",
                    std::ios::binary);
  std::vector<unsigned char> chinese((std::istreambuf_iterator<char>(cjk)), {});
  assert(!chinese.empty());
  std::ifstream icons("upstream/moonlight/resources/font/switch_icons.ttf",
                      std::ios::binary);
  std::vector<unsigned char> symbols((std::istreambuf_iterator<char>(icons)),
                                     {});
  assert(!symbols.empty());
  assert(ui::initializeFont(renderer, data.data(), chinese.data(),
                            symbols.data()));
  assert(ui::supportsText("\uE0EF\uE0E1\uE0E0"));
  for (const auto &translations : locale::strings)
    for (auto text : translations)
      assert(ui::supportsText(text));
  for (int i = 0; i < 4; ++i)
    assert(ui::supportsText(locale::name(static_cast<locale::Language>(i))));
  for (int language = 0; language < 4; ++language) {
    dashboard::draw(renderer, "Connected. Waiting for H.264 video...",
                    "192.168.1.100", false, true, 2, true, true,
                    static_cast<locale::Language>(language));
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = 1280;
    image.height = 720;
    image.format = PNG_FORMAT_RGBA;
    std::string path = "build/dashboard-" + std::to_string(language) + ".png";
    assert(png_image_write_to_file(&image, path.c_str(), 0, surface->pixels,
                                   surface->pitch, nullptr));
  }
  ui::releaseFont();
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);
  SDL_Quit();
}
