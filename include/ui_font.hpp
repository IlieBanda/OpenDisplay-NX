#pragma once
#include <SDL2/SDL.h>
namespace ui {
bool initializeFont(SDL_Renderer *renderer, const unsigned char *data,
                    const unsigned char *chinese = nullptr,
                    const unsigned char *icons = nullptr);
void releaseFont();
bool supportsText(const char *text);
float textWidth(const char *text, float size);
void wrappedLabel(SDL_Renderer *renderer, const char *text, float x, float y,
                  float size, float width);
void label(SDL_Renderer *renderer, const char *text, float x, float y,
           float size);
} // namespace ui
