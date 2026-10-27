// Drawing on a fixed 1280x720 logical canvas with SDL's built-in 8x8 debug
// font.
#pragma once

#include <string_view>

#include <SDL3/SDL.h>

namespace onboarding::ui {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kGlyph = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

struct Color {
  Uint8 r, g, b, a = 255;
};

namespace colors {
constexpr Color kBackground{18, 20, 28};
constexpr Color kText{235, 235, 240};
constexpr Color kMuted{150, 155, 170};
constexpr Color kAccent{76, 175, 80};
constexpr Color kBad{239, 83, 80};
constexpr Color kPanel{34, 38, 52};
constexpr Color kFocus{255, 200, 60};
}  // namespace colors

float TextWidth(std::string_view text, float scale);
void Text(SDL_Renderer* renderer, float x, float y, float scale, Color color, std::string_view text);
void TextCentered(SDL_Renderer* renderer, float center_x, float y, float scale, Color color,
                  std::string_view text);
void FillRect(SDL_Renderer* renderer, const SDL_FRect& rect, Color color);
void OutlineRect(SDL_Renderer* renderer, const SDL_FRect& rect, Color color, float thickness);
// `fraction` is clamped to [0, 1].
void ProgressBar(SDL_Renderer* renderer, const SDL_FRect& rect, double fraction);
// Draws a QR code for `text` inside a white square of `size` pixels.
void QrCode(SDL_Renderer* renderer, float x, float y, float size, std::string_view text);

}  // namespace onboarding::ui
