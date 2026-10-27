#include "draw.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "qrcodegen.hpp"

namespace onboarding::ui {
namespace {

void SetColor(SDL_Renderer* renderer, Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

}  // namespace

float TextWidth(std::string_view text, float scale) {
  return static_cast<float>(text.size()) * kGlyph * scale;
}

void Text(SDL_Renderer* renderer, float x, float y, float scale, Color color, std::string_view text) {
  SetColor(renderer, color);
  SDL_SetRenderScale(renderer, scale, scale);
  SDL_RenderDebugText(renderer, x / scale, y / scale, std::string(text).c_str());
  SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

void TextCentered(SDL_Renderer* renderer, float center_x, float y, float scale, Color color,
                  std::string_view text) {
  Text(renderer, center_x - TextWidth(text, scale) / 2, y, scale, color, text);
}

void FillRect(SDL_Renderer* renderer, const SDL_FRect& rect, Color color) {
  SetColor(renderer, color);
  SDL_RenderFillRect(renderer, &rect);
}

void OutlineRect(SDL_Renderer* renderer, const SDL_FRect& rect, Color color, float thickness) {
  FillRect(renderer, {rect.x, rect.y, rect.w, thickness}, color);
  FillRect(renderer, {rect.x, rect.y + rect.h - thickness, rect.w, thickness}, color);
  FillRect(renderer, {rect.x, rect.y, thickness, rect.h}, color);
  FillRect(renderer, {rect.x + rect.w - thickness, rect.y, thickness, rect.h}, color);
}

void ProgressBar(SDL_Renderer* renderer, const SDL_FRect& rect, double fraction) {
  fraction = std::clamp(fraction, 0.0, 1.0);
  FillRect(renderer, rect, colors::kPanel);
  FillRect(renderer, {rect.x, rect.y, static_cast<float>(rect.w * fraction), rect.h}, colors::kAccent);
}

void QrCode(SDL_Renderer* renderer, float x, float y, float size, std::string_view text) {
  using qrcodegen::QrCode;
  const QrCode qr = QrCode::encodeText(std::string(text).c_str(), QrCode::Ecc::MEDIUM);
  // Four modules of quiet zone on each side, as the standard asks.
  const int modules = qr.getSize() + 8;
  const float cell = std::floor(size / modules);
  const float offset = (size - cell * modules) / 2;
  FillRect(renderer, {x, y, size, size}, Color{255, 255, 255});
  SetColor(renderer, Color{0, 0, 0});
  for (int row = 0; row < qr.getSize(); ++row) {
    for (int col = 0; col < qr.getSize(); ++col) {
      if (!qr.getModule(col, row)) continue;
      SDL_FRect module{x + offset + (col + 4) * cell, y + offset + (row + 4) * cell, cell, cell};
      SDL_RenderFillRect(renderer, &module);
    }
  }
}

}  // namespace onboarding::ui
