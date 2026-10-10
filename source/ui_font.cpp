#include "ui_font.hpp"
#include "localization.hpp"
#include "overlay.hpp"
#include "utf8.hpp"
#include <cctype>
#include <map>
#include <set>
#include <string>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <vector>
namespace ui {
static SDL_Texture *atlas = nullptr;
static std::map<uint32_t, stbtt_packedchar> glyphs;
bool initializeFont(SDL_Renderer *renderer, const unsigned char *data,
                    const unsigned char *chinese, const unsigned char *icons) {
  releaseFont();
  if (!data)
    return false;
  std::set<uint32_t> codes;
  for (uint32_t c = 32; c < 127; ++c)
    codes.insert(c);
  for (auto &translations : locale::strings)
    for (auto value : translations)
      for (auto p = value; *p;)
        codes.insert(utf8::next(p));
  for (int i = 0; i < 4; ++i)
    for (auto p = locale::name(static_cast<locale::Language>(i)); *p;)
      codes.insert(utf8::next(p));
  codes.insert(0xFFFD);
  for (auto code : {0xE0E0u, 0xE0E1u, 0xE0EFu})
    codes.insert(code);
  stbtt_fontinfo standard{}, han{}, symbols{};
  if (!stbtt_InitFont(&standard, data, stbtt_GetFontOffsetForIndex(data, 0)))
    return false;
  bool hasChinese =
      chinese &&
      stbtt_InitFont(&han, chinese, stbtt_GetFontOffsetForIndex(chinese, 0));
  bool hasIcons =
      icons &&
      stbtt_InitFont(&symbols, icons, stbtt_GetFontOffsetForIndex(icons, 0));
  std::vector<int> primary, secondary, iconCodes;
  for (auto c : codes) {
    if (c >= 0xE000 && c <= 0xF8FF && hasIcons &&
        stbtt_FindGlyphIndex(&symbols, c))
      iconCodes.push_back(c);
    else if (hasChinese && c >= 0x3400 && c <= 0x9FFF &&
             stbtt_FindGlyphIndex(&han, c))
      secondary.push_back(c);
    else if (stbtt_FindGlyphIndex(&standard, c))
      primary.push_back(c);
    else if (hasChinese && stbtt_FindGlyphIndex(&han, c))
      secondary.push_back(c);
  }
  std::vector<unsigned char> alpha(1024 * 1024);
  stbtt_pack_context pack{};
  if (!stbtt_PackBegin(&pack, alpha.data(), 1024, 1024, 0, 1, nullptr))
    return false;
  auto bake = [&](const unsigned char *font, std::vector<int> &list) {
    if (list.empty())
      return true;
    std::vector<stbtt_packedchar> chars(list.size());
    stbtt_pack_range range{};
    range.font_size = 42;
    range.array_of_unicode_codepoints = list.data();
    range.num_chars = list.size();
    range.chardata_for_range = chars.data();
    if (!stbtt_PackFontRanges(&pack, font, 0, &range, 1))
      return false;
    for (size_t i = 0; i < list.size(); ++i)
      glyphs[list[i]] = chars[i];
    return true;
  };
  bool baked =
      bake(data, primary) && bake(chinese, secondary) && bake(icons, iconCodes);
  stbtt_PackEnd(&pack);
  if (!baked) {
    glyphs.clear();
    return false;
  }
  std::vector<unsigned char> rgba(alpha.size() * 4, 255);
  for (size_t i = 0; i < alpha.size(); ++i)
    rgba[i * 4 + 3] = alpha[i];
  atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                            SDL_TEXTUREACCESS_STATIC, 1024, 1024);
  if (!atlas)
    return false;
  if (SDL_UpdateTexture(atlas, nullptr, rgba.data(), 1024 * 4) < 0) {
    releaseFont();
    return false;
  }
  SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(atlas, SDL_ScaleModeLinear);
  return true;
}
void releaseFont() {
  if (atlas)
    SDL_DestroyTexture(atlas);
  atlas = nullptr;
  glyphs.clear();
}
float textWidth(const char *text, float size) {
  float width = 0;
  for (auto p = text; *p;) {
    const auto code = utf8::next(p);
    auto found = glyphs.find(code);
    if (found == glyphs.end())
      found = glyphs.find('?');
    width += found == glyphs.end() ? size * 2 / 3
                                   : found->second.xadvance * size / 42;
  }
  return width;
}
void wrappedLabel(SDL_Renderer *renderer, const char *text, float x, float y,
                  float size, float width) {
  std::string line;
  for (auto p = text; *p;) {
    auto start = p;
    utf8::next(p);
    std::string next = line + std::string(start, p);
    if (!line.empty() && textWidth(next.c_str(), size) > width) {
      auto space = line.find_last_of(' ');
      if (space != std::string::npos) {
        label(renderer, line.substr(0, space).c_str(), x, y, size);
        line = line.substr(space + 1) + std::string(start, p);
      } else {
        label(renderer, line.c_str(), x, y, size);
        line.assign(start, p);
      }
      y += size * 1.5f;
    } else
      line = std::move(next);
  }
  if (!line.empty())
    label(renderer, line.c_str(), x, y, size);
}
bool supportsText(const char *text) {
  for (auto p = text; *p;)
    if (!glyphs.count(utf8::next(p)))
      return false;
  return true;
}
void label(SDL_Renderer *renderer, const char *text, float x, float y,
           float size) {
  if (!atlas) {
    std::string upper;
    for (auto p = text; *p;) {
      auto c = utf8::next(p);
      upper +=
          c < 128 ? char(std::toupper(static_cast<unsigned char>(c))) : '?';
    }
    overlay::bitmapText(renderer, upper.c_str(), x, y, int(size / 9));
    return;
  }
  Uint8 r, g, b, a;
  SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
  SDL_SetTextureColorMod(atlas, r, g, b);
  SDL_SetTextureAlphaMod(atlas, a);
  float scale = size / 42;
  for (auto p = text; *p;) {
    auto found = glyphs.find(utf8::next(p));
    if (found == glyphs.end())
      found = glyphs.find('?');
    if (found == glyphs.end())
      continue;
    const auto &glyph = found->second;
    SDL_Rect src{glyph.x0, glyph.y0, glyph.x1 - glyph.x0, glyph.y1 - glyph.y0};
    SDL_FRect dest{x + glyph.xoff * scale, y + size + glyph.yoff * scale,
                   src.w * scale, src.h * scale};
    SDL_RenderCopyF(renderer, atlas, &src, &dest);
    x += glyph.xadvance * scale;
  }
}
} // namespace ui
